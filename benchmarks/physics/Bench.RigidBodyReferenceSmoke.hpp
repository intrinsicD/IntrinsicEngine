#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Physics
{
    inline constexpr const char* kRigidBodyReferenceSmokeBenchmarkId = "physics.rigid_body_reference.smoke";
    inline constexpr const char* kRigidBodyReferenceSmokeMethod      = "physics.rigid_body_reference";
    inline constexpr const char* kRigidBodyReferenceSmokeDataset     = "builtin.rigid_body.two_sphere";

    struct RigidBodyReferenceSmokeMetrics
    {
        double      RuntimeMilliseconds{0.0};
        double      QualityErrorL2{0.0};
        std::size_t ContactCount{0};
        std::size_t UnsupportedPairCount{0};
        double      FinalVelocityA{0.0};
        double      FinalVelocityB{0.0};
        bool        Succeeded{false};
    };

    [[nodiscard]] RigidBodyReferenceSmokeMetrics RunRigidBodyReferenceSmoke();
} // namespace Intrinsic::Bench::Physics
