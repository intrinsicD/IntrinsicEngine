// METHOD-012 — progressive Poisson-disk CPU reference smoke benchmark declaration.
//
// Exercises the hermetic geometry.progressive_poisson cpu_reference backend on a
// deterministic uniform-cube workload and reports runtime plus blue-noise
// quality summaries. The companion manifest lives at
// benchmarks/geometry/manifests/progressive_poisson_reference_smoke.yaml; the
// runner that emits result JSON lives at
// benchmarks/runners/BenchmarkSmokeRunner.cpp.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kProgressivePoissonReferenceSmokeBenchmarkId = "geometry.progressive_poisson.smoke";
    inline constexpr const char* kProgressivePoissonReferenceSmokeMethod      = "geometry.progressive_poisson";
    inline constexpr const char* kProgressivePoissonReferenceSmokeDataset     = "builtin.uniform_cube.4096";

    struct ProgressivePoissonReferenceSmokeMetrics
    {
        double        RuntimeMilliseconds{0.0};
        double        QualityErrorL2{0.0};   ///< max(0, 1 - min Poisson-disk ratio); 0 when the guarantee holds.
        double        PoissonRatioMin{0.0};  ///< min over levels of measured_min_dist / r_L (>= 1 means guarantee held).
        double        CoverageFraction{0.0}; ///< accepted / input count.
        std::uint32_t AcceptedCount{0};
        std::uint32_t LevelCount{0};
        bool          Succeeded{false};
    };

    // Deterministic on a fixed seed; safe to call from any thread (no shared state).
    [[nodiscard]] ProgressivePoissonReferenceSmokeMetrics RunProgressivePoissonReferenceSmoke();
} // namespace Intrinsic::Bench::Geometry
