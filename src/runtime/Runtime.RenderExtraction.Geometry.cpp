module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.RenderExtraction;

import :Internal;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GraphGeometryPacker;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.MeshPrimitiveViewPacker;
import Extrinsic.Runtime.PointCloudGeometryPacker;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SpatialDebugAdapters;
import Extrinsic.Runtime.VisualizationAdapters;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    bool RenderExtractionCache::State::BindMeshGeometry(
        entt::registry& registry,
        entt::entity entity,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        const RenderExtractionGeometryDirtyPlan dirtyPlan =
            BuildRenderExtractionMeshGeometryDirtyPlan(registry, entity);
        const bool dirty = dirtyPlan.Dirty;
        const bool hadResidency = sidecar.MeshGeometry.IsValid();

        // Fail-closed release for a dirty-reupload pack/upload failure. When the
        // entity already has a valid upload and a later dirty update makes the
        // source unrenderable (empty / non-finite positions, broken topology),
        // the stale geometry must not keep rendering authoritative-but-invalid
        // data. The caller's eligibility-flip release cannot cover this because
        // the entity is still mesh-domain, so this is the only place a
        // dirty-reupload failure can release. The dirty tags are intentionally
        // left in place so a later frame re-attempts and uploads fresh once the
        // input recovers. Within this bridge no other domain/procedural path
        // can have re-bound the instance this frame (the domain branches are
        // mutually exclusive and run only when no procedural/asset source is
        // present), so the detach is unconditional.
        const auto releaseStaleResidency = [&]() {
            if (!sidecar.MeshGeometry.IsValid())
            {
                return;
            }
            EnqueueMeshRetire(sidecar.MeshGeometry);
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                Graphics::GpuGeometryHandle{});
            sidecar.MeshGeometry = {};
            ++stats.MeshGeometryReleases;
        };

        // Reuse path: clean entity with a cached upload. The procedural-
        // cache analogue is a refcount-only `EnsureResident` hit; for mesh
        // residency the per-entity handle is single-owner so the reuse is
        // a direct rebind without any cache lookup.
        if (hadResidency && !dirty)
        {
            ++stats.MeshGeometryReuseHits;
            sidecar.Geometry = sidecar.MeshGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.MeshGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                sidecar.MeshGeometry);
            return true;
        }

        const RenderExtractionMeshTexcoordFallbackDiagnostics texcoordFallback =
            DiagnoseRenderExtractionMeshTexcoordFallback(view);
        const auto* channelBindings =
            registry.try_get<VertexChannelBindingSet>(entity);
        MeshPackResult packResult =
            PackMesh(view, channelBindings, m_MeshPack);
        if (packResult.Status != MeshPackStatus::Success)
        {
            switch (packResult.Status)
            {
            case MeshPackStatus::MissingPositions:
                ++stats.MeshGeometryMissingPositions;
                break;
            case MeshPackStatus::InvalidTopology:
                ++stats.MeshGeometryInvalidTopology;
                break;
            case MeshPackStatus::MissingTexcoords:
                ++stats.MeshGeometryMissingTexcoords;
                break;
            case MeshPackStatus::NonFiniteTexcoord:
                ++stats.MeshGeometryNonFiniteTexcoords;
                break;
            default:
                ++stats.MeshGeometryFailedPack;
                break;
            }
            // Fail-closed: release any prior residency so invalid source data
            // does not keep stale geometry bound; the dirty tags stay set so a
            // later frame can recover the input.
            releaseStaleResidency();
            return false;
        }
        if (texcoordFallback.MissingOrMismatched)
        {
            ++stats.MeshGeometryMissingTexcoords;
        }
        if (texcoordFallback.NonFinite)
        {
            ++stats.MeshGeometryNonFiniteTexcoords;
        }

        if (hadResidency && dirty && !dirtyPlan.RequiresFullUpload)
        {
            const Graphics::GpuWorld::GeometryChannelUpdateResult update =
                renderer.GetGpuWorld().UpdateGeometryChannels(
                    sidecar.MeshGeometry,
                    *packResult.Upload,
                    dirtyPlan.Channels);
            if (update.Succeeded())
            {
                ++stats.MeshGeometryReuploads;
                ++stats.MeshGeometryPartialUploads;
                registry.remove<D::GpuDirty,
                                D::DirtyVertexPositions,
                                D::DirtyVertexAttributes,
                                D::DirtyVertexTexcoords,
                                D::DirtyVertexNormals,
                                D::DirtyVertexColors,
                                D::DirtyFaceTopology,
                                D::DirtyEdgeTopology>(entity);
                sidecar.Geometry = sidecar.MeshGeometry;
                sidecar.GpuSlot.SetGeometryHandle(sidecar.MeshGeometry);
                sidecar.GpuSlot.ClearSourceAsset();
                renderer.GetGpuWorld().SetInstanceGeometry(
                    sidecar.Instance,
                    sidecar.MeshGeometry);
                return true;
            }
        }

        const Graphics::GpuGeometryHandle handle =
            renderer.GetGpuWorld().UploadGeometry(*packResult.Upload);
        if (!handle.IsValid())
        {
            ++stats.MeshGeometryFailedPack;
            releaseStaleResidency();
            return false;
        }

        if (hadResidency)
        {
            // Dirty reupload: queue the prior handle for the same
            // `framesInFlight` deferred-retire window the procedural cache
            // uses, then swap to the new handle. The new
            // `SetInstanceGeometry` below detaches the instance from the
            // old slot before the slot is freed.
            EnqueueMeshRetire(sidecar.MeshGeometry);
            ++stats.MeshGeometryReuploads;
            ++stats.MeshGeometryReleases;
        }
        else
        {
            ++stats.MeshGeometryUploads;
        }

        // Drain the dirty tags consumed by this (re)upload. Tags are
        // additive (set by `MarkVertex*/Face*/Edge*/GpuDirty` producers)
        // so this matches the existing `DirtyTransform` drain pattern in
        // `ExtractAndSubmit`.
        if (dirty)
        {
            registry.remove<D::GpuDirty,
                            D::DirtyVertexPositions,
                            D::DirtyVertexAttributes,
                            D::DirtyVertexTexcoords,
                            D::DirtyVertexNormals,
                            D::DirtyVertexColors,
                            D::DirtyFaceTopology,
                            D::DirtyEdgeTopology>(entity);
        }

        sidecar.MeshGeometry = handle;
        sidecar.Geometry = handle;
        sidecar.GpuSlot.SetGeometryHandle(handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, handle);
        return true;
    }

    bool RenderExtractionCache::State::BindGraphGeometry(
        entt::registry& registry,
        entt::entity entity,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        namespace G = Graphics::Components;

        // The render hints select the lanes: `RenderEdges` packs edge line
        // indices, `RenderPoints` packs the node point lane. Both share the
        // single node-position vertex buffer (one handle per graph entity).
        const bool wantLines = registry.all_of<G::RenderEdges>(entity);
        const bool wantPoints = registry.all_of<G::RenderPoints>(entity);

        const RenderExtractionGeometryDirtyPlan dirtyPlan =
            BuildRenderExtractionGraphGeometryDirtyPlan(registry, entity);
        const bool dirty = dirtyPlan.Dirty;
        const bool hadResidency = sidecar.GraphGeometry.IsValid();
        // A change in requested render lanes repacks: the cached upload was
        // packed for a specific lane mask, and the line lane in particular
        // changes the packed line indices. Without this, gaining/losing a
        // line hint on an otherwise-clean graph would rebind a stale upload.
        const bool lanesChanged = hadResidency
            && (sidecar.GraphPackedLines != wantLines
                || sidecar.GraphPackedPoints != wantPoints);

        // Fail-closed release for a dirty-reupload pack/upload failure — see the
        // mesh bridge for the rationale. The caller's eligibility-flip release
        // cannot cover this because the entity is still graph-domain. Clears the
        // packed-lane flags alongside the handle so a later fresh upload re-sets
        // them.
        const auto releaseStaleResidency = [&]() {
            if (!sidecar.GraphGeometry.IsValid())
            {
                return;
            }
            EnqueueGraphRetire(sidecar.GraphGeometry);
            ReleaseGraphPointLaneInstance(sidecar, renderer, stats);
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                Graphics::GpuGeometryHandle{});
            sidecar.GraphGeometry = {};
            sidecar.GraphPackedLines = false;
            sidecar.GraphPackedPoints = false;
            ++stats.GraphGeometryReleases;
        };

        // Reuse path: clean graph entity, unchanged lanes, and a cached
        // upload. Mirrors the single-owner mesh reuse — a direct rebind
        // without any repack.
        if (hadResidency && !dirty && !lanesChanged)
        {
            ++stats.GraphGeometryReuseHits;
            sidecar.Geometry = sidecar.GraphGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.GraphGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                sidecar.GraphGeometry);
            return true;
        }

        const auto* channelBindings =
            registry.try_get<VertexChannelBindingSet>(entity);
        GraphPackResult packResult = PackGraph(
            view,
            wantLines,
            wantPoints,
            channelBindings,
            m_GraphPack);
        if (packResult.Status != GraphPackStatus::Success)
        {
            switch (packResult.Status)
            {
            case GraphPackStatus::MissingNodes:
            case GraphPackStatus::EmptyGraph:
                ++stats.GraphGeometryMissingNodes;
                break;
            case GraphPackStatus::InvalidEdge:
                ++stats.GraphGeometryInvalidEdges;
                break;
            default:
                // `WrongDomain`, `NoRenderLane` (graph entity with no
                // line/point hint), `MissingEdgeTopology`, `NonFinitePosition`.
                ++stats.GraphGeometryFailedPack;
                break;
            }
            // Fail-closed: release any prior residency so invalid source data
            // does not keep stale geometry bound; the dirty tags stay set so a
            // later frame can recover the input.
            releaseStaleResidency();
            return false;
        }

        if (hadResidency && dirty && !dirtyPlan.RequiresFullUpload
            && !lanesChanged)
        {
            const Graphics::GpuWorld::GeometryChannelUpdateResult update =
                renderer.GetGpuWorld().UpdateGeometryChannels(
                    sidecar.GraphGeometry,
                    *packResult.Upload,
                    dirtyPlan.Channels);
            if (update.Succeeded())
            {
                ++stats.GraphGeometryReuploads;
                ++stats.GraphGeometryPartialUploads;
                registry.remove<D::GpuDirty,
                                D::DirtyVertexPositions,
                                D::DirtyVertexAttributes,
                                D::DirtyVertexTexcoords,
                                D::DirtyVertexNormals,
                                D::DirtyVertexColors,
                                D::DirtyEdgeTopology>(entity);
                sidecar.Geometry = sidecar.GraphGeometry;
                sidecar.GpuSlot.SetGeometryHandle(sidecar.GraphGeometry);
                sidecar.GpuSlot.ClearSourceAsset();
                renderer.GetGpuWorld().SetInstanceGeometry(
                    sidecar.Instance,
                    sidecar.GraphGeometry);
                return true;
            }
        }

        const Graphics::GpuGeometryHandle handle =
            renderer.GetGpuWorld().UploadGeometry(*packResult.Upload);
        if (!handle.IsValid())
        {
            ++stats.GraphGeometryFailedPack;
            releaseStaleResidency();
            return false;
        }

        if (hadResidency)
        {
            // Dirty reupload: queue the prior handle for the same
            // `framesInFlight` deferred-retire window, then swap. The new
            // `SetInstanceGeometry` below detaches the instance from the old
            // slot before it is freed.
            EnqueueGraphRetire(sidecar.GraphGeometry);
            ++stats.GraphGeometryReuploads;
            ++stats.GraphGeometryReleases;
        }
        else
        {
            ++stats.GraphGeometryUploads;
        }

        if (dirty)
        {
            registry.remove<D::GpuDirty,
                            D::DirtyVertexPositions,
                            D::DirtyVertexAttributes,
                            D::DirtyVertexTexcoords,
                            D::DirtyVertexNormals,
                            D::DirtyVertexColors,
                            D::DirtyEdgeTopology>(entity);
        }

        sidecar.GraphGeometry = handle;
        sidecar.GraphPackedLines = wantLines;
        sidecar.GraphPackedPoints = wantPoints;
        sidecar.Geometry = handle;
        sidecar.GpuSlot.SetGeometryHandle(handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, handle);
        return true;
    }

    bool RenderExtractionCache::State::BindPointCloudGeometry(
        entt::registry& registry,
        entt::entity entity,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        namespace G = Graphics::Components;

        // Fail-closed release for an unsupported-size-source or dirty-reupload
        // pack/upload failure — see the mesh bridge for the rationale. The
        // caller's eligibility-flip release cannot cover this because the entity
        // is still point-cloud-domain, so this is the only place such a failure
        // can release a previously-resident cloud.
        const auto releaseStaleResidency = [&]() {
            if (!sidecar.PointCloudGeometry.IsValid())
            {
                return;
            }
            EnqueuePointCloudRetire(sidecar.PointCloudGeometry);
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                Graphics::GpuGeometryHandle{});
            sidecar.PointCloudGeometry = {};
            ++stats.PointCloudGeometryReleases;
        };

        // Size-source policy for this slice: only a uniform screen-space size
        // (the `float` alternative of `RenderPoints::SizeSource`) is supported.
        // A per-point size buffer (the `std::string` alternative) requires a
        // per-point size upload that is not implemented here, so it fails
        // closed rather than silently rendering with a default size. The
        // render-type enum (`Flat`/`Sphere`/`Surfel`) only selects the
        // downstream point shader and does not affect the position-only upload,
        // so all three are accepted by the geometry-residency bridge.
        if (const auto* points = registry.try_get<G::RenderPoints>(entity);
            points != nullptr
            && std::holds_alternative<std::string>(points->SizeSource))
        {
            ++stats.PointCloudGeometryFailedPack;
            // Fail-closed: release any prior residency (a resident cloud that
            // switches to an unsupported size source stops rendering) and leave
            // the dirty tags in place so a later frame can recover once the size
            // source becomes supported.
            releaseStaleResidency();
            return false;
        }

        const RenderExtractionGeometryDirtyPlan dirtyPlan =
            BuildRenderExtractionPointCloudGeometryDirtyPlan(registry, entity);
        const bool dirty = dirtyPlan.Dirty;
        const bool hadResidency = sidecar.PointCloudGeometry.IsValid();

        // Reuse path: clean point-cloud entity with a cached upload. Mirrors
        // the single-owner mesh reuse — a direct rebind without any repack.
        if (hadResidency && !dirty)
        {
            ++stats.PointCloudGeometryReuseHits;
            sidecar.Geometry = sidecar.PointCloudGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.PointCloudGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                sidecar.PointCloudGeometry);
            return true;
        }

        const auto* channelBindings =
            registry.try_get<VertexChannelBindingSet>(entity);
        PointCloudPackResult packResult =
            PackCloud(view, channelBindings, m_PointCloudPack);
        if (packResult.Status != PointCloudPackStatus::Success)
        {
            switch (packResult.Status)
            {
            case PointCloudPackStatus::MissingPositions:
            case PointCloudPackStatus::EmptyCloud:
                ++stats.PointCloudGeometryMissingPositions;
                break;
            case PointCloudPackStatus::NonFinitePosition:
                ++stats.PointCloudGeometryInvalidPoints;
                break;
            default:
                // `WrongDomain`.
                ++stats.PointCloudGeometryFailedPack;
                break;
            }
            // Fail-closed: release any prior residency so invalid source data
            // does not keep stale geometry bound; the dirty tags stay set so a
            // later frame can recover the input.
            releaseStaleResidency();
            return false;
        }

        if (hadResidency && dirty && !dirtyPlan.RequiresFullUpload)
        {
            const Graphics::GpuWorld::GeometryChannelUpdateResult update =
                renderer.GetGpuWorld().UpdateGeometryChannels(
                    sidecar.PointCloudGeometry,
                    *packResult.Upload,
                    dirtyPlan.Channels);
            if (update.Succeeded())
            {
                ++stats.PointCloudGeometryReuploads;
                ++stats.PointCloudGeometryPartialUploads;
                registry.remove<D::GpuDirty,
                                D::DirtyVertexPositions,
                                D::DirtyVertexAttributes,
                                D::DirtyVertexTexcoords,
                                D::DirtyVertexNormals,
                                D::DirtyVertexColors>(entity);
                sidecar.Geometry = sidecar.PointCloudGeometry;
                sidecar.GpuSlot.SetGeometryHandle(
                    sidecar.PointCloudGeometry);
                sidecar.GpuSlot.ClearSourceAsset();
                renderer.GetGpuWorld().SetInstanceGeometry(
                    sidecar.Instance,
                    sidecar.PointCloudGeometry);
                return true;
            }
        }

        const Graphics::GpuGeometryHandle handle =
            renderer.GetGpuWorld().UploadGeometry(*packResult.Upload);
        if (!handle.IsValid())
        {
            ++stats.PointCloudGeometryFailedPack;
            releaseStaleResidency();
            return false;
        }

        if (hadResidency)
        {
            // Dirty reupload: queue the prior handle for the same
            // `framesInFlight` deferred-retire window, then swap. The new
            // `SetInstanceGeometry` below detaches the instance from the old
            // slot before it is freed.
            EnqueuePointCloudRetire(sidecar.PointCloudGeometry);
            ++stats.PointCloudGeometryReuploads;
            ++stats.PointCloudGeometryReleases;
        }
        else
        {
            ++stats.PointCloudGeometryUploads;
        }

        if (dirty)
        {
            registry.remove<D::GpuDirty,
                            D::DirtyVertexPositions,
                            D::DirtyVertexAttributes,
                            D::DirtyVertexTexcoords,
                            D::DirtyVertexNormals,
                            D::DirtyVertexColors>(entity);
        }

        sidecar.PointCloudGeometry = handle;
        sidecar.Geometry = handle;
        sidecar.GpuSlot.SetGeometryHandle(handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, handle);
        return true;
    }

    bool RenderExtractionCache::State::ReconcileMeshPrimitiveView(
        MeshPrimitiveViewKind kind,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        const glm::mat4& model,
        std::uint32_t materialSlot,
        const RHI::GpuBounds& bounds,
        std::uint32_t stableId,
        bool desired,
        const Graphics::Components::RenderEdges* edges,
        const Graphics::Components::RenderPoints* points,
        const Graphics::Components::VisualizationConfig* visualization,
        bool meshDirty,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        const bool isEdge = kind == MeshPrimitiveViewKind::Edge;
        Graphics::GpuInstanceHandle& instance =
            isEdge
                ? sidecar.MeshEdgeViewInstance
                : sidecar.MeshVertexViewInstance;
        Graphics::GpuGeometryHandle& geometry =
            isEdge
                ? sidecar.MeshEdgeViewGeometry
                : sidecar.MeshVertexViewGeometry;

        // Disabled (or parent no longer resident): release any existing view.
        if (!desired)
        {
            ReleaseMeshPrimitiveView(kind, sidecar, renderer, stats);
            return false;
        }

        if (!isEdge
            && (points == nullptr
                || !std::holds_alternative<float>(points->SizeSource)))
        {
            ++stats.MeshVertexViewFailedPack;
            ReleaseMeshPrimitiveView(kind, sidecar, renderer, stats);
            return false;
        }

        const bool hadView = geometry.IsValid();

        // Append the per-frame transform/render record so the view lane renders
        // with the parent surface's transform/bounds/material but its own
        // line/point render flag. Called for every resident-and-bound frame
        // (upload, reupload, and reuse), mirroring the surface mesh which is
        // re-submitted to `m_Transforms` every frame.
        const auto submitTransform = [&]() {
            const std::uint32_t laneFlag = isEdge
                ? (RHI::GpuRender_Line | RHI::GpuRender_Unlit)
                : (RHI::GpuRender_Point | RHI::GpuRender_Unlit);
            const RHI::GpuEntityConfig cfg =
                BuildRenderExtractionImmediateLaneConfig(
                    visualization,
                    edges,
                    points);
            renderer.GetGpuWorld().SetEntityConfig(instance, cfg);
            m_Transforms.push_back(Graphics::TransformSyncRecord{
                .StableId = stableId,
                .Instance = instance,
                .Model = model,
                .RenderFlags =
                    RHI::GpuRender_Visible
                    | RHI::GpuRender_Opaque
                    | laneFlag,
                .Bounds = bounds,
                .MaterialSlot = materialSlot,
                .HasMaterialSlot = true,
            });
        };

        // Reuse path: resident view, parent clean. Direct re-submit, no repack.
        if (hadView && !meshDirty)
        {
            if (isEdge)
            {
                ++stats.MeshEdgeViewReuseHits;
            }
            else
            {
                ++stats.MeshVertexViewReuseHits;
            }
            submitTransform();
            return true;
        }

        // Pack (first upload, or parent-dirty reupload). The shared scratch
        // buffer is reused serially; the returned descriptor views into it, so
        // upload happens before the next view packs.
        const MeshPrimitiveViewResult packResult = isEdge
            ? PackMeshEdgeView(view, m_MeshPrimitiveViewPack)
            : PackMeshVertexView(view, m_MeshPrimitiveViewPack);
        if (packResult.Status != MeshPrimitiveViewStatus::Success)
        {
            switch (packResult.Status)
            {
            case MeshPrimitiveViewStatus::MissingPositions:
            case MeshPrimitiveViewStatus::EmptyMesh:
                if (isEdge)
                {
                    ++stats.MeshEdgeViewMissingPositions;
                }
                else
                {
                    ++stats.MeshVertexViewMissingPositions;
                }
                break;
            case MeshPrimitiveViewStatus::MissingEdgeTopology:
                // Edge-only status (the vertex view never requests edges).
                ++stats.MeshEdgeViewMissingEdgeTopology;
                break;
            case MeshPrimitiveViewStatus::InvalidEdge:
                ++stats.MeshEdgeViewInvalidEdges;
                break;
            default:
                // `WrongDomain`, `NonFinitePosition`.
                if (isEdge)
                {
                    ++stats.MeshEdgeViewFailedPack;
                }
                else
                {
                    ++stats.MeshVertexViewFailedPack;
                }
                break;
            }
            // Fail-closed: drop any stale view so invalid source data does not
            // keep rendering. The parent surface owns its own fail-closed path;
            // a missing/invalid edge view simply disappears until the source
            // recovers on a later dirty frame.
            ReleaseMeshPrimitiveView(kind, sidecar, renderer, stats);
            return false;
        }

        const Graphics::GpuGeometryHandle handle =
            renderer.GetGpuWorld().UploadGeometry(*packResult.Upload);
        if (!handle.IsValid())
        {
            if (isEdge)
            {
                ++stats.MeshEdgeViewFailedPack;
            }
            else
            {
                ++stats.MeshVertexViewFailedPack;
            }
            ReleaseMeshPrimitiveView(kind, sidecar, renderer, stats);
            return false;
        }

        // Ensure the view has its own instance. Allocated lazily on first
        // upload and kept until the view is released. If the instance pool is
        // exhausted, free the just-uploaded geometry to avoid a leak and bail.
        if (!instance.IsValid())
        {
            instance = renderer.GetGpuWorld().AllocateInstance(stableId);
            if (!instance.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(handle);
                if (isEdge)
                {
                    ++stats.MeshEdgeViewFailedPack;
                }
                else
                {
                    ++stats.MeshVertexViewFailedPack;
                }
                ReleaseMeshPrimitiveView(kind, sidecar, renderer, stats);
                return false;
            }
            ++stats.AllocatedInstanceCount;
        }

        if (hadView)
        {
            // Parent-dirty reupload: queue the prior view handle for deferred
            // retire, then swap. The `SetInstanceGeometry` below rebinds the
            // existing view instance to the fresh upload.
            EnqueueMeshPrimitiveViewRetire(geometry);
            if (isEdge)
            {
                ++stats.MeshEdgeViewReuploads;
                ++stats.MeshEdgeViewReleases;
            }
            else
            {
                ++stats.MeshVertexViewReuploads;
                ++stats.MeshVertexViewReleases;
            }
        }
        else
        {
            if (isEdge)
            {
                ++stats.MeshEdgeViewUploads;
            }
            else
            {
                ++stats.MeshVertexViewUploads;
            }
        }

        geometry = handle;
        renderer.GetGpuWorld().SetInstanceGeometry(instance, handle);
        submitTransform();
        return true;
    }

    std::optional<RenderExtractionCache::State::RenderableSidecarView>
    RenderExtractionCache::State::FindRenderableSidecarForTest(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_Renderables.find(stableEntityId);
        if (it == m_Renderables.end())
        {
            return std::nullopt;
        }
        return RenderableSidecarView{
            .Instance = it->second.Instance,
            .Geometry = it->second.Geometry,
            .ProceduralKey = it->second.ProceduralKey,
            .HasSourceAsset = it->second.GpuSlot.HasSourceAsset(),
            .GeometrySlot = it->second.GpuSlot.GeometrySlot,
            .GeometryGeneration = it->second.GpuSlot.GeometryGeneration,
            .MeshGeometry = it->second.MeshGeometry,
            .HasMeshResidency = it->second.MeshGeometry.IsValid(),
            .GraphGeometry = it->second.GraphGeometry,
            .HasGraphResidency = it->second.GraphGeometry.IsValid(),
            .GraphPointLaneInstance = it->second.GraphPointLaneInstance,
            .HasGraphPointLaneInstance =
                it->second.GraphPointLaneInstance.IsValid(),
            .PointCloudGeometry = it->second.PointCloudGeometry,
            .HasPointCloudResidency = it->second.PointCloudGeometry.IsValid(),
            .MeshEdgeViewInstance = it->second.MeshEdgeViewInstance,
            .MeshEdgeViewGeometry = it->second.MeshEdgeViewGeometry,
            .HasMeshEdgeView = it->second.MeshEdgeViewGeometry.IsValid(),
            .MeshVertexViewInstance = it->second.MeshVertexViewInstance,
            .MeshVertexViewGeometry = it->second.MeshVertexViewGeometry,
            .HasMeshVertexView = it->second.MeshVertexViewGeometry.IsValid(),
            .MaterialHandle = it->second.Material.Lease.GetHandle(),
            .MaterialSlot = it->second.Material.EffectiveSlot,
            .HasMaterialLease = it->second.Material.Lease.IsValid(),
        };
    }

    std::optional<RenderExtractionCache::State::GpuRenderableAvailabilityView>
    RenderExtractionCache::State::FindGpuRenderableAvailability(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_Renderables.find(stableEntityId);
        if (it == m_Renderables.end())
        {
            return std::nullopt;
        }

        const auto& renderable = it->second;
        GpuRenderableAvailabilityView view{};
        view.StableEntityId = stableEntityId;
        view.HasRenderable = true;
        view.HasSourceAsset = renderable.GpuSlot.HasSourceAsset();
        view.GeometrySlot = renderable.GpuSlot.GeometrySlot;
        view.GeometryGeneration = renderable.GpuSlot.GeometryGeneration;
        view.NamedBufferCount =
            static_cast<std::uint32_t>(
                renderable.GpuSlot.NamedBufferEntries.size());
        view.HasPositionsBuffer =
            renderable.GpuSlot.FindEntry("positions") != nullptr;
        view.HasNormalsBuffer =
            renderable.GpuSlot.FindEntry("normals") != nullptr;
        view.HasEdgesBuffer =
            renderable.GpuSlot.FindEntry("edges") != nullptr;
        view.HasColorsBuffer =
            renderable.GpuSlot.FindEntry("colors") != nullptr;
        view.HasScalarsBuffer =
            renderable.GpuSlot.FindEntry("scalars") != nullptr;
        view.HasSizesBuffer =
            renderable.GpuSlot.FindEntry("sizes") != nullptr;

        view.Surface.Lane = GeometryRenderLane::Surface;
        const bool hasNonSurfaceLaneGeometry =
            renderable.GraphGeometry.IsValid() ||
            renderable.PointCloudGeometry.IsValid() ||
            renderable.MeshEdgeViewGeometry.IsValid() ||
            renderable.MeshVertexViewGeometry.IsValid();
        if (renderable.MeshGeometry.IsValid())
        {
            view.Surface.Instance = renderable.Instance;
            view.Surface.Geometry = renderable.MeshGeometry;
        }
        else if (!hasNonSurfaceLaneGeometry &&
                 (renderable.ProceduralKey.has_value() ||
                  renderable.GpuSlot.HasSourceAsset()) &&
                 renderable.Geometry.IsValid())
        {
            view.Surface.Instance = renderable.Instance;
            view.Surface.Geometry = renderable.Geometry;
        }
        view.Surface.HasInstance = view.Surface.Instance.IsValid();
        view.Surface.HasGeometry = view.Surface.Geometry.IsValid();

        view.Edges.Lane = GeometryRenderLane::Edges;
        if (renderable.MeshEdgeViewGeometry.IsValid())
        {
            view.Edges.Instance = renderable.MeshEdgeViewInstance;
            view.Edges.Geometry = renderable.MeshEdgeViewGeometry;
        }
        else if (renderable.GraphGeometry.IsValid() &&
                 renderable.GraphPackedLines)
        {
            view.Edges.Instance = renderable.Instance;
            view.Edges.Geometry = renderable.GraphGeometry;
        }
        view.Edges.HasInstance = view.Edges.Instance.IsValid();
        view.Edges.HasGeometry = view.Edges.Geometry.IsValid();

        view.Points.Lane = GeometryRenderLane::Points;
        if (renderable.MeshVertexViewGeometry.IsValid())
        {
            view.Points.Instance = renderable.MeshVertexViewInstance;
            view.Points.Geometry = renderable.MeshVertexViewGeometry;
        }
        else if (renderable.GraphGeometry.IsValid() &&
                 renderable.GraphPackedPoints)
        {
            view.Points.Instance =
                renderable.GraphPointLaneInstance.IsValid()
                    ? renderable.GraphPointLaneInstance
                    : renderable.Instance;
            view.Points.Geometry = renderable.GraphGeometry;
        }
        else if (renderable.PointCloudGeometry.IsValid())
        {
            view.Points.Instance = renderable.Instance;
            view.Points.Geometry = renderable.PointCloudGeometry;
        }
        view.Points.HasInstance = view.Points.Instance.IsValid();
        view.Points.HasGeometry = view.Points.Geometry.IsValid();

        return view;
    }

    const ProceduralGeometryCache&
    RenderExtractionCache::State::GetProceduralGeometryCacheForTest()
        const noexcept
    {
        return m_ProceduralGeometry;
    }

    void RenderExtractionCache::State::SetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        m_MeshPrimitiveViewSettings.insert_or_assign(
            stableEntityId,
            settings);
    }

    void RenderExtractionCache::State::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_MeshPrimitiveViewSettings.erase(stableEntityId);
    }

    MeshPrimitiveViewSettings
    RenderExtractionCache::State::GetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_MeshPrimitiveViewSettings.find(stableEntityId);
        return it != m_MeshPrimitiveViewSettings.end()
            ? it->second
            : MeshPrimitiveViewSettings{};
    }

    void RenderExtractionCache::State::SetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId,
        Graphics::MaterialTextureAssetBindings bindings)
    {
        if (stableEntityId == 0u)
        {
            return;
        }
        m_MaterialTextureBindings.insert_or_assign(
            stableEntityId,
            bindings);
    }

    void RenderExtractionCache::State::ClearMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_MaterialTextureBindings.erase(stableEntityId);
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    RenderExtractionCache::State::GetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_MaterialTextureBindings.find(stableEntityId);
        if (it == m_MaterialTextureBindings.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    bool RenderExtractionCache::State::BindProceduralGeometry(
        const ECS::Components::ProceduralGeometryRef& ref,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        const bool packerSupportsKind =
            ref.Kind ==
            ECS::Components::ProceduralGeometryKind::Triangle;
        if (!packerSupportsKind)
        {
            ++stats.ProceduralGeometryMissingPacker;
            return false;
        }

        const ProceduralGeometryKey key =
            MakeProceduralGeometryKey(ref.Kind, ref.Params);

        Graphics::GpuWorld::GeometryUploadDesc desc{};
        const ProceduralGeometryCacheEntry* existing =
            m_ProceduralGeometry.Find(key);
        if (existing == nullptr)
        {
            std::optional<Graphics::GpuWorld::GeometryUploadDesc> packed =
                Pack(ref.Kind, ref.Params, m_ProceduralPack);
            if (!packed.has_value())
            {
                ++stats.ProceduralGeometryInvalidParams;
                return false;
            }
            desc = *packed;
        }

        const auto preUploads = m_ProceduralGeometry.Stats().Uploads;
        const auto preReuse = m_ProceduralGeometry.Stats().ReuseHits;

        ProceduralGeometryCache::UploadFn upload =
            [&renderer](
                const Graphics::GpuWorld::GeometryUploadDesc& uploadDesc)
            {
                return renderer.GetGpuWorld().UploadGeometry(uploadDesc);
            };

        const Graphics::GpuGeometryHandle handle =
            m_ProceduralGeometry.EnsureResident(key, desc, upload);
        if (!handle.IsValid())
        {
            ++stats.ProceduralGeometryFailedPack;
            return false;
        }

        if (m_ProceduralGeometry.Stats().Uploads > preUploads)
        {
            ++stats.ProceduralGeometryUploads;
        }
        if (m_ProceduralGeometry.Stats().ReuseHits > preReuse)
        {
            ++stats.ProceduralGeometryReuseHits;
        }

        sidecar.Geometry = handle;
        sidecar.ProceduralKey = key;
        sidecar.GpuSlot.SetGeometryHandle(handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(
            sidecar.Instance,
            handle);
        return true;
    }

    bool RenderExtractionCache::State::EnsureGraphPointLaneInstance(
        RenderableSidecar& sidecar,
        const std::uint32_t stableId,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        if (!sidecar.GraphGeometry.IsValid())
        {
            return false;
        }

        if (!sidecar.GraphPointLaneInstance.IsValid())
        {
            sidecar.GraphPointLaneInstance =
                renderer.GetGpuWorld().AllocateInstance(stableId);
            if (!sidecar.GraphPointLaneInstance.IsValid())
            {
                return false;
            }
            ++stats.AllocatedInstanceCount;
        }
        renderer.GetGpuWorld().SetInstanceGeometry(
            sidecar.GraphPointLaneInstance,
            sidecar.GraphGeometry);
        return true;
    }

    void RenderExtractionCache::State::ReleaseGraphPointLaneInstance(
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        if (!sidecar.GraphPointLaneInstance.IsValid())
        {
            return;
        }

        renderer.GetGpuWorld().FreeInstance(
            sidecar.GraphPointLaneInstance);
        sidecar.GraphPointLaneInstance = {};
        ++stats.FreedInstanceCount;
    }

    void RenderExtractionCache::State::EnqueueMeshRetire(
        const Graphics::GpuGeometryHandle handle)
    {
        if (handle.IsValid())
        {
            m_MeshRetire.push_back(
                GeometryRetireRecord{handle, 0, false});
        }
    }

    void RenderExtractionCache::State::EnqueueGraphRetire(
        const Graphics::GpuGeometryHandle handle)
    {
        if (handle.IsValid())
        {
            m_GraphRetire.push_back(
                GeometryRetireRecord{handle, 0, false});
        }
    }

    void RenderExtractionCache::State::EnqueuePointCloudRetire(
        const Graphics::GpuGeometryHandle handle)
    {
        if (handle.IsValid())
        {
            m_PointCloudRetire.push_back(
                GeometryRetireRecord{handle, 0, false});
        }
    }

    void RenderExtractionCache::State::EnqueueMeshPrimitiveViewRetire(
        const Graphics::GpuGeometryHandle handle)
    {
        if (handle.IsValid())
        {
            m_MeshPrimitiveViewRetire.push_back(
                GeometryRetireRecord{handle, 0, false});
        }
    }

    void RenderExtractionCache::State::ReleaseMeshPrimitiveView(
        const MeshPrimitiveViewKind kind,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        const bool isEdge = kind == MeshPrimitiveViewKind::Edge;
        Graphics::GpuInstanceHandle& instance =
            isEdge
                ? sidecar.MeshEdgeViewInstance
                : sidecar.MeshVertexViewInstance;
        Graphics::GpuGeometryHandle& geometry =
            isEdge
                ? sidecar.MeshEdgeViewGeometry
                : sidecar.MeshVertexViewGeometry;

        if (geometry.IsValid())
        {
            EnqueueMeshPrimitiveViewRetire(geometry);
            geometry = {};
            if (isEdge)
            {
                ++stats.MeshEdgeViewReleases;
            }
            else
            {
                ++stats.MeshVertexViewReleases;
            }
        }
        if (instance.IsValid())
        {
            renderer.GetGpuWorld().FreeInstance(instance);
            instance = {};
            ++stats.FreedInstanceCount;
        }
    }

    void RenderExtractionCache::State::RetireMissingRenderables(
        const std::unordered_set<std::uint32_t>& liveKeys,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        for (auto it = m_Renderables.begin();
             it != m_Renderables.end();)
        {
            if (liveKeys.contains(it->first))
            {
                ++it;
                continue;
            }

            if (it->second.ProceduralKey.has_value())
            {
                m_ProceduralGeometry.Release(
                    *it->second.ProceduralKey);
            }
            if (it->second.MeshGeometry.IsValid())
            {
                EnqueueMeshRetire(it->second.MeshGeometry);
                ++stats.MeshGeometryReleases;
            }
            if (it->second.GraphGeometry.IsValid())
            {
                EnqueueGraphRetire(it->second.GraphGeometry);
                ++stats.GraphGeometryReleases;
            }
            ReleaseGraphPointLaneInstance(
                it->second,
                renderer,
                stats);
            if (it->second.PointCloudGeometry.IsValid())
            {
                EnqueuePointCloudRetire(
                    it->second.PointCloudGeometry);
                ++stats.PointCloudGeometryReleases;
            }
            ReleaseMeshPrimitiveView(
                MeshPrimitiveViewKind::Edge,
                it->second,
                renderer,
                stats);
            ReleaseMeshPrimitiveView(
                MeshPrimitiveViewKind::Vertex,
                it->second,
                renderer,
                stats);
            m_MeshPrimitiveViewSettings.erase(it->first);
            m_MaterialTextureBindings.erase(it->first);
            renderer.GetGpuWorld().FreeInstance(
                it->second.Instance);
            it = m_Renderables.erase(it);
            ++stats.FreedInstanceCount;
        }
    }

    void RenderExtractionCache::State::TickProceduralGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        m_ProceduralGeometry.Tick(
            currentFrame,
            framesInFlight,
            [&renderer](const Graphics::GpuGeometryHandle handle)
            {
                renderer.GetGpuWorld().FreeGeometry(handle);
            });
    }

    void RenderExtractionCache::State::TickMeshGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        const std::uint64_t deadline =
            currentFrame + std::uint64_t{framesInFlight};
        for (auto& record : m_MeshRetire)
        {
            if (!record.DeadlineSet)
            {
                record.Deadline = deadline;
                record.DeadlineSet = true;
            }
        }

        auto it = m_MeshRetire.begin();
        while (it != m_MeshRetire.end())
        {
            if (it->DeadlineSet && it->Deadline <= currentFrame)
            {
                if (it->Handle.IsValid())
                {
                    renderer.GetGpuWorld().FreeGeometry(it->Handle);
                    ++m_MeshFreeRetires;
                }
                it = m_MeshRetire.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void RenderExtractionCache::State::TickGraphGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        const std::uint64_t deadline =
            currentFrame + std::uint64_t{framesInFlight};
        for (auto& record : m_GraphRetire)
        {
            if (!record.DeadlineSet)
            {
                record.Deadline = deadline;
                record.DeadlineSet = true;
            }
        }

        auto it = m_GraphRetire.begin();
        while (it != m_GraphRetire.end())
        {
            if (it->DeadlineSet && it->Deadline <= currentFrame)
            {
                if (it->Handle.IsValid())
                {
                    renderer.GetGpuWorld().FreeGeometry(it->Handle);
                    ++m_GraphFreeRetires;
                }
                it = m_GraphRetire.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void RenderExtractionCache::State::TickPointCloudGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        const std::uint64_t deadline =
            currentFrame + std::uint64_t{framesInFlight};
        for (auto& record : m_PointCloudRetire)
        {
            if (!record.DeadlineSet)
            {
                record.Deadline = deadline;
                record.DeadlineSet = true;
            }
        }

        auto it = m_PointCloudRetire.begin();
        while (it != m_PointCloudRetire.end())
        {
            if (it->DeadlineSet && it->Deadline <= currentFrame)
            {
                if (it->Handle.IsValid())
                {
                    renderer.GetGpuWorld().FreeGeometry(it->Handle);
                    ++m_PointCloudFreeRetires;
                }
                it = m_PointCloudRetire.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void RenderExtractionCache::State::TickMeshPrimitiveViewGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        const std::uint64_t deadline =
            currentFrame + std::uint64_t{framesInFlight};
        for (auto& record : m_MeshPrimitiveViewRetire)
        {
            if (!record.DeadlineSet)
            {
                record.Deadline = deadline;
                record.DeadlineSet = true;
            }
        }

        auto it = m_MeshPrimitiveViewRetire.begin();
        while (it != m_MeshPrimitiveViewRetire.end())
        {
            if (it->DeadlineSet && it->Deadline <= currentFrame)
            {
                if (it->Handle.IsValid())
                {
                    renderer.GetGpuWorld().FreeGeometry(it->Handle);
                    ++m_MeshPrimitiveViewFreeRetires;
                }
                it = m_MeshPrimitiveViewRetire.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
