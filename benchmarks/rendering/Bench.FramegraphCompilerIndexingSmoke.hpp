// GRAPHICS-120 - framegraph compiler indexing smoke benchmark.
//
// Compares the legacy duplicate-pass and barrier-packet insertion scan shapes
// against the sorted/indexed compiler shape on deterministic synthetic work.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Rendering
{
    inline constexpr const char* kFramegraphCompilerIndexingSmokeBenchmarkId =
        "rendering.framegraph_compiler_indexing.smoke";
    inline constexpr const char* kFramegraphCompilerIndexingSmokeMethod =
        "rendering.framegraph_compiler_indexing";
    inline constexpr const char* kFramegraphCompilerIndexingSmokeDataset =
        "builtin.synthetic_framegraph_compiler_indexing.512_passes";

    struct FramegraphCompilerIndexingSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double LegacyScanMilliseconds{0.0};
        double IndexedMilliseconds{0.0};
        double QualityErrorL2{0.0};
        std::uint32_t PassCount{0u};
        std::uint32_t RequestCount{0u};
        std::uint32_t BarrierPacketCount{0u};
        std::uint32_t WarmupIterations{0u};
        std::uint32_t MeasuredIterations{0u};
        std::uint64_t LegacyPassIdComparisons{0u};
        std::uint64_t IndexedPassIdComparisons{0u};
        std::uint64_t LegacyPacketComparisons{0u};
        std::uint64_t IndexedPacketLookups{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] FramegraphCompilerIndexingSmokeMetrics RunFramegraphCompilerIndexingSmoke();
} // namespace Intrinsic::Bench::Rendering
