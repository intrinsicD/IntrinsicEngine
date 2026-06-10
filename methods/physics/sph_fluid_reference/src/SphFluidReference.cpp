#include "SphFluidReference.hpp"

#include <algorithm>
#include <cmath>

namespace Intrinsic::Methods::Physics::SphFluidReference
{
    namespace
    {
        constexpr double kPi = 3.141592653589793238462643383279502884;
        constexpr double kDistanceEpsilon = 1.0e-12;

        [[nodiscard]] auto AllFinite(const std::vector<ParticleState>& particles) -> bool
        {
            for (const ParticleState& particle : particles)
            {
                if (!IsFinite(particle.Position) || !IsFinite(particle.Velocity))
                {
                    return false;
                }
            }
            return true;
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

    auto Poly6Kernel(double r, double h) -> double
    {
        if (r < 0.0 || r >= h)
        {
            return 0.0;
        }
        const double h2 = h * h;
        const double diff = h2 - r * r;
        const double h9 = h2 * h2 * h2 * h2 * h;
        return (315.0 / (64.0 * kPi * h9)) * diff * diff * diff;
    }

    auto SpikyGradientMagnitude(double r, double h) -> double
    {
        if (r < 0.0 || r >= h)
        {
            return 0.0;
        }
        const double diff = h - r;
        const double h6 = h * h * h * h * h * h;
        return (45.0 / (kPi * h6)) * diff * diff;
    }

    auto ViscosityLaplacian(double r, double h) -> double
    {
        if (r < 0.0 || r >= h)
        {
            return 0.0;
        }
        const double h6 = h * h * h * h * h * h;
        return (45.0 / (kPi * h6)) * (h - r);
    }

    auto MakeBoundaryPlane(Vec3 normal, double offset) -> BoundaryPlane
    {
        BoundaryPlane plane{};
        const double length = Length(normal);
        plane.Normal = (std::isfinite(length) && length > 0.0) ? normal / length : Vec3{0.0, 1.0, 0.0};
        plane.Offset = offset;
        return plane;
    }

    auto Validate(const ParticleState& particle) -> ValidationCode
    {
        if (!IsFinite(particle.Position) || !IsFinite(particle.Velocity))
        {
            return ValidationCode::InvalidParticle;
        }
        return ValidationCode::Valid;
    }

    auto Validate(const std::vector<ParticleState>& particles, const StepParams& params) -> ValidationCode
    {
        if (!std::isfinite(params.DeltaTime) || params.DeltaTime <= 0.0)
        {
            return ValidationCode::InvalidTimeStep;
        }
        if (!std::isfinite(params.SmoothingLength) || params.SmoothingLength <= 0.0 ||
            !std::isfinite(params.ParticleMass) || params.ParticleMass <= 0.0 ||
            !std::isfinite(params.RestDensity) || params.RestDensity <= 0.0 ||
            !std::isfinite(params.Stiffness) || params.Stiffness < 0.0 ||
            !std::isfinite(params.Viscosity) || params.Viscosity < 0.0 ||
            !std::isfinite(params.BoundaryRestitution) || params.BoundaryRestitution < 0.0 ||
            params.BoundaryRestitution > 1.0 || !IsFinite(params.Gravity))
        {
            return ValidationCode::InvalidParams;
        }
        for (const BoundaryPlane& plane : params.Boundaries)
        {
            if (!IsFinite(plane.Normal) || !std::isfinite(plane.Offset) ||
                Length(plane.Normal) < kDistanceEpsilon ||
                !std::isfinite(Dot(plane.Normal, plane.Normal)))
            {
                return ValidationCode::InvalidBoundary;
            }
        }
        for (const ParticleState& particle : particles)
        {
            const ValidationCode code = Validate(particle);
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        return ValidationCode::Valid;
    }

    auto ComputeDensities(const std::vector<ParticleState>& particles, const StepParams& params)
        -> std::vector<double>
    {
        const double h = params.SmoothingLength;
        std::vector<double> densities(particles.size(), 0.0);
        for (std::size_t i = 0; i < particles.size(); ++i)
        {
            double density = 0.0;
            for (std::size_t j = 0; j < particles.size(); ++j)
            {
                const double r = Length(particles[i].Position - particles[j].Position);
                density += params.ParticleMass * Poly6Kernel(r, h);
            }
            densities[i] = density;
        }
        return densities;
    }

    auto ComputeKineticEnergy(const std::vector<ParticleState>& particles, const StepParams& params)
        -> double
    {
        double energy = 0.0;
        for (const ParticleState& particle : particles)
        {
            energy += 0.5 * params.ParticleMass * Dot(particle.Velocity, particle.Velocity);
        }
        return energy;
    }

    auto Step(const std::vector<ParticleState>& particles, const StepParams& params) -> StepResult
    {
        StepResult result{};
        result.Particles = particles;
        result.Diagnostics.ParticleCount = particles.size();

        const ValidationCode validation = Validate(particles, params);
        if (validation != ValidationCode::Valid)
        {
            result.Diagnostics.Code = validation;
            result.Diagnostics.Stable = false;
            return result;
        }

        const double h = params.SmoothingLength;
        const double mass = params.ParticleMass;
        result.Diagnostics.TotalMass = mass * static_cast<double>(particles.size());
        result.Diagnostics.KineticEnergyBefore = ComputeKineticEnergy(particles, params);

        // Density and pressure from current positions. The reference uses
        // deterministic all-pairs neighbor enumeration in index order; the
        // neighbor limit is advisory (reported, never truncating).
        const std::vector<double> densities = ComputeDensities(particles, params);
        std::vector<double> pressures(particles.size(), 0.0);
        for (std::size_t i = 0; i < particles.size(); ++i)
        {
            // Pressure clamped at zero: classic WCSPH avoids tensile
            // instability by not modeling negative (suction) pressure.
            pressures[i] = std::max(0.0, params.Stiffness * (densities[i] - params.RestDensity));
        }

        if (params.MaxNeighborLimit > 0)
        {
            for (std::size_t i = 0; i < particles.size(); ++i)
            {
                std::size_t neighbors = 0;
                for (std::size_t j = 0; j < particles.size(); ++j)
                {
                    if (i == j)
                    {
                        continue;
                    }
                    if (Length(particles[i].Position - particles[j].Position) < h)
                    {
                        ++neighbors;
                    }
                }
                result.Diagnostics.MaxNeighborCount =
                    std::max(result.Diagnostics.MaxNeighborCount, neighbors);
                if (neighbors > params.MaxNeighborLimit)
                {
                    ++result.Diagnostics.NeighborOverflowCount;
                }
            }
        }
        else
        {
            for (std::size_t i = 0; i < particles.size(); ++i)
            {
                std::size_t neighbors = 0;
                for (std::size_t j = 0; j < particles.size(); ++j)
                {
                    if (i != j && Length(particles[i].Position - particles[j].Position) < h)
                    {
                        ++neighbors;
                    }
                }
                result.Diagnostics.MaxNeighborCount =
                    std::max(result.Diagnostics.MaxNeighborCount, neighbors);
            }
        }

        // Symmetric pressure force plus viscosity (Mueller 2003), then
        // gravity, integrated with semi-implicit Euler.
        for (std::size_t i = 0; i < particles.size(); ++i)
        {
            Vec3 pressureForce{};
            Vec3 viscosityForce{};
            for (std::size_t j = 0; j < particles.size(); ++j)
            {
                if (i == j)
                {
                    continue;
                }
                const Vec3   delta = particles[i].Position - particles[j].Position;
                const double r = Length(delta);
                if (r >= h)
                {
                    continue;
                }
                if (r > kDistanceEpsilon)
                {
                    const Vec3   direction = delta / r;
                    const double sharedPressure = (pressures[i] + pressures[j]) / (2.0 * densities[j]);
                    pressureForce =
                        pressureForce + direction * (mass * sharedPressure * SpikyGradientMagnitude(r, h));
                }
                viscosityForce =
                    viscosityForce + (particles[j].Velocity - particles[i].Velocity) *
                                         (params.Viscosity * mass / densities[j] * ViscosityLaplacian(r, h));
            }

            ParticleState& particle = result.Particles[i];
            const Vec3 acceleration = (pressureForce + viscosityForce) / densities[i] + params.Gravity;
            particle.Velocity = particle.Velocity + acceleration * params.DeltaTime;
            particle.Position = particle.Position + particle.Velocity * params.DeltaTime;
        }

        // Boundary half-spaces: project position back and reflect the normal
        // velocity scaled by the boundary restitution (0 = inelastic).
        // Validated normals need not be unit length, so the projection and
        // reflection scale by dot(N, N) to stay exact for any accepted plane.
        for (const BoundaryPlane& plane : params.Boundaries)
        {
            const double normalLengthSquared = Dot(plane.Normal, plane.Normal);
            for (ParticleState& particle : result.Particles)
            {
                const double distance =
                    (Dot(plane.Normal, particle.Position) - plane.Offset) / normalLengthSquared;
                if (distance < 0.0)
                {
                    particle.Position = particle.Position - plane.Normal * distance;
                    const double normalVelocity =
                        Dot(plane.Normal, particle.Velocity) / normalLengthSquared;
                    if (normalVelocity < 0.0)
                    {
                        particle.Velocity =
                            particle.Velocity -
                            plane.Normal * ((1.0 + params.BoundaryRestitution) * normalVelocity);
                    }
                }
            }
        }

        if (!AllFinite(result.Particles))
        {
            // Fail closed: callers never observe non-finite fluid state.
            result.Particles = particles;
            result.Diagnostics.Code = ValidationCode::NonFiniteState;
            result.Diagnostics.Stable = false;
            result.Diagnostics.FallbackApplied = true;
            return result;
        }

        double densitySum = 0.0;
        double densityErrorSum = 0.0;
        result.Diagnostics.MinDensity = densities.empty() ? 0.0 : densities.front();
        for (const double density : densities)
        {
            densitySum += density;
            densityErrorSum += std::abs(density - params.RestDensity) / params.RestDensity;
            result.Diagnostics.MinDensity = std::min(result.Diagnostics.MinDensity, density);
            result.Diagnostics.MaxDensity = std::max(result.Diagnostics.MaxDensity, density);
            result.Diagnostics.MaxCompression =
                std::max(result.Diagnostics.MaxCompression,
                         (density - params.RestDensity) / params.RestDensity);
        }
        result.Diagnostics.MaxCompression = std::max(0.0, result.Diagnostics.MaxCompression);
        if (!densities.empty())
        {
            result.Diagnostics.AverageDensity = densitySum / static_cast<double>(densities.size());
            result.Diagnostics.AverageDensityError =
                densityErrorSum / static_cast<double>(densities.size());
        }
        for (const ParticleState& particle : result.Particles)
        {
            result.Diagnostics.MaxVelocity =
                std::max(result.Diagnostics.MaxVelocity, Length(particle.Velocity));
        }
        result.Diagnostics.KineticEnergyAfter = ComputeKineticEnergy(result.Particles, params);
        result.Diagnostics.EnergyDrift =
            result.Diagnostics.KineticEnergyAfter - result.Diagnostics.KineticEnergyBefore;
        return result;
    }
} // namespace Intrinsic::Methods::Physics::SphFluidReference
