#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Physics
{
    inline constexpr const char* kParticleSpringReferenceSmokeBenchmarkId = "physics.particle_spring_reference.smoke";
    inline constexpr const char* kParticleSpringReferenceSmokeMethod      = "physics.particle_spring_reference";
    inline constexpr const char* kParticleSpringReferenceSmokeDataset     = "builtin.particle_spring.symmetric_stretched_pair";

    struct ParticleSpringReferenceSmokeMetrics
    {
        double      RuntimeMilliseconds{0.0};
        double      QualityErrorL2{0.0};
        std::size_t DegenerateSpringCount{0};
        double      MaxSpringResidual{0.0};
        double      EnergyDrift{0.0};
        double      MaxStiffnessDtRatio{0.0};
        bool        Succeeded{false};
    };

    [[nodiscard]] ParticleSpringReferenceSmokeMetrics RunParticleSpringReferenceSmoke();
} // namespace Intrinsic::Bench::Physics
