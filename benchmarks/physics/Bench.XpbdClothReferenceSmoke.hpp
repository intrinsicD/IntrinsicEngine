#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Physics
{
    inline constexpr const char* kXpbdClothReferenceSmokeBenchmarkId = "physics.xpbd_cloth_reference.smoke";
    inline constexpr const char* kXpbdClothReferenceSmokeMethod      = "physics.xpbd_cloth_reference";
    inline constexpr const char* kXpbdClothReferenceSmokeDataset     = "builtin.xpbd_cloth.pinned_hanging_patch_3x3";

    struct XpbdClothReferenceSmokeMetrics
    {
        double      RuntimeMilliseconds{0.0};
        double      QualityErrorL2{0.0}; // final max stretch residual (meters)
        double      MaxBendResidual{0.0};
        std::size_t DegenerateTriangleCount{0};
        std::size_t DegenerateConstraintCount{0};
        bool        Converged{false};
        bool        Succeeded{false};
    };

    [[nodiscard]] XpbdClothReferenceSmokeMetrics RunXpbdClothReferenceSmoke();
} // namespace Intrinsic::Bench::Physics
