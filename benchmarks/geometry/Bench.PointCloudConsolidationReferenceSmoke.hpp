// METHOD-016 — LOP/WLOP CPU-reference correctness smoke declaration.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kPointCloudConsolidationReferenceSmokeBenchmarkId =
        "geometry.locally_optimal_projection.reference.smoke";
    inline constexpr const char* kPointCloudConsolidationReferenceSmokeMethod =
        "geometry.locally_optimal_projection";
    inline constexpr const char* kPointCloudConsolidationReferenceSmokeDataset =
        "builtin.noisy_plane_sphere.lop_family.confirmation.v2";

    struct PointCloudConsolidationReferenceSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double RawPlaneError{0.0};
        double WlopPlaneError{0.0};
        double RawSphereError{0.0};
        double WlopSphereError{0.0};
        double UniformityWithoutRepulsion{0.0};
        double UniformityWithRepulsion{0.0};
        double OutlierPatchMaxDisplacement{0.0};
        std::uint32_t LopIterations{0u};
        std::uint32_t WlopPlaneIterations{0u};
        std::uint32_t WlopSphereIterations{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] PointCloudConsolidationReferenceSmokeMetrics
    RunPointCloudConsolidationReferenceSmoke();
}
