#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Physics
{
    inline constexpr const char* kSphFluidReferenceSmokeBenchmarkId = "physics.sph_fluid_reference.smoke";
    inline constexpr const char* kSphFluidReferenceSmokeMethod      = "physics.sph_fluid_reference";
    inline constexpr const char* kSphFluidReferenceSmokeDataset     = "builtin.sph_fluid.uniform_grid_and_toy_column";

    struct SphFluidReferenceSmokeMetrics
    {
        double      RuntimeMilliseconds{0.0};
        double      QualityErrorL2{0.0}; // interior relative density error on the static grid
        double      ColumnMaxCompression{0.0};
        double      ColumnAverageDensityError{0.0};
        std::size_t ColumnMaxNeighborCount{0};
        bool        ColumnStable{false};
        bool        Succeeded{false};
    };

    [[nodiscard]] SphFluidReferenceSmokeMetrics RunSphFluidReferenceSmoke();
} // namespace Intrinsic::Bench::Physics
