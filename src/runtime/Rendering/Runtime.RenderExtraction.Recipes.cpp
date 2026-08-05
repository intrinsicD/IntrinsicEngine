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
    void RenderExtractionCache::State::AppendVisualizationRecipe(
        const GeometryEntityAvailability& availability,
        const VisualizationRecipe& recipe,
        RuntimeRenderExtractionStats& stats)
    {
        VisualizationEncodingResult encoded =
            EncodeVisualizationRecipe(availability, recipe);
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
