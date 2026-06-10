#include "ParticleSpringReference.hpp"

#include <algorithm>
#include <cmath>

namespace Intrinsic::Methods::Physics::ParticleSpringReference
{
    namespace
    {
        constexpr double kDegenerateLengthEpsilon = 1.0e-12;
        // Undamped semi-implicit Euler on a harmonic oscillator is stable for
        // omega * dt < 2 with omega = sqrt(k * (invMassA + invMassB)).
        constexpr double kStabilityRatioLimit = 2.0;

        [[nodiscard]] auto DampingFactor(double damping, double dt) -> double
        {
            if (!std::isfinite(damping) || damping <= 0.0)
            {
                return 1.0;
            }
            return std::max(0.0, 1.0 - damping * dt);
        }

        [[nodiscard]] auto AllFinite(const SystemState& state) -> bool
        {
            for (const ParticleState& particle : state.Particles)
            {
                if (!IsFinite(particle.Position) || !IsFinite(particle.Velocity))
                {
                    return false;
                }
            }
            return true;
        }

        void AccumulateSpringResiduals(const SystemState& state, Diagnostics& diagnostics)
        {
            diagnostics.MaxSpringResidual = 0.0;
            diagnostics.SpringResidualL2 = 0.0;
            double sumSquared = 0.0;
            for (const SpringRecord& spring : state.Springs)
            {
                const Vec3   d = state.Particles[spring.ParticleB].Position -
                               state.Particles[spring.ParticleA].Position;
                const double residual = std::abs(Length(d) - spring.RestLength);
                diagnostics.MaxSpringResidual = std::max(diagnostics.MaxSpringResidual, residual);
                sumSquared += residual * residual;
            }
            diagnostics.SpringResidualL2 = std::sqrt(sumSquared);
        }
    } // namespace

    auto operator+(Vec3 lhs, Vec3 rhs) -> Vec3 { return Vec3{lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z}; }
    auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3 { return Vec3{lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z}; }
    auto operator-(Vec3 value) -> Vec3 { return Vec3{-value.X, -value.Y, -value.Z}; }
    auto operator*(Vec3 lhs, double rhs) -> Vec3 { return Vec3{lhs.X * rhs, lhs.Y * rhs, lhs.Z * rhs}; }
    auto operator*(double lhs, Vec3 rhs) -> Vec3 { return rhs * lhs; }
    auto operator/(Vec3 lhs, double rhs) -> Vec3 { return Vec3{lhs.X / rhs, lhs.Y / rhs, lhs.Z / rhs}; }

    auto Dot(Vec3 lhs, Vec3 rhs) -> double { return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z; }
    auto Length(Vec3 value) -> double { return std::sqrt(Dot(value, value)); }

    auto IsFinite(Vec3 value) -> bool
    {
        return std::isfinite(value.X) && std::isfinite(value.Y) && std::isfinite(value.Z);
    }

    auto MakeParticle(Vec3 position, double mass) -> ParticleState
    {
        ParticleState particle{};
        particle.Position = position;
        particle.InverseMass = (std::isfinite(mass) && mass > 0.0) ? (1.0 / mass) : 0.0;
        return particle;
    }

    auto MakePinnedParticle(Vec3 position) -> ParticleState
    {
        ParticleState particle{};
        particle.Position = position;
        particle.InverseMass = 0.0;
        return particle;
    }

    auto MakeSpring(std::size_t particleA,
                    std::size_t particleB,
                    double restLength,
                    double stiffness,
                    double damping) -> SpringRecord
    {
        SpringRecord spring{};
        spring.ParticleA = particleA;
        spring.ParticleB = particleB;
        spring.RestLength = restLength;
        spring.Stiffness = stiffness;
        spring.Damping = damping;
        return spring;
    }

    auto Validate(const ParticleState& particle) -> ValidationCode
    {
        if (!IsFinite(particle.Position) || !IsFinite(particle.Velocity))
        {
            return ValidationCode::InvalidParticle;
        }
        if (!std::isfinite(particle.InverseMass) || particle.InverseMass < 0.0)
        {
            return ValidationCode::InvalidParticle;
        }
        return ValidationCode::Valid;
    }

    auto Validate(const SpringRecord& spring, std::size_t particleCount) -> ValidationCode
    {
        if (spring.ParticleA >= particleCount || spring.ParticleB >= particleCount ||
            spring.ParticleA == spring.ParticleB)
        {
            return ValidationCode::InvalidSpring;
        }
        if (!std::isfinite(spring.RestLength) || spring.RestLength < 0.0)
        {
            return ValidationCode::InvalidSpring;
        }
        if (!std::isfinite(spring.Stiffness) || spring.Stiffness < 0.0)
        {
            return ValidationCode::InvalidSpring;
        }
        if (!std::isfinite(spring.Damping) || spring.Damping < 0.0)
        {
            return ValidationCode::InvalidSpring;
        }
        return ValidationCode::Valid;
    }

    auto Validate(const SystemState& state, const StepParams& params) -> ValidationCode
    {
        if (!std::isfinite(params.DeltaTime) || params.DeltaTime <= 0.0)
        {
            return ValidationCode::InvalidTimeStep;
        }
        if (!IsFinite(params.Gravity) || !std::isfinite(params.GlobalDamping))
        {
            return ValidationCode::NonFiniteState;
        }
        for (const ParticleState& particle : state.Particles)
        {
            const ValidationCode code = Validate(particle);
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        for (const SpringRecord& spring : state.Springs)
        {
            const ValidationCode code = Validate(spring, state.Particles.size());
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        return ValidationCode::Valid;
    }

    auto ComputeKineticEnergy(const SystemState& state) -> double
    {
        double energy = 0.0;
        for (const ParticleState& particle : state.Particles)
        {
            if (particle.InverseMass <= 0.0)
            {
                continue;
            }
            energy += 0.5 * (1.0 / particle.InverseMass) * Dot(particle.Velocity, particle.Velocity);
        }
        return energy;
    }

    auto ComputeTotalEnergy(const SystemState& state, const StepParams& params) -> double
    {
        double energy = ComputeKineticEnergy(state);
        for (const SpringRecord& spring : state.Springs)
        {
            if (spring.ParticleA >= state.Particles.size() || spring.ParticleB >= state.Particles.size())
            {
                continue;
            }
            const Vec3   d = state.Particles[spring.ParticleB].Position -
                           state.Particles[spring.ParticleA].Position;
            const double extension = Length(d) - spring.RestLength;
            energy += 0.5 * spring.Stiffness * extension * extension;
        }
        // Gravitational potential -m * dot(g, x): constant offsets cancel in
        // drift; pinned particles carry no defined mass and never move.
        for (const ParticleState& particle : state.Particles)
        {
            if (particle.InverseMass <= 0.0)
            {
                continue;
            }
            energy -= (1.0 / particle.InverseMass) * Dot(params.Gravity, particle.Position);
        }
        return energy;
    }

    auto Step(const SystemState& state, const StepParams& params) -> StepResult
    {
        StepResult result{};
        result.State = state;
        result.Diagnostics.ParticleCount = state.Particles.size();
        result.Diagnostics.SpringCount = state.Springs.size();
        for (const ParticleState& particle : state.Particles)
        {
            if (particle.InverseMass <= 0.0)
            {
                ++result.Diagnostics.PinnedParticleCount;
            }
        }

        const ValidationCode validation = Validate(state, params);
        if (validation != ValidationCode::Valid)
        {
            result.Diagnostics.Code = validation;
            result.Diagnostics.Stable = false;
            return result;
        }

        result.Diagnostics.KineticEnergyBefore = ComputeKineticEnergy(state);
        result.Diagnostics.TotalEnergyBefore = ComputeTotalEnergy(state, params);

        const double dt = params.DeltaTime;

        // Stability report: omega * dt per spring with the reduced mass
        // 1/m_red = invMassA + invMassB. Reported as a warning, not an error.
        for (const SpringRecord& spring : state.Springs)
        {
            const double invMassSum = state.Particles[spring.ParticleA].InverseMass +
                                      state.Particles[spring.ParticleB].InverseMass;
            const double ratio = dt * std::sqrt(spring.Stiffness * invMassSum);
            result.Diagnostics.MaxStiffnessDtRatio = std::max(result.Diagnostics.MaxStiffnessDtRatio, ratio);
        }
        result.Diagnostics.StabilityLimitExceeded =
            result.Diagnostics.MaxStiffnessDtRatio >= kStabilityRatioLimit;

        // Force accumulation from the pre-step state (semi-implicit Euler).
        std::vector<Vec3> forces(state.Particles.size());
        for (const SpringRecord& spring : state.Springs)
        {
            const ParticleState& a = state.Particles[spring.ParticleA];
            const ParticleState& b = state.Particles[spring.ParticleB];
            const Vec3           d = b.Position - a.Position;
            const double         length = Length(d);
            if (length < kDegenerateLengthEpsilon)
            {
                // Spring direction is undefined; skipping the force for this
                // step is deterministic and reported instead of guessing.
                ++result.Diagnostics.DegenerateSpringCount;
                continue;
            }
            const Vec3   direction = d / length;
            const double springTerm = spring.Stiffness * (length - spring.RestLength);
            const double dampingTerm = spring.Damping * Dot(b.Velocity - a.Velocity, direction);
            const Vec3   forceOnA = (springTerm + dampingTerm) * direction;
            forces[spring.ParticleA] = forces[spring.ParticleA] + forceOnA;
            forces[spring.ParticleB] = forces[spring.ParticleB] - forceOnA;
        }

        // Integrate: velocity first (gravity + spring forces + global drag),
        // then position from the updated velocity. Pinned particles never
        // integrate and discard accumulated forces.
        const double globalDampingFactor = DampingFactor(params.GlobalDamping, dt);
        for (std::size_t i = 0; i < result.State.Particles.size(); ++i)
        {
            ParticleState& particle = result.State.Particles[i];
            if (particle.InverseMass <= 0.0)
            {
                continue;
            }
            particle.Velocity = particle.Velocity + (params.Gravity + forces[i] * particle.InverseMass) * dt;
            particle.Velocity = particle.Velocity * globalDampingFactor;
            particle.Position = particle.Position + particle.Velocity * dt;
        }

        if (!AllFinite(result.State))
        {
            // Fail closed: return the input state unchanged so callers never
            // observe non-finite particles, and report the instability.
            result.State = state;
            result.Diagnostics.Code = ValidationCode::NonFiniteState;
            result.Diagnostics.Stable = false;
            result.Diagnostics.FallbackApplied = true;
            AccumulateSpringResiduals(result.State, result.Diagnostics);
            return result;
        }

        AccumulateSpringResiduals(result.State, result.Diagnostics);
        result.Diagnostics.KineticEnergyAfter = ComputeKineticEnergy(result.State);
        result.Diagnostics.TotalEnergyAfter = ComputeTotalEnergy(result.State, params);
        result.Diagnostics.EnergyDrift =
            result.Diagnostics.TotalEnergyAfter - result.Diagnostics.TotalEnergyBefore;
        return result;
    }
} // namespace Intrinsic::Methods::Physics::ParticleSpringReference
