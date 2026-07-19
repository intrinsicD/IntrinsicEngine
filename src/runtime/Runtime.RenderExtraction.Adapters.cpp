module;

#include <cstdint>
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

import :Internal;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.ECS.Component.SpatialDebugBinding;
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
import Extrinsic.Runtime.SpatialDebugAdapters;
import Extrinsic.Runtime.VisualizationAdapters;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    void RenderExtractionCache::State::AppendVisualizationAdapters(
        const std::uint32_t stableId,
        const RenderableSidecar& sidecar,
        RuntimeRenderExtractionStats& stats)
    {
        const auto* visualization =
            sidecar.HasVisualization
                ? &sidecar.Visualization
                : nullptr;
        const bool scalarConfigRequested =
            IsRenderExtractionScalarVisualizationSource(visualization);
        const bool colorConfigRequested =
            IsRenderExtractionColorBufferVisualizationSource(visualization);
        const auto bindingIt =
            m_VisualizationState->Bindings.find(stableId);
        const bool bindingRequested =
            bindingIt != m_VisualizationState->Bindings.end();

        if (!scalarConfigRequested
            && !colorConfigRequested
            && !bindingRequested)
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
            m_VisualizationState->Registry.Find(
                bindingIt->second.AdapterKey);
        if (adapter == nullptr)
        {
            ++stats.VisualizationAdapterMissingAdapterCount;
            return;
        }

        VisualizationAdapterStats perAdapter{};
        const VisualizationAdapterOptions options =
            BuildRenderExtractionVisualizationAdapterOptions(
                stableId,
                bindingIt->second,
                visualization);
        adapter->Append(
            m_VisualizationState->Batch,
            options,
            perAdapter);

        ++stats.VisualizationAdapterInvokedCount;
        stats.VisualizationAdapterPacketAppendCount +=
            perAdapter.PacketAppendCount;
        stats.VisualizationAdapterMissingSourceCount +=
            perAdapter.MissingSourceCount
            + perAdapter.MissingTexcoordCount;
        stats.VisualizationAdapterUnsupportedSourceTypeCount +=
            perAdapter.UnsupportedSourceTypeCount;
        stats.VisualizationAdapterEmptySourceCount +=
            perAdapter.EmptySourceCount;
        stats.VisualizationAdapterInvalidBufferCount +=
            perAdapter.InvalidBufferCount
            + perAdapter.InvalidResourceCount;
        stats.VisualizationAdapterInvalidRangeCount +=
            perAdapter.InvalidRangeCount;
        stats.VisualizationAdapterNonFiniteValueCount +=
            perAdapter.NonFiniteValueCount;
        stats.VisualizationAdapterElementCountOverflowCount +=
            perAdapter.ElementCountOverflowCount;
        stats.VisualizationAdapterManualRangeCount +=
            perAdapter.ManualRangeCount;
        stats.VisualizationAdapterFlatAutoRangeExpandedCount +=
            perAdapter.FlatAutoRangeExpandedCount;
        stats.VisualizationAdapterRobustAutoRangeClampedCount +=
            perAdapter.RobustAutoRangeClampedCount;
        stats.VisualizationAdapterScalarValueScanCount +=
            perAdapter.ScalarValueScanCount;
    }

    void RenderExtractionCache::State::RegisterSpatialDebugAdapter(
        const std::uint64_t key,
        std::unique_ptr<ISpatialDebugAdapter> adapter)
    {
        m_SpatialDebugRegistry.Unregister(key);
        auto [it, inserted] =
            m_SpatialDebugAdapters.insert_or_assign(
                key,
                std::move(adapter));
        (void)inserted;
        if (it->second)
        {
            m_SpatialDebugRegistry.Register(key, *it->second);
        }
    }

    bool RenderExtractionCache::State::UnregisterSpatialDebugAdapter(
        const std::uint64_t key) noexcept
    {
        m_SpatialDebugRegistry.Unregister(key);
        return m_SpatialDebugAdapters.erase(key) != 0u;
    }

    std::size_t
    RenderExtractionCache::State::GetSpatialDebugAdapterCount()
        const noexcept
    {
        return m_SpatialDebugAdapters.size();
    }

    const SpatialDebugAdapterRegistry&
    RenderExtractionCache::State::GetSpatialDebugRegistryForTest()
        const noexcept
    {
        return m_SpatialDebugRegistry;
    }

    void RenderExtractionCache::State::RegisterVisualizationAdapter(
        const std::uint64_t key,
        std::unique_ptr<IVisualizationAdapter> adapter)
    {
        if (adapter == nullptr)
        {
            (void)UnregisterVisualizationAdapter(key);
            return;
        }
        m_VisualizationState->Registry.Register(key, *adapter);
        m_VisualizationState->Adapters.insert_or_assign(
            key,
            std::move(adapter));
    }

    bool RenderExtractionCache::State::UnregisterVisualizationAdapter(
        const std::uint64_t key) noexcept
    {
        m_VisualizationState->Registry.Unregister(key);
        return m_VisualizationState->Adapters.erase(key) != 0u;
    }

    std::size_t
    RenderExtractionCache::State::GetVisualizationAdapterCount()
        const noexcept
    {
        return m_VisualizationState->Adapters.size();
    }

    const VisualizationAdapterRegistry&
    RenderExtractionCache::State::
        GetVisualizationAdapterRegistryForTest() const noexcept
    {
        return m_VisualizationState->Registry;
    }

    void RenderExtractionCache::State::SetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId,
        VisualizationAdapterBinding binding)
    {
        m_VisualizationState->Bindings.insert_or_assign(
            stableEntityId,
            std::move(binding));
        ++m_VisualizationState->BindingRevision;
    }

    void RenderExtractionCache::State::ClearVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) noexcept
    {
        if (m_VisualizationState->Bindings.erase(stableEntityId) != 0u)
        {
            ++m_VisualizationState->BindingRevision;
        }
    }

    std::optional<
        RenderExtractionCache::State::VisualizationAdapterBinding>
    RenderExtractionCache::State::GetVisualizationAdapterBinding(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it =
            m_VisualizationState->Bindings.find(stableEntityId);
        if (it == m_VisualizationState->Bindings.end())
        {
            return std::nullopt;
        }
        return it->second;
    }

    std::uint64_t
    RenderExtractionCache::State::
        GetVisualizationAdapterBindingRevision() const noexcept
    {
        return m_VisualizationState->BindingRevision;
    }

    void RenderExtractionCache::State::ExtractSpatialDebug(
        entt::registry& registry,
        RuntimeRenderExtractionStats& stats)
    {
        m_SpatialDebugBatch.Clear();
        auto spatialDebugView =
            registry.view<ECS::Components::SpatialDebugBinding>();
        for (const entt::entity entity : spatialDebugView)
        {
            if (!registry.valid(entity))
            {
                ++stats.SkippedInvalidEntityCount;
                continue;
            }

            const auto& binding =
                spatialDebugView
                    .get<ECS::Components::SpatialDebugBinding>(entity);
            ++stats.SpatialDebugBindingsObserved;

            const auto* adapter =
                m_SpatialDebugRegistry.Find(binding.RegistryKey);
            if (adapter == nullptr)
            {
                ++stats.SpatialDebugMissingAdapterCount;
                continue;
            }

            const SpatialDebugAdapterOptions options{
                .LeafOnly = binding.LeafOnly,
                .OccupancyOnly = binding.OccupancyOnly,
                .MaxDepth = binding.MaxDepth,
            };
            SpatialDebugAdapterStats perAdapter{};
            adapter->Append(
                m_SpatialDebugBatch,
                options,
                perAdapter);
            ++stats.SpatialDebugAdaptersInvoked;

            stats.SpatialDebugLeafNodeAccumulator +=
                perAdapter.LeafNodeCount;
            stats.SpatialDebugInnerNodeAccumulator +=
                perAdapter.InnerNodeCount;
            stats.SpatialDebugEmptyNodeSkippedAccumulator +=
                perAdapter.EmptyNodeSkippedCount;
            stats.SpatialDebugDepthCapTruncationAccumulator +=
                perAdapter.DepthCapTruncationCount;
        }
    }
}
