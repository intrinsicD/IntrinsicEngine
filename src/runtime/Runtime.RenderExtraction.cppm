module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

export module Extrinsic.Runtime.RenderExtraction;

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.AssetInstance;
import Extrinsic.ECS.Component.DirtyTags;
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

export namespace Extrinsic::Runtime
{
    enum class RuntimeRenderableAssetObservationStatus : std::uint8_t
    {
        NoSourceAsset,
        CacheUnavailable,
        ViewUnavailable,
        GenerationUnavailable,
        UpToDate,
        RebindRequired,
    };

    struct RuntimeRenderableAssetGenerationObservation
    {
        RuntimeRenderableAssetObservationStatus Status = RuntimeRenderableAssetObservationStatus::NoSourceAsset;
        Graphics::Components::GpuSceneSlotAssetRebindDecision Decision =
            Graphics::Components::GpuSceneSlotAssetRebindDecision::NoSourceAsset;
        Assets::AssetId SourceAsset{};
        std::uint64_t ObservedGeneration = 0;
    };

    enum class RuntimeRenderableAssetAcknowledgmentResult : std::uint8_t
    {
        Acknowledged,
        SkippedNoSourceAsset,
        SkippedAssetMismatch,
        SkippedNoObservedGeneration,
    };

    struct RuntimeRenderExtractionStats
    {
        std::uint32_t CandidateRenderableCount{0};
        std::uint32_t SubmittedTransformCount{0};
        std::uint32_t SubmittedVisualizationCount{0};
        std::uint32_t SubmittedLightCount{0};
        std::uint32_t AllocatedInstanceCount{0};
        std::uint32_t FreedInstanceCount{0};
        std::uint32_t DirtyTransformCount{0};
        std::uint32_t SkippedInvalidEntityCount{0};
        std::uint32_t SourceAssetObservationCount{0};
        std::uint32_t SourceAssetCacheUnavailableCount{0};
        std::uint32_t SourceAssetViewUnavailableCount{0};
        std::uint32_t SourceAssetGenerationUnavailableCount{0};
        std::uint32_t SourceAssetUpToDateCount{0};
        std::uint32_t SourceAssetRebindRequiredCount{0};
        std::uint32_t SourceAssetRebindAcknowledgedCount{0};
    };

    [[nodiscard]] RuntimeRenderableAssetGenerationObservation ObserveRenderableAssetGeneration(
        Graphics::Components::GpuSceneSlot& slot,
        Assets::AssetId sourceAsset,
        Graphics::GpuAssetCache* gpuAssets);

    [[nodiscard]] RuntimeRenderableAssetAcknowledgmentResult AcknowledgeRenderableAssetRebind(
        Graphics::Components::GpuSceneSlot& slot,
        const RuntimeRenderableAssetGenerationObservation& observation) noexcept;

    class RenderExtractionCache
    {
    public:
        RenderExtractionCache() = default;
        ~RenderExtractionCache() = default;

        RenderExtractionCache(const RenderExtractionCache&) = delete;
        RenderExtractionCache& operator=(const RenderExtractionCache&) = delete;

        [[nodiscard]] RuntimeRenderExtractionStats ExtractAndSubmit(ECS::Scene::Registry& scene,
                                                                    Graphics::IRenderer& renderer,
                                                                    Graphics::GpuAssetCache* gpuAssets = nullptr);
        void Shutdown(Graphics::IRenderer& renderer);

        [[nodiscard]] const RuntimeRenderExtractionStats& GetLastStats() const noexcept { return m_LastStats; }
        [[nodiscard]] std::uint32_t GetTrackedRenderableCount() const noexcept
        {
            return static_cast<std::uint32_t>(m_Renderables.size());
        }

    private:
        struct RenderableSidecar
        {
            Graphics::GpuInstanceHandle Instance{};
            Graphics::Components::GpuSceneSlot GpuSlot{};
            Graphics::Components::MaterialInstance Material{};
            Graphics::Components::VisualizationConfig Visualization{};
            bool HasVisualization{false};
        };

        [[nodiscard]] RenderableSidecar* EnsureRenderable(std::uint32_t stableId,
                                                          Graphics::IRenderer& renderer,
                                                          RuntimeRenderExtractionStats& stats);
        void RetireMissingRenderables(const std::unordered_set<std::uint32_t>& liveKeys,
                                      Graphics::IRenderer& renderer,
                                      RuntimeRenderExtractionStats& stats);

        std::unordered_map<std::uint32_t, RenderableSidecar> m_Renderables{};
        std::vector<Graphics::TransformSyncRecord> m_Transforms{};
        std::vector<Graphics::VisualizationSyncRecord> m_Visualizations{};
        std::vector<Graphics::LightSnapshot> m_Lights{};
        RuntimeRenderExtractionStats m_LastStats{};
    };
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
            if (const auto* worldBounds = registry.try_get<ECS::Components::Culling::Bounds>(entity))
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

            if (const auto* source = registry.try_get<ECS::Components::AssetInstance::Source>(entity))
            {
                ++stats.SourceAssetObservationCount;
                const RuntimeRenderableAssetGenerationObservation observation = ObserveRenderableAssetGeneration(
                    sidecar->GpuSlot,
                    NormalizeAssetSource(*source),
                    gpuAssets);
                AccumulateAssetObservationStats(observation, stats);
            }
            else
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

        stats.SubmittedTransformCount = static_cast<std::uint32_t>(m_Transforms.size());
        stats.SubmittedVisualizationCount = static_cast<std::uint32_t>(m_Visualizations.size());
        stats.SubmittedLightCount = static_cast<std::uint32_t>(m_Lights.size());

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .Transforms = m_Transforms,
            .Lights = m_Lights,
            .Visualizations = m_Visualizations,
        });

        m_LastStats = stats;
        return m_LastStats;
    }

    void RenderExtractionCache::Shutdown(Graphics::IRenderer& renderer)
    {
        RuntimeRenderExtractionStats stats{};
        for (auto& [_, sidecar] : m_Renderables)
        {
            renderer.GetGpuWorld().FreeInstance(sidecar.Instance);
            ++stats.FreedInstanceCount;
        }
        m_Renderables.clear();
        m_Transforms.clear();
        m_Visualizations.clear();
        m_Lights.clear();
        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{});
        m_LastStats = stats;
    }
}


