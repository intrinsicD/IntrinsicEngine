module;

#include <cstdint>
#include <cstring>
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

import :Internal;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Components.GeometrySources;
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
import Extrinsic.Runtime.GeometryPlanBuilders;
import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool RemapSurfaceVertexPropertyBuffers(
            VisualizationEncodingBatch& batch,
            const std::span<const std::uint32_t> sourceVertexForGpuVertex,
            const std::uint64_t remapRevision)
        {
            if (sourceVertexForGpuVertex.empty())
                return true;
            if (remapRevision == 0u ||
                sourceVertexForGpuVertex.size() >
                    std::numeric_limits<std::uint32_t>::max())
            {
                return false;
            }

            for (std::size_t descriptorIndex = 0u;
                 descriptorIndex < batch.PropertyBuffers.size();
                 ++descriptorIndex)
            {
                auto& descriptor = batch.PropertyBuffers[descriptorIndex];
                if (descriptor.Domain !=
                    Graphics::VisualizationAttributeDomain::Vertex)
                {
                    continue;
                }

                const std::string& sourceKey = descriptor.SourceKey;
                bool usedBySurfacePacket = false;
                for (const Graphics::ScalarAttributePacket& scalar :
                     batch.Scalars)
                {
                    usedBySurfacePacket = usedBySurfacePacket ||
                        (scalar.Domain ==
                             Graphics::VisualizationAttributeDomain::Vertex &&
                         scalar.SourceBufferKey == sourceKey);
                }
                for (const Graphics::ColorAttributePacket& color :
                     batch.Colors)
                {
                    usedBySurfacePacket = usedBySurfacePacket ||
                        (color.Domain ==
                             Graphics::VisualizationAttributeDomain::Vertex &&
                         color.SourceBufferKey == sourceKey);
                }
                for (const Graphics::IsolineOverlayPacket& isoline :
                     batch.Isolines)
                {
                    usedBySurfacePacket = usedBySurfacePacket ||
                        (isoline.Domain ==
                             Graphics::VisualizationAttributeDomain::Vertex &&
                         isoline.ScalarBufferSourceKey == sourceKey);
                }
                if (!usedBySurfacePacket)
                    continue;

                if (descriptorIndex >= batch.PropertyBufferPayloads.size() ||
                    descriptor.ElementCount == 0u ||
                    descriptor.StrideBytes == 0u ||
                    descriptor.Bytes.size() !=
                        static_cast<std::size_t>(descriptor.ElementCount) *
                            descriptor.StrideBytes)
                {
                    return false;
                }

                std::vector<std::byte> remapped(
                    sourceVertexForGpuVertex.size() *
                    descriptor.StrideBytes);
                for (std::size_t gpuVertex = 0u;
                     gpuVertex < sourceVertexForGpuVertex.size();
                     ++gpuVertex)
                {
                    const std::uint32_t sourceVertex =
                        sourceVertexForGpuVertex[gpuVertex];
                    if (sourceVertex >= descriptor.ElementCount)
                        return false;
                    std::memcpy(
                        remapped.data() + gpuVertex * descriptor.StrideBytes,
                        descriptor.Bytes.data() +
                            static_cast<std::size_t>(sourceVertex) *
                                descriptor.StrideBytes,
                        descriptor.StrideBytes);
                }

                batch.PropertyBufferPayloads[descriptorIndex] =
                    std::move(remapped);
                const auto& payload =
                    batch.PropertyBufferPayloads[descriptorIndex];
                descriptor.ElementCount = static_cast<std::uint32_t>(
                    sourceVertexForGpuVertex.size());
                descriptor.SourceLayoutStamp = remapRevision;
                descriptor.Bytes = std::span<const std::byte>{
                    payload.data(), payload.size()};

                for (Graphics::ScalarAttributePacket& scalar : batch.Scalars)
                {
                    if (scalar.Domain ==
                            Graphics::VisualizationAttributeDomain::Vertex &&
                        scalar.SourceBufferKey == sourceKey)
                    {
                        scalar.ElementCount = descriptor.ElementCount;
                    }
                }
                for (Graphics::ColorAttributePacket& color : batch.Colors)
                {
                    if (color.Domain ==
                            Graphics::VisualizationAttributeDomain::Vertex &&
                        color.SourceBufferKey == sourceKey)
                    {
                        color.ElementCount = descriptor.ElementCount;
                    }
                }
            }
            return true;
        }
    }

    void RenderExtractionCache::State::AppendVisualizationRecipe(
        const GeometryEntityAvailability& availability,
        const VisualizationRecipe& recipe,
        RuntimeRenderExtractionStats& stats,
        const std::span<const std::uint32_t> surfaceVertexRemap,
        const std::uint64_t surfaceVertexRemapRevision)
    {
        VisualizationEncodingResult encoded =
            EncodeVisualizationRecipe(availability, recipe);
        if (!RemapSurfaceVertexPropertyBuffers(
                encoded.Batch,
                surfaceVertexRemap,
                surfaceVertexRemapRevision))
        {
            encoded.Batch.Clear();
            encoded.Status = VisualizationRecipeStatus::InvalidBuffer;
            ++encoded.Diagnostics.InvalidBufferCount;
        }
        const VisualizationEncodingDiagnostics& diagnostics = encoded.Diagnostics;

        ++stats.VisualizationRecipeEncodeCount;
        stats.VisualizationRecipePacketAppendCount +=
            diagnostics.PacketAppendCount;
        stats.VisualizationRecipeMissingSourceCount +=
            diagnostics.MissingSourceCount;
        stats.VisualizationRecipeUnsupportedSourceTypeCount +=
            diagnostics.UnsupportedSourceTypeCount;
        stats.VisualizationRecipeEmptySourceCount +=
            diagnostics.EmptySourceCount;
        stats.VisualizationRecipeInvalidBufferCount +=
            diagnostics.InvalidBufferCount;
        stats.VisualizationRecipeInvalidResourceCount +=
            diagnostics.InvalidResourceCount;
        stats.VisualizationRecipeMissingTexcoordCount +=
            diagnostics.MissingTexcoordCount;
        stats.VisualizationRecipeInvalidRangeCount +=
            diagnostics.InvalidRangeCount;
        stats.VisualizationRecipeNonFiniteValueCount +=
            diagnostics.NonFiniteValueCount;
        stats.VisualizationRecipeElementCountOverflowCount +=
            diagnostics.ElementCountOverflowCount;
        stats.VisualizationRecipeManualRangeCount +=
            diagnostics.ManualRangeCount;
        stats.VisualizationRecipeFlatAutoRangeExpandedCount +=
            diagnostics.FlatAutoRangeExpandedCount;
        stats.VisualizationRecipeRobustAutoRangeClampedCount +=
            diagnostics.RobustAutoRangeClampedCount;
        stats.VisualizationRecipeScalarValueScanCount +=
            diagnostics.ScalarValueScanCount;

        switch (encoded.Status)
        {
        case VisualizationRecipeStatus::EmptyRecipe:
            ++stats.VisualizationRecipeEmptyCount;
            break;
        case VisualizationRecipeStatus::UnsupportedDomain:
            ++stats.VisualizationRecipeUnsupportedDomainCount;
            break;
        case VisualizationRecipeStatus::ElementCountMismatch:
            ++stats.VisualizationRecipeElementCountMismatchCount;
            break;
        case VisualizationRecipeStatus::MissingSource:
            if (diagnostics.MissingSourceCount == 0u)
                ++stats.VisualizationRecipeMissingSourceCount;
            break;
        case VisualizationRecipeStatus::UnsupportedSourceType:
            if (diagnostics.UnsupportedSourceTypeCount == 0u)
                ++stats.VisualizationRecipeUnsupportedSourceTypeCount;
            break;
        case VisualizationRecipeStatus::EmptySource:
            if (diagnostics.EmptySourceCount == 0u)
                ++stats.VisualizationRecipeEmptySourceCount;
            break;
        case VisualizationRecipeStatus::InvalidBuffer:
            if (diagnostics.InvalidBufferCount == 0u)
                ++stats.VisualizationRecipeInvalidBufferCount;
            break;
        case VisualizationRecipeStatus::InvalidResource:
            if (diagnostics.InvalidResourceCount == 0u)
                ++stats.VisualizationRecipeInvalidResourceCount;
            break;
        case VisualizationRecipeStatus::MissingTexcoord:
            if (diagnostics.MissingTexcoordCount == 0u)
                ++stats.VisualizationRecipeMissingTexcoordCount;
            break;
        case VisualizationRecipeStatus::InvalidRange:
            if (diagnostics.InvalidRangeCount == 0u)
                ++stats.VisualizationRecipeInvalidRangeCount;
            break;
        case VisualizationRecipeStatus::NonFiniteValue:
            if (diagnostics.NonFiniteValueCount == 0u)
                ++stats.VisualizationRecipeNonFiniteValueCount;
            break;
        case VisualizationRecipeStatus::ElementCountOverflow:
            if (diagnostics.ElementCountOverflowCount == 0u)
                ++stats.VisualizationRecipeElementCountOverflowCount;
            break;
        case VisualizationRecipeStatus::Encoded:
            break;
        }

        m_VisualizationState->Batch.Append(std::move(encoded.Batch));
    }

    void RenderExtractionCache::State::SetVisualizationRecipe(
        const std::uint32_t stableEntityId,
        VisualizationRecipe recipe)
    {
        m_VisualizationState->Recipes.insert_or_assign(
            stableEntityId,
            std::move(recipe));
        ++m_VisualizationState->RecipeRevision;
    }

    void RenderExtractionCache::State::ClearVisualizationRecipe(
        const std::uint32_t stableEntityId) noexcept
    {
        if (m_VisualizationState->Recipes.erase(stableEntityId) != 0u)
            ++m_VisualizationState->RecipeRevision;
    }

    std::optional<VisualizationRecipe>
    RenderExtractionCache::State::GetVisualizationRecipe(
        const std::uint32_t stableEntityId) const noexcept
    {
        const auto it =
            m_VisualizationState->Recipes.find(stableEntityId);
        if (it == m_VisualizationState->Recipes.end())
            return std::nullopt;
        return it->second;
    }

    std::uint64_t
    RenderExtractionCache::State::
        GetVisualizationRecipeRevision() const noexcept
    {
        return m_VisualizationState->RecipeRevision;
    }

}
