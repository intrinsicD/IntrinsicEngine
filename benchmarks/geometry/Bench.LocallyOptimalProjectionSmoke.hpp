// METHOD-016 — WLOP consolidation CPU reference smoke benchmark declaration.
//
// Exercises the Geometry.PointCloud.Consolidation cpu_reference backend on a
// deterministic noisy-plane workload. The companion manifest lives at
// benchmarks/geometry/manifests/locally_optimal_projection_reference_smoke.yaml.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kLocallyOptimalProjectionSmokeBenchmarkId =
        "geometry.locally_optimal_projection.smoke";
    inline constexpr const char* kLocallyOptimalProjectionSmokeMethod =
        "geometry.locally_optimal_projection";
    inline constexpr const char* kLocallyOptimalProjectionSmokeDataset =
        "builtin.noisy_plane.24x24.sigma2e-2";

    struct LocallyOptimalProjectionSmokeMetrics
    {
        double        RuntimeMilliseconds{0.0};
        double        QualityErrorL2{0.0};
        double        RawQualityErrorL2{0.0};
        double        MinPairwiseDistance{0.0};
        double        LastMeanMovement{0.0};
        std::uint32_t IterationsRun{0};
        std::uint32_t ProjectedPointCount{0};
        std::uint32_t EmptyAttractionNeighborhoods{0};
        bool          Succeeded{false};
    };

    // Deterministic fixed workload; safe to call from any thread with no shared state.
    [[nodiscard]] LocallyOptimalProjectionSmokeMetrics RunLocallyOptimalProjectionSmoke();
} // namespace Intrinsic::Bench::Geometry
