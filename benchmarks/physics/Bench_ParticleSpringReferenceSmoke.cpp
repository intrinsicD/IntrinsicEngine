#include "Bench.ParticleSpringReferenceSmoke.hpp"

#include "ParticleSpringReference.hpp"

#include <chrono>
#include <cmath>

namespace Intrinsic::Bench::Physics
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr int kSteps = 16;

        using namespace Intrinsic::Methods::Physics::ParticleSpringReference;

        // Symmetric stretched two-particle spring with no gravity: momentum
        // and center of mass are conserved exactly by the equal-and-opposite
        // force application, so their magnitudes are the quality metric.
        [[nodiscard]] auto RunWorkload() -> ParticleSpringReferenceSmokeMetrics
        {
            SystemState state{};
            state.Particles = {
                MakeParticle(Vec3{-1.0, 0.0, 0.0}, 1.0),
                MakeParticle(Vec3{1.0, 0.0, 0.0}, 1.0),
            };
            state.Springs = {MakeSpring(0, 1, 1.5, 100.0)};

            StepParams params{};
            params.DeltaTime = 0.005;
            params.Gravity = Vec3{};

            ParticleSpringReferenceSmokeMetrics metrics{};
            bool valid = true;
            Diagnostics lastDiagnostics{};
            for (int i = 0; i < kSteps; ++i)
            {
                const StepResult result = Step(state, params);
                valid = valid && result.Diagnostics.Code == ValidationCode::Valid &&
                        result.Diagnostics.Stable;
                lastDiagnostics = result.Diagnostics;
                state = result.State;
            }

            const Vec3 momentum = state.Particles[0].Velocity + state.Particles[1].Velocity;
            const Vec3 centerOfMass = state.Particles[0].Position + state.Particles[1].Position;
            const double momentumError = Length(momentum);
            const double centerError = Length(centerOfMass);

            metrics.QualityErrorL2 = std::sqrt(momentumError * momentumError + centerError * centerError);
            metrics.DegenerateSpringCount = lastDiagnostics.DegenerateSpringCount;
            metrics.MaxSpringResidual = lastDiagnostics.MaxSpringResidual;
            metrics.EnergyDrift = lastDiagnostics.EnergyDrift;
            metrics.MaxStiffnessDtRatio = lastDiagnostics.MaxStiffnessDtRatio;
            metrics.Succeeded = valid && metrics.DegenerateSpringCount == 0 &&
                                metrics.QualityErrorL2 <= 1.0e-12;
            return metrics;
        }
    } // namespace

    ParticleSpringReferenceSmokeMetrics RunParticleSpringReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)RunWorkload();
        }

        ParticleSpringReferenceSmokeMetrics last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = RunWorkload();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.RuntimeMilliseconds = (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last;
    }
} // namespace Intrinsic::Bench::Physics
