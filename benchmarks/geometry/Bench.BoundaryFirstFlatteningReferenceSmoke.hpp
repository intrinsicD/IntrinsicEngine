// METHOD-023 — deterministic Boundary First Flattening reference smoke.
#pragma once

#include <cstddef>
#include <string_view>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kBoundaryFirstFlatteningReferenceSmokeBenchmarkId =
        "geometry.boundary_first_flattening.smoke";
    inline constexpr const char* kBoundaryFirstFlatteningReferenceSmokeMethod =
        "geometry.boundary_first_flattening";
    inline constexpr const char* kBoundaryFirstFlatteningReferenceSmokeDataset =
        "builtin.curved_grid_disk_n3";
    inline constexpr std::size_t
        kBoundaryFirstFlatteningReferenceSmokeWarmupIterations = 1u;
    inline constexpr std::size_t
        kBoundaryFirstFlatteningReferenceSmokeMeasuredIterations = 3u;
    inline constexpr double
        kBoundaryFirstFlatteningReferenceSmokeRuntimeMillisecondsMax = 250.0;
    inline constexpr double
        kBoundaryFirstFlatteningReferenceSmokeQualityErrorL2Max = 0.5;

    struct BoundaryFirstFlatteningReferenceSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double MeanConformalDistortion{0.0};
        double MaxConformalDistortion{0.0};
        double ClosureAdjustmentRmsRelative{0.0};
        double ClosureAdjustmentMaxRelative{0.0};
        std::size_t VertexCount{0u};
        std::size_t FaceCount{0u};
        std::size_t EvaluatedFaceCount{0u};
        std::size_t FlippedElementCount{0u};
        std::size_t FailedMeasuredIterationCount{0u};
        std::string_view FailureReason{"not_run"};
        bool Succeeded{false};
    };

    [[nodiscard]] BoundaryFirstFlatteningReferenceSmokeMetrics
    RunBoundaryFirstFlatteningReferenceSmoke();
}
