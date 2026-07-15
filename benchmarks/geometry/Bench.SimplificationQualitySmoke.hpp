// GEOM-014 — deterministic simplification quality smoke declaration.
#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kSimplificationQualitySmokeBenchmarkId =
        "geometry.simplification.fa_qem_quality.smoke";
    inline constexpr const char* kSimplificationQualitySmokeMethod =
        "geometry.halfedge.simplification.fa_qem_adaptation";
    inline constexpr const char* kSimplificationQualitySmokeDataset =
        "builtin.tessellated_cube_n4";
    inline constexpr double kSimplificationQualitySmokeRuntimeMillisecondsMax =
        5000.0;

    struct SimplificationQualitySmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double QualityErrorLinf{0.0};
        double FeatureAwareRmsDistance{0.0};
        double ClassicalRmsDistance{0.0};
        double FeatureAwareMaxDistance{0.0};
        double ClassicalMaxDistance{0.0};
        double SensitivityControlMaxDistance{0.0};
        std::size_t SampleCount{0u};
        std::size_t TargetFaceCount{0u};
        std::size_t FeatureAwareFinalFaceCount{0u};
        std::size_t ClassicalFinalFaceCount{0u};
        std::size_t FeatureAwarePinnedCornerCount{0u};
        std::size_t FeatureAwareQualityRejectionCount{0u};
        std::size_t FailedMeasuredIterationCount{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] SimplificationQualitySmokeMetrics RunSimplificationQualitySmoke();
}
