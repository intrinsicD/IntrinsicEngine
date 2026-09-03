module;

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
import Extrinsic.Graphics.GeometryResidency;
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
import Extrinsic.Runtime.GeometryPlanBuilders;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    Graphics::GeometryResidencyKey BuildRenderExtractionGeometryResidencyKey(
        const RenderExtractionGeometryResidencyKind kind,
        const std::uint64_t identity,
        const std::uint32_t lane) noexcept
    {
        return Graphics::GeometryResidencyKey{
            .Namespace = static_cast<std::uint64_t>(kind),
            .Identity = identity,
            .Lane = lane,
        };
    }

    namespace
    {
        using ECS::Components::GeometrySources::ConstSourceView;
        using ECS::Components::GeometrySources::PropertyNames::kEdgeV0;
        using ECS::Components::GeometrySources::PropertyNames::kEdgeV1;
        using ECS::Components::GeometrySources::PropertyNames::kFaceHalfedge;
        using ECS::Components::GeometrySources::PropertyNames::kHalfedgeFace;
        using ECS::Components::GeometrySources::PropertyNames::kHalfedgeNext;
        using ECS::Components::GeometrySources::PropertyNames::kHalfedgeToVertex;
        using ECS::Components::GeometrySources::PropertyNames::kNormal;
        using ECS::Components::GeometrySources::PropertyNames::kPosition;

        [[nodiscard]] Geometry::PropertyRevision PropertyRevisionOf(
            const Geometry::PropertySet& properties,
            const std::string_view name) noexcept
        {
            return properties.FindPropertyRevision(name).value_or(0u);
        }

        [[nodiscard]] std::size_t PositionCountOf(
            const Geometry::PropertySet& properties) noexcept
        {
            const auto position =
                Geometry::ConstPropertySet{properties}.Get<glm::vec3>(kPosition);
            return position.IsValid() ? position.Size() : 0u;
        }

        [[nodiscard]] bool BindingMatches(
            const VertexChannelSourceBinding& binding,
            const GeometryElementDomain expectedDomain,
            const bool color) noexcept
        {
            if (!IsVertexChannelBindingEnabled(binding) ||
                binding.Property.Domain != expectedDomain)
            {
                return false;
            }
            if (color)
            {
                return binding.Property.ValueKind ==
                           Geometry::PropertyValueKind::Vec3 ||
                    binding.Property.ValueKind ==
                           Geometry::PropertyValueKind::Vec4;
            }
            return binding.Property.ValueKind == Geometry::PropertyValueKind::Vec3;
        }

        [[nodiscard]] RenderExtractionGeometrySourceRevisions
        CaptureVertexChannelRevisions(
            const ConstSourceView& view,
            const VertexChannelBindingSet* bindings,
            const GeometryElementDomain expectedDomain,
            const bool meshDefaults) noexcept
        {
            RenderExtractionGeometrySourceRevisions snapshot{};
            if (view.VertexSource == nullptr)
                return snapshot;

            const Geometry::PropertySet& properties =
                view.VertexSource->Properties;
            snapshot.VertexCount = properties.Size();
            snapshot.PositionCount = PositionCountOf(properties);
            snapshot.Position = PropertyRevisionOf(properties, kPosition);
            if (meshDefaults)
            {
                // BUG-137 — track whichever domain owns the UVs. A corner-UV
                // mesh has no `v:texcoord`, so watching only the vertex channel
                // left every seam-mesh UV edit invisible to the reupload plan.
                const auto cornerTexcoord = view.HalfedgeSource != nullptr
                    ? view.HalfedgeSource->Properties.Get<glm::vec2>(
                          "h:texcoord")
                    : Geometry::ConstProperty<glm::vec2>{};
                snapshot.Texcoord =
                    cornerTexcoord.IsValid() &&
                        view.HalfedgeSource != nullptr &&
                        cornerTexcoord.Vector().size() ==
                            view.HalfedgeSource->Properties.Size()
                    ? cornerTexcoord.Revision()
                    : PropertyRevisionOf(properties, "v:texcoord");
            }

            if (bindings != nullptr)
            {
                snapshot.BindingGeneration = bindings->BindingGeneration;
                if (BindingMatches(bindings->Normal, expectedDomain, false))
                {
                    snapshot.Normal = PropertyRevisionOf(
                        properties, bindings->Normal.Property.Name);
                }
                if (BindingMatches(bindings->Color, expectedDomain, true))
                {
                    snapshot.Color = PropertyRevisionOf(
                        properties, bindings->Color.Property.Name);
                }
            }

            if (snapshot.Normal == 0u &&
                meshDefaults &&
                (bindings == nullptr ||
                 !IsVertexChannelBindingEnabled(bindings->Normal)))
            {
                const auto cornerNormal = view.HalfedgeSource != nullptr
                    ? view.HalfedgeSource->Properties.Get<glm::vec3>(
                          "h:normal")
                    : Geometry::ConstProperty<glm::vec3>{};
                if (cornerNormal.IsValid() &&
                    view.HalfedgeSource != nullptr &&
                    cornerNormal.Vector().size() ==
                        view.HalfedgeSource->Properties.Size())
                {
                    snapshot.Normal = cornerNormal.Revision();
                }
                else
                {
                    const auto normal =
                        Geometry::ConstPropertySet{properties}.Get<glm::vec3>(
                            kNormal);
                    if (normal.IsValid())
                        snapshot.Normal = normal.Revision();
                }
            }
            if (snapshot.Color == 0u &&
                meshDefaults &&
                (bindings == nullptr ||
                 !IsVertexChannelBindingEnabled(bindings->Color)))
            {
                const Geometry::ConstPropertySet constProperties{properties};
                if (constProperties.Get<glm::vec4>("v:color").IsValid() ||
                    constProperties.Get<glm::vec3>("v:color").IsValid())
                {
                    snapshot.Color =
                        PropertyRevisionOf(properties, "v:color");
                }
            }

            // Acknowledge non-rendering property edits without turning them
            // into uploads. A later bound-channel mutation receives a new
            // process-monotonic token.
            (void)properties.Revision();
            return snapshot;
        }

        [[nodiscard]] RenderExtractionGeometrySourceRevisions
        CaptureMeshSourceRevisions(
            const ConstSourceView& view,
            const VertexChannelBindingSet* bindings) noexcept
        {
            RenderExtractionGeometrySourceRevisions snapshot =
                CaptureVertexChannelRevisions(
                    view,
                    bindings,
                    GeometryElementDomain::MeshVertex,
                    true);
            if (view.HalfedgeSource != nullptr)
            {
                const Geometry::PropertySet& properties =
                    view.HalfedgeSource->Properties;
                snapshot.Topology0 =
                    PropertyRevisionOf(properties, kHalfedgeToVertex);
                snapshot.Topology1 =
                    PropertyRevisionOf(properties, kHalfedgeNext);
                snapshot.Topology2 =
                    PropertyRevisionOf(properties, kHalfedgeFace);
                snapshot.TopologyElementCount0 = properties.Size();
                (void)properties.Revision();
            }
            if (view.FaceSource != nullptr)
            {
                const Geometry::PropertySet& properties =
                    view.FaceSource->Properties;
                snapshot.Topology3 =
                    PropertyRevisionOf(properties, kFaceHalfedge);
                snapshot.TopologyElementCount1 = properties.Size();
                (void)properties.Revision();
            }
            return snapshot;
        }

        [[nodiscard]] RenderExtractionGeometrySourceRevisions
        CaptureGraphSourceRevisions(
            const ConstSourceView& view,
            const VertexChannelBindingSet* bindings,
            const bool wantLines) noexcept
        {
            RenderExtractionGeometrySourceRevisions snapshot =
                CaptureVertexChannelRevisions(
                    view,
                    bindings,
                    GeometryElementDomain::GraphNode,
                    false);
            if (wantLines && view.EdgeSource != nullptr)
            {
                const Geometry::PropertySet& properties =
                    view.EdgeSource->Properties;
                snapshot.Topology0 = PropertyRevisionOf(properties, kEdgeV0);
                snapshot.Topology1 = PropertyRevisionOf(properties, kEdgeV1);
                snapshot.TopologyElementCount0 = properties.Size();
                (void)properties.Revision();
            }
            return snapshot;
        }

        [[nodiscard]] RenderExtractionGeometrySourceRevisions
        CapturePointCloudSourceRevisions(
            const ConstSourceView& view,
            const VertexChannelBindingSet* bindings) noexcept
        {
            return CaptureVertexChannelRevisions(
                view,
                bindings,
                GeometryElementDomain::PointCloudPoint,
                false);
        }

        [[nodiscard]] RenderExtractionGeometrySourceRevisions
        CaptureMeshPrimitiveViewSourceRevisions(
            const ConstSourceView& view,
            const bool edgeView) noexcept
        {
            RenderExtractionGeometrySourceRevisions snapshot =
                CaptureVertexChannelRevisions(
                    view,
                    nullptr,
                    GeometryElementDomain::MeshVertex,
                    false);
            if (edgeView && view.EdgeSource != nullptr)
            {
                const Geometry::PropertySet& properties =
                    view.EdgeSource->Properties;
                snapshot.Topology0 = PropertyRevisionOf(properties, kEdgeV0);
                snapshot.Topology1 = PropertyRevisionOf(properties, kEdgeV1);
                snapshot.TopologyElementCount0 = properties.Size();
                (void)properties.Revision();
            }
            if (edgeView)
            {
                bool usesExplicitEdges = false;
                if (view.EdgeSource != nullptr)
                {
                    const Geometry::ConstPropertySet edges{
                        view.EdgeSource->Properties};
                    const auto v0 = edges.Get<std::uint32_t>(kEdgeV0);
                    const auto v1 = edges.Get<std::uint32_t>(kEdgeV1);
                    usesExplicitEdges = v0.IsValid() && v1.IsValid() &&
                        v0.Size() != 0u;
                }
                if (!usesExplicitEdges && view.HalfedgeSource != nullptr)
                {
                    const Geometry::PropertySet& properties =
                        view.HalfedgeSource->Properties;
                    snapshot.Topology2 =
                        PropertyRevisionOf(properties, kHalfedgeToVertex);
                    snapshot.Topology3 =
                        PropertyRevisionOf(properties, kHalfedgeNext);
                    snapshot.Topology4 =
                        PropertyRevisionOf(properties, kHalfedgeFace);
                    snapshot.TopologyElementCount1 = properties.Size();
                    (void)properties.Revision();
                }
                if (!usesExplicitEdges && view.FaceSource != nullptr)
                {
                    const Geometry::PropertySet& properties =
                        view.FaceSource->Properties;
                    snapshot.Topology5 =
                        PropertyRevisionOf(properties, kFaceHalfedge);
                    snapshot.TopologyElementCount2 = properties.Size();
                    (void)properties.Revision();
                }
            }
            return snapshot;
        }

        void MergeVertexRevisionDelta(
            RenderExtractionGeometryDirtyPlan& plan,
            const RenderExtractionGeometrySourceRevisions& current,
            const RenderExtractionGeometrySourceRevisions& previous) noexcept
        {
            if (current.VertexCount != previous.VertexCount ||
                current.PositionCount != previous.PositionCount)
            {
                plan.RequiresFullUpload = true;
            }
            if (current.Position != previous.Position)
                plan.Channels.Position = true;
            if (current.Texcoord != previous.Texcoord)
                plan.Channels.Texcoord = true;
            if (current.Normal != previous.Normal)
                plan.Channels.Normal = true;
            if (current.Color != previous.Color)
                plan.Channels.Color = true;
            if (current.BindingGeneration != previous.BindingGeneration)
            {
                plan.Channels.Normal = true;
                plan.Channels.Color = true;
            }
        }

        void FinalizeRevisionDirtyPlan(
            RenderExtractionGeometryDirtyPlan& plan) noexcept
        {
            plan.Dirty = plan.RequiresFullUpload || plan.Channels.Any();
            plan.MeshPrimitiveViewDirty =
                plan.RequiresFullUpload || plan.Channels.Position;
        }

        void MergeMeshRevisionDelta(
            RenderExtractionGeometryDirtyPlan& plan,
            const RenderExtractionGeometrySourceRevisions& current,
            const RenderExtractionGeometrySourceRevisions& previous) noexcept
        {
            MergeVertexRevisionDelta(plan, current, previous);
            if (current.Topology0 != previous.Topology0 ||
                current.Topology1 != previous.Topology1 ||
                current.Topology2 != previous.Topology2 ||
                current.Topology3 != previous.Topology3 ||
                current.TopologyElementCount0 != previous.TopologyElementCount0 ||
                current.TopologyElementCount1 != previous.TopologyElementCount1)
            {
                plan.RequiresFullUpload = true;
            }
            FinalizeRevisionDirtyPlan(plan);
        }

        void MergeGraphRevisionDelta(
            RenderExtractionGeometryDirtyPlan& plan,
            const RenderExtractionGeometrySourceRevisions& current,
            const RenderExtractionGeometrySourceRevisions& previous) noexcept
        {
            MergeVertexRevisionDelta(plan, current, previous);
            if (current.Topology0 != previous.Topology0 ||
                current.Topology1 != previous.Topology1 ||
                current.TopologyElementCount0 != previous.TopologyElementCount0)
            {
                plan.RequiresFullUpload = true;
            }
            FinalizeRevisionDirtyPlan(plan);
        }

        void MergePointCloudRevisionDelta(
            RenderExtractionGeometryDirtyPlan& plan,
            const RenderExtractionGeometrySourceRevisions& current,
            const RenderExtractionGeometrySourceRevisions& previous) noexcept
        {
            MergeVertexRevisionDelta(plan, current, previous);
            FinalizeRevisionDirtyPlan(plan);
        }

        [[nodiscard]] bool MeshPrimitiveViewRevisionChanged(
            const RenderExtractionGeometrySourceRevisions& current,
            const RenderExtractionGeometrySourceRevisions& previous,
            const bool edgeView) noexcept
        {
            if (current.VertexCount != previous.VertexCount ||
                current.PositionCount != previous.PositionCount ||
                current.Position != previous.Position)
            {
                return true;
            }
            return edgeView &&
                (current.Topology0 != previous.Topology0 ||
                 current.Topology1 != previous.Topology1 ||
                 current.Topology2 != previous.Topology2 ||
                 current.Topology3 != previous.Topology3 ||
                 current.Topology4 != previous.Topology4 ||
                 current.Topology5 != previous.Topology5 ||
                 current.TopologyElementCount0 !=
                     previous.TopologyElementCount0 ||
                 current.TopologyElementCount1 !=
                     previous.TopologyElementCount1 ||
                 current.TopologyElementCount2 !=
                     previous.TopologyElementCount2);
        }
    }

    Graphics::GeometryResidencyCoordinator&
    RenderExtractionCache::State::EnsureGeometryResidency(
        Graphics::IRenderer& renderer)
    {
        Graphics::GpuWorld& world = renderer.GetGpuWorld();
        if (m_GeometryResidency == nullptr ||
            m_GeometryResidencyWorld != &world)
        {
            if (m_GeometryResidency != nullptr)
            {
                m_GeometryResidency->Shutdown();
            }
            m_GeometryResidency =
                std::make_unique<Graphics::GeometryResidencyCoordinator>(world);
            m_GeometryResidencyWorld = &world;
        }
        return *m_GeometryResidency;
    }

    std::uint64_t
    RenderExtractionCache::State::IssueGeometryPlanGeneration() noexcept
    {
        const std::uint64_t generation = m_NextGeometryPlanGeneration;
        if (m_NextGeometryPlanGeneration !=
            std::numeric_limits<std::uint64_t>::max())
        {
            ++m_NextGeometryPlanGeneration;
        }
        return generation;
    }

    bool RenderExtractionCache::State::ReleaseGeometryResidency(
        const Graphics::GeometryResidencyKey key)
    {
        return m_GeometryResidency != nullptr &&
            m_GeometryResidency->Release(key);
    }

    bool RenderExtractionCache::State::BindMeshGeometry(
        entt::registry& registry,
        entt::entity entity,
        const std::uint32_t stableId,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        const bool hadResidency = sidecar.MeshGeometry.IsValid();
        const auto* channelBindings =
            registry.try_get<VertexChannelBindingSet>(entity);
        const RenderExtractionGeometrySourceRevisions sourceRevisions =
            CaptureMeshSourceRevisions(view, channelBindings);
        RenderExtractionGeometryDirtyPlan dirtyPlan =
            BuildRenderExtractionMeshGeometryDirtyPlan(registry, entity);
        if (hadResidency)
        {
            MergeMeshRevisionDelta(
                dirtyPlan, sourceRevisions, sidecar.MeshSourceRevisions);
        }
        const bool dirty = dirtyPlan.Dirty;
        const Graphics::GeometryResidencyKey residencyKey =
            BuildRenderExtractionGeometryResidencyKey(
                RenderExtractionGeometryResidencyKind::Mesh,
                stableId);

        // Fail-closed release for a dirty plan/reconcile failure. When the
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
                sidecar.MeshSourceVertexForGpuVertex.clear();
                return;
            }
            (void)ReleaseGeometryResidency(residencyKey);
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                Graphics::GpuGeometryHandle{});
            sidecar.MeshGeometry = {};
            sidecar.MeshSourceVertexForGpuVertex.clear();
            ++stats.MeshGeometryReleases;
        };

        // Reuse path: clean entity with a cached upload. The procedural-
        // cache analogue is a refcount-only `EnsureResident` hit; for mesh
        // residency the per-entity handle is single-owner so the reuse is
        // a direct rebind without any cache lookup.
        if (hadResidency && !dirty)
        {
            sidecar.MeshSourceRevisions = sourceRevisions;
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
        const bool partialPreferred =
            hadResidency && dirty && !dirtyPlan.RequiresFullUpload;
        MeshPlanBuildResult packResult = BuildMeshGeometryPlan(
            view,
            channelBindings,
            GeometryPlanBuildRequest{
                .Key = residencyKey,
                .Generation = IssueGeometryPlanGeneration(),
                .UpdateClass = partialPreferred
                    ? Graphics::GeometryUploadUpdateClass::PartialPreferred
                    : Graphics::GeometryUploadUpdateClass::FullReplacement,
                .UpdateChannels = partialPreferred
                    ? dirtyPlan.Channels
                    : Graphics::GpuWorld::GeometryChannelUpdateMask{},
            },
            m_MeshPack);
        if (packResult.Status != MeshPackStatus::Success
            || !packResult.Plan.has_value())
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

        const Graphics::GeometryResidencyResult residency =
            EnsureGeometryResidency(renderer).Reconcile(*packResult.Plan);
        if (!residency.Succeeded())
        {
            ++stats.MeshGeometryFailedPack;
            releaseStaleResidency();
            return false;
        }

        if (residency.Status ==
            Graphics::GeometryResidencyStatus::PartiallyUpdated)
        {
            ++stats.MeshGeometryReuploads;
            ++stats.MeshGeometryPartialUploads;
        }
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::FullyReuploaded)
        {
            ++stats.MeshGeometryReuploads;
            ++stats.MeshGeometryReleases;
        }
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::Uploaded)
        {
            ++stats.MeshGeometryUploads;
        }
        else
        {
            ++stats.MeshGeometryReuseHits;
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

        if (sidecar.MeshSourceVertexForGpuVertex !=
            m_MeshPack.SourceVertexForGpuVertex)
        {
            ++sidecar.MeshVertexRemapRevision;
            if (sidecar.MeshVertexRemapRevision == 0u)
                ++sidecar.MeshVertexRemapRevision;
        }
        sidecar.MeshSourceVertexForGpuVertex =
            m_MeshPack.SourceVertexForGpuVertex;
        sidecar.MeshGeometry = residency.Handle;
        sidecar.MeshSourceRevisions = sourceRevisions;
        sidecar.Geometry = residency.Handle;
        sidecar.GpuSlot.SetGeometryHandle(residency.Handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(
            sidecar.Instance, residency.Handle);
        return true;
    }

    bool RenderExtractionCache::State::BindGraphGeometry(
        entt::registry& registry,
        entt::entity entity,
        const std::uint32_t stableId,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        namespace G = Graphics::Components;

        // The render hints select the lanes: `RenderEdges` requests edge line
        // indices, `RenderPoints` requests the node point lane. Both share the
        // single node-position vertex buffer (one handle per graph entity).
        const bool wantLines = registry.all_of<G::RenderEdges>(entity);
        const bool wantPoints = registry.all_of<G::RenderPoints>(entity);

        const bool hadResidency = sidecar.GraphGeometry.IsValid();
        const auto* channelBindings =
            registry.try_get<VertexChannelBindingSet>(entity);
        const RenderExtractionGeometrySourceRevisions sourceRevisions =
            CaptureGraphSourceRevisions(view, channelBindings, wantLines);
        RenderExtractionGeometryDirtyPlan dirtyPlan =
            BuildRenderExtractionGraphGeometryDirtyPlan(registry, entity);
        if (hadResidency)
        {
            MergeGraphRevisionDelta(
                dirtyPlan, sourceRevisions, sidecar.GraphSourceRevisions);
        }
        const bool dirty = dirtyPlan.Dirty;
        const Graphics::GeometryResidencyKey residencyKey =
            BuildRenderExtractionGeometryResidencyKey(
                RenderExtractionGeometryResidencyKind::Graph,
                stableId);
        // A change in requested render lanes repacks: the cached upload was
        // packed for a specific lane mask, and the line lane in particular
        // changes the packed line indices. Without this, gaining/losing a
        // line hint on an otherwise-clean graph would rebind a stale upload.
        const bool lanesChanged = hadResidency
            && (sidecar.GraphPackedLines != wantLines
                || sidecar.GraphPackedPoints != wantPoints);

        // Fail-closed release for a dirty plan/reconcile failure — see the
        // mesh bridge for the rationale. The caller's eligibility-flip release
        // cannot cover this because the entity is still graph-domain. Clears the
        // packed-lane flags alongside the handle so a later fresh upload re-sets
        // them.
        const auto releaseStaleResidency = [&]() {
            if (!sidecar.GraphGeometry.IsValid())
            {
                return;
            }
            (void)ReleaseGeometryResidency(residencyKey);
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
            sidecar.GraphSourceRevisions = sourceRevisions;
            ++stats.GraphGeometryReuseHits;
            sidecar.Geometry = sidecar.GraphGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.GraphGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                sidecar.GraphGeometry);
            return true;
        }

        const bool partialPreferred =
            hadResidency && dirty && !dirtyPlan.RequiresFullUpload
            && !lanesChanged;
        GraphPlanBuildResult packResult = BuildGraphGeometryPlan(
            view,
            wantLines,
            wantPoints,
            channelBindings,
            GeometryPlanBuildRequest{
                .Key = residencyKey,
                .Generation = IssueGeometryPlanGeneration(),
                .UpdateClass = partialPreferred
                    ? Graphics::GeometryUploadUpdateClass::PartialPreferred
                    : Graphics::GeometryUploadUpdateClass::FullReplacement,
                .UpdateChannels = partialPreferred
                    ? dirtyPlan.Channels
                    : Graphics::GpuWorld::GeometryChannelUpdateMask{},
            },
            m_GraphPack);
        if (packResult.Status != GraphPackStatus::Success
            || !packResult.Plan.has_value())
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

        const Graphics::GeometryResidencyResult residency =
            EnsureGeometryResidency(renderer).Reconcile(*packResult.Plan);
        if (!residency.Succeeded())
        {
            ++stats.GraphGeometryFailedPack;
            releaseStaleResidency();
            return false;
        }

        if (residency.Status ==
            Graphics::GeometryResidencyStatus::PartiallyUpdated)
        {
            ++stats.GraphGeometryReuploads;
            ++stats.GraphGeometryPartialUploads;
        }
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::FullyReuploaded)
        {
            ++stats.GraphGeometryReuploads;
            ++stats.GraphGeometryReleases;
        }
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::Uploaded)
        {
            ++stats.GraphGeometryUploads;
        }
        else
        {
            ++stats.GraphGeometryReuseHits;
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

        sidecar.GraphGeometry = residency.Handle;
        sidecar.GraphSourceRevisions = sourceRevisions;
        sidecar.GraphPackedLines = wantLines;
        sidecar.GraphPackedPoints = wantPoints;
        sidecar.Geometry = residency.Handle;
        sidecar.GpuSlot.SetGeometryHandle(residency.Handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(
            sidecar.Instance, residency.Handle);
        return true;
    }

    bool RenderExtractionCache::State::BindPointCloudGeometry(
        entt::registry& registry,
        entt::entity entity,
        const std::uint32_t stableId,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        namespace G = Graphics::Components;
        const Graphics::GeometryResidencyKey residencyKey =
            BuildRenderExtractionGeometryResidencyKey(
                RenderExtractionGeometryResidencyKind::PointCloud,
                stableId);

        // Fail-closed release for an unsupported-size-source or dirty-reupload
        // plan/reconcile failure — see the mesh bridge for the rationale. The
        // caller's eligibility-flip release cannot cover this because the entity
        // is still point-cloud-domain, so this is the only place such a failure
        // can release a previously-resident cloud.
        const auto releaseStaleResidency = [&]() {
            if (!sidecar.PointCloudGeometry.IsValid())
            {
                return;
            }
            (void)ReleaseGeometryResidency(residencyKey);
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

        const bool hadResidency = sidecar.PointCloudGeometry.IsValid();
        const auto* channelBindings =
            registry.try_get<VertexChannelBindingSet>(entity);
        const RenderExtractionGeometrySourceRevisions sourceRevisions =
            CapturePointCloudSourceRevisions(view, channelBindings);
        RenderExtractionGeometryDirtyPlan dirtyPlan =
            BuildRenderExtractionPointCloudGeometryDirtyPlan(registry, entity);
        if (hadResidency)
        {
            MergePointCloudRevisionDelta(
                dirtyPlan,
                sourceRevisions,
                sidecar.PointCloudSourceRevisions);
        }
        const bool dirty = dirtyPlan.Dirty;

        // Reuse path: clean point-cloud entity with a cached upload. Mirrors
        // the single-owner mesh reuse — a direct rebind without any repack.
        if (hadResidency && !dirty)
        {
            sidecar.PointCloudSourceRevisions = sourceRevisions;
            ++stats.PointCloudGeometryReuseHits;
            sidecar.Geometry = sidecar.PointCloudGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.PointCloudGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(
                sidecar.Instance,
                sidecar.PointCloudGeometry);
            return true;
        }

        const bool partialPreferred =
            hadResidency && dirty && !dirtyPlan.RequiresFullUpload;
        PointCloudPlanBuildResult packResult =
            BuildPointCloudGeometryPlan(
                view,
                channelBindings,
                GeometryPlanBuildRequest{
                    .Key = residencyKey,
                    .Generation = IssueGeometryPlanGeneration(),
                    .UpdateClass = partialPreferred
                        ? Graphics::GeometryUploadUpdateClass::PartialPreferred
                        : Graphics::GeometryUploadUpdateClass::FullReplacement,
                    .UpdateChannels = partialPreferred
                        ? dirtyPlan.Channels
                        : Graphics::GpuWorld::GeometryChannelUpdateMask{},
                },
                m_PointCloudPack);
        if (packResult.Status != PointCloudPackStatus::Success
            || !packResult.Plan.has_value())
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

        const Graphics::GeometryResidencyResult residency =
            EnsureGeometryResidency(renderer).Reconcile(*packResult.Plan);
        if (!residency.Succeeded())
        {
            ++stats.PointCloudGeometryFailedPack;
            releaseStaleResidency();
            return false;
        }

        if (residency.Status ==
            Graphics::GeometryResidencyStatus::PartiallyUpdated)
        {
            ++stats.PointCloudGeometryReuploads;
            ++stats.PointCloudGeometryPartialUploads;
        }
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::FullyReuploaded)
        {
            ++stats.PointCloudGeometryReuploads;
            ++stats.PointCloudGeometryReleases;
        }
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::Uploaded)
        {
            ++stats.PointCloudGeometryUploads;
        }
        else
        {
            ++stats.PointCloudGeometryReuseHits;
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

        sidecar.PointCloudGeometry = residency.Handle;
        sidecar.PointCloudSourceRevisions = sourceRevisions;
        sidecar.Geometry = residency.Handle;
        sidecar.GpuSlot.SetGeometryHandle(residency.Handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(
            sidecar.Instance, residency.Handle);
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
        const Graphics::GeometryResidencyKey residencyKey =
            BuildRenderExtractionGeometryResidencyKey(
                RenderExtractionGeometryResidencyKind::MeshPrimitiveView,
                stableId,
                isEdge ? 1u : 2u);

        // Disabled (or parent no longer resident): release any existing view.
        if (!desired)
        {
            ReleaseMeshPrimitiveView(
                kind, stableId, sidecar, renderer, stats);
            return false;
        }

        if (!isEdge
            && (points == nullptr
                || !std::holds_alternative<float>(points->SizeSource)))
        {
            ++stats.MeshVertexViewFailedPack;
            ReleaseMeshPrimitiveView(
                kind, stableId, sidecar, renderer, stats);
            return false;
        }

        const bool hadView = geometry.IsValid();
        RenderExtractionGeometrySourceRevisions& observedSourceRevisions =
            isEdge
                ? sidecar.MeshEdgeViewSourceRevisions
                : sidecar.MeshVertexViewSourceRevisions;
        const RenderExtractionGeometrySourceRevisions sourceRevisions =
            CaptureMeshPrimitiveViewSourceRevisions(view, isEdge);
        const bool sourceRevisionDirty = hadView &&
            MeshPrimitiveViewRevisionChanged(
                sourceRevisions, observedSourceRevisions, isEdge);
        const bool effectiveMeshDirty = meshDirty || sourceRevisionDirty;

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
        if (hadView && !effectiveMeshDirty)
        {
            observedSourceRevisions = sourceRevisions;
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

        // Build an owning plan for the first upload or parent-dirty reupload.
        // The domain-specific topology conversion stays private to runtime.
        const GeometryPlanBuildRequest request{
            .Key = residencyKey,
            .Generation = IssueGeometryPlanGeneration(),
        };
        const MeshPrimitiveViewPlanBuildResult packResult = isEdge
            ? BuildMeshEdgeViewPlan(view, request, m_MeshPrimitiveViewPack)
            : BuildMeshVertexViewPlan(view, request, m_MeshPrimitiveViewPack);
        if (packResult.Status != MeshPrimitiveViewStatus::Success
            || !packResult.Plan.has_value())
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
            ReleaseMeshPrimitiveView(
                kind, stableId, sidecar, renderer, stats);
            return false;
        }

        // Ensure the view has its own instance. Allocated lazily on first
        // upload and kept until the view is released. Allocate before
        // publishing the plan so an exhausted instance pool cannot create an
        // unowned geometry allocation.
        if (!instance.IsValid())
        {
            instance = renderer.GetGpuWorld().AllocateInstance(stableId);
            if (!instance.IsValid())
            {
                if (isEdge)
                {
                    ++stats.MeshEdgeViewFailedPack;
                }
                else
                {
                    ++stats.MeshVertexViewFailedPack;
                }
                ReleaseMeshPrimitiveView(
                    kind, stableId, sidecar, renderer, stats);
                return false;
            }
            ++stats.AllocatedInstanceCount;
        }

        const Graphics::GeometryResidencyResult residency =
            EnsureGeometryResidency(renderer).Reconcile(*packResult.Plan);
        if (!residency.Succeeded())
        {
            if (isEdge)
            {
                ++stats.MeshEdgeViewFailedPack;
            }
            else
            {
                ++stats.MeshVertexViewFailedPack;
            }
            ReleaseMeshPrimitiveView(
                kind, stableId, sidecar, renderer, stats);
            return false;
        }

        if (residency.Status ==
            Graphics::GeometryResidencyStatus::FullyReuploaded)
        {
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
        else if (residency.Status ==
                 Graphics::GeometryResidencyStatus::Uploaded)
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
        else
        {
            if (isEdge)
            {
                ++stats.MeshEdgeViewReuseHits;
            }
            else
            {
                ++stats.MeshVertexViewReuseHits;
            }
        }

        geometry = residency.Handle;
        observedSourceRevisions = sourceRevisions;
        renderer.GetGpuWorld().SetInstanceGeometry(
            instance, residency.Handle);
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
            .HasProceduralResidency =
                it->second.ProceduralKey.has_value(),
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
        const bool builderSupportsKind =
            ref.Kind ==
            ECS::Components::ProceduralGeometryKind::Triangle;
        if (!builderSupportsKind)
        {
            ++stats.ProceduralGeometryMissingPacker;
            return false;
        }

        const std::uint64_t paramsHash =
            HashProceduralGeometryParams(ref.Params);
        const std::uint64_t residencyIdentity =
            paramsHash == 0u ? 1u : paramsHash;
        const Graphics::GeometryResidencyKey residencyKey =
            BuildRenderExtractionGeometryResidencyKey(
                RenderExtractionGeometryResidencyKind::Procedural,
                residencyIdentity,
                static_cast<std::uint32_t>(ref.Kind) + 1u);

        // A sidecar already owns exactly one reference. Rebinding the same
        // procedural source every frame must not acquire repeatedly.
        if (sidecar.ProceduralKey.has_value()
            && *sidecar.ProceduralKey == residencyKey)
        {
            const auto resident =
                EnsureGeometryResidency(renderer).Find(residencyKey);
            if (resident.has_value() && resident->RefCount > 0u)
            {
                ++stats.ProceduralGeometryReuseHits;
                sidecar.Geometry = resident->Handle;
                sidecar.GpuSlot.SetGeometryHandle(resident->Handle);
                sidecar.GpuSlot.ClearSourceAsset();
                renderer.GetGpuWorld().SetInstanceGeometry(
                    sidecar.Instance,
                    resident->Handle);
                return true;
            }
        }

        if (sidecar.ProceduralKey.has_value()
            && *sidecar.ProceduralKey != residencyKey)
        {
            (void)ReleaseGeometryResidency(*sidecar.ProceduralKey);
            ++stats.ProceduralGeometryReleases;
            sidecar.ProceduralKey.reset();
        }

        const std::optional<Graphics::GeometryUploadPlan> plan =
            BuildProceduralGeometryPlan(
                ref.Kind,
                ref.Params,
                GeometryPlanBuildRequest{
                    .Key = residencyKey,
                    .Generation = residencyIdentity,
                    .UpdateClass = Graphics::GeometryUploadUpdateClass::
                        FullReplacement,
                },
                m_ProceduralPack);
        if (!plan.has_value())
        {
            ++stats.ProceduralGeometryInvalidParams;
            return false;
        }

        Graphics::GeometryResidencyCoordinator& coordinator =
            EnsureGeometryResidency(renderer);
        const Graphics::GeometryResidencyStats before = coordinator.Stats();
        const Graphics::GeometryResidencyResult residency =
            coordinator.Acquire(*plan);
        if (!residency.Succeeded())
        {
            if (residency.Status ==
                Graphics::GeometryResidencyStatus::RefCountSaturated)
            {
                ++stats.ProceduralGeometryRefCountSaturated;
            }
            else
            {
                ++stats.ProceduralGeometryFailedPack;
            }
            return false;
        }
        if (residency.Status == Graphics::GeometryResidencyStatus::Uploaded)
        {
            ++stats.ProceduralGeometryUploads;
        }
        else
        {
            ++stats.ProceduralGeometryReuseHits;
        }
        stats.ProceduralGeometryRetireCancellations +=
            static_cast<std::uint32_t>(
                coordinator.Stats().RetireCancellations
                - before.RetireCancellations);

        sidecar.Geometry = residency.Handle;
        sidecar.ProceduralKey = residencyKey;
        sidecar.GpuSlot.SetGeometryHandle(residency.Handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(
            sidecar.Instance,
            residency.Handle);
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

    void RenderExtractionCache::State::ReleaseMeshPrimitiveView(
        const MeshPrimitiveViewKind kind,
        const std::uint32_t stableId,
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
            (void)ReleaseGeometryResidency(
                BuildRenderExtractionGeometryResidencyKey(
                    RenderExtractionGeometryResidencyKind::MeshPrimitiveView,
                    stableId,
                    isEdge ? 1u : 2u));
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
                (void)ReleaseGeometryResidency(
                    *it->second.ProceduralKey);
                ++stats.ProceduralGeometryReleases;
            }
            if (it->second.MeshGeometry.IsValid())
            {
                (void)ReleaseGeometryResidency(
                    BuildRenderExtractionGeometryResidencyKey(
                        RenderExtractionGeometryResidencyKind::Mesh,
                        it->first));
                ++stats.MeshGeometryReleases;
            }
            if (it->second.GraphGeometry.IsValid())
            {
                (void)ReleaseGeometryResidency(
                    BuildRenderExtractionGeometryResidencyKey(
                        RenderExtractionGeometryResidencyKind::Graph,
                        it->first));
                ++stats.GraphGeometryReleases;
            }
            ReleaseGraphPointLaneInstance(
                it->second,
                renderer,
                stats);
            if (it->second.PointCloudGeometry.IsValid())
            {
                (void)ReleaseGeometryResidency(
                    BuildRenderExtractionGeometryResidencyKey(
                        RenderExtractionGeometryResidencyKind::PointCloud,
                        it->first));
                ++stats.PointCloudGeometryReleases;
            }
            ReleaseMeshPrimitiveView(
                MeshPrimitiveViewKind::Edge,
                it->first,
                it->second,
                renderer,
                stats);
            ReleaseMeshPrimitiveView(
                MeshPrimitiveViewKind::Vertex,
                it->first,
                it->second,
                renderer,
                stats);
            m_MaterialTextureBindings.erase(it->first);
            renderer.GetGpuWorld().FreeInstance(
                it->second.Instance);
            it = m_Renderables.erase(it);
            ++stats.FreedInstanceCount;
        }
    }

    void RenderExtractionCache::State::TickGeometryResidency(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        const Graphics::GeometryResidencyTickResult retired =
            EnsureGeometryResidency(renderer).Tick(
                currentFrame, framesInFlight);
        for (const Graphics::GeometryResidencyKey key : retired.FreedKeys)
        {
            switch (static_cast<RenderExtractionGeometryResidencyKind>(
                key.Namespace))
            {
            case RenderExtractionGeometryResidencyKind::Mesh:
                ++m_MeshFreeRetires;
                break;
            case RenderExtractionGeometryResidencyKind::Graph:
                ++m_GraphFreeRetires;
                break;
            case RenderExtractionGeometryResidencyKind::PointCloud:
                ++m_PointCloudFreeRetires;
                break;
            case RenderExtractionGeometryResidencyKind::Procedural:
                ++m_ProceduralFreeRetires;
                break;
            case RenderExtractionGeometryResidencyKind::MeshPrimitiveView:
                ++m_MeshPrimitiveViewFreeRetires;
                break;
            }
        }
    }
}
