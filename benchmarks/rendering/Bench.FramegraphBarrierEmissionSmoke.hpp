// GRAPHICS-120 - framegraph barrier emission traversal smoke benchmark.
//
// Compares the legacy full-scan traversal shape against the shared indexed
// range lookup over the same deterministic synthetic barrier packet set. This
// is CPU-side traversal evidence only; it does not claim renderer-wide frame
// time adoption.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Rendering
{
    inline constexpr const char* kFramegraphBarrierEmissionSmokeBenchmarkId =
        "rendering.framegraph_barrier_emission.smoke";
    inline constexpr const char* kFramegraphBarrierEmissionSmokeMethod =
        "rendering.framegraph_barrier_emission";
    inline constexpr const char* kFramegraphBarrierEmissionSmokeDataset =
        "builtin.synthetic_framegraph_barriers.256_passes";

    struct FramegraphBarrierEmissionSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double LegacyFullScanMilliseconds{0.0};
        double IndexedRangeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        std::uint32_t PassCount{0u};
        std::uint32_t BarrierPacketCount{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
        std::uint64_t LegacyPacketComparisons{0u};
        std::uint64_t IndexedRangePacketVisits{0u};
        std::uint64_t TextureBarrierVisits{0u};
        std::uint64_t BufferBarrierVisits{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] FramegraphBarrierEmissionSmokeMetrics RunFramegraphBarrierEmissionSmoke();
} // namespace Intrinsic::Bench::Rendering
