// GRAPHICS-120 - framegraph reset/redeclare scratch reuse smoke benchmark.
//
// Compares fresh graph rebuilds against steady-state RenderGraph::Reset()
// rebuilds on deterministic pass-heavy work. Allocation counts are measured
// with a scoped global-new probe and reported as diagnostics.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Rendering
{
    inline constexpr const char* kFramegraphScratchReuseSmokeBenchmarkId =
        "rendering.framegraph_scratch_reuse.smoke";
    inline constexpr const char* kFramegraphScratchReuseSmokeMethod =
        "rendering.framegraph_scratch_reuse";
    inline constexpr const char* kFramegraphScratchReuseSmokeDataset =
        "builtin.synthetic_framegraph_scratch_reuse.192_passes";

    struct FramegraphScratchReuseSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double FreshDeclareCompileMilliseconds{0.0};
        double ReusedDeclareCompileMilliseconds{0.0};
        double QualityErrorL2{0.0};
        std::uint32_t PassCount{0u};
        std::uint32_t ResourceCount{0u};
        std::uint32_t BarrierPacketCount{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
        std::uint64_t FreshDeclareAllocations{0u};
        std::uint64_t ReusedDeclareAllocations{0u};
        std::uint64_t FreshDeclareBytes{0u};
        std::uint64_t ReusedDeclareBytes{0u};
        std::uint64_t FreshDeclareCompileAllocations{0u};
        std::uint64_t ReusedDeclareCompileAllocations{0u};
        std::uint64_t FreshDeclareCompileBytes{0u};
        std::uint64_t ReusedDeclareCompileBytes{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] FramegraphScratchReuseSmokeMetrics RunFramegraphScratchReuseSmoke();
} // namespace Intrinsic::Bench::Rendering
