module;

#include <algorithm>
#include <cmath>
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
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.GraphGeometryPacker;
import Extrinsic.Runtime.MeshGeometryPacker;
import Extrinsic.Runtime.PointCloudGeometryPacker;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.StableEntityLookup;
import Extrinsic.Runtime.ProceduralGeometry;
import Extrinsic.Runtime.ProceduralGeometryPacker;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.SpatialDebugAdapters;
import Extrinsic.Runtime.VisualizationAdapters;

namespace Extrinsic::Runtime
{
    struct RenderExtractionCache::VisualizationAdapterState
    {
        std::unordered_map<std::uint64_t, std::unique_ptr<IVisualizationAdapter>> Adapters{};
        VisualizationAdapterRegistry Registry{};
        std::unordered_map<std::uint32_t, VisualizationAdapterBinding> Bindings{};
        VisualizationAdapterBatch Batch{};
    };

    RenderExtractionCache::RenderExtractionCache()
        : m_VisualizationState(std::make_unique<VisualizationAdapterState>())
    {
    }

    RenderExtractionCache::~RenderExtractionCache() = default;

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
            .GraphGeometry = it->second.GraphGeometry,
            .HasGraphResidency = it->second.GraphGeometry.IsValid(),
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

    const ProceduralGeometryCache& RenderExtractionCache::GetProceduralGeometryCacheForTest() const noexcept
    {
        return m_ProceduralGeometry;
    }

    void RenderExtractionCache::SetMeshPrimitiveViewSettings(std::uint32_t stableEntityId,
                                                             MeshPrimitiveViewSettings settings)
    {
        m_MeshPrimitiveViewSettings.insert_or_assign(stableEntityId, settings);
    }

    void RenderExtractionCache::ClearMeshPrimitiveViewSettings(std::uint32_t stableEntityId) noexcept
    {
        m_MeshPrimitiveViewSettings.erase(stableEntityId);
    }

    MeshPrimitiveViewSettings RenderExtractionCache::GetMeshPrimitiveViewSettings(
        std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_MeshPrimitiveViewSettings.find(stableEntityId);
        return it != m_MeshPrimitiveViewSettings.end() ? it->second : MeshPrimitiveViewSettings{};
    }

    void RenderExtractionCache::SetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId,
        Graphics::MaterialTextureAssetBindings bindings)
    {
        if (stableEntityId == 0u)
        {
            return;
        }
        m_MaterialTextureBindings.insert_or_assign(stableEntityId, bindings);
    }

    void RenderExtractionCache::ClearMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_MaterialTextureBindings.erase(stableEntityId);
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    RenderExtractionCache::GetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_MaterialTextureBindings.find(stableEntityId);
        if (it == m_MaterialTextureBindings.end())
        {
            return std::nullopt;
        }
        return it->second;
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

    void RenderExtractionCache::RegisterVisualizationAdapter(
        const std::uint64_t key,
        std::unique_ptr<IVisualizationAdapter> adapter)
    {
        if (adapter == nullptr)
        {
            (void)UnregisterVisualizationAdapter(key);
            return;
        }
        m_VisualizationState->Registry.Register(key, *adapter);
        m_VisualizationState->Adapters.insert_or_assign(key, std::move(adapter));
    }

    bool RenderExtractionCache::UnregisterVisualizationAdapter(
        const std::uint64_t key) noexcept
    {
        m_VisualizationState->Registry.Unregister(key);
        return m_VisualizationState->Adapters.erase(key) != 0u;
    }

    std::size_t RenderExtractionCache::GetVisualizationAdapterCount() const noexcept
    {
        return m_VisualizationState->Adapters.size();
    }

    const VisualizationAdapterRegistry& RenderExtractionCache::GetVisualizationAdapterRegistryForTest() const noexcept
    {
        return m_VisualizationState->Registry;
    }

    void RenderExtractionCache::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        VisualizationAdapterBinding binding)
    {
        m_VisualizationState->Bindings.insert_or_assign(stableEntityId, binding);
    }

    void RenderExtractionCache::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_VisualizationState->Bindings.erase(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    RenderExtractionCache::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it = m_VisualizationState->Bindings.find(stableEntityId);
        if (it == m_VisualizationState->Bindings.end())
        {
            return std::nullopt;
        }
        return it->second;
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] std::uint32_t StableEntityId(entt::entity entity) noexcept
        {
            // Render-id authority: entt handle + 1 so id 0 stays the GPU
            // background sentinel (BUG-026). Decoders must use
            // StableEntityLookup::ToEntityHandle.
            return StableEntityLookup::ToRenderId(entity);
        }

        [[nodiscard]] bool HasRenderableHint(const entt::registry& registry, entt::entity entity)
        {
            namespace G = Graphics::Components;
            return registry.any_of<G::RenderSurface, G::RenderEdges, G::RenderPoints>(entity);
        }

        [[nodiscard]] Graphics::VisualizationAttributeDomain ToVisualizationAttributeDomain(
            const Graphics::Components::VisualizationConfig::Domain domain) noexcept
        {
            using Domain = Graphics::Components::VisualizationConfig::Domain;
            switch (domain)
            {
            case Domain::Vertex:
                return Graphics::VisualizationAttributeDomain::Vertex;
            case Domain::Edge:
                return Graphics::VisualizationAttributeDomain::Edge;
            case Domain::Face:
                return Graphics::VisualizationAttributeDomain::Face;
            }
            return Graphics::VisualizationAttributeDomain::Vertex;
        }

        [[nodiscard]] bool IsScalarVisualizationSource(
            const Graphics::Components::VisualizationConfig* visualization) noexcept
        {
            using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
            return visualization != nullptr &&
                   visualization->Source == ColorSource::ScalarField;
        }

        [[nodiscard]] bool IsColorBufferVisualizationSource(
            const Graphics::Components::VisualizationConfig* visualization) noexcept
        {
            using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
            return visualization != nullptr &&
                   (visualization->Source == ColorSource::PerVertexBuffer ||
                    visualization->Source == ColorSource::PerEdgeBuffer ||
                    visualization->Source == ColorSource::PerFaceBuffer);
        }

        [[nodiscard]] Graphics::VisualizationAttributeDomain ToColorBufferDomain(
            const Graphics::Components::VisualizationConfig::ColorSource source) noexcept
        {
            using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
            switch (source)
            {
            case ColorSource::PerVertexBuffer:
                return Graphics::VisualizationAttributeDomain::Vertex;
            case ColorSource::PerEdgeBuffer:
                return Graphics::VisualizationAttributeDomain::Edge;
            case ColorSource::PerFaceBuffer:
                return Graphics::VisualizationAttributeDomain::Face;
            default:
                return Graphics::VisualizationAttributeDomain::Vertex;
            }
        }

        [[nodiscard]] std::string BuildVisualizationPropertySourceKey(
            const std::uint32_t stableId,
            const std::string_view lane,
            const std::string_view sourceName)
        {
            std::string key = std::to_string(stableId);
            key += ':';
            key += lane;
            key += ':';
            key += sourceName;
            return key;
        }

        [[nodiscard]] VisualizationAdapterOptions BuildVisualizationAdapterOptions(
            const std::uint32_t stableId,
            const RenderExtractionCache::VisualizationAdapterBinding& binding,
            const Graphics::Components::VisualizationConfig* visualization)
        {
            using BindingKind = RenderExtractionCache::VisualizationAdapterBindingKind;

            VisualizationAdapterOptions options = binding.Options;
            switch (binding.Kind)
            {
            case BindingKind::Scalar:
                if (IsScalarVisualizationSource(visualization))
                {
                    options.SourceName = visualization->ScalarFieldName;
                    options.OutputName = visualization->ScalarFieldName;
                    options.Domain = ToVisualizationAttributeDomain(
                        visualization->ScalarDomain);
                    options.AutoRange = visualization->Scalar.AutoRange;
                    options.RangeMin = visualization->Scalar.RangeMin;
                    options.RangeMax = visualization->Scalar.RangeMax;
                    options.Colormap = visualization->Scalar.Map;
                }
                if (options.PropertyBufferSourceKey.empty())
                {
                    const std::string_view scalarName =
                        options.OutputName.empty()
                            ? std::string_view{options.SourceName}
                            : std::string_view{options.OutputName};
                    if (!scalarName.empty())
                    {
                        options.PropertyBufferSourceKey =
                            BuildVisualizationPropertySourceKey(
                                stableId, "scalar", scalarName);
                    }
                }
                if (options.BufferBDA == 0u)
                {
                    options.BufferBDA = binding.BufferBDA;
                }
                break;
            case BindingKind::Color:
                if (IsColorBufferVisualizationSource(visualization))
                {
                    options.SourceName = visualization->ColorBufferName;
                    options.OutputName = visualization->ColorBufferName;
                    options.Domain = ToColorBufferDomain(visualization->Source);
                }
                if (options.PropertyBufferSourceKey.empty())
                {
                    const std::string_view colorName =
                        options.OutputName.empty()
                            ? std::string_view{options.SourceName}
                            : std::string_view{options.OutputName};
                    if (!colorName.empty())
                    {
                        options.PropertyBufferSourceKey =
                            BuildVisualizationPropertySourceKey(
                                stableId, "color", colorName);
                    }
                }
                if (options.ColorBufferBDA == 0u)
                {
                    options.ColorBufferBDA = binding.BufferBDA;
                }
                break;
            case BindingKind::VectorField:
                if (options.PropertyBufferSourceKey.empty())
                {
                    const std::string_view vectorName =
                        options.OutputName.empty()
                            ? std::string_view{options.SourceName}
                            : std::string_view{options.OutputName};
                    if (!vectorName.empty())
                    {
                        options.PropertyBufferSourceKey =
                            BuildVisualizationPropertySourceKey(
                                stableId, "vector", vectorName);
                    }
                }
                if (options.VectorBufferBDA == 0u)
                {
                    options.VectorBufferBDA = binding.BufferBDA;
                }
                break;
            case BindingKind::Isoline:
                if (IsScalarVisualizationSource(visualization))
                {
                    options.SourceName = visualization->ScalarFieldName;
                    options.OutputName = visualization->ScalarFieldName;
                    options.Domain = ToVisualizationAttributeDomain(
                        visualization->ScalarDomain);
                    options.AutoRange = visualization->Scalar.AutoRange;
                    options.RangeMin = visualization->Scalar.RangeMin;
                    options.RangeMax = visualization->Scalar.RangeMax;
                    options.IsoValueCount = visualization->Scalar.Isolines.Num;
                    options.LineWidth = visualization->Scalar.Isolines.Width;
                    options.OverlayColor = visualization->Scalar.Isolines.Color;
                }
                if (options.PropertyBufferSourceKey.empty())
                {
                    const std::string_view scalarName =
                        options.OutputName.empty()
                            ? std::string_view{options.SourceName}
                            : std::string_view{options.OutputName};
                    if (!scalarName.empty())
                    {
                        options.PropertyBufferSourceKey =
                            BuildVisualizationPropertySourceKey(
                                stableId, "isoline", scalarName);
                    }
                }
                break;
            case BindingKind::HtexMetadata:
                if (options.TexcoordBufferBDA == 0u)
                {
                    options.TexcoordBufferBDA = binding.BufferBDA;
                }
                break;
            }
            return options;
        }

        [[nodiscard]] std::uint32_t BuildRenderFlags(const entt::registry& registry, entt::entity entity)
        {
            namespace G = Graphics::Components;
            std::uint32_t flags = RHI::GpuRender_Visible | RHI::GpuRender_Opaque;
            if (registry.all_of<G::RenderSurface>(entity))
                flags |= RHI::GpuRender_Surface;
            if (registry.all_of<G::RenderEdges>(entity))
                flags |= RHI::GpuRender_Line | RHI::GpuRender_Unlit;
            if (registry.all_of<G::RenderPoints>(entity))
                flags |= RHI::GpuRender_Point | RHI::GpuRender_Unlit;
            return flags;
        }

        [[nodiscard]] std::uint32_t ToRenderPointMode(
            const Graphics::Components::RenderPoints::RenderType type) noexcept
        {
            namespace G = Graphics::Components;
            switch (type)
            {
            case G::RenderPoints::RenderType::Flat:
                return 0u;
            case G::RenderPoints::RenderType::Sphere:
                return 1u;
            case G::RenderPoints::RenderType::Surfel:
                return 2u;
            }
            return 1u;
        }

        [[nodiscard]] float UniformPointSizeOrDefault(
            const Graphics::Components::RenderPoints* points) noexcept
        {
            if (points == nullptr)
            {
                return 1.0f;
            }
            const auto* uniform = std::get_if<float>(&points->SizeSource);
            return uniform != nullptr ? *uniform : 1.0f;
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

        struct MeshTexcoordFallbackDiagnostics
        {
            bool MissingOrMismatched{false};
            bool NonFinite{false};
        };

        [[nodiscard]] MeshTexcoordFallbackDiagnostics DiagnoseMeshTexcoordFallback(
            const ECS::Components::GeometrySources::ConstSourceView& view) noexcept
        {
            using namespace ECS::Components::GeometrySources;

            MeshTexcoordFallbackDiagnostics diagnostics{};
            if (view.ActiveDomain != Domain::Mesh || view.VertexSource == nullptr)
            {
                return diagnostics;
            }

            const auto positions =
                view.VertexSource->Properties.Get<glm::vec3>(PropertyNames::kPosition);
            if (!positions)
            {
                return diagnostics;
            }
            const std::size_t vertexCount = positions.Vector().size();
            if (vertexCount == 0u)
            {
                return diagnostics;
            }

            const auto texcoords =
                view.VertexSource->Properties.Get<glm::vec2>("v:texcoord");
            if (!texcoords || texcoords.Vector().size() != vertexCount)
            {
                diagnostics.MissingOrMismatched = true;
                return diagnostics;
            }

            for (const glm::vec2 uv : texcoords.Vector())
            {
                if (!std::isfinite(uv.x) || !std::isfinite(uv.y))
                {
                    diagnostics.NonFinite = true;
                    return diagnostics;
                }
            }
            return diagnostics;
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

    void MirrorRenderWorldPoolDiagnostics(const RenderWorldPool& pool,
                                          RuntimeRenderExtractionStats& stats) noexcept
    {
        const RenderWorldPoolDiagnostics& diag = pool.GetDiagnostics();
        stats.RenderWorldPipelineStallCount  = diag.PipelineStallCount;
        stats.RenderWorldExtractionSkipCount = diag.ExtractionSkipCount;
        stats.RenderWorldFrameAgeFrames      = diag.LastConsumedFrameAge;
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

    void RenderExtractionCache::ApplyMaterialTextureBindings(
        const std::uint32_t stableId,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        Graphics::GpuAssetCache* gpuAssets,
        RuntimeRenderExtractionStats& stats)
    {
        const auto it = m_MaterialTextureBindings.find(stableId);
        if (it == m_MaterialTextureBindings.end())
        {
            return;
        }

        ++stats.MaterialTextureBindingRecordCount;
        if (gpuAssets == nullptr || !sidecar.Material.Lease.IsValid())
        {
            ++stats.MaterialTextureBindingResolveFailureCount;
            return;
        }

        auto resolved = renderer.GetMaterialSystem().ResolveTextureAssetBindings(
            sidecar.Material.Lease.GetHandle(),
            it->second,
            *gpuAssets);
        if (resolved.has_value())
        {
            sidecar.Material.EffectiveSlot =
                renderer.GetMaterialSystem().GetMaterialSlot(
                    sidecar.Material.Lease.GetHandle());
            ++stats.MaterialTextureBindingResolveCount;
        }
        else
        {
            ++stats.MaterialTextureBindingResolveFailureCount;
        }
    }

    namespace
    {
        [[nodiscard]] bool AssignProgressiveTextureBinding(
            Graphics::MaterialTextureAssetBindings& bindings,
            const ProgressiveSlotExtraction& slot)
        {
            if (!slot.TextureReady || !slot.TextureAsset.IsValid())
            {
                return false;
            }

            switch (slot.Semantic)
            {
            case ProgressiveSlotSemantic::Albedo:
            case ProgressiveSlotSemantic::ScalarField:
                bindings.Albedo = slot.TextureAsset;
                return true;
            case ProgressiveSlotSemantic::Normal:
                bindings.Normal = slot.TextureAsset;
                return true;
            case ProgressiveSlotSemantic::Roughness:
            case ProgressiveSlotSemantic::Metallic:
                bindings.MetallicRoughness = slot.TextureAsset;
                return true;
            case ProgressiveSlotSemantic::Displacement:
            case ProgressiveSlotSemantic::PointColor:
            case ProgressiveSlotSemantic::PointScalarField:
            case ProgressiveSlotSemantic::PointSize:
            case ProgressiveSlotSemantic::PointNormalOrientation:
            case ProgressiveSlotSemantic::LineColor:
            case ProgressiveSlotSemantic::LineScalarField:
            case ProgressiveSlotSemantic::LineWidth:
                return false;
            }
            return false;
        }
    }

    void RenderExtractionCache::ApplyProgressivePresentationBindings(
        entt::registry& registry,
        const entt::entity entity,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        Graphics::IRenderer& renderer,
        Graphics::GpuAssetCache* gpuAssets,
        RuntimeRenderExtractionStats& stats)
    {
        const auto* bindings = registry.try_get<ProgressivePresentationBindings>(entity);
        if (bindings == nullptr)
        {
            return;
        }

        const ProgressivePresentationExtractionSnapshot snapshot =
            BuildProgressivePresentationSnapshot(view, *bindings);
        ++stats.ProgressivePresentationEntityCount;
        stats.ProgressivePresentationLaneCount += snapshot.Stats.LaneCount;
        stats.ProgressivePresentationSlotCount += snapshot.Stats.SlotCount;
        stats.ProgressiveDefaultSlotCount += snapshot.Stats.DefaultSlotCount;
        stats.ProgressiveReadyTextureSlotCount += snapshot.Stats.ReadyTextureSlotCount;
        stats.ProgressivePropertyBufferReadyCount += snapshot.Stats.PropertyBufferReadyCount;
        stats.ProgressivePendingSlotCount += snapshot.Stats.PendingSlotCount;
        stats.ProgressiveUnsupportedSlotCount += snapshot.Stats.UnsupportedSlotCount;
        stats.ProgressivePreviousOutputRetainedCount += snapshot.Stats.PreviousOutputRetainedCount;
        stats.ProgressiveDiagnosticCount += snapshot.Stats.DiagnosticCount;

        Graphics::MaterialTextureAssetBindings textureBindings{};
        bool hasTextureBinding = false;
        for (const ProgressiveSlotExtraction& slot : snapshot.Slots)
        {
            hasTextureBinding =
                AssignProgressiveTextureBinding(textureBindings, slot) || hasTextureBinding;
        }

        if (!hasTextureBinding)
        {
            return;
        }

        if (gpuAssets == nullptr || !sidecar.Material.Lease.IsValid())
        {
            ++stats.ProgressiveMaterialTextureBindingResolveFailureCount;
            return;
        }

        auto resolved = renderer.GetMaterialSystem().ResolveTextureAssetBindings(
            sidecar.Material.Lease.GetHandle(),
            textureBindings,
            *gpuAssets);
        if (resolved.has_value())
        {
            sidecar.Material.EffectiveSlot =
                renderer.GetMaterialSystem().GetMaterialSlot(
                    sidecar.Material.Lease.GetHandle());
            ++stats.ProgressiveMaterialTextureBindingResolveCount;
        }
        else
        {
            ++stats.ProgressiveMaterialTextureBindingResolveFailureCount;
        }
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

    bool RenderExtractionCache::BindMeshGeometry(entt::registry& registry,
                                                  entt::entity entity,
                                                  const ECS::Components::GeometrySources::ConstSourceView& view,
                                                  RenderableSidecar& sidecar,
                                                  Graphics::IRenderer& renderer,
                                                  RuntimeRenderExtractionStats& stats)
    {
        namespace D = ECS::Components::DirtyTags;
        const bool dirty = registry.any_of<D::GpuDirty,
                                            D::DirtyVertexPositions,
                                            D::DirtyFaceTopology,
                                            D::DirtyEdgeTopology>(entity);
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
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, Graphics::GpuGeometryHandle{});
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
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, sidecar.MeshGeometry);
            return true;
        }

        const MeshTexcoordFallbackDiagnostics texcoordFallback =
            DiagnoseMeshTexcoordFallback(view);
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

    bool RenderExtractionCache::BindGraphGeometry(entt::registry& registry,
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

        const bool dirty = registry.any_of<D::GpuDirty,
                                            D::DirtyVertexPositions,
                                            D::DirtyVertexAttributes,
                                            D::DirtyEdgeTopology>(entity);
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
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, Graphics::GpuGeometryHandle{});
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
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, sidecar.GraphGeometry);
            return true;
        }

        GraphPackResult packResult = PackGraph(view, wantLines, wantPoints, m_GraphPack);
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

    bool RenderExtractionCache::BindPointCloudGeometry(entt::registry& registry,
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
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, Graphics::GpuGeometryHandle{});
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
            points != nullptr && std::holds_alternative<std::string>(points->SizeSource))
        {
            ++stats.PointCloudGeometryFailedPack;
            // Fail-closed: release any prior residency (a resident cloud that
            // switches to an unsupported size source stops rendering) and leave
            // the dirty tags in place so a later frame can recover once the size
            // source becomes supported.
            releaseStaleResidency();
            return false;
        }

        const bool dirty = registry.any_of<D::GpuDirty,
                                            D::DirtyVertexPositions,
                                            D::DirtyVertexAttributes>(entity);
        const bool hadResidency = sidecar.PointCloudGeometry.IsValid();

        // Reuse path: clean point-cloud entity with a cached upload. Mirrors
        // the single-owner mesh reuse — a direct rebind without any repack.
        if (hadResidency && !dirty)
        {
            ++stats.PointCloudGeometryReuseHits;
            sidecar.Geometry = sidecar.PointCloudGeometry;
            sidecar.GpuSlot.SetGeometryHandle(sidecar.PointCloudGeometry);
            sidecar.GpuSlot.ClearSourceAsset();
            renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, sidecar.PointCloudGeometry);
            return true;
        }

        PointCloudPackResult packResult = PackCloud(view, m_PointCloudPack);
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
                            D::DirtyVertexAttributes>(entity);
        }

        sidecar.PointCloudGeometry = handle;
        sidecar.Geometry = handle;
        sidecar.GpuSlot.SetGeometryHandle(handle);
        sidecar.GpuSlot.ClearSourceAsset();
        renderer.GetGpuWorld().SetInstanceGeometry(sidecar.Instance, handle);
        return true;
    }

    void RenderExtractionCache::EnqueueMeshRetire(Graphics::GpuGeometryHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }
        m_MeshRetire.push_back(GeometryRetireRecord{handle, 0, false});
    }

    void RenderExtractionCache::EnqueueGraphRetire(Graphics::GpuGeometryHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }
        m_GraphRetire.push_back(GeometryRetireRecord{handle, 0, false});
    }

    void RenderExtractionCache::EnqueuePointCloudRetire(Graphics::GpuGeometryHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }
        m_PointCloudRetire.push_back(GeometryRetireRecord{handle, 0, false});
    }

    void RenderExtractionCache::EnqueueMeshPrimitiveViewRetire(Graphics::GpuGeometryHandle handle)
    {
        if (!handle.IsValid())
        {
            return;
        }
        m_MeshPrimitiveViewRetire.push_back(GeometryRetireRecord{handle, 0, false});
    }

    void RenderExtractionCache::ReleaseMeshPrimitiveView(MeshPrimitiveViewKind kind,
                                                         RenderableSidecar& sidecar,
                                                         Graphics::IRenderer& renderer,
                                                         RuntimeRenderExtractionStats& stats)
    {
        const bool isEdge = kind == MeshPrimitiveViewKind::Edge;
        Graphics::GpuInstanceHandle& instance =
            isEdge ? sidecar.MeshEdgeViewInstance : sidecar.MeshVertexViewInstance;
        Graphics::GpuGeometryHandle& geometry =
            isEdge ? sidecar.MeshEdgeViewGeometry : sidecar.MeshVertexViewGeometry;

        if (geometry.IsValid())
        {
            // Route the runtime-owned view upload through the same
            // `framesInFlight` deferred-retire window the surface mesh uses.
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
            // The view instance owns nothing the retire window guards, so free
            // it immediately (GpuWorld defers the slot reuse internally). Count
            // it so `FreedInstanceCount` balances the `AllocatedInstanceCount`
            // bump the reconcile path makes when the view instance is created.
            renderer.GetGpuWorld().FreeInstance(instance);
            instance = {};
            ++stats.FreedInstanceCount;
        }
    }

    void RenderExtractionCache::AppendVisualizationAdapters(
        const std::uint32_t stableId,
        const RenderableSidecar& sidecar,
        RuntimeRenderExtractionStats& stats)
    {
        const auto* visualization =
            sidecar.HasVisualization ? &sidecar.Visualization : nullptr;
        const bool scalarConfigRequested =
            IsScalarVisualizationSource(visualization);
        const bool colorConfigRequested =
            IsColorBufferVisualizationSource(visualization);
        const auto bindingIt = m_VisualizationState->Bindings.find(stableId);
        const bool bindingRequested =
            bindingIt != m_VisualizationState->Bindings.end();

        if (!scalarConfigRequested && !colorConfigRequested && !bindingRequested)
        {
            return;
        }

        if (scalarConfigRequested)
        {
            ++stats.VisualizationAdapterScalarConfigsObserved;
        }

        if (!bindingRequested)
        {
            ++stats.VisualizationAdapterBindingsMissing;
            return;
        }

        const IVisualizationAdapter* adapter =
            m_VisualizationState->Registry.Find(bindingIt->second.AdapterKey);
        if (adapter == nullptr)
        {
            ++stats.VisualizationAdapterMissingAdapterCount;
            return;
        }

        VisualizationAdapterStats perAdapter{};
        const VisualizationAdapterOptions options =
            BuildVisualizationAdapterOptions(
                stableId, bindingIt->second, visualization);
        adapter->Append(m_VisualizationState->Batch, options, perAdapter);

        ++stats.VisualizationAdapterInvokedCount;
        stats.VisualizationAdapterPacketAppendCount += perAdapter.PacketAppendCount;
        stats.VisualizationAdapterMissingSourceCount +=
            perAdapter.MissingSourceCount + perAdapter.MissingTexcoordCount;
        stats.VisualizationAdapterUnsupportedSourceTypeCount += perAdapter.UnsupportedSourceTypeCount;
        stats.VisualizationAdapterEmptySourceCount += perAdapter.EmptySourceCount;
        stats.VisualizationAdapterInvalidBufferCount +=
            perAdapter.InvalidBufferCount + perAdapter.InvalidResourceCount;
        stats.VisualizationAdapterInvalidRangeCount += perAdapter.InvalidRangeCount;
        stats.VisualizationAdapterNonFiniteValueCount += perAdapter.NonFiniteValueCount;
        stats.VisualizationAdapterElementCountOverflowCount += perAdapter.ElementCountOverflowCount;
        stats.VisualizationAdapterManualRangeCount += perAdapter.ManualRangeCount;
        stats.VisualizationAdapterFlatAutoRangeExpandedCount += perAdapter.FlatAutoRangeExpandedCount;
    }

    bool RenderExtractionCache::ReconcileMeshPrimitiveView(
        MeshPrimitiveViewKind kind,
        const ECS::Components::GeometrySources::ConstSourceView& view,
        RenderableSidecar& sidecar,
        const glm::mat4& model,
        std::uint32_t materialSlot,
        const RHI::GpuBounds& bounds,
        std::uint32_t stableId,
        bool desired,
        const Graphics::Components::RenderPoints* points,
        bool meshDirty,
        Graphics::IRenderer& renderer,
        RuntimeRenderExtractionStats& stats)
    {
        const bool isEdge = kind == MeshPrimitiveViewKind::Edge;
        Graphics::GpuInstanceHandle& instance =
            isEdge ? sidecar.MeshEdgeViewInstance : sidecar.MeshVertexViewInstance;
        Graphics::GpuGeometryHandle& geometry =
            isEdge ? sidecar.MeshEdgeViewGeometry : sidecar.MeshVertexViewGeometry;

        // Disabled (or parent no longer resident): release any existing view.
        if (!desired)
        {
            ReleaseMeshPrimitiveView(kind, sidecar, renderer, stats);
            return false;
        }

        if (!isEdge &&
            (points == nullptr || !std::holds_alternative<float>(points->SizeSource)))
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
            RHI::GpuEntityConfig cfg{};
            cfg.ColorSourceMode = 1u;
            cfg.VisualizationAlpha = 1.0f;
            cfg.UniformColor = {0.02f, 0.02f, 0.02f, 1.0f};
            if (!isEdge)
            {
                cfg.Point.PointSize = UniformPointSizeOrDefault(points);
                cfg.Point.PointMode = ToRenderPointMode(points->Type);
            }
            renderer.GetGpuWorld().SetEntityConfig(instance, cfg);
            m_Transforms.push_back(Graphics::TransformSyncRecord{
                .StableId = stableId,
                .Instance = instance,
                .Model = model,
                .RenderFlags = RHI::GpuRender_Visible | RHI::GpuRender_Opaque | laneFlag,
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
                // Slice C — route through the same framesInFlight
                // deferred-retire window the procedural cache uses.
                // `FreeInstance` (below) detaches the instance from the
                // queued slot so the still-live mesh slot is not
                // observable via any live instance during the window.
                EnqueueMeshRetire(it->second.MeshGeometry);
                ++stats.MeshGeometryReleases;
            }
            if (it->second.GraphGeometry.IsValid())
            {
                // RUNTIME-086 Slice B — same deferred-retire window for the
                // runtime-owned graph upload.
                EnqueueGraphRetire(it->second.GraphGeometry);
                ++stats.GraphGeometryReleases;
            }
            if (it->second.PointCloudGeometry.IsValid())
            {
                // RUNTIME-087 — same deferred-retire window for the
                // runtime-owned point-cloud upload.
                EnqueuePointCloudRetire(it->second.PointCloudGeometry);
                ++stats.PointCloudGeometryReleases;
            }
            // RUNTIME-088 Slice B — retire the edge/vertex view sidecars: enqueue
            // their geometry for the deferred-retire window and free their own
            // instances. Also drop the entity's view settings so the cache-owned
            // control surface does not accumulate stale entries.
            ReleaseMeshPrimitiveView(MeshPrimitiveViewKind::Edge, it->second, renderer, stats);
            ReleaseMeshPrimitiveView(MeshPrimitiveViewKind::Vertex, it->second, renderer, stats);
            m_MeshPrimitiveViewSettings.erase(it->first);
            m_MaterialTextureBindings.erase(it->first);
            renderer.GetGpuWorld().FreeInstance(it->second.Instance);
            it = m_Renderables.erase(it);
            ++stats.FreedInstanceCount;
        }
    }

    RuntimeRenderExtractionStats RenderExtractionCache::ExtractAndSubmit(
        ECS::Scene::Registry& scene,
        Graphics::IRenderer& renderer,
        Graphics::GpuAssetCache* gpuAssets,
        const SelectionController* selection,
        const std::uint32_t runtimeSnapshotStorageSlot,
        std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos)
    {
        RuntimeRenderExtractionStats stats{};
        auto& registry = scene.Raw();
        std::unordered_set<std::uint32_t> liveRenderableKeys{};

        m_Transforms.clear();
        m_Visualizations.clear();
        m_Lights.clear();
        m_VisualizationState->Batch.Clear();

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
            ApplyMaterialTextureBindings(
                stableId,
                *sidecar,
                renderer,
                gpuAssets,
                stats);

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

            // RUNTIME-085 Slice B / RUNTIME-086 Slice B — runtime-authored
            // `GeometrySources` residency. The mesh and graph bridges run only
            // when the entity has stated no procedural intent and no asset
            // source at all; both are treated as declared alternatives the
            // residency bridges must not race against. Domain detection uses
            // the same `BuildConstView` path the rest of the engine uses, and
            // the resolved `ActiveDomain` selects exactly one bridge (mesh and
            // graph domains are mutually exclusive per entity).
            const bool sourceEligible = !proceduralBound
                && proceduralRef == nullptr
                && assetSource == nullptr;
            bool meshBoundThisFrame = false;
            bool meshDomainThisFrame = false;
            bool graphBoundThisFrame = false;
            bool graphDomainThisFrame = false;
            bool pointCloudBoundThisFrame = false;
            bool pointCloudDomainThisFrame = false;
            bool pointCloudResidencyDesiredThisFrame = false;
            // RUNTIME-088 Slice B — captured before `BindMeshGeometry` drains
            // the mesh dirty tags so the edge/vertex views can repack on the
            // same dirty frame; `meshViewsResident` records whether the views
            // were reconciled in the mesh branch (otherwise the post-flip block
            // releases any lingering views).
            bool meshDirtyThisFrame = false;
            bool meshViewsResident = false;
            if (sourceEligible)
            {
                namespace GS = ECS::Components::GeometrySources;
                const auto view = GS::BuildConstView(registry, entity);
                ApplyProgressivePresentationBindings(registry,
                                                     entity,
                                                     view,
                                                     *sidecar,
                                                     renderer,
                                                     gpuAssets,
                                                     stats);
                if (view.ActiveDomain == GS::Domain::Mesh)
                {
                    namespace D = ECS::Components::DirtyTags;
                    namespace G = Graphics::Components;
                    meshDomainThisFrame = true;
                    const bool wantsSurface =
                        registry.all_of<G::RenderSurface>(entity);
                    const bool wantsEdges =
                        registry.all_of<G::RenderEdges>(entity);
                    const auto* pointHint =
                        registry.try_get<G::RenderPoints>(entity);
                    const bool wantsPoints = pointHint != nullptr;
                    // Snapshot the mesh dirty state before BindMeshGeometry
                    // may drain the tags; all mesh render lanes key their
                    // reupload off the same coalesced signal.
                    meshDirtyThisFrame = registry.any_of<D::GpuDirty,
                                                         D::DirtyVertexPositions,
                                                         D::DirtyFaceTopology,
                                                         D::DirtyEdgeTopology>(entity);
                    if (wantsSurface)
                    {
                        meshBoundThisFrame = BindMeshGeometry(registry,
                                                              entity,
                                                              view,
                                                              *sidecar,
                                                              renderer,
                                                              stats);
                    }

                    // RUNTIME-106 — mesh render lanes compose through the same
                    // ECS-facing components as graph/point-cloud domains.
                    // Surface residency is no longer a prerequisite for edge
                    // or vertex rendering; the sidecars derive directly from
                    // the mesh domain view when their components are present.
                    if (wantsEdges || wantsPoints)
                    {
                        const RHI::GpuBounds viewBounds =
                            ExtractBounds(registry, entity, world.Matrix);
                        meshViewsResident = true;
                        const bool edgeSubmitted =
                            ReconcileMeshPrimitiveView(MeshPrimitiveViewKind::Edge,
                                                       view,
                                                       *sidecar,
                                                       world.Matrix,
                                                       sidecar->Material.EffectiveSlot,
                                                       viewBounds,
                                                       stableId,
                                                       wantsEdges,
                                                       nullptr,
                                                       meshDirtyThisFrame,
                                                       renderer,
                                                       stats);
                        const bool pointSubmitted =
                            ReconcileMeshPrimitiveView(MeshPrimitiveViewKind::Vertex,
                                                       view,
                                                       *sidecar,
                                                       world.Matrix,
                                                       sidecar->Material.EffectiveSlot,
                                                       viewBounds,
                                                       stableId,
                                                       wantsPoints,
                                                       pointHint,
                                                       meshDirtyThisFrame,
                                                       renderer,
                                                       stats);
                        if (!wantsSurface &&
                            meshDirtyThisFrame &&
                            (edgeSubmitted || pointSubmitted))
                        {
                            registry.remove<D::GpuDirty,
                                            D::DirtyVertexPositions,
                                            D::DirtyFaceTopology,
                                            D::DirtyEdgeTopology>(entity);
                        }
                    }
                    else
                    {
                        ReleaseMeshPrimitiveView(MeshPrimitiveViewKind::Edge,
                                                 *sidecar,
                                                 renderer,
                                                 stats);
                        ReleaseMeshPrimitiveView(MeshPrimitiveViewKind::Vertex,
                                                 *sidecar,
                                                 renderer,
                                                 stats);
                    }
                }
                else if (view.ActiveDomain == GS::Domain::Graph)
                {
                    graphDomainThisFrame = true;
                    graphBoundThisFrame = BindGraphGeometry(registry,
                                                            entity,
                                                            view,
                                                            *sidecar,
                                                            renderer,
                                                            stats);
                }
                else if (view.ActiveDomain == GS::Domain::PointCloud)
                {
                    pointCloudDomainThisFrame = true;
                    pointCloudResidencyDesiredThisFrame =
                        registry.all_of<Graphics::Components::RenderPoints>(entity);
                    if (pointCloudResidencyDesiredThisFrame)
                    {
                        // A point cloud is only renderable through the
                        // `RenderPoints` hint — `RenderSurface`/`RenderEdges`
                        // have no faces/edges to draw from a cloud.
                        pointCloudBoundThisFrame = BindPointCloudGeometry(registry,
                                                                          entity,
                                                                          view,
                                                                          *sidecar,
                                                                          renderer,
                                                                          stats);
                    }
                    else if (registry.any_of<Graphics::Components::RenderSurface,
                                             Graphics::Components::RenderEdges>(entity))
                    {
                        // Unsupported point-cloud lanes fail closed with a
                        // deterministic diagnostic instead of silently keeping
                        // or creating stale residency.
                        ++stats.PointCloudGeometryFailedPack;
                    }
                }
            }

            // Eligibility-flip release: if mesh was uploaded on a prior
            // frame but the entity no longer selects the mesh source this
            // frame (gained `ProceduralGeometryRef` / `AssetInstance::Source`,
            // or lost mesh-domain `GeometrySources` topology), enqueue the
            // cached upload for the same `framesInFlight` deferred-retire
            // window the procedural cache uses and increment
            // `MeshGeometryReleases`. When no other path re-bound the
            // instance this frame, detach the instance from the queued
            // mesh slot explicitly so the instance does not observe a
            // still-live but doomed slot during the retire window (the
            // procedural path's `SetInstanceGeometry` already covers the
            // procedural-replacement case). Transient pack failures on a
            // still-mesh-domain entity do NOT release: the old residency
            // remains bound so a later frame can recover, mirroring the
            // dirty-reupload fail-closed contract inside `BindMeshGeometry`.
            const bool stillMeshAttached =
                sourceEligible &&
                meshDomainThisFrame &&
                registry.all_of<Graphics::Components::RenderSurface>(entity);
            if (!stillMeshAttached && sidecar->MeshGeometry.IsValid())
            {
                EnqueueMeshRetire(sidecar->MeshGeometry);
                const bool replacementBoundThisFrame =
                    proceduralBound || graphBoundThisFrame || pointCloudBoundThisFrame;
                // Only detach if nothing else re-bound the instance this frame
                // (procedural take-over, a graph/point-cloud rebind after a
                // mesh→graph / mesh→point-cloud domain flip, or a mesh surface
                // reupload in the surface lane).
                if (!replacementBoundThisFrame)
                {
                    renderer.GetGpuWorld().SetInstanceGeometry(sidecar->Instance,
                                                                Graphics::GpuGeometryHandle{});
                    sidecar->Geometry = {};
                    sidecar->GpuSlot.SetGeometryHandle(Graphics::GpuGeometryHandle{});
                }
                sidecar->MeshGeometry = {};
                ++stats.MeshGeometryReleases;
            }

            // RUNTIME-086 Slice B — graph-residency eligibility flip, mirroring
            // the mesh release above. Fires when a previously-uploaded graph
            // entity gains a procedural/asset source, loses graph-domain
            // topology, or flips to mesh domain. A transient pack failure on a
            // still-graph-domain entity does NOT release (old residency stays
            // bound), matching the dirty-reupload fail-closed contract.
            const bool stillGraphAttached = sourceEligible && graphDomainThisFrame;
            if (!stillGraphAttached && sidecar->GraphGeometry.IsValid())
            {
                EnqueueGraphRetire(sidecar->GraphGeometry);
                if (!proceduralBound && !meshBoundThisFrame && !pointCloudBoundThisFrame)
                {
                    renderer.GetGpuWorld().SetInstanceGeometry(sidecar->Instance,
                                                                Graphics::GpuGeometryHandle{});
                }
                sidecar->GraphGeometry = {};
                sidecar->GraphPackedLines = false;
                sidecar->GraphPackedPoints = false;
                ++stats.GraphGeometryReleases;
            }

            // RUNTIME-087 — point-cloud-residency eligibility flip, mirroring
            // the mesh/graph releases above. Fires when a previously-uploaded
            // point-cloud entity gains a procedural/asset source, loses
            // point-cloud-domain topology, or flips to mesh/graph domain. A
            // transient pack failure on a still-point-cloud-domain entity does
            // NOT release (old residency stays bound), matching the
            // dirty-reupload fail-closed contract.
            const bool stillPointCloudAttached =
                sourceEligible &&
                pointCloudDomainThisFrame &&
                pointCloudResidencyDesiredThisFrame;
            if (!stillPointCloudAttached && sidecar->PointCloudGeometry.IsValid())
            {
                EnqueuePointCloudRetire(sidecar->PointCloudGeometry);
                if (!proceduralBound && !meshBoundThisFrame && !graphBoundThisFrame)
                {
                    renderer.GetGpuWorld().SetInstanceGeometry(sidecar->Instance,
                                                                Graphics::GpuGeometryHandle{});
                }
                sidecar->PointCloudGeometry = {};
                ++stats.PointCloudGeometryReleases;
            }

            // RUNTIME-088 Slice B — when the entity is not a resident mesh this
            // frame (never mesh-domain, flipped to procedural/asset/another
            // domain, or its surface residency was released above), drop any
            // edge/vertex views it may have carried. The in-branch reconcile
            // above already handles per-view disable while the parent stays a
            // resident mesh; this covers the parent-level flips.
            if (!meshViewsResident)
            {
                ReleaseMeshPrimitiveView(MeshPrimitiveViewKind::Edge, *sidecar, renderer, stats);
                ReleaseMeshPrimitiveView(MeshPrimitiveViewKind::Vertex, *sidecar, renderer, stats);
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
            else if (!proceduralBound && !meshBoundThisFrame && !graphBoundThisFrame
                     && !pointCloudBoundThisFrame)
            {
                sidecar->GpuSlot.ClearSourceAsset();
            }

            m_Visualizations.push_back(Graphics::VisualizationSyncRecord{
                .StableId = stableId,
                .Material = &sidecar->Material,
                .GpuSlot = &sidecar->GpuSlot,
                .Visualization = sidecar->HasVisualization ? &sidecar->Visualization : nullptr,
                .Points = registry.try_get<Graphics::Components::RenderPoints>(entity),
            });
            AppendVisualizationAdapters(stableId, *sidecar, stats);

            const bool primaryRenderableSubmitted =
                proceduralBound ||
                meshBoundThisFrame ||
                graphBoundThisFrame ||
                pointCloudBoundThisFrame ||
                assetSource != nullptr;
            if (primaryRenderableSubmitted)
            {
                std::uint32_t renderFlags = BuildRenderFlags(registry, entity);
                if (meshDomainThisFrame)
                {
                    renderFlags &= ~(RHI::GpuRender_Line | RHI::GpuRender_Point);
                    renderFlags &= ~RHI::GpuRender_Unlit;
                }
                m_Transforms.push_back(Graphics::TransformSyncRecord{
                    .StableId = stableId,
                    .Instance = sidecar->Instance,
                    .Model = world.Matrix,
                    .RenderFlags = renderFlags,
                    .Bounds = ExtractBounds(registry, entity, world.Matrix),
                    .MaterialSlot = sidecar->Material.EffectiveSlot,
                    .HasMaterialSlot = true,
                });
            }
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

        stats.VisualizationAttributeBufferPacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.AttributeBuffers.size());
        stats.VisualizationScalarPacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.Scalars.size());
        stats.VisualizationColorPacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.Colors.size());
        stats.VisualizationVectorFieldPacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.VectorFields.size());
        stats.VisualizationIsolinePacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.Isolines.size());
        stats.VisualizationHtexAtlasPacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.HtexAtlases.size());
        stats.VisualizationFragmentBakeAtlasPacketCount =
            static_cast<std::uint32_t>(m_VisualizationState->Batch.FragmentBakeAtlases.size());

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

        // RUNTIME-085 Slice C — mesh deferred-retire FreeRetires delta is
        // surfaced via the same release/tick cadence the procedural cache
        // uses. The releases-this-frame counter is folded inline
        // (`stats.MeshGeometryReleases` is bumped on each enqueue or
        // reupload), so only the actual-free count needs the snapshot
        // diff here.
        stats.MeshGeometryFreeRetires =
            m_MeshFreeRetires - m_PrevMeshFreeRetires;
        m_PrevMeshFreeRetires = m_MeshFreeRetires;

        // RUNTIME-086 Slice B — graph deferred-retire FreeRetires delta,
        // mirroring the mesh accounting above.
        stats.GraphGeometryFreeRetires =
            m_GraphFreeRetires - m_PrevGraphFreeRetires;
        m_PrevGraphFreeRetires = m_GraphFreeRetires;

        // RUNTIME-087 — point-cloud deferred-retire FreeRetires delta,
        // mirroring the graph accounting above.
        stats.PointCloudGeometryFreeRetires =
            m_PointCloudFreeRetires - m_PrevPointCloudFreeRetires;
        m_PrevPointCloudFreeRetires = m_PointCloudFreeRetires;

        // RUNTIME-088 Slice B — mesh-primitive-view deferred-retire FreeRetires
        // delta, mirroring the per-domain accounting above. Edge and vertex
        // view frees share this one counter (one retire queue, one tick).
        stats.MeshPrimitiveViewFreeRetires =
            m_MeshPrimitiveViewFreeRetires - m_PrevMeshPrimitiveViewFreeRetires;
        m_PrevMeshPrimitiveViewFreeRetires = m_MeshPrimitiveViewFreeRetires;

        // RUNTIME-089 Slice B — attach the runtime selection snapshot when a
        // controller is wired. The controller owns the backing storage for
        // `SelectedStableIds()`; the renderer copies it during
        // `SubmitRuntimeSnapshots`, so the span only needs to be valid for the
        // duration of this call.
        Graphics::RuntimeRenderSnapshotBatch batch{
            .Transforms                     = m_Transforms,
            .Lights                         = m_Lights,
            .Visualizations                 = m_Visualizations,
            .VisualizationPropertyBuffers   = m_VisualizationState->Batch.PropertyBuffers,
            .VisualizationAttributeBuffers  = m_VisualizationState->Batch.AttributeBuffers,
            .VisualizationScalars           = m_VisualizationState->Batch.Scalars,
            .VisualizationColors            = m_VisualizationState->Batch.Colors,
            .VisualizationVectorFields      = m_VisualizationState->Batch.VectorFields,
            .VisualizationIsolines          = m_VisualizationState->Batch.Isolines,
            .VisualizationHtexAtlases       = m_VisualizationState->Batch.HtexAtlases,
            .VisualizationFragmentBakeAtlases = m_VisualizationState->Batch.FragmentBakeAtlases,
            .TransformGizmos                = transformGizmos,
            .SpatialDebugBounds             = m_SpatialDebugBatch.Bounds,
            .SpatialDebugHierarchyNodes     = m_SpatialDebugBatch.HierarchyNodes,
            .SpatialDebugSplitPlanes        = m_SpatialDebugBatch.SplitPlanes,
            .SpatialDebugConvexHullVertices = m_SpatialDebugBatch.ConvexHullVertices,
            .SpatialDebugConvexHullEdges    = m_SpatialDebugBatch.ConvexHullEdges,
            .SpatialDebugPointMarkers       = m_SpatialDebugBatch.PointMarkers,
        };
        if (selection != nullptr)
        {
            batch.SelectionSelectedStableIds = selection->SelectedStableIds();
            batch.SelectionHoveredStableId   = selection->HoveredStableId();
            batch.SelectionHasHovered        = selection->HasHovered();
        }
        renderer.SubmitRuntimeSnapshots(batch, runtimeSnapshotStorageSlot);

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

    void RenderExtractionCache::TickMeshGeometry(std::uint64_t currentFrame,
                                                  std::uint32_t framesInFlight,
                                                  Graphics::IRenderer& renderer)
    {
        // Mirror `ProceduralGeometryCache::Tick`: anchor deadlines on
        // newly-enqueued records the first time the tick observes them,
        // then free entries whose deadline has been reached.
        const std::uint64_t deadline = currentFrame + std::uint64_t{framesInFlight};
        for (auto& rec : m_MeshRetire)
        {
            if (!rec.DeadlineSet)
            {
                rec.Deadline = deadline;
                rec.DeadlineSet = true;
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

    void RenderExtractionCache::TickGraphGeometry(std::uint64_t currentFrame,
                                                  std::uint32_t framesInFlight,
                                                  Graphics::IRenderer& renderer)
    {
        // Mirror `TickMeshGeometry`: anchor deadlines on newly-enqueued
        // records the first time the tick observes them, then free entries
        // whose deadline has been reached.
        const std::uint64_t deadline = currentFrame + std::uint64_t{framesInFlight};
        for (auto& rec : m_GraphRetire)
        {
            if (!rec.DeadlineSet)
            {
                rec.Deadline = deadline;
                rec.DeadlineSet = true;
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

    void RenderExtractionCache::TickPointCloudGeometry(std::uint64_t currentFrame,
                                                       std::uint32_t framesInFlight,
                                                       Graphics::IRenderer& renderer)
    {
        // Mirror `TickGraphGeometry`: anchor deadlines on newly-enqueued
        // records the first time the tick observes them, then free entries
        // whose deadline has been reached.
        const std::uint64_t deadline = currentFrame + std::uint64_t{framesInFlight};
        for (auto& rec : m_PointCloudRetire)
        {
            if (!rec.DeadlineSet)
            {
                rec.Deadline = deadline;
                rec.DeadlineSet = true;
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

    void RenderExtractionCache::TickMeshPrimitiveViewGeometry(std::uint64_t currentFrame,
                                                              std::uint32_t framesInFlight,
                                                              Graphics::IRenderer& renderer)
    {
        // Mirror `TickMeshGeometry`: anchor deadlines on newly-enqueued records
        // the first time the tick observes them, then free entries whose
        // deadline has been reached. Edge and vertex view handles share this
        // queue and the shared `m_MeshPrimitiveViewFreeRetires` accumulator.
        const std::uint64_t deadline = currentFrame + std::uint64_t{framesInFlight};
        for (auto& rec : m_MeshPrimitiveViewRetire)
        {
            if (!rec.DeadlineSet)
            {
                rec.Deadline = deadline;
                rec.DeadlineSet = true;
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

    void RenderExtractionCache::ClearSceneState(Graphics::IRenderer& renderer)
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
            if (sidecar.GraphGeometry.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(sidecar.GraphGeometry);
                ++stats.GraphGeometryReleases;
            }
            if (sidecar.PointCloudGeometry.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(sidecar.PointCloudGeometry);
                ++stats.PointCloudGeometryReleases;
            }
            // RUNTIME-088 Slice B — hard teardown of the edge/vertex view
            // sidecars: free geometry directly (the deferred window is collapsed
            // at shutdown) and free their own instances.
            if (sidecar.MeshEdgeViewGeometry.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(sidecar.MeshEdgeViewGeometry);
                sidecar.MeshEdgeViewGeometry = {};
                ++stats.MeshEdgeViewReleases;
            }
            if (sidecar.MeshEdgeViewInstance.IsValid())
            {
                renderer.GetGpuWorld().FreeInstance(sidecar.MeshEdgeViewInstance);
                sidecar.MeshEdgeViewInstance = {};
                ++stats.FreedInstanceCount;
            }
            if (sidecar.MeshVertexViewGeometry.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(sidecar.MeshVertexViewGeometry);
                sidecar.MeshVertexViewGeometry = {};
                ++stats.MeshVertexViewReleases;
            }
            if (sidecar.MeshVertexViewInstance.IsValid())
            {
                renderer.GetGpuWorld().FreeInstance(sidecar.MeshVertexViewInstance);
                sidecar.MeshVertexViewInstance = {};
                ++stats.FreedInstanceCount;
            }
            renderer.GetGpuWorld().FreeInstance(sidecar.Instance);
            ++stats.FreedInstanceCount;
        }
        m_Renderables.clear();

        // Scene replacement is a hard boundary. Collapse procedural deferred
        // retirement immediately so no previous-scene handles or retire deltas
        // leak into the next scene's first extraction.
        m_ProceduralGeometry.Tick(
            std::numeric_limits<std::uint64_t>::max(),
            0u,
            [&renderer](Graphics::GpuGeometryHandle handle)
            {
                renderer.GetGpuWorld().FreeGeometry(handle);
            });
        m_PrevProceduralStats = m_ProceduralGeometry.Stats();

        // RUNTIME-085 Slice C — drain any pending mesh deferred-retire
        // records inline. Scene reset is a hard teardown, so the
        // `framesInFlight` window is collapsed and handles are freed directly
        // rather than waiting on `TickMeshGeometry`.
        for (auto& rec : m_MeshRetire)
        {
            if (rec.Handle.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(rec.Handle);
            }
        }
        m_MeshRetire.clear();

        // RUNTIME-086 Slice B — drain the graph deferred-retire queue inline,
        // mirroring the mesh teardown above.
        for (auto& rec : m_GraphRetire)
        {
            if (rec.Handle.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(rec.Handle);
            }
        }
        m_GraphRetire.clear();

        // RUNTIME-087 — drain the point-cloud deferred-retire queue inline,
        // mirroring the graph teardown above.
        for (auto& rec : m_PointCloudRetire)
        {
            if (rec.Handle.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(rec.Handle);
            }
        }
        m_PointCloudRetire.clear();

        // RUNTIME-088 Slice B — drain the mesh-primitive-view deferred-retire
        // queue inline and drop the cache-owned view settings.
        for (auto& rec : m_MeshPrimitiveViewRetire)
        {
            if (rec.Handle.IsValid())
            {
                renderer.GetGpuWorld().FreeGeometry(rec.Handle);
            }
        }
        m_MeshPrimitiveViewRetire.clear();
        m_MeshPrimitiveViewSettings.clear();
        m_MaterialTextureBindings.clear();
        m_PrevMeshFreeRetires = m_MeshFreeRetires;
        m_PrevGraphFreeRetires = m_GraphFreeRetires;
        m_PrevPointCloudFreeRetires = m_PointCloudFreeRetires;
        m_PrevMeshPrimitiveViewFreeRetires = m_MeshPrimitiveViewFreeRetires;

        m_Transforms.clear();
        m_Visualizations.clear();
        m_Lights.clear();

        m_SpatialDebugBatch.Clear();
        m_VisualizationState->Bindings.clear();
        m_VisualizationState->Batch.Clear();

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{});
        m_LastStats = stats;
    }

    void RenderExtractionCache::Shutdown(Graphics::IRenderer& renderer)
    {
        ClearSceneState(renderer);

        // RUNTIME-082 Slice D — shutdown drops owned adapters + clears the
        // registry mirrors. Scene replacement intentionally preserves these
        // registrations; full renderer teardown owns their destruction.
        m_SpatialDebugRegistry.Clear();
        m_SpatialDebugAdapters.clear();
        m_VisualizationState->Registry.Clear();
        m_VisualizationState->Adapters.clear();
    }
}
