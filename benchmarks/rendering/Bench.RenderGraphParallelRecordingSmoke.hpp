// GRAPHICS-119 - render-graph parallel recording smoke benchmark declaration.
//
// Compares serial executor pass recording against scheduler-backed parallel
// record/join on a deterministic pass-heavy CPU/null graph. This records
// benchmark evidence only; Vulkan smoke remains the operational gate.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Rendering
{
    inline constexpr const char* kRenderGraphParallelRecordingSmokeBenchmarkId =
        "rendering.rendergraph_parallel_recording.smoke";
    inline constexpr const char* kRenderGraphParallelRecordingSmokeMethod =
        "rendering.rendergraph_parallel_recording";
    inline constexpr const char* kRenderGraphParallelRecordingSmokeDataset =
        "builtin.synthetic_parallel_recording.256_independent_passes";

    struct RenderGraphParallelRecordingSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double SerialRecordMilliseconds{0.0};
        double ParallelRecordMilliseconds{0.0};
        double ParallelToSerialRuntimeRatio{0.0};
        double QualityErrorL2{0.0};
        std::uint32_t PassCount{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
        std::uint32_t SchedulerWorkerCount{0u};
        std::uint32_t RecordOpsPerPass{0u};
        std::uint32_t ParallelLayerCount{0u};
        std::uint32_t ParallelMaxLayerWidth{0u};
        std::uint32_t ParallelWorkerTaskCount{0u};
        std::uint32_t ParallelCallerRecordCount{0u};
        std::uint64_t SerialChecksum{0u};
        std::uint64_t ParallelChecksum{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] RenderGraphParallelRecordingSmokeMetrics RunRenderGraphParallelRecordingSmoke();
} // namespace Intrinsic::Bench::Rendering
