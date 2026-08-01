// METHOD-018 — EAR CPU-reference correctness smoke declaration.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kEdgeAwareResamplingReferenceSmokeBenchmarkId =
        "geometry.edge_aware_resampling.reference.smoke";
    inline constexpr const char* kEdgeAwareResamplingReferenceSmokeMethod =
        "geometry.edge_aware_resampling";
    inline constexpr const char* kEdgeAwareResamplingReferenceSmokeDataset =
        "builtin.noisy_dihedral.ear.confirmation.v1";

    struct EdgeAwareResamplingReferenceSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double RawExpectedPlaneError{0.0};
        double IsotropicExpectedPlaneError{0.0};
        double AnisotropicExpectedPlaneError{0.0};
        double EdgeSharpnessPreservation{0.0};
        double NormalAngularErrorRadians{0.0};
        double UniformityMinimumPairwiseDistance{0.0};
        std::size_t InputPointCount{0u};
        std::size_t OutputPointCount{0u};
        std::size_t InsertedPointCount{0u};
        std::size_t InsertedNearFeatureCount{0u};
        std::size_t EdgePriorityEvaluations{0u};
        std::uint32_t AnisotropicIterations{0u};
        std::uint32_t NormalRefinementIterations{0u};
        bool UsedAuthoredNormals{false};
        bool NormalsRequiredFailedClosed{false};
        bool Deterministic{false};
        bool Succeeded{false};
    };

    [[nodiscard]] EdgeAwareResamplingReferenceSmokeMetrics
    RunEdgeAwareResamplingReferenceSmoke();
}
