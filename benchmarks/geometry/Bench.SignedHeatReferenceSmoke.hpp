// METHOD-002 — signed heat CPU reference smoke benchmark declaration.
//
// Exercises the repo-native Geometry.SignedHeatMethod cpu_reference backend on a
// deterministic flat-grid workload with an oriented square source curve. The
// companion manifest lives at
// benchmarks/geometry/manifests/signed_heat_reference_smoke.yaml.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kSignedHeatReferenceSmokeBenchmarkId = "geometry.signed_heat.smoke";
    inline constexpr const char* kSignedHeatReferenceSmokeMethod      = "geometry.signed_heat";
    inline constexpr const char* kSignedHeatReferenceSmokeDataset     = "builtin.flat_grid.square_boundary.8";

    struct SignedHeatReferenceSmokeMetrics
    {
        double        RuntimeMilliseconds{0.0};
        double        QualityErrorL2{0.0};
        double        MaxAbsDistance{0.0};
        double        MeanBoundaryOffset{0.0};
        std::uint32_t SourceVertexCount{0};
        std::uint32_t DegenerateBoundaryVertexCount{0};
        bool          Succeeded{false};
    };

    // Deterministic fixed workload; safe to call from any thread with no shared state.
    [[nodiscard]] SignedHeatReferenceSmokeMetrics RunSignedHeatReferenceSmoke();
} // namespace Intrinsic::Bench::Geometry
