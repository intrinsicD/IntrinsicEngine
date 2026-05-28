module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.RenderExtraction;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.SpatialDebugBinding;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Light;
import Extrinsic.Asset.Registry;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;
import Extrinsic.Runtime.SpatialDebugAdapters;

namespace Extrinsic::Runtime
{
    const RuntimeRenderExtractionStats& RenderExtractionCache::GetLastStats() const noexcept
    {
        return m_LastStats;
    }

    std::uint32_t RenderExtractionCache::GetTrackedRenderableCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_Renderables.size());
    }

    std::optional<RenderExtractionCache::RenderableSidecarView> RenderExtractionCache::FindRenderableSidecarForTest(
        std::uint32_t stableEntityId) const noexcept
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
        };
    }

    const ProceduralGeometryCache& RenderExtractionCache::GetProceduralGeometryCacheForTest() const noexcept
    {
        return m_ProceduralGeometry;
    }

    void RenderExtractionCache::RegisterSpatialDebugAdapter(std::uint64_t key,
                                                            std::unique_ptr<ISpatialDebugAdapter> adapter)
    {
        // Replace-on-collision: clear the registry slot before destroying
        // the prior adapter so the registry never observes a dangling raw
        // pointer between erase and re-register.
        m_SpatialDebugRegistry.Unregister(key);
        auto [it, inserted] = m_SpatialDebugAdapters.insert_or_assign(key, std::move(adapter));
        if (it->second)
        {
            m_SpatialDebugRegistry.Register(key, *it->second);
        }
    }

    bool RenderExtractionCache::UnregisterSpatialDebugAdapter(std::uint64_t key) noexcept
    {
        m_SpatialDebugRegistry.Unregister(key);
        return m_SpatialDebugAdapters.erase(key) != 0u;
    }

    std::size_t RenderExtractionCache::GetSpatialDebugAdapterCount() const noexcept
    {
        return m_SpatialDebugAdapters.size();
    }

    const SpatialDebugAdapterRegistry& RenderExtractionCache::GetSpatialDebugRegistryForTest() const noexcept
    {
        return m_SpatialDebugRegistry;
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::uint32_t StableEntityId(entt::entity entity) noexcept
        {
            return static_cast<std::uint32_t>(entity);
        }

        [[nodiscard]] bool HasRenderableHint(const entt::registry& registry, entt::entity entity)
        {
            namespace G = Graphics::Components;
            return registry.any_of<G::RenderSurface, G::RenderLines, G::RenderPoints>(entity);
        }

        [[nodiscard]] std::uint32_t BuildRenderFlags(const entt::registry& registry, entt::entity entity)
        {
            namespace G = Graphics::Components;
            std::uint32_t flags = RHI::GpuRender_Visible | RHI::GpuRender_Opaque;
            if (registry.all_of<G::RenderSurface>(entity))
                flags |= RHI::GpuRender_Surface;
            if (registry.all_of<G::RenderLines>(entity))
                flags |= RHI::GpuRender_Line | RHI::GpuRender_Unlit;
            if (registry.all_of<G::RenderPoints>(entity))
                flags |= RHI::GpuRender_Point | RHI::GpuRender_Unlit;
            return flags;
        }

        [[nodiscard]] glm::vec3 TranslationOf(const glm::mat4& model) noexcept
        {
            return {model[3].x, model[3].y, model[3].z};
        }

        [[nodiscard]] RHI::GpuBounds DefaultBoundsFor(const glm::mat4& model) noexcept
        {
            const glm::vec3 center = TranslationOf(model);
            RHI::GpuBounds bounds{};
            bounds.LocalSphere = {0.f, 0.f, 0.f, 1.f};
            bounds.WorldSphere = {center, 1.f};
            bounds.WorldAabbMin = {center - glm::vec3{1.f}, 0.f};
            bounds.WorldAabbMax = {center + glm::vec3{1.f}, 0.f};
            return bounds;
        }

        [[nodiscard]] RHI::GpuBounds ExtractBounds(const entt::registry& registry,
                                                   entt::entity entity,
                                                   const glm::mat4& model)
        {
            RHI::GpuBounds bounds = DefaultBoundsFor(model);
            if (const auto* worldBounds = registry.try_get<ECS::Components::Culling::World::Bounds>(entity))
            {
                bounds.WorldSphere = {worldBounds->WorldBoundingSphere.Center,
                                      worldBounds->WorldBoundingSphere.Radius};
                bounds.WorldAabbMin = {worldBounds->WorldBoundingOBB.Center - worldBounds->WorldBoundingOBB.Extents, 0.f};
                bounds.WorldAabbMax = {worldBounds->WorldBoundingOBB.Center + worldBounds->WorldBoundingOBB.Extents, 0.f};
            }
            return bounds;
        }

        [[nodiscard]] Graphics::LightSnapshot MakeDirectionalLight(const ECS::Components::Lights::DirectionalLight& light,
                                                                   const glm::mat4& model)
        {
            Graphics::LightSnapshot snapshot{};
            snapshot.LightType = Graphics::LightSnapshot::Type::Directional;
            snapshot.Direction = glm::vec3(model * glm::vec4(0.f, -1.f, 0.f, 0.f));
            snapshot.Color = light.Color;
            snapshot.Intensity = light.Intensity;
            return snapshot;
        }

        [[nodiscard]] Graphics::LightSnapshot MakePointLight(const ECS::Components::Lights::PointLight& light,
                                                             const glm::mat4& model)
        {
            Graphics::LightSnapshot snapshot{};
            snapshot.LightType = Graphics::LightSnapshot::Type::Point;
            snapshot.Position = TranslationOf(model);
            snapshot.Range = 10.f;
            snapshot.Color = light.Color;
            snapshot.Intensity = light.Intensity;
            return snapshot;
        }

        [[nodiscard]] Graphics::LightSnapshot MakeSpotLight(const ECS::Components::Lights::SpotLight& light,
                                                            const glm::mat4& model)
        {
            Graphics::LightSnapshot snapshot{};
            snapshot.LightType = Graphics::LightSnapshot::Type::Spot;
            snapshot.Position = TranslationOf(model);
            snapshot.Direction = light.Direction;
            snapshot.Range = 10.f;
            snapshot.Color = light.Color;
            snapshot.Intensity = light.Intensity;
            return snapshot;
        }

        [[nodiscard]] Assets::AssetId NormalizeAssetSource(
            const ECS::Components::AssetInstance::Source& source) noexcept
        {
            if (source.AssetId == std::numeric_limits<std::uint32_t>::max())
            {
                return {};
            }
            return Assets::AssetId{source.AssetId, 1u};
        }

        void AccumulateAssetObservationStats(const RuntimeRenderableAssetGenerationObservation& observation,
                                             RuntimeRenderExtractionStats& stats) noexcept
        {
            switch (observation.Status)
            {
            case RuntimeRenderableAssetObservationStatus::NoSourceAsset:
                break;
            case RuntimeRenderableAssetObservationStatus::CacheUnavailable:
                ++stats.SourceAssetCacheUnavailableCount;
                break;
            case RuntimeRenderableAssetObservationStatus::ViewUnavailable:
                ++stats.SourceAssetViewUnavailableCount;
                break;
            case RuntimeRenderableAssetObservationStatus::GenerationUnavailable:
                ++stats.SourceAssetGenerationUnavailableCount;
                break;
            case RuntimeRenderableAssetObservationStatus::UpToDate:
                ++stats.SourceAssetUpToDateCount;
                break;
            case RuntimeRenderableAssetObservationStatus::RebindRequired:
                ++stats.SourceAssetRebindRequiredCount;
                break;
            }
        }
    }

    RuntimeRenderableAssetGenerationObservation ObserveRenderableAssetGeneration(
        Graphics::Components::GpuSceneSlot& slot,
        const Assets::AssetId sourceAsset,
        Graphics::GpuAssetCache* gpuAssets)
    {
        RuntimeRenderableAssetGenerationObservation observation{};
        observation.SourceAsset = sourceAsset;

        if (!sourceAsset.IsValid())
        {
            slot.ClearSourceAsset();
            return observation;
        }

        if (!slot.HasSourceAsset() || slot.SourceAsset != sourceAsset)
        {
            slot.SetSourceAsset(sourceAsset, 0);
        }

        if (!gpuAssets)
        {
            observation.Status = RuntimeRenderableAssetObservationStatus::CacheUnavailable;
            return observation;
        }

        auto view = gpuAssets->GetView(sourceAsset);
        if (!view.has_value())
        {
            observation.Status = RuntimeRenderableAssetObservationStatus::ViewUnavailable;
            return observation;
        }

        observation.ObservedGeneration = view->Generation;
        observation.Decision = slot.EvaluateSourceAssetRebind(sourceAsset, view->Generation);
        switch (observation.Decision)
        {
        case Graphics::Components::GpuSceneSlotAssetRebindDecision::GenerationUnavailable:
            observation.Status = RuntimeRenderableAssetObservationStatus::GenerationUnavailable;
            break;
        case Graphics::Components::GpuSceneSlotAssetRebindDecision::UpToDate:
            observation.Status = RuntimeRenderableAssetObservationStatus::UpToDate;
            break;
        case Graphics::Components::GpuSceneSlotAssetRebindDecision::RebindRequired:
            observation.Status = RuntimeRenderableAssetObservationStatus::RebindRequired;
            break;
        case Graphics::Components::GpuSceneSlotAssetRebindDecision::NoSourceAsset:
        case Graphics::Components::GpuSceneSlotAssetRebindDecision::AssetMismatch:
            observation.Status = RuntimeRenderableAssetObservationStatus::NoSourceAsset;
            break;
        }
        return observation;
    }

    RuntimeRenderableAssetAcknowledgmentResult AcknowledgeRenderableAssetRebind(
        Graphics::Components::GpuSceneSlot& slot,
        const RuntimeRenderableAssetGenerationObservation& observation) noexcept
    {
        if (!slot.HasSourceAsset())
        {
            return RuntimeRenderableAssetAcknowledgmentResult::SkippedNoSourceAsset;
        }
        if (!observation.SourceAsset.IsValid() || observation.SourceAsset != slot.SourceAsset)
        {
            return RuntimeRenderableAssetAcknowledgmentResult::SkippedAssetMismatch;
        }
        if (observation.ObservedGeneration == 0)
        {
            return RuntimeRenderableAssetAcknowledgmentResult::SkippedNoObservedGeneration;
        }
        slot.UpdateLastSeenAssetGeneration(observation.ObservedGeneration);
        return RuntimeRenderableAssetAcknowledgmentResult::Acknowledged;
    }

    RenderExtractionCache::RenderableSidecar* RenderExtractionCache::EnsureRenderable(
        const std::uint32_t stableId,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        if (auto it = m_Renderables.find(stableId); it != m_Renderables.end())
        {
            return &it->second;
        }

        RenderableSidecar sidecar{};
        sidecar.Instance = renderer.GetGpuWorld().AllocateInstance(stableId);
        if (!sidecar.Instance.IsValid())
        {
            return nullptr;
        }
        sidecar.GpuSlot.SetInstanceHandle(sidecar.Instance);

        const Graphics::MaterialTypeHandle standardType = renderer.GetMaterialSystem().FindType("StandardPBR");
        if (standardType.IsValid())
        {
            sidecar.Material.Lease = renderer.GetMaterialSystem().CreateInstance(standardType, Graphics::MaterialParams{});
            sidecar.Material.EffectiveSlot = renderer.GetMaterialSystem().GetMaterialSlot(sidecar.Material.Lease.GetHandle());
        }

        ++stats.AllocatedInstanceCount;
        auto [it, inserted] = m_Renderables.emplace(stableId, std::move(sidecar));
        (void)inserted;
        return &it->second;
    }

    bool RenderExtractionCache::BindProceduralGeometry(const ECS::Components::ProceduralGeometryRef& ref,
                                                         RenderableSidecar& sidecar,
                                                         Graphics::IRenderer& renderer,
                                                         RuntimeRenderExtractionStats& stats)
    {
        const bool packerSupportsKind = ref.Kind == ECS::Components::ProceduralGeometryKind::Triangle;
        if (!packerSupportsKind)
        {
            ++stats.ProceduralGeometryMissingPacker;
            return false;
        }

        const ProceduralGeometryKey key = MakeProceduralGeometryKey(ref.Kind, ref.Params);

        Graphics::GpuWorld::GeometryUploadDesc desc{};
        const ProceduralGeometryCacheEntry* existing = m_ProceduralGeometry.Find(key);
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

        ProceduralGeometryCache::UploadFn upload = [&renderer](
            const Graphics::GpuWorld::GeometryUploadDesc& uploadDesc) {
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
        renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, handle);
        return true;
    }

    bool RenderExtractionCache::BindMeshGeometry(const ECS::Components::GeometrySources::ConstSourceView& view,
                                                  RenderableSidecar& sidecar,
                                                  Graphics::IRenderer& renderer,
                                                  RuntimeRenderExtractionStats& stats)
    {
        // Reuse path: once we have packed and uploaded a mesh for this
        // entity, subsequent extractions reuse the same handle until
        // Slice C drains the relevant dirty-domain tags and triggers a
        // reupload. This keeps Slice B's upload counter at exactly one
        // per entity and matches the procedural cache's per-frame reuse
        // shape without tying mesh entities into the shared procedural
        // refcount table.
        if (sidecar.MeshGeometry.IsValid())
        {
            ++stats.MeshGeometryReuseHits;
            sidecar.Geometry = sidecar.MeshGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.MeshGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, sidecar.MeshGeometry);
            return true;
        }

        MeshPackResult packResult = PackMesh(view, m_MeshPack);
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
            default:
                ++stats.MeshGeometryFailedPack;
                break;
            }
            return false;
        }

        const Graphics::GpuGeometryHandle handle =
            renderer.GetGpuWorld().UploadGeometry(*packResult.Upload);
        if (!handle.IsValid())
        {
            ++stats.MeshGeometryFailedPack;
            return false;
        }

        ++stats.MeshGeometryUploads;
        sidecar.MeshGeometry = handle;
        sidecar.Geometry = handle;
        sidecar.GpuSlot.SetGeometryHandle(handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, handle);
        return true;
    }

    void RenderExtractionCache::RetireMissingRenderables(const std::unordered_set<std::uint32_t>& liveKeys,
                                                         Graphics::IRenderer& renderer,
                                                         RuntimeRenderExtractionStats& stats)
    {
        for (auto it = m_Renderables.begin(); it != m_Renderables.end();)
        {
            if (liveKeys.contains(it->first))
            {
                ++it;
                continue;
            }

            if (it->second.ProceduralKey.has_value())
            {
                m_ProceduralGeometry.Release(*it->second.ProceduralKey);
            }
            if (it->second.MeshGeometry.IsValid())
            {
                // Slice B frees the runtime-owned mesh upload immediately
                // on retirement; Slice C will route this through the same
                // framesInFlight deferred-retire window the procedural
                // cache uses.
                renderer.GetGpuWorld().FreeGeometry(it->second.MeshGeometry);
                ++stats.MeshGeometryReleases;
            }
            renderer.GetGpuWorld().FreeInstance(it->second.Instance);
            it = m_Renderables.erase(it);
            ++stats.FreedInstanceCount;
        }
    }

    RuntimeRenderExtractionStats RenderExtractionCache::ExtractAndSubmit(ECS::Scene::Registry& scene,
                                                                         Graphics::IRenderer& renderer,
                                                                         Graphics::GpuAssetCache* gpuAssets)
    {
        RuntimeRenderExtractionStats stats{};
        auto& registry = scene.Raw();
        std::unordered_set<std::uint32_t> liveRenderableKeys{};

        m_Transforms.clear();
        m_Visualizations.clear();
        m_Lights.clear();

        auto transformView = registry.view<ECS::Components::Transform::WorldMatrix>();
        for (const entt::entity entity : transformView)
        {
            if (!registry.valid(entity))
            {
                ++stats.SkippedInvalidEntityCount;
                continue;
            }

            const auto& world = transformView.get<ECS::Components::Transform::WorldMatrix>(entity);

            if (const auto* directional = registry.try_get<ECS::Components::Lights::DirectionalLight>(entity))
            {
                m_Lights.push_back(MakeDirectionalLight(*directional, world.Matrix));
            }
            if (const auto* point = registry.try_get<ECS::Components::Lights::PointLight>(entity))
            {
                m_Lights.push_back(MakePointLight(*point, world.Matrix));
            }
            if (const auto* spot = registry.try_get<ECS::Components::Lights::SpotLight>(entity))
            {
                m_Lights.push_back(MakeSpotLight(*spot, world.Matrix));
            }

            if (!HasRenderableHint(registry, entity))
            {
                continue;
            }

            ++stats.CandidateRenderableCount;
            const std::uint32_t stableId = StableEntityId(entity);
            liveRenderableKeys.insert(stableId);

            RenderableSidecar* sidecar = EnsureRenderable(stableId, renderer, stats);
            if (!sidecar)
            {
                continue;
            }

            const bool dirtyTransform = registry.any_of<ECS::Components::DirtyTags::DirtyTransform>(entity);
            if (dirtyTransform)
            {
                ++stats.DirtyTransformCount;
                registry.remove<ECS::Components::DirtyTags::DirtyTransform>(entity);
            }

            if (const auto* visualization = registry.try_get<Graphics::Components::VisualizationConfig>(entity))
            {
                sidecar->Visualization = *visualization;
                sidecar->HasVisualization = true;
            }
            else
            {
                sidecar->HasVisualization = false;
            }

            const ECS::Components::ProceduralGeometryRef* proceduralRef =
                registry.try_get<ECS::Components::ProceduralGeometryRef>(entity);
            const ECS::Components::AssetInstance::Source* assetSource =
                registry.try_get<ECS::Components::AssetInstance::Source>(entity);
            const bool assetSourcePresent = assetSource != nullptr
                && NormalizeAssetSource(*assetSource).IsValid();

            bool proceduralBound = false;
            if (proceduralRef != nullptr)
            {
                ++stats.ProceduralRenderablesEnumerated;
                if (assetSourcePresent)
                {
                    ++stats.ProceduralAndAssetSourceConflict;
                }
                else
                {
                    proceduralBound = BindProceduralGeometry(*proceduralRef,
                                                              *sidecar,
                                                              renderer,
                                                              stats);
                }
            }

            // RUNTIME-085 Slice B — runtime-authored mesh-source residency.
            // Mesh path runs only when the entity has stated no procedural
            // intent and no asset source at all; both are treated as
            // declared alternatives that the mesh bridge must not race
            // against. Domain detection uses the same `BuildConstView`
            // path the rest of the engine uses, so `Vertices`+`Halfedges`+
            // `Faces` (or the `HasMeshTopology` marker) decides whether
            // the entity is a mesh.
            const bool meshEligible = !proceduralBound
                && proceduralRef == nullptr
                && assetSource == nullptr;
            if (meshEligible)
            {
                const auto view = ECS::Components::GeometrySources::BuildConstView(registry, entity);
                if (view.ActiveDomain == ECS::Components::GeometrySources::Domain::Mesh)
                {
                    (void)BindMeshGeometry(view, *sidecar, renderer, stats);
                }
            }

            if (!proceduralBound && assetSource != nullptr)
            {
                ++stats.SourceAssetObservationCount;
                const RuntimeRenderableAssetGenerationObservation observation = ObserveRenderableAssetGeneration(
                    sidecar->GpuSlot,
                    NormalizeAssetSource(*assetSource),
                    gpuAssets);
                AccumulateAssetObservationStats(observation, stats);
            }
            else if (!proceduralBound && !sidecar->MeshGeometry.IsValid())
            {
                sidecar->GpuSlot.ClearSourceAsset();
            }

            m_Visualizations.push_back(Graphics::VisualizationSyncRecord{
                .StableId = stableId,
                .Material = &sidecar->Material,
                .GpuSlot = &sidecar->GpuSlot,
                .Visualization = sidecar->HasVisualization ? &sidecar->Visualization : nullptr,
            });

            m_Transforms.push_back(Graphics::TransformSyncRecord{
                .StableId = stableId,
                .Instance = sidecar->Instance,
                .Model = world.Matrix,
                .RenderFlags = BuildRenderFlags(registry, entity),
                .Bounds = ExtractBounds(registry, entity, world.Matrix),
                .MaterialSlot = sidecar->Material.EffectiveSlot,
                .HasMaterialSlot = true,
            });
        }

        RetireMissingRenderables(liveRenderableKeys, renderer, stats);

        // RUNTIME-082 Slice D — spatial-debug adapter pump. Iterated
        // independently of `HasRenderableHint`: a SpatialDebugBinding may
        // attach to a renderable entity (to visualize its acceleration
        // structure) or to a debug-only entity that owns no surface/line/
        // point hint. The pump walks the binding view, looks up the active
        // adapter through the cache-owned registry, accumulates a single
        // shared `SpatialDebugSnapshotBatch`, and reports per-frame stats.
        m_SpatialDebugBatch.Clear();
        auto spatialDebugView = registry.view<ECS::Components::SpatialDebugBinding>();
        for (const entt::entity entity : spatialDebugView)
        {
            if (!registry.valid(entity))
            {
                ++stats.SkippedInvalidEntityCount;
                continue;
            }

            const auto& binding = spatialDebugView.get<ECS::Components::SpatialDebugBinding>(entity);
            ++stats.SpatialDebugBindingsObserved;

            const auto* adapter = m_SpatialDebugRegistry.Find(binding.RegistryKey);
            if (adapter == nullptr)
            {
                ++stats.SpatialDebugMissingAdapterCount;
                continue;
            }

            const SpatialDebugAdapterOptions options{
                .LeafOnly      = binding.LeafOnly,
                .OccupancyOnly = binding.OccupancyOnly,
                .MaxDepth      = binding.MaxDepth,
            };
            SpatialDebugAdapterStats perAdapter{};
            adapter->Append(m_SpatialDebugBatch, options, perAdapter);
            ++stats.SpatialDebugAdaptersInvoked;

            stats.SpatialDebugLeafNodeAccumulator         += perAdapter.LeafNodeCount;
            stats.SpatialDebugInnerNodeAccumulator        += perAdapter.InnerNodeCount;
            stats.SpatialDebugEmptyNodeSkippedAccumulator += perAdapter.EmptyNodeSkippedCount;
            stats.SpatialDebugDepthCapTruncationAccumulator += perAdapter.DepthCapTruncationCount;
        }

        stats.SpatialDebugBoundsCount =
            static_cast<std::uint32_t>(m_SpatialDebugBatch.Bounds.size());
        stats.SpatialDebugHierarchyNodeCount =
            static_cast<std::uint32_t>(m_SpatialDebugBatch.HierarchyNodes.size());
        stats.SpatialDebugSplitPlaneCount =
            static_cast<std::uint32_t>(m_SpatialDebugBatch.SplitPlanes.size());
        stats.SpatialDebugConvexHullVertexCount =
            static_cast<std::uint32_t>(m_SpatialDebugBatch.ConvexHullVertices.size());
        stats.SpatialDebugConvexHullEdgeCount =
            static_cast<std::uint32_t>(m_SpatialDebugBatch.ConvexHullEdges.size());
        stats.SpatialDebugPointMarkerCount =
            static_cast<std::uint32_t>(m_SpatialDebugBatch.PointMarkers.size());

        stats.SubmittedTransformCount = static_cast<std::uint32_t>(m_Transforms.size());
        stats.SubmittedVisualizationCount = static_cast<std::uint32_t>(m_Visualizations.size());
        stats.SubmittedLightCount = static_cast<std::uint32_t>(m_Lights.size());

        // Per-tick deltas vs the cache snapshot recorded at the end of the
        // previous ExtractAndSubmit.  This captures both within-extraction
        // changes (Release/EnsureResident) and out-of-extraction Tick
        // increments (FreeRetires) that ran in the maintenance phase between
        // frames.
        const auto postCacheStats = m_ProceduralGeometry.Stats();
        stats.ProceduralGeometryReleases =
            postCacheStats.Releases - m_PrevProceduralStats.Releases;
        stats.ProceduralGeometryFreeRetires =
            postCacheStats.FreeRetires - m_PrevProceduralStats.FreeRetires;
        stats.ProceduralGeometryRetireCancellations =
            postCacheStats.RetireCancellations - m_PrevProceduralStats.RetireCancellations;
        stats.ProceduralGeometryRefCountSaturated =
            postCacheStats.RefCountSaturated - m_PrevProceduralStats.RefCountSaturated;
        m_PrevProceduralStats = postCacheStats;

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .Transforms                     = m_Transforms,
            .Lights                         = m_Lights,
            .Visualizations                 = m_Visualizations,
            .SpatialDebugBounds             = m_SpatialDebugBatch.Bounds,
            .SpatialDebugHierarchyNodes     = m_SpatialDebugBatch.HierarchyNodes,
            .SpatialDebugSplitPlanes        = m_SpatialDebugBatch.SplitPlanes,
            .SpatialDebugConvexHullVertices = m_SpatialDebugBatch.ConvexHullVertices,
            .SpatialDebugConvexHullEdges    = m_SpatialDebugBatch.ConvexHullEdges,
            .SpatialDebugPointMarkers       = m_SpatialDebugBatch.PointMarkers,
        });

        m_LastStats = stats;
        return m_LastStats;
    }

    void RenderExtractionCache::TickProceduralGeometry(std::uint64_t currentFrame,
                                                       std::uint32_t framesInFlight,
                                                       Graphics::IRenderer& renderer)
    {
        m_ProceduralGeometry.Tick(currentFrame,
                                   framesInFlight,
                                   [&renderer](Graphics::GpuGeometryHandle handle) {
                                       renderer.GetGpuWorld().FreeGeometry(handle);
                                   });
    }

    void RenderExtractionCache::Shutdown(Graphics::IRenderer& renderer)
    {
        RuntimeRenderExtractionStats stats{};
        for (auto& [_, sidecar] : m_Renderables)
        {
            if (sidecar.ProceduralKey.has_value())
            {
                m_ProceduralGeometry.Release(*sidecar.ProceduralKey);
            }
            if (sidecar.MeshGeometry.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(sidecar.MeshGeometry);
                ++stats.MeshGeometryReleases;
            }
            renderer.GetGpuWorld().FreeInstance(sidecar.Instance);
            ++stats.FreedInstanceCount;
        }
        m_Renderables.clear();
        m_Transforms.clear();
        m_Visualizations.clear();
        m_Lights.clear();

        // RUNTIME-082 Slice D — drop owned adapters + clear the registry
        // mirror. The registry must drop its raw pointers before the
        // unique_ptr map destroys the adapter instances.
        m_SpatialDebugRegistry.Clear();
        m_SpatialDebugAdapters.clear();
        m_SpatialDebugBatch.Clear();

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{});
        m_LastStats = stats;
    }
}
