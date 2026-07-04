// GRAPHICS-117 - frame-recipe compile-cache smoke benchmark declaration.
//
// Records PR-fast baseline/probe evidence for the default frame recipe's
// declare+compile stage. The benchmark compares the rebuild-each-frame path
// against the renderer cache contract's steady-state compile-attempt count; it
// does not claim a renderer-wide frame-time improvement.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Intrinsic::Bench::Rendering
{
    inline constexpr const char* kFrameRecipeCompileCacheSmokeBenchmarkId =
        "rendering.frame_recipe_compile_cache.smoke";
    inline constexpr const char* kFrameRecipeCompileCacheSmokeMethod =
        "rendering.frame_recipe_compile_cache";
    inline constexpr const char* kFrameRecipeCompileCacheSmokeDataset =
        "builtin.default_frame_recipe.1280x720";

    struct FrameRecipeCompileCacheSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double BaselineRebuildDeclareCompileMilliseconds{0.0};
        double CachedSteadyStateDeclareCompileMilliseconds{0.0};
        double AvoidedDeclareCompileMilliseconds{0.0};
        double QualityErrorL2{0.0};
        std::uint32_t PassCount{0u};
        std::uint32_t ResourceCount{0u};
        std::uint32_t BarrierCount{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
        std::uint32_t BaselineCompileAttemptsPerFrame{0u};
        std::uint32_t CachedCompileAttemptsPerFrame{0u};
        std::size_t ValidationErrorCount{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] FrameRecipeCompileCacheSmokeMetrics RunFrameRecipeCompileCacheSmoke();
} // namespace Intrinsic::Bench::Rendering
