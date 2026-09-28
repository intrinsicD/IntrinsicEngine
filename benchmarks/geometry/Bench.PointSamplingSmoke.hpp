// GEOM-111 — unified point sampling smoke benchmark declaration.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kPointSamplingSmokeBenchmarkId = "geometry.point_sampling.smoke";
    inline constexpr const char* kPointSamplingSmokeMethod      = "geometry.point_sampling";
    inline constexpr const char* kPointSamplingSmokeDataset     = "builtin.point_sampling_uniform_box";

    struct PointSamplingSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};   // hole-sieve FPS, 1000 of 20000 points (mean of measured runs)
        double QualityErrorL2{0.0};        // fraction of ranks differing from brute-force FPS (3000 points)
        std::uint64_t SievePairs{0};       // pairs evaluated for 1000 of 20000 points
        double PairReduction{0.0};         // flat-scan pairs / sieve pairs
        bool Succeeded{false};
    };

    [[nodiscard]] PointSamplingSmokeMetrics RunPointSamplingSmoke();
}
