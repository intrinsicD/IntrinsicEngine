// METHOD-017 — CLOP CPU-reference correctness smoke declaration.
#pragma once

#include <cstddef>
#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kContinuousLopReferenceSmokeBenchmarkId =
        "geometry.continuous_lop.reference.smoke";
    inline constexpr const char* kContinuousLopReferenceSmokeMethod =
        "geometry.continuous_lop";
    inline constexpr const char* kContinuousLopReferenceSmokeDataset =
        "builtin.noisy_plane_sphere.clop.confirmation.v1";

    struct ContinuousLopReferenceSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double RawPlaneError{0.0};
        double ClopPlaneError{0.0};
        double RawSphereError{0.0};
        double ClopSphereError{0.0};
        double WlopParityMeanDistance{0.0};
        double CompactPlaneError{0.0};
        double UniformityWithoutRepulsion{0.0};
        double UniformityWithRepulsion{0.0};
        double OutlierPatchMaxDisplacement{0.0};
        std::size_t RichMixtureComponentCount{0u};
        std::size_t CompactMixtureComponentCount{0u};
        std::size_t RichAttractionContributions{0u};
        std::size_t CompactAttractionContributions{0u};
        std::uint32_t RichMixtureIterations{0u};
        std::uint32_t SphereMixtureIterations{0u};
        std::uint32_t PlaneProjectionIterations{0u};
        std::uint32_t SphereProjectionIterations{0u};
        bool MixturesConverged{false};
        bool InvalidRequestFailedClosed{false};
        bool Succeeded{false};
    };

    [[nodiscard]] ContinuousLopReferenceSmokeMetrics
    RunContinuousLopReferenceSmoke();
}
