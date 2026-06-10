#include "XpbdClothReference.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace Intrinsic::Methods::Physics::XpbdClothReference
{
    namespace
    {
        constexpr double kDegenerateLengthEpsilon = 1.0e-12;
        constexpr double kDegenerateAreaEpsilon = 1.0e-16;

        [[nodiscard]] auto DampingFactor(double damping, double dt) -> double
        {
            if (!std::isfinite(damping) || damping <= 0.0)
            {
                return 1.0;
            }
            return std::max(0.0, 1.0 - damping * dt);
        }

        [[nodiscard]] auto AllFinite(const ClothState& state) -> bool
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

        // XPBD position projection for one distance constraint. Returns the
        // updated lambda; positions are modified in place.
        void SolveDistanceConstraint(std::vector<ParticleState>& particles,
                                     const DistanceConstraint& constraint,
                                     double dt,
                                     double& lambda,
                                     bool countDegenerate,
                                     std::size_t& degenerateCount)
        {
            ParticleState& a = particles[constraint.ParticleA];
            ParticleState& b = particles[constraint.ParticleB];
            const double   wSum = a.InverseMass + b.InverseMass;
            if (wSum <= 0.0)
            {
                return; // both endpoints pinned
            }

            const Vec3   d = a.Position - b.Position;
            const double length = Length(d);
            if (length < kDegenerateLengthEpsilon)
            {
                if (countDegenerate)
                {
                    ++degenerateCount;
                }
                return; // undefined gradient; skip deterministically
            }

            const Vec3   gradient = d / length;
            const double c = length - constraint.RestLength;
            const double alphaTilde = constraint.Compliance / (dt * dt);
            const double deltaLambda = (-c - alphaTilde * lambda) / (wSum + alphaTilde);
            lambda += deltaLambda;

            a.Position = a.Position + gradient * (a.InverseMass * deltaLambda);
            b.Position = b.Position - gradient * (b.InverseMass * deltaLambda);
        }

        void ProjectColliders(std::vector<ParticleState>& particles,
                              const std::vector<Collider>& colliders)
        {
            for (const Collider& collider : colliders)
            {
                if (collider.Kind != ColliderKind::HalfSpace)
                {
                    continue; // unsupported kinds counted once per step elsewhere
                }
                // Validated normals need not be unit length, so the projection
                // scales by dot(N, N) to stay exact for any accepted plane.
                const double normalLengthSquared = Dot(collider.Normal, collider.Normal);
                for (ParticleState& particle : particles)
                {
                    if (particle.InverseMass <= 0.0)
                    {
                        continue;
                    }
                    const double distance =
                        (Dot(collider.Normal, particle.Position) - collider.Offset) / normalLengthSquared;
                    if (distance < 0.0)
                    {
                        particle.Position = particle.Position - collider.Normal * distance;
                    }
                }
            }
        }

        void AccumulateResiduals(const ClothState& state, Diagnostics& diagnostics)
        {
            double stretchSumSquared = 0.0;
            for (const DistanceConstraint& constraint : state.StretchConstraints)
            {
                const Vec3   d = state.Particles[constraint.ParticleA].Position -
                               state.Particles[constraint.ParticleB].Position;
                const double residual = std::abs(Length(d) - constraint.RestLength);
                diagnostics.MaxStretchResidual = std::max(diagnostics.MaxStretchResidual, residual);
                stretchSumSquared += residual * residual;
            }
            diagnostics.StretchResidualL2 = std::sqrt(stretchSumSquared);

            for (const DistanceConstraint& constraint : state.BendConstraints)
            {
                const Vec3   d = state.Particles[constraint.ParticleA].Position -
                               state.Particles[constraint.ParticleB].Position;
                const double residual = std::abs(Length(d) - constraint.RestLength);
                diagnostics.MaxBendResidual = std::max(diagnostics.MaxBendResidual, residual);
            }
        }
    } // namespace

    auto operator+(Vec3 lhs, Vec3 rhs) -> Vec3 { return Vec3{lhs.X + rhs.X, lhs.Y + rhs.Y, lhs.Z + rhs.Z}; }
    auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3 { return Vec3{lhs.X - rhs.X, lhs.Y - rhs.Y, lhs.Z - rhs.Z}; }
    auto operator-(Vec3 value) -> Vec3 { return Vec3{-value.X, -value.Y, -value.Z}; }
    auto operator*(Vec3 lhs, double rhs) -> Vec3 { return Vec3{lhs.X * rhs, lhs.Y * rhs, lhs.Z * rhs}; }
    auto operator*(double lhs, Vec3 rhs) -> Vec3 { return rhs * lhs; }
    auto operator/(Vec3 lhs, double rhs) -> Vec3 { return Vec3{lhs.X / rhs, lhs.Y / rhs, lhs.Z / rhs}; }

    auto Dot(Vec3 lhs, Vec3 rhs) -> double { return lhs.X * rhs.X + lhs.Y * rhs.Y + lhs.Z * rhs.Z; }

    auto Cross(Vec3 lhs, Vec3 rhs) -> Vec3
    {
        return Vec3{
            lhs.Y * rhs.Z - lhs.Z * rhs.Y,
            lhs.Z * rhs.X - lhs.X * rhs.Z,
            lhs.X * rhs.Y - lhs.Y * rhs.X,
        };
    }

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

    auto MakeHalfSpaceCollider(Vec3 normal, double offset) -> Collider
    {
        Collider collider{};
        collider.Kind = ColliderKind::HalfSpace;
        const double length = Length(normal);
        collider.Normal = (std::isfinite(length) && length > 0.0) ? normal / length : Vec3{0.0, 1.0, 0.0};
        collider.Offset = offset;
        return collider;
    }

    auto MakeSphereCollider(Vec3 center, double radius) -> Collider
    {
        Collider collider{};
        collider.Kind = ColliderKind::Sphere;
        collider.Center = center;
        collider.Radius = radius;
        return collider;
    }

    auto BuildClothFromTriangles(const std::vector<Vec3>& positions,
                                 const std::vector<Triangle>& triangles,
                                 double particleMass,
                                 double stretchCompliance,
                                 double bendCompliance) -> ClothState
    {
        ClothState state{};
        state.Particles.reserve(positions.size());
        for (const Vec3& position : positions)
        {
            state.Particles.push_back(MakeParticle(position, particleMass));
        }
        state.Triangles = triangles;

        auto restLength = [&](std::size_t a, std::size_t b) -> double
        {
            if (a >= positions.size() || b >= positions.size())
            {
                return 0.0;
            }
            return Length(positions[a] - positions[b]);
        };

        // Unique edges -> structural constraints; interior edges (shared by
        // exactly two triangles) -> bending pair between the opposite
        // vertices. std::map keeps the constraint order deterministic.
        std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> edgeOpposites;
        for (const Triangle& triangle : triangles)
        {
            const std::size_t idx[3] = {triangle.A, triangle.B, triangle.C};
            if (idx[0] == idx[1] || idx[1] == idx[2] || idx[0] == idx[2])
            {
                continue; // repeated-index triangles are rejected by Validate
            }
            for (int e = 0; e < 3; ++e)
            {
                const std::size_t a = idx[e];
                const std::size_t b = idx[(e + 1) % 3];
                const std::size_t opposite = idx[(e + 2) % 3];
                const auto        key = std::minmax(a, b);
                edgeOpposites[{key.first, key.second}].push_back(opposite);
            }
        }

        for (const auto& [edge, opposites] : edgeOpposites)
        {
            state.StretchConstraints.push_back(
                DistanceConstraint{edge.first, edge.second, restLength(edge.first, edge.second), stretchCompliance});
            if (opposites.size() == 2 && opposites[0] != opposites[1])
            {
                const auto pair = std::minmax(opposites[0], opposites[1]);
                state.BendConstraints.push_back(
                    DistanceConstraint{pair.first, pair.second, restLength(pair.first, pair.second), bendCompliance});
            }
        }
        return state;
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

    auto Validate(const DistanceConstraint& constraint, std::size_t particleCount) -> ValidationCode
    {
        if (constraint.ParticleA >= particleCount || constraint.ParticleB >= particleCount ||
            constraint.ParticleA == constraint.ParticleB)
        {
            return ValidationCode::InvalidConstraint;
        }
        if (!std::isfinite(constraint.RestLength) || constraint.RestLength < 0.0)
        {
            return ValidationCode::InvalidConstraint;
        }
        if (!std::isfinite(constraint.Compliance) || constraint.Compliance < 0.0)
        {
            return ValidationCode::InvalidConstraint;
        }
        return ValidationCode::Valid;
    }

    auto Validate(const Collider& collider) -> ValidationCode
    {
        if (!IsFinite(collider.Normal) || !std::isfinite(collider.Offset) ||
            !IsFinite(collider.Center) || !std::isfinite(collider.Radius))
        {
            return ValidationCode::InvalidCollider;
        }
        if (collider.Kind == ColliderKind::HalfSpace &&
            (Length(collider.Normal) < kDegenerateLengthEpsilon ||
             !std::isfinite(Dot(collider.Normal, collider.Normal))))
        {
            return ValidationCode::InvalidCollider;
        }
        return ValidationCode::Valid;
    }

    auto Validate(const ClothState& state, const StepParams& params) -> ValidationCode
    {
        if (!std::isfinite(params.DeltaTime) || params.DeltaTime <= 0.0)
        {
            return ValidationCode::InvalidTimeStep;
        }
        if (params.Iterations <= 0)
        {
            return ValidationCode::InvalidIterations;
        }
        if (!IsFinite(params.Gravity) || !std::isfinite(params.GlobalDamping) ||
            !std::isfinite(params.ResidualTolerance))
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
        for (const Triangle& triangle : state.Triangles)
        {
            if (triangle.A >= state.Particles.size() || triangle.B >= state.Particles.size() ||
                triangle.C >= state.Particles.size() || triangle.A == triangle.B ||
                triangle.B == triangle.C || triangle.A == triangle.C)
            {
                return ValidationCode::InvalidTopology;
            }
        }
        for (const DistanceConstraint& constraint : state.StretchConstraints)
        {
            const ValidationCode code = Validate(constraint, state.Particles.size());
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        for (const DistanceConstraint& constraint : state.BendConstraints)
        {
            const ValidationCode code = Validate(constraint, state.Particles.size());
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        for (const Collider& collider : params.Colliders)
        {
            const ValidationCode code = Validate(collider);
            if (code != ValidationCode::Valid)
            {
                return code;
            }
        }
        return ValidationCode::Valid;
    }

    auto ComputeKineticEnergy(const ClothState& state) -> double
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

    auto ComputeMechanicalEnergy(const ClothState& state, const StepParams& params) -> double
    {
        double energy = ComputeKineticEnergy(state);
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

    auto Step(const ClothState& state, const StepParams& params) -> StepResult
    {
        StepResult result{};
        result.State = state;
        result.Diagnostics.ParticleCount = state.Particles.size();
        result.Diagnostics.TriangleCount = state.Triangles.size();
        result.Diagnostics.StretchConstraintCount = state.StretchConstraints.size();
        result.Diagnostics.BendConstraintCount = state.BendConstraints.size();
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
            result.Diagnostics.Converged = false;
            return result;
        }

        for (const Collider& collider : params.Colliders)
        {
            if (collider.Kind != ColliderKind::HalfSpace)
            {
                ++result.Diagnostics.UnsupportedColliderCount;
            }
        }
        for (const Triangle& triangle : state.Triangles)
        {
            const Vec3 ab = state.Particles[triangle.B].Position - state.Particles[triangle.A].Position;
            const Vec3 ac = state.Particles[triangle.C].Position - state.Particles[triangle.A].Position;
            const Vec3 crossProduct = Cross(ab, ac);
            if (Dot(crossProduct, crossProduct) < kDegenerateAreaEpsilon)
            {
                ++result.Diagnostics.DegenerateTriangleCount;
            }
        }

        result.Diagnostics.KineticEnergyBefore = ComputeKineticEnergy(state);
        result.Diagnostics.MechanicalEnergyBefore = ComputeMechanicalEnergy(state, params);

        const double dt = params.DeltaTime;

        // Predict positions (semi-implicit velocity, then position).
        std::vector<Vec3> previousPositions;
        previousPositions.reserve(result.State.Particles.size());
        for (ParticleState& particle : result.State.Particles)
        {
            previousPositions.push_back(particle.Position);
            if (particle.InverseMass <= 0.0)
            {
                continue;
            }
            particle.Velocity = particle.Velocity + params.Gravity * dt;
            particle.Position = particle.Position + particle.Velocity * dt;
        }

        // XPBD iterations with per-constraint Lagrange multipliers.
        std::vector<double> stretchLambdas(state.StretchConstraints.size(), 0.0);
        std::vector<double> bendLambdas(state.BendConstraints.size(), 0.0);
        for (int iteration = 0; iteration < params.Iterations; ++iteration)
        {
            const bool countDegenerate = iteration == 0;
            for (std::size_t i = 0; i < state.StretchConstraints.size(); ++i)
            {
                SolveDistanceConstraint(result.State.Particles, state.StretchConstraints[i], dt,
                                        stretchLambdas[i], countDegenerate,
                                        result.Diagnostics.DegenerateConstraintCount);
            }
            for (std::size_t i = 0; i < state.BendConstraints.size(); ++i)
            {
                SolveDistanceConstraint(result.State.Particles, state.BendConstraints[i], dt,
                                        bendLambdas[i], countDegenerate,
                                        result.Diagnostics.DegenerateConstraintCount);
            }
            ProjectColliders(result.State.Particles, params.Colliders);
        }
        result.Diagnostics.IterationsUsed = params.Iterations;

        // Velocities from position change (PBD), then global damping.
        const double globalDampingFactor = DampingFactor(params.GlobalDamping, dt);
        for (std::size_t i = 0; i < result.State.Particles.size(); ++i)
        {
            ParticleState& particle = result.State.Particles[i];
            if (particle.InverseMass <= 0.0)
            {
                continue;
            }
            particle.Velocity = (particle.Position - previousPositions[i]) / dt;
            particle.Velocity = particle.Velocity * globalDampingFactor;
        }

        if (!AllFinite(result.State))
        {
            // Fail closed: callers never observe non-finite cloth state.
            result.State = state;
            result.Diagnostics.Code = ValidationCode::NonFiniteState;
            result.Diagnostics.Stable = false;
            result.Diagnostics.FallbackApplied = true;
            result.Diagnostics.Converged = false;
            AccumulateResiduals(result.State, result.Diagnostics);
            return result;
        }

        AccumulateResiduals(result.State, result.Diagnostics);
        result.Diagnostics.Converged =
            std::max(result.Diagnostics.MaxStretchResidual, result.Diagnostics.MaxBendResidual) <=
            params.ResidualTolerance;
        result.Diagnostics.KineticEnergyAfter = ComputeKineticEnergy(result.State);
        result.Diagnostics.MechanicalEnergyAfter = ComputeMechanicalEnergy(result.State, params);
        result.Diagnostics.EnergyDrift =
            result.Diagnostics.MechanicalEnergyAfter - result.Diagnostics.MechanicalEnergyBefore;
        return result;
    }
} // namespace Intrinsic::Methods::Physics::XpbdClothReference
