module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
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
import Extrinsic.Runtime.GeometryAvailability;
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
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    RenderExtractionCache::RenderExtractionCache()
        : m_State(std::make_unique<State>())
    {
    }

    RenderExtractionCache::~RenderExtractionCache() = default;

    RuntimeRenderExtractionStats RenderExtractionCache::ExtractAndSubmit(
        ECS::Scene::Registry& scene,
        Graphics::IRenderer& renderer,
        Graphics::GpuAssetCache* gpuAssets,
        const SelectionController* selection,
        const std::uint32_t runtimeSnapshotStorageSlot,
        std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos,
        const WorldHandle world)
    {
        return m_State->ExtractAndSubmit(
            scene,
            renderer,
            gpuAssets,
            selection,
            runtimeSnapshotStorageSlot,
            transformGizmos,
            world);
    }

    void RenderExtractionCache::ClearSceneState(Graphics::IRenderer& renderer)
    {
        m_State->ClearSceneState(renderer);
    }

    void RenderExtractionCache::Shutdown(Graphics::IRenderer& renderer)
    {
        m_State->Shutdown(renderer);
    }

    void RenderExtractionCache::TickProceduralGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        m_State->TickProceduralGeometry(
            currentFrame,
            framesInFlight,
            renderer);
    }

    void RenderExtractionCache::TickMeshGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        m_State->TickMeshGeometry(currentFrame, framesInFlight, renderer);
    }

    void RenderExtractionCache::TickGraphGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        m_State->TickGraphGeometry(currentFrame, framesInFlight, renderer);
    }

    void RenderExtractionCache::TickPointCloudGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        m_State->TickPointCloudGeometry(
            currentFrame,
            framesInFlight,
            renderer);
    }

    void RenderExtractionCache::TickMeshPrimitiveViewGeometry(
        const std::uint64_t currentFrame,
        const std::uint32_t framesInFlight,
        Graphics::IRenderer& renderer)
    {
        m_State->TickMeshPrimitiveViewGeometry(
            currentFrame,
            framesInFlight,
            renderer);
    }

    void RenderExtractionCache::SetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId,
        const MeshPrimitiveViewSettings settings)
    {
        m_State->SetMeshPrimitiveViewSettings(stableEntityId, settings);
    }

    void RenderExtractionCache::ClearMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_State->ClearMeshPrimitiveViewSettings(stableEntityId);
    }

    MeshPrimitiveViewSettings
    RenderExtractionCache::GetMeshPrimitiveViewSettings(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_State->GetMeshPrimitiveViewSettings(stableEntityId);
    }

    void RenderExtractionCache::SetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId,
        Graphics::MaterialTextureAssetBindings bindings)
    {
        m_State->SetMaterialTextureAssetBindings(
            stableEntityId,
            std::move(bindings));
    }

    void RenderExtractionCache::ClearMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) noexcept
    {
        m_State->ClearMaterialTextureAssetBindings(stableEntityId);
    }

    std::optional<Graphics::MaterialTextureAssetBindings>
    RenderExtractionCache::GetMaterialTextureAssetBindings(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_State->GetMaterialTextureAssetBindings(stableEntityId);
    }

    const RuntimeRenderExtractionStats&
    RenderExtractionCache::GetLastStats() const noexcept
    {
        return m_State->GetLastStats();
    }

    std::uint32_t
    RenderExtractionCache::GetTrackedRenderableCount() const noexcept
    {
        return m_State->GetTrackedRenderableCount();
    }

    std::size_t
    RenderExtractionCache::GetLiveRenderableKeyScratchBucketCountForTest()
        const noexcept
    {
        return m_State->GetLiveRenderableKeyScratchBucketCountForTest();
    }

    std::optional<RenderExtractionCache::RenderableSidecarView>
    RenderExtractionCache::FindRenderableSidecarForTest(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_State->FindRenderableSidecarForTest(stableEntityId);
    }

    std::optional<RenderExtractionCache::GpuRenderableAvailabilityView>
    RenderExtractionCache::FindGpuRenderableAvailability(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_State->FindGpuRenderableAvailability(stableEntityId);
    }

    const ProceduralGeometryCache&
    RenderExtractionCache::GetProceduralGeometryCacheForTest() const noexcept
    {
        return m_State->GetProceduralGeometryCacheForTest();
    }

    void RenderExtractionCache::RegisterSpatialDebugAdapter(
        const std::uint64_t key,
        std::unique_ptr<ISpatialDebugAdapter> adapter)
    {
        m_State->RegisterSpatialDebugAdapter(key, std::move(adapter));
    }

    bool RenderExtractionCache::UnregisterSpatialDebugAdapter(
        const std::uint64_t key) noexcept
    {
        return m_State->UnregisterSpatialDebugAdapter(key);
    }

    std::size_t
    RenderExtractionCache::GetSpatialDebugAdapterCount() const noexcept
    {
        return m_State->GetSpatialDebugAdapterCount();
    }

    const SpatialDebugAdapterRegistry&
    RenderExtractionCache::GetSpatialDebugRegistryForTest() const noexcept
    {
        return m_State->GetSpatialDebugRegistryForTest();
    }

    void RenderExtractionCache::RegisterVisualizationAdapter(
        const std::uint64_t key,
        std::unique_ptr<IVisualizationAdapter> adapter)
    {
        m_State->RegisterVisualizationAdapter(key, std::move(adapter));
    }

    bool RenderExtractionCache::UnregisterVisualizationAdapter(
        const std::uint64_t key) noexcept
    {
        return m_State->UnregisterVisualizationAdapter(key);
    }

    std::size_t
    RenderExtractionCache::GetVisualizationAdapterCount() const noexcept
    {
        return m_State->GetVisualizationAdapterCount();
    }

    const VisualizationAdapterRegistry&
    RenderExtractionCache::GetVisualizationAdapterRegistryForTest()
        const noexcept
    {
        return m_State->GetVisualizationAdapterRegistryForTest();
    }

    void RenderExtractionCache::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        VisualizationAdapterBinding binding)
    {
        m_State->SetVisualizationAdapterBinding(
            stableEntityId,
            std::move(binding));
    }

    void RenderExtractionCache::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        m_State->ClearVisualizationAdapterBinding(stableEntityId);
    }

    std::optional<RenderExtractionCache::VisualizationAdapterBinding>
    RenderExtractionCache::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        return m_State->GetVisualizationAdapterBinding(stableEntityId);
    }

    std::uint64_t
    RenderExtractionCache::GetVisualizationAdapterBindingRevision()
        const noexcept
    {
        return m_State->GetVisualizationAdapterBindingRevision();
    }

    RenderExtractionCache::State::State()
        : m_VisualizationState(std::make_unique<VisualizationAdapterState>())
    {
    }

    RenderExtractionCache::State::~State() = default;

    const RuntimeRenderExtractionStats& RenderExtractionCache::State::GetLastStats() const noexcept
    {
        return m_LastStats;
    }

    std::uint32_t RenderExtractionCache::State::GetTrackedRenderableCount() const noexcept
    {
        return static_cast<std::uint32_t>(m_Renderables.size());
    }

    std::size_t RenderExtractionCache::State::GetLiveRenderableKeyScratchBucketCountForTest() const noexcept
    {
        return m_LiveRenderableKeys.bucket_count();
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

        [[nodiscard]] Graphics::Components::VisualizationConfig::Domain ToColorBufferConfigDomain(
            const Graphics::Components::VisualizationConfig::ColorSource source) noexcept
        {
            using ColorSource = Graphics::Components::VisualizationConfig::ColorSource;
            using Domain = Graphics::Components::VisualizationConfig::Domain;
            switch (source)
            {
            case ColorSource::PerEdgeBuffer:
                return Domain::Edge;
            case ColorSource::PerFaceBuffer:
                return Domain::Face;
            case ColorSource::PerVertexBuffer:
            case ColorSource::Material:
            case ColorSource::UniformColor:
            case ColorSource::ScalarField:
                return Domain::Vertex;
            }
            return Domain::Vertex;
        }

        [[nodiscard]] const Geometry::PropertySet* PropertySetForVisualizationDomain(
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const Graphics::Components::VisualizationConfig::Domain domain) noexcept
        {
            namespace GS = ECS::Components::GeometrySources;
            using Domain = Graphics::Components::VisualizationConfig::Domain;
            const GS::SourceAvailability sources =
                GS::BuildSourceAvailability(view);

            if (sources.ProvenanceDomain == GS::Domain::Mesh)
            {
                switch (domain)
                {
                case Domain::Vertex:
                    return view.VertexSource != nullptr
                        ? &view.VertexSource->Properties
                        : nullptr;
                case Domain::Edge:
                    return view.EdgeSource != nullptr
                        ? &view.EdgeSource->Properties
                        : nullptr;
                case Domain::Face:
                    return view.FaceSource != nullptr
                        ? &view.FaceSource->Properties
                        : nullptr;
                }
            }

            if (sources.ProvenanceDomain == GS::Domain::Graph)
            {
                switch (domain)
                {
                case Domain::Vertex:
                    return view.NodeSource != nullptr
                        ? &view.NodeSource->Properties
                        : nullptr;
                case Domain::Edge:
                    return view.EdgeSource != nullptr
                        ? &view.EdgeSource->Properties
                        : nullptr;
                case Domain::Face:
                    return nullptr;
                }
            }

            if (sources.ProvenanceDomain == GS::Domain::PointCloud &&
                domain == Domain::Vertex)
            {
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            }

            return nullptr;
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

        void AppendScalarVisualizationPropertyBuffer(
            const std::uint32_t stableId,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const Graphics::Components::VisualizationConfig* visualization,
            VisualizationAdapterBatch& batch,
            RuntimeRenderExtractionStats& stats)
        {
            if (!IsScalarVisualizationSource(visualization) ||
                visualization->ScalarFieldName.empty())
            {
                return;
            }

            const Geometry::PropertySet* properties =
                PropertySetForVisualizationDomain(
                    view, visualization->ScalarDomain);
            if (properties == nullptr)
            {
                return;
            }

            PropertyScalarAdapter adapter{Geometry::ConstPropertySet{*properties}};
            VisualizationAdapterStats perAdapter{};
            VisualizationAdapterOptions options{};
            options.SourceName = visualization->ScalarFieldName;
            options.OutputName = visualization->ScalarFieldName;
            options.Domain = ToVisualizationAttributeDomain(
                visualization->ScalarDomain);
            options.AutoRange = visualization->Scalar.AutoRange;
            options.RangeMin = visualization->Scalar.RangeMin;
            options.RangeMax = visualization->Scalar.RangeMax;
            options.Colormap = visualization->Scalar.Map;
            options.PropertyBufferSourceKey =
                BuildVisualizationPropertySourceKey(
                    stableId, "scalar", visualization->ScalarFieldName);
            adapter.Append(batch, options, perAdapter);

            stats.VisualizationAdapterPacketAppendCount += perAdapter.PacketAppendCount;
            stats.VisualizationAdapterMissingSourceCount += perAdapter.MissingSourceCount;
            stats.VisualizationAdapterUnsupportedSourceTypeCount += perAdapter.UnsupportedSourceTypeCount;
            stats.VisualizationAdapterEmptySourceCount += perAdapter.EmptySourceCount;
            stats.VisualizationAdapterInvalidBufferCount +=
                perAdapter.InvalidBufferCount + perAdapter.InvalidResourceCount;
            stats.VisualizationAdapterInvalidRangeCount += perAdapter.InvalidRangeCount;
            stats.VisualizationAdapterNonFiniteValueCount += perAdapter.NonFiniteValueCount;
            stats.VisualizationAdapterElementCountOverflowCount += perAdapter.ElementCountOverflowCount;
            stats.VisualizationAdapterManualRangeCount += perAdapter.ManualRangeCount;
            stats.VisualizationAdapterFlatAutoRangeExpandedCount += perAdapter.FlatAutoRangeExpandedCount;
            stats.VisualizationAdapterRobustAutoRangeClampedCount += perAdapter.RobustAutoRangeClampedCount;
            stats.VisualizationAdapterScalarValueScanCount += perAdapter.ScalarValueScanCount;
        }

        void AppendColorVisualizationPropertyBuffer(
            const std::uint32_t stableId,
            const ECS::Components::GeometrySources::ConstSourceView& view,
            const Graphics::Components::VisualizationConfig* visualization,
            VisualizationAdapterBatch& batch,
            RuntimeRenderExtractionStats& stats)
        {
            if (!IsColorBufferVisualizationSource(visualization) ||
                visualization->ColorBufferName.empty())
            {
                return;
            }

            const Geometry::PropertySet* properties =
                PropertySetForVisualizationDomain(
                    view,
                    ToColorBufferConfigDomain(visualization->Source));
            if (properties == nullptr)
            {
                return;
            }

            KMeansLabelAdapter adapter{Geometry::ConstPropertySet{*properties}};
            VisualizationAdapterStats perAdapter{};
            VisualizationAdapterOptions options{};
            options.SourceName = visualization->ColorBufferName;
            options.OutputName = visualization->ColorBufferName;
            options.Domain = ToColorBufferDomain(visualization->Source);
            options.PropertyBufferSourceKey =
                BuildVisualizationPropertySourceKey(
                    stableId, "color", visualization->ColorBufferName);
            adapter.Append(batch, options, perAdapter);

            stats.VisualizationAdapterPacketAppendCount += perAdapter.PacketAppendCount;
            stats.VisualizationAdapterMissingSourceCount += perAdapter.MissingSourceCount;
            stats.VisualizationAdapterUnsupportedSourceTypeCount += perAdapter.UnsupportedSourceTypeCount;
            stats.VisualizationAdapterEmptySourceCount += perAdapter.EmptySourceCount;
            stats.VisualizationAdapterInvalidBufferCount +=
                perAdapter.InvalidBufferCount + perAdapter.InvalidResourceCount;
            stats.VisualizationAdapterInvalidRangeCount += perAdapter.InvalidRangeCount;
            stats.VisualizationAdapterNonFiniteValueCount += perAdapter.NonFiniteValueCount;
            stats.VisualizationAdapterElementCountOverflowCount += perAdapter.ElementCountOverflowCount;
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

        enum class VisualizationLane : std::uint8_t
        {
            Surface,
            Edges,
            Points,
        };

        [[nodiscard]] const Graphics::Components::VisualizationConfig*
        ResolveVisualizationForLane(
            const Graphics::Components::VisualizationConfig* fallback,
            const Graphics::Components::VisualizationLaneOverrides* overrides,
            const VisualizationLane lane) noexcept
        {
            if (overrides != nullptr)
            {
                switch (lane)
                {
                case VisualizationLane::Surface:
                    if (overrides->Surface.has_value())
                        return &*overrides->Surface;
                    break;
                case VisualizationLane::Edges:
                    if (overrides->Edges.has_value())
                        return &*overrides->Edges;
                    break;
                case VisualizationLane::Points:
                    if (overrides->Points.has_value())
                        return &*overrides->Points;
                    break;
                }
            }
            return fallback;
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

        [[nodiscard]] RHI::GpuEntityConfig BuildImmediateLaneConfig(
            const Graphics::Components::VisualizationConfig* visualization,
            const Graphics::Components::RenderEdges* edges,
            const Graphics::Components::RenderPoints* points) noexcept
        {
            namespace G = Graphics::Components;
            RHI::GpuEntityConfig cfg{};
            cfg.ColorSourceMode = 1u;
            cfg.VisualizationAlpha = 1.0f;
            cfg.UniformColor = {0.02f, 0.02f, 0.02f, 1.0f};
            if (visualization != nullptr &&
                visualization->Source ==
                    G::VisualizationConfig::ColorSource::UniformColor)
            {
                cfg.UniformColor = visualization->Color;
            }
            if (edges != nullptr)
            {
                if (const auto* uniform =
                        std::get_if<float>(&edges->WidthSource);
                    uniform != nullptr)
                {
                    cfg.Line.LineWidth = *uniform;
                }
            }
            if (points != nullptr)
            {
                cfg.Point.PointSize = UniformPointSizeOrDefault(points);
                cfg.Point.PointMode = ToRenderPointMode(points->Type);
            }
            return cfg;
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

        struct GeometryDirtyPlan
        {
            bool Dirty = false;
            bool RequiresFullUpload = false;
            bool MeshPrimitiveViewDirty = false;
            Graphics::GpuWorld::GeometryChannelUpdateMask Channels{};
        };

        [[nodiscard]] GeometryDirtyPlan BuildMeshGeometryDirtyPlan(
            const entt::registry& registry,
            const entt::entity entity)
        {
            namespace D = ECS::Components::DirtyTags;
            GeometryDirtyPlan plan{};
            const bool vertexAttributes =
                registry.any_of<D::DirtyVertexAttributes>(entity);

            plan.Channels.Position =
                registry.any_of<D::DirtyVertexPositions>(entity);
            plan.Channels.Texcoord =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexTexcoords>(entity);
            plan.Channels.Normal =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexNormals>(entity);
            plan.Channels.Color =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexColors>(entity);

            plan.RequiresFullUpload =
                registry.any_of<D::GpuDirty,
                                D::DirtyFaceTopology,
                                D::DirtyEdgeTopology>(entity);
            plan.Dirty = plan.RequiresFullUpload || plan.Channels.Any();
            plan.MeshPrimitiveViewDirty =
                plan.RequiresFullUpload || plan.Channels.Position;
            return plan;
        }

        [[nodiscard]] GeometryDirtyPlan BuildGraphGeometryDirtyPlan(
            const entt::registry& registry,
            const entt::entity entity)
        {
            namespace D = ECS::Components::DirtyTags;
            GeometryDirtyPlan plan{};
            const bool vertexAttributes =
                registry.any_of<D::DirtyVertexAttributes>(entity);

            plan.Channels.Position =
                registry.any_of<D::DirtyVertexPositions>(entity);
            plan.Channels.Texcoord =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexTexcoords>(entity);
            plan.Channels.Normal =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexNormals>(entity);
            plan.Channels.Color =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexColors>(entity);

            plan.RequiresFullUpload =
                registry.any_of<D::GpuDirty,
                                D::DirtyEdgeTopology>(entity);
            plan.Dirty = plan.RequiresFullUpload || plan.Channels.Any();
            return plan;
        }

        [[nodiscard]] GeometryDirtyPlan BuildPointCloudGeometryDirtyPlan(
            const entt::registry& registry,
            const entt::entity entity)
        {
            namespace D = ECS::Components::DirtyTags;
            GeometryDirtyPlan plan{};
            const bool vertexAttributes =
                registry.any_of<D::DirtyVertexAttributes>(entity);

            plan.Channels.Position =
                registry.any_of<D::DirtyVertexPositions>(entity);
            plan.Channels.Texcoord =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexTexcoords>(entity);
            plan.Channels.Normal =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexNormals>(entity);
            plan.Channels.Color =
                vertexAttributes ||
                registry.any_of<D::DirtyVertexColors>(entity);

            plan.RequiresFullUpload = registry.any_of<D::GpuDirty>(entity);
            plan.Dirty = plan.RequiresFullUpload || plan.Channels.Any();
            return plan;
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
            const SourceAvailability sources = BuildSourceAvailability(view);
            if (sources.ProvenanceDomain != Domain::Mesh ||
                view.VertexSource == nullptr)
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

    RenderExtractionGeometryDirtyPlan
    BuildRenderExtractionMeshGeometryDirtyPlan(
        const entt::registry& registry,
        const entt::entity entity)
    {
        const GeometryDirtyPlan plan =
            BuildMeshGeometryDirtyPlan(registry, entity);
        return {
            .Dirty = plan.Dirty,
            .RequiresFullUpload = plan.RequiresFullUpload,
            .MeshPrimitiveViewDirty = plan.MeshPrimitiveViewDirty,
            .Channels = plan.Channels,
        };
    }

    RenderExtractionGeometryDirtyPlan
    BuildRenderExtractionGraphGeometryDirtyPlan(
        const entt::registry& registry,
        const entt::entity entity)
    {
        const GeometryDirtyPlan plan =
            BuildGraphGeometryDirtyPlan(registry, entity);
        return {
            .Dirty = plan.Dirty,
            .RequiresFullUpload = plan.RequiresFullUpload,
            .MeshPrimitiveViewDirty = plan.MeshPrimitiveViewDirty,
            .Channels = plan.Channels,
        };
    }

    RenderExtractionGeometryDirtyPlan
    BuildRenderExtractionPointCloudGeometryDirtyPlan(
        const entt::registry& registry,
        const entt::entity entity)
    {
        const GeometryDirtyPlan plan =
            BuildPointCloudGeometryDirtyPlan(registry, entity);
        return {
            .Dirty = plan.Dirty,
            .RequiresFullUpload = plan.RequiresFullUpload,
            .MeshPrimitiveViewDirty = plan.MeshPrimitiveViewDirty,
            .Channels = plan.Channels,
        };
    }

    RenderExtractionMeshTexcoordFallbackDiagnostics
    DiagnoseRenderExtractionMeshTexcoordFallback(
        const ECS::Components::GeometrySources::ConstSourceView& view) noexcept
    {
        const MeshTexcoordFallbackDiagnostics diagnostics =
            DiagnoseMeshTexcoordFallback(view);
        return {
            .MissingOrMismatched = diagnostics.MissingOrMismatched,
            .NonFinite = diagnostics.NonFinite,
        };
    }

    RHI::GpuEntityConfig BuildRenderExtractionImmediateLaneConfig(
        const Graphics::Components::VisualizationConfig* visualization,
        const Graphics::Components::RenderEdges* edges,
        const Graphics::Components::RenderPoints* points) noexcept
    {
        return BuildImmediateLaneConfig(visualization, edges, points);
    }

    bool IsRenderExtractionScalarVisualizationSource(
        const Graphics::Components::VisualizationConfig* visualization) noexcept
    {
        return IsScalarVisualizationSource(visualization);
    }

    bool IsRenderExtractionColorBufferVisualizationSource(
        const Graphics::Components::VisualizationConfig* visualization) noexcept
    {
        return IsColorBufferVisualizationSource(visualization);
    }

    VisualizationAdapterOptions
    BuildRenderExtractionVisualizationAdapterOptions(
        const std::uint32_t stableId,
        const RenderExtractionCache::VisualizationAdapterBinding& binding,
        const Graphics::Components::VisualizationConfig* visualization)
    {
        return BuildVisualizationAdapterOptions(
            stableId,
            binding,
            visualization);
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

    RenderExtractionCache::State::RenderableSidecar*
    RenderExtractionCache::State::EnsureRenderable(
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

    void RenderExtractionCache::State::ApplyMaterialTextureBindings(
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
                bindings.NormalSpace =
                    (slot.SourceKind == ProgressiveSlotSourceKind::GeneratedTextureAsset ||
                     slot.SourceKind == ProgressiveSlotSourceKind::PropertyBake)
                        ? Graphics::MaterialNormalTextureSpace::ObjectSpaceNormal
                        : Graphics::MaterialNormalTextureSpace::TangentSpaceNormal;
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

    void RenderExtractionCache::State::ApplyProgressivePresentationBindings(
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

    RuntimeRenderExtractionStats RenderExtractionCache::State::ExtractAndSubmit(
        ECS::Scene::Registry& scene,
        Graphics::IRenderer& renderer,
        Graphics::GpuAssetCache* gpuAssets,
        const SelectionController* selection,
        const std::uint32_t runtimeSnapshotStorageSlot,
        std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos,
        WorldHandle world)
    {
        RuntimeRenderExtractionStats stats{};
        stats.World = world;
        auto& registry = scene.Raw();
        m_LiveRenderableKeys.clear();

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

            const auto& worldMatrix =
                transformView.get<ECS::Components::Transform::WorldMatrix>(entity).Matrix;

            ExtractLightsForEntity(registry, entity, worldMatrix);

            if (!HasRenderableHint(registry, entity))
            {
                continue;
            }

            ReconcileRenderableEntity(registry,
                                      entity,
                                      worldMatrix,
                                      renderer,
                                      gpuAssets,
                                      stats);
        }

        RetireMissingRenderables(m_LiveRenderableKeys, renderer, stats);

        ExtractSpatialDebug(registry, stats);

        FinalizeAndSubmitSnapshot(renderer,
                                  selection,
                                  runtimeSnapshotStorageSlot,
                                  transformGizmos,
                                  stats);

        m_LastStats = stats;
        return m_LastStats;
    }

    void RenderExtractionCache::State::ExtractLightsForEntity(
        entt::registry& registry,
        const entt::entity entity,
        const glm::mat4& worldMatrix)
    {
        if (const auto* directional = registry.try_get<ECS::Components::Lights::DirectionalLight>(entity))
        {
            m_Lights.push_back(MakeDirectionalLight(*directional, worldMatrix));
        }
        if (const auto* point = registry.try_get<ECS::Components::Lights::PointLight>(entity))
        {
            m_Lights.push_back(MakePointLight(*point, worldMatrix));
        }
        if (const auto* spot = registry.try_get<ECS::Components::Lights::SpotLight>(entity))
        {
            m_Lights.push_back(MakeSpotLight(*spot, worldMatrix));
        }
    }

    void RenderExtractionCache::State::ReconcileRenderableEntity(
        entt::registry& registry,
        const entt::entity entity,
        const glm::mat4& worldMatrix,
        Graphics::IRenderer& renderer,
        Graphics::GpuAssetCache* gpuAssets,
        RuntimeRenderExtractionStats& stats)
    {
        ++stats.CandidateRenderableCount;
        const std::uint32_t stableId = StableEntityId(entity);
        m_LiveRenderableKeys.insert(stableId);

        RenderableSidecar* sidecar = EnsureRenderable(stableId, renderer, stats);
        if (!sidecar)
        {
            return;
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
        if (const auto* visualizationOverrides =
                registry.try_get<Graphics::Components::VisualizationLaneOverrides>(entity))
        {
            sidecar->VisualizationOverrides = *visualizationOverrides;
            sidecar->HasVisualizationOverrides = true;
        }
        else
        {
            sidecar->HasVisualizationOverrides = false;
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

        // Runtime-authored `GeometrySources` residency. The bridges run
        // only when the entity has stated no procedural intent and no
        // asset source at all; both are treated as declared alternatives
        // the residency bridges must not race against. Source availability
        // separates provenance from exact-domain detection so a mesh can
        // still expose its vertex/edge lanes when the full surface source
        // set is incomplete.
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
        bool meshSurfaceLaneReadyThisFrame = false;
        bool graphLaneReadyThisFrame = false;
        std::optional<ECS::Components::GeometrySources::ConstSourceView>
            sourceViewThisFrame{};
        if (sourceEligible)
        {
            namespace GS = ECS::Components::GeometrySources;
            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(registry, entity);
            const auto& view = availability.SourceView;
            const GeometryRenderLaneAvailability surfaceLane =
                ResolveRenderLaneAvailability(availability, GeometryRenderLane::Surface);
            const GeometryRenderLaneAvailability edgeLane =
                ResolveRenderLaneAvailability(availability, GeometryRenderLane::Edges);
            const GeometryRenderLaneAvailability pointLane =
                ResolveRenderLaneAvailability(availability, GeometryRenderLane::Points);
            meshSurfaceLaneReadyThisFrame =
                availability.Sources.ProvenanceDomain == GS::Domain::Mesh &&
                surfaceLane.Ready();
            graphLaneReadyThisFrame =
                availability.Sources.ProvenanceDomain == GS::Domain::Graph &&
                (edgeLane.Ready() || pointLane.Ready());
            sourceViewThisFrame = view;
            ApplyProgressivePresentationBindings(registry,
                                                 entity,
                                                 view,
                                                 *sidecar,
                                                 renderer,
                                                 gpuAssets,
                                                 stats);
            if (availability.Sources.ProvenanceDomain == GS::Domain::Mesh)
            {
                namespace D = ECS::Components::DirtyTags;
                namespace G = Graphics::Components;
                meshDomainThisFrame = true;
                const bool wantsSurface = surfaceLane.Requested;
                const bool wantsEdges = edgeLane.Requested;
                const auto* edgeHint =
                    registry.try_get<G::RenderEdges>(entity);
                const auto* pointHint =
                    registry.try_get<G::RenderPoints>(entity);
                const auto* baseVisualization =
                    sidecar->HasVisualization ? &sidecar->Visualization : nullptr;
                const auto* visualizationOverrides =
                    sidecar->HasVisualizationOverrides
                        ? &sidecar->VisualizationOverrides
                        : nullptr;
                const auto* edgeVisualization =
                    ResolveVisualizationForLane(
                        baseVisualization,
                        visualizationOverrides,
                        VisualizationLane::Edges);
                const auto* pointVisualization =
                    ResolveVisualizationForLane(
                        baseVisualization,
                        visualizationOverrides,
                        VisualizationLane::Points);
                const bool wantsPoints = pointLane.Requested;
                // Snapshot the mesh dirty state before BindMeshGeometry
                // may drain the tags. Primitive edge/point views only pack
                // position/topology-derived streams, so normal/color-only
                // channel dirtiness does not reupload those sidecars.
                meshDirtyThisFrame =
                    BuildMeshGeometryDirtyPlan(registry, entity).MeshPrimitiveViewDirty;
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
                        ExtractBounds(registry, entity, worldMatrix);
                    meshViewsResident = true;
                    const bool edgeSubmitted =
                        ReconcileMeshPrimitiveView(MeshPrimitiveViewKind::Edge,
                                                   view,
                                                   *sidecar,
                                                   worldMatrix,
                                                   sidecar->Material.EffectiveSlot,
                                                   viewBounds,
                                                   stableId,
                                                   wantsEdges,
                                                   edgeHint,
                                                   nullptr,
                                                   edgeVisualization,
                                                   meshDirtyThisFrame,
                                                   renderer,
                                                   stats);
                    const bool pointSubmitted =
                        ReconcileMeshPrimitiveView(MeshPrimitiveViewKind::Vertex,
                                                   view,
                                                   *sidecar,
                                                   worldMatrix,
                                                   sidecar->Material.EffectiveSlot,
                                                   viewBounds,
                                                   stableId,
                                                   wantsPoints,
                                                   nullptr,
                                                   pointHint,
                                                   pointVisualization,
                                                   meshDirtyThisFrame,
                                                   renderer,
                                                   stats);
                    if (!wantsSurface &&
                        meshDirtyThisFrame &&
                        (edgeSubmitted || pointSubmitted))
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
            else if (availability.Sources.ProvenanceDomain == GS::Domain::Graph)
            {
                graphDomainThisFrame = true;
                graphBoundThisFrame = BindGraphGeometry(registry,
                                                        entity,
                                                        view,
                                                        *sidecar,
                                                        renderer,
                                                        stats);
            }
            else if (availability.Sources.ProvenanceDomain == GS::Domain::PointCloud)
            {
                pointCloudDomainThisFrame = true;
                pointCloudResidencyDesiredThisFrame = pointLane.Ready();
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
                else if (surfaceLane.Requested || edgeLane.Requested)
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
            meshSurfaceLaneReadyThisFrame;
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
        const bool stillGraphAttached =
            sourceEligible &&
            graphDomainThisFrame &&
            graphLaneReadyThisFrame;
        if (!stillGraphAttached && sidecar->GraphGeometry.IsValid())
        {
            EnqueueGraphRetire(sidecar->GraphGeometry);
            ReleaseGraphPointLaneInstance(*sidecar, renderer, stats);
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

        const auto* visualization =
            sidecar->HasVisualization ? &sidecar->Visualization : nullptr;
        const auto* visualizationOverrides =
            sidecar->HasVisualizationOverrides
                ? &sidecar->VisualizationOverrides
                : nullptr;
        const auto* surfaceVisualization =
            ResolveVisualizationForLane(
                visualization,
                visualizationOverrides,
                VisualizationLane::Surface);
        const auto* edgeVisualization =
            ResolveVisualizationForLane(
                visualization,
                visualizationOverrides,
                VisualizationLane::Edges);
        const auto* pointVisualization =
            ResolveVisualizationForLane(
                visualization,
                visualizationOverrides,
                VisualizationLane::Points);
        const auto* renderEdges =
            registry.try_get<Graphics::Components::RenderEdges>(entity);
        const auto* renderPoints =
            registry.try_get<Graphics::Components::RenderPoints>(entity);
        const auto scalarKeyFor =
            [stableId](const Graphics::Components::VisualizationConfig* config)
            {
                if (IsScalarVisualizationSource(config) &&
                    !config->ScalarFieldName.empty())
                {
                    return BuildVisualizationPropertySourceKey(
                        stableId,
                        "scalar",
                        config->ScalarFieldName);
                }
                return std::string{};
            };
        const auto colorKeyFor =
            [stableId](const Graphics::Components::VisualizationConfig* config)
            {
                if (IsColorBufferVisualizationSource(config) &&
                    !config->ColorBufferName.empty())
                {
                    return BuildVisualizationPropertySourceKey(
                        stableId,
                        "color",
                        config->ColorBufferName);
                }
                return std::string{};
            };
        const bool splitGraphPointLane =
            graphDomainThisFrame &&
            graphBoundThisFrame &&
            renderEdges != nullptr &&
            renderPoints != nullptr &&
            EnsureGraphPointLaneInstance(
                *sidecar,
                stableId,
                renderer,
                stats);
        if (!splitGraphPointLane)
        {
            ReleaseGraphPointLaneInstance(*sidecar, renderer, stats);
        }

        const Graphics::Components::VisualizationConfig* primaryVisualization =
            visualization;
        const Graphics::Components::RenderEdges* primaryEdges = renderEdges;
        const Graphics::Components::RenderPoints* primaryPoints = renderPoints;
        if (meshDomainThisFrame)
        {
            primaryVisualization = surfaceVisualization;
            primaryEdges = nullptr;
            primaryPoints = nullptr;
        }
        else if (graphDomainThisFrame)
        {
            if (renderEdges != nullptr)
            {
                primaryVisualization = edgeVisualization;
                primaryPoints = nullptr;
            }
            else if (renderPoints != nullptr)
            {
                primaryVisualization = pointVisualization;
                primaryEdges = nullptr;
            }
        }
        else if (pointCloudDomainThisFrame)
        {
            primaryVisualization = pointVisualization;
            primaryEdges = nullptr;
        }
        m_Visualizations.push_back(Graphics::VisualizationSyncRecord{
            .StableId = stableId,
            .Material = &sidecar->Material,
            .GpuSlot = &sidecar->GpuSlot,
            .Visualization = primaryVisualization,
            .Edges = primaryEdges,
            .Points = primaryPoints,
            .ScalarPropertyBufferSourceKey = scalarKeyFor(primaryVisualization),
            .ColorPropertyBufferSourceKey = colorKeyFor(primaryVisualization),
        });
        if (splitGraphPointLane)
        {
            m_Visualizations.push_back(Graphics::VisualizationSyncRecord{
                .StableId = stableId,
                .GpuSlot = &sidecar->GpuSlot,
                .Visualization = pointVisualization,
                .Points = renderPoints,
                .TargetInstance = sidecar->GraphPointLaneInstance,
                .ScalarPropertyBufferSourceKey = scalarKeyFor(pointVisualization),
                .ColorPropertyBufferSourceKey = colorKeyFor(pointVisualization),
            });
        }
        if (renderEdges != nullptr && sidecar->MeshEdgeViewInstance.IsValid())
        {
            m_Visualizations.push_back(Graphics::VisualizationSyncRecord{
                .StableId = stableId,
                .GpuSlot = &sidecar->GpuSlot,
                .Visualization = edgeVisualization,
                .Edges = renderEdges,
                .TargetInstance = sidecar->MeshEdgeViewInstance,
                .ScalarPropertyBufferSourceKey = scalarKeyFor(edgeVisualization),
                .ColorPropertyBufferSourceKey = colorKeyFor(edgeVisualization),
            });
        }
        if (renderPoints != nullptr && sidecar->MeshVertexViewInstance.IsValid())
        {
            m_Visualizations.push_back(Graphics::VisualizationSyncRecord{
                .StableId = stableId,
                .GpuSlot = &sidecar->GpuSlot,
                .Visualization = pointVisualization,
                .Points = renderPoints,
                .TargetInstance = sidecar->MeshVertexViewInstance,
                .ScalarPropertyBufferSourceKey = scalarKeyFor(pointVisualization),
                .ColorPropertyBufferSourceKey = colorKeyFor(pointVisualization),
            });
        }
        if (sourceViewThisFrame.has_value() &&
            !m_VisualizationState->Bindings.contains(stableId))
        {
            const std::array<const Graphics::Components::VisualizationConfig*, 3u>
                configs{surfaceVisualization, edgeVisualization, pointVisualization};
            for (std::size_t i = 0u; i < configs.size(); ++i)
            {
                bool alreadyAppended = false;
                for (std::size_t j = 0u; j < i; ++j)
                {
                    alreadyAppended = alreadyAppended || configs[j] == configs[i];
                }
                if (alreadyAppended)
                    continue;
                AppendScalarVisualizationPropertyBuffer(
                    stableId,
                    *sourceViewThisFrame,
                    configs[i],
                    m_VisualizationState->Batch,
                    stats);
                AppendColorVisualizationPropertyBuffer(
                    stableId,
                    *sourceViewThisFrame,
                    configs[i],
                    m_VisualizationState->Batch,
                    stats);
            }
        }
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
            if (graphDomainThisFrame)
            {
                renderFlags &= ~(RHI::GpuRender_Surface |
                                 RHI::GpuRender_Line |
                                 RHI::GpuRender_Point);
                renderFlags &= ~RHI::GpuRender_Unlit;
                if (renderEdges != nullptr)
                    renderFlags |= RHI::GpuRender_Line | RHI::GpuRender_Unlit;
                else if (renderPoints != nullptr)
                    renderFlags |= RHI::GpuRender_Point | RHI::GpuRender_Unlit;
            }
            if (graphDomainThisFrame && graphBoundThisFrame)
            {
                renderer.GetGpuWorld().SetEntityConfig(
                    sidecar->Instance,
                    BuildImmediateLaneConfig(
                        primaryVisualization,
                        primaryEdges,
                        primaryPoints));
            }
            if (pointCloudDomainThisFrame && pointCloudBoundThisFrame)
            {
                renderer.GetGpuWorld().SetEntityConfig(
                    sidecar->Instance,
                    BuildImmediateLaneConfig(
                        pointVisualization,
                        nullptr,
                        renderPoints));
            }
            m_Transforms.push_back(Graphics::TransformSyncRecord{
                .StableId = stableId,
                .Instance = sidecar->Instance,
                .Model = worldMatrix,
                .RenderFlags = renderFlags,
                .Bounds = ExtractBounds(registry, entity, worldMatrix),
                .MaterialSlot = sidecar->Material.EffectiveSlot,
                .HasMaterialSlot = true,
            });
            if (splitGraphPointLane)
            {
                renderer.GetGpuWorld().SetEntityConfig(
                    sidecar->GraphPointLaneInstance,
                    BuildImmediateLaneConfig(
                        pointVisualization,
                        nullptr,
                        renderPoints));
                m_Transforms.push_back(Graphics::TransformSyncRecord{
                    .StableId = stableId,
                    .Instance = sidecar->GraphPointLaneInstance,
                    .Model = worldMatrix,
                    .RenderFlags = RHI::GpuRender_Visible |
                                   RHI::GpuRender_Opaque |
                                   RHI::GpuRender_Point |
                                   RHI::GpuRender_Unlit,
                    .Bounds = ExtractBounds(registry, entity, worldMatrix),
                    .MaterialSlot = sidecar->Material.EffectiveSlot,
                    .HasMaterialSlot = true,
                });
            }
        }
    }

    void RenderExtractionCache::State::FinalizeAndSubmitSnapshot(
        Graphics::IRenderer& renderer,
        const SelectionController* selection,
        const std::uint32_t runtimeSnapshotStorageSlot,
        std::span<const Graphics::TransformGizmoRenderPacket> transformGizmos,
        RuntimeRenderExtractionStats& stats)
    {
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
    }

    void RenderExtractionCache::State::ClearSceneState(
        Graphics::IRenderer& renderer)
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
            ReleaseGraphPointLaneInstance(sidecar, renderer, stats);
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
        m_LiveRenderableKeys.clear();

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
        if (!m_VisualizationState->Bindings.empty())
        {
            ++m_VisualizationState->BindingRevision;
        }
        m_VisualizationState->Bindings.clear();
        m_VisualizationState->Batch.Clear();

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{});
        m_LastStats = stats;
    }

    void RenderExtractionCache::State::Shutdown(Graphics::IRenderer& renderer)
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
