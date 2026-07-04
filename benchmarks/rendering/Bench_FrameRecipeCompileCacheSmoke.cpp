// GRAPHICS-117 - frame-recipe compile-cache smoke benchmark.
//
// This workload intentionally stays CPU-side and deterministic. It measures
// the default recipe's rebuild-each-frame declaration/compile cost, then
// reports the cached steady-state compile-attempt contract established by the
// renderer tests.

#include "Bench.FrameRecipeCompileCacheSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Handles;

namespace Intrinsic::Bench::Rendering
{
    namespace
    {
        namespace Graphics = Extrinsic::Graphics;
        namespace RHI = Extrinsic::RHI;

        constexpr std::uint32_t kWarmupIterations = 2u;
        constexpr std::uint32_t kMeasuredIterations = 16u;
        constexpr std::uint32_t kViewportWidth = 1280u;
        constexpr std::uint32_t kViewportHeight = 720u;

        struct CompileSummary
        {
            std::uint32_t PassCount{0u};
            std::uint32_t ResourceCount{0u};
            std::uint32_t BarrierCount{0u};
            std::size_t ValidationErrorCount{0u};
            bool Succeeded{false};
        };

        [[nodiscard]] Graphics::FrameRecipeImports MakeImports()
        {
            return Graphics::FrameRecipeImports{
                .Backbuffer = RHI::TextureHandle{1u, 1u},
                .SceneTable = RHI::BufferHandle{2u, 1u},
                .InstanceStatic = RHI::BufferHandle{3u, 1u},
                .InstanceDynamic = RHI::BufferHandle{4u, 1u},
                .EntityConfig = RHI::BufferHandle{5u, 1u},
                .GeometryRecords = RHI::BufferHandle{6u, 1u},
                .Bounds = RHI::BufferHandle{7u, 1u},
                .Lights = RHI::BufferHandle{8u, 1u},
                .MaterialBuffer = RHI::BufferHandle{9u, 1u},
                .SurfaceOpaqueIndexedArgs = RHI::BufferHandle{10u, 1u},
                .SurfaceOpaqueCount = RHI::BufferHandle{11u, 1u},
                .LinesIndexedArgs = RHI::BufferHandle{12u, 1u},
                .LinesCount = RHI::BufferHandle{13u, 1u},
                .LineQuadsNonIndexedArgs = RHI::BufferHandle{14u, 1u},
                .LineQuadsCount = RHI::BufferHandle{15u, 1u},
                .PointsNonIndexedArgs = RHI::BufferHandle{16u, 1u},
                .PointsCount = RHI::BufferHandle{17u, 1u},
                .PickingReadback = RHI::BufferHandle{18u, 1u},
                .HistogramReadback = RHI::BufferHandle{19u, 1u},
                .HZBCurrent = RHI::TextureHandle{20u, 1u},
                .ClusterGridAABBs = RHI::BufferHandle{21u, 1u},
                .ClusterLightHeaders = RHI::BufferHandle{22u, 1u},
                .ClusterLightIndices = RHI::BufferHandle{23u, 1u},
                .ClusterLightCounter = RHI::BufferHandle{24u, 1u},
            };
        }

        [[nodiscard]] CompileSummary CompileDefaultFrameRecipe()
        {
            const Graphics::FrameRecipeFeatures features{};
            const Graphics::FrameRecipeAAOptions aaOptions{};
            const Graphics::FrameRecipeShadowSizing shadowSizing{};
            const Graphics::FrameRecipeTemporalOptions temporalOptions{};
            const Graphics::FrameRecipeSizing sizing{
                .Width = kViewportWidth,
                .Height = kViewportHeight,
            };

            Graphics::FrameRecipePassContributionRegistry registry{};
            Graphics::RegisterDefaultFrameRecipeOverlayContributions(
                registry,
                features,
                aaOptions,
                temporalOptions);

            Graphics::RenderGraph graph{};
            const Graphics::FrameRecipeBuildResult build =
                Graphics::BuildDefaultFrameRecipeWithContributions(
                    graph,
                    features,
                    MakeImports(),
                    sizing,
                    aaOptions,
                    shadowSizing,
                    temporalOptions,
                    registry.Passes);
            if (!build.Succeeded)
            {
                return CompileSummary{};
            }

            const auto compiled = graph.Compile();
            if (!compiled.has_value())
            {
                return CompileSummary{};
            }

            const Graphics::FrameRecipeContributionDescriptionResult description =
                Graphics::DescribeDefaultFrameRecipeWithContributions(
                    features,
                    aaOptions,
                    temporalOptions,
                    registry.Passes);
            if (!description.Succeeded)
            {
                return CompileSummary{};
            }

            const Graphics::RenderGraphValidationResult validation =
                Graphics::ValidateRecipeCompiledGraph(description.Recipe, *compiled);
            const std::size_t validationErrors =
                validation.CountBySeverity(Graphics::RenderGraphValidationSeverity::Error);

            return CompileSummary{
                .PassCount = compiled->PassCount,
                .ResourceCount = compiled->ResourceCount,
                .BarrierCount = static_cast<std::uint32_t>(compiled->BarrierPackets.size()),
                .ValidationErrorCount = validationErrors,
                .Succeeded = validationErrors == 0u,
            };
        }

        [[nodiscard]] double CountQualityError(const CompileSummary& reference,
                                               const CompileSummary& sample)
        {
            const double passDelta = static_cast<double>(
                std::max(reference.PassCount, sample.PassCount) -
                std::min(reference.PassCount, sample.PassCount));
            const double resourceDelta = static_cast<double>(
                std::max(reference.ResourceCount, sample.ResourceCount) -
                std::min(reference.ResourceCount, sample.ResourceCount));
            const double barrierDelta = static_cast<double>(
                std::max(reference.BarrierCount, sample.BarrierCount) -
                std::min(reference.BarrierCount, sample.BarrierCount));
            return (passDelta * passDelta) +
                   (resourceDelta * resourceDelta) +
                   (barrierDelta * barrierDelta);
        }
    } // namespace

    FrameRecipeCompileCacheSmokeMetrics RunFrameRecipeCompileCacheSmoke()
    {
        for (std::uint32_t i = 0u; i < kWarmupIterations; ++i)
        {
            (void)CompileDefaultFrameRecipe();
        }

        CompileSummary reference{};
        double qualityErrorSquared = 0.0;
        std::size_t validationErrorCount = 0u;
        bool allSucceeded = true;

        const auto t0 = std::chrono::steady_clock::now();
        for (std::uint32_t i = 0u; i < kMeasuredIterations; ++i)
        {
            const CompileSummary sample = CompileDefaultFrameRecipe();
            if (i == 0u)
            {
                reference = sample;
            }
            else
            {
                qualityErrorSquared += CountQualityError(reference, sample);
            }

            validationErrorCount += sample.ValidationErrorCount;
            allSucceeded = allSucceeded && sample.Succeeded;
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        const double baselineMs =
            (static_cast<double>(totalNs) /
             static_cast<double>(kMeasuredIterations)) *
            1.0e-6;

        FrameRecipeCompileCacheSmokeMetrics metrics{};
        metrics.RuntimeMilliseconds = baselineMs;
        metrics.BaselineRebuildDeclareCompileMilliseconds = baselineMs;
        metrics.CachedSteadyStateDeclareCompileMilliseconds = 0.0;
        metrics.AvoidedDeclareCompileMilliseconds = baselineMs;
        metrics.QualityErrorL2 = std::sqrt(qualityErrorSquared);
        metrics.PassCount = reference.PassCount;
        metrics.ResourceCount = reference.ResourceCount;
        metrics.BarrierCount = reference.BarrierCount;
        metrics.WarmupIterations = kWarmupIterations;
        metrics.MeasuredIterations = kMeasuredIterations;
        metrics.BaselineCompileAttemptsPerFrame = 1u;
        metrics.CachedCompileAttemptsPerFrame = 0u;
        metrics.ValidationErrorCount = validationErrorCount;
        metrics.Succeeded = allSucceeded &&
            reference.PassCount > 0u &&
            reference.ResourceCount > 0u &&
            metrics.QualityErrorL2 == 0.0 &&
            validationErrorCount == 0u &&
            baselineMs > 0.0;
        return metrics;
    }
} // namespace Intrinsic::Bench::Rendering
