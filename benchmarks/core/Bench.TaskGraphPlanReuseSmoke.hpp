// CORE-008 - compiled TaskGraph plan-reuse smoke benchmark declarations.
//
// The two workloads time the public registration/compile/replay lifecycle for
// stable ECS-like and render-prep-like chain shapes. Correctness validation is
// performed outside the timed window so the same frozen harness can measure
// the full-rebuild baseline and the exact-structure reuse candidate.
#pragma once

#include <array>
#include <cstdint>

namespace Intrinsic::Bench::Core
{
    inline constexpr const char* kTaskGraphPlanReuseEcs3SmokeBenchmarkId =
        "core.taskgraph_plan_reuse.ecs3.smoke";
    inline constexpr const char* kTaskGraphPlanReuseEcs3SmokeDataset =
        "builtin.synthetic_taskgraph.ecs_bundle_3.v1";
    inline constexpr const char* kTaskGraphPlanReuseRenderPrep9SmokeBenchmarkId =
        "core.taskgraph_plan_reuse.renderprep9.smoke";
    inline constexpr const char* kTaskGraphPlanReuseRenderPrep9SmokeDataset =
        "builtin.synthetic_taskgraph.render_prep_9.v1";
    inline constexpr const char* kTaskGraphPlanReuseSmokeMethod =
        "core.taskgraph_plan_reuse";

    inline constexpr std::uint32_t kTaskGraphPlanReuseWarmupBatches = 3u;
    inline constexpr std::uint32_t kTaskGraphPlanReuseMeasuredBatches = 9u;
    inline constexpr std::uint32_t kTaskGraphPlanReuseEpochsPerBatch = 512u;

    struct TaskGraphPlanReuseSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double ThroughputItemsPerSecond{0.0};
        double QualityErrorL2{0.0};
        std::array<double, kTaskGraphPlanReuseMeasuredBatches>
            RuntimeSamplesMilliseconds{};
        std::uint32_t WarmupBatches{0u};
        std::uint32_t MeasuredBatches{0u};
        std::uint32_t EpochsPerBatch{0u};
        std::uint32_t PassCount{0u};
        std::uint32_t EdgeCount{0u};
        std::uint32_t LayerCount{0u};
        std::uint64_t CompileCallCount{0u};
        std::uint64_t PlanBuildCount{0u};
        std::uint64_t PlanReuseCount{0u};
        std::uint64_t PlanChecksum{0u};
        std::uint64_t CallbackChecksum{0u};
        std::uint64_t FailureCount{0u};
        bool LastCompileReusedPlan{false};
        bool Succeeded{false};
    };

    [[nodiscard]] TaskGraphPlanReuseSmokeMetrics
    RunTaskGraphPlanReuseEcs3Smoke();

    [[nodiscard]] TaskGraphPlanReuseSmokeMetrics
    RunTaskGraphPlanReuseRenderPrep9Smoke();
} // namespace Intrinsic::Bench::Core
