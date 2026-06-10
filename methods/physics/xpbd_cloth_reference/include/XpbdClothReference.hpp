#pragma once

#include <cstddef>
#include <vector>

namespace Intrinsic::Methods::Physics::XpbdClothReference
{
    inline constexpr const char* kMethodId = "physics.xpbd_cloth_reference";
    inline constexpr const char* kBackendId = "cpu_reference";

    struct Vec3
    {
        double X{0.0};
        double Y{0.0};
        double Z{0.0};
    };

    enum class ValidationCode
    {
        Valid,
        InvalidTimeStep,
        InvalidIterations,
        InvalidParticle,
        InvalidConstraint,
        InvalidTopology,
        InvalidCollider,
        NonFiniteState,
    };

    // Inverse mass of 0 pins the particle: it never integrates, ignores
    // constraint corrections, and may be repositioned by the caller between
    // steps (kinematic authoring).
    struct ParticleState
    {
        Vec3   Position{};
        Vec3   Velocity{};
        double InverseMass{1.0};
    };

    struct Triangle
    {
        std::size_t A{0};
        std::size_t B{0};
        std::size_t C{0};
    };

    // XPBD distance constraint: C = |x_A - x_B| - RestLength with compliance
    // alpha (0 = rigid). Used for both structural (edge) and bending
    // (opposite-vertex pair across an interior edge) constraints.
    struct DistanceConstraint
    {
        std::size_t ParticleA{0};
        std::size_t ParticleB{0};
        double      RestLength{1.0};
        double      Compliance{0.0};
    };

    enum class ColliderKind
    {
        HalfSpace, // supported: keeps particles on dot(Normal, x) >= Offset
        Sphere,    // declared input shape, not supported by this reference slice
    };

    struct Collider
    {
        ColliderKind Kind{ColliderKind::HalfSpace};
        Vec3         Normal{0.0, 1.0, 0.0};
        double       Offset{0.0};
        Vec3         Center{};
        double       Radius{0.0};
    };

    struct ClothState
    {
        std::vector<ParticleState>      Particles{};
        std::vector<Triangle>           Triangles{};
        std::vector<DistanceConstraint> StretchConstraints{};
        std::vector<DistanceConstraint> BendConstraints{};
    };

    struct StepParams
    {
        double                DeltaTime{1.0 / 60.0};
        Vec3                  Gravity{0.0, -9.80665, 0.0};
        int                   Iterations{20};
        double                GlobalDamping{0.0};      // velocity *= max(0, 1 - GlobalDamping * dt)
        double                ResidualTolerance{1.0e-3};
        std::vector<Collider> Colliders{};
    };

    struct Diagnostics
    {
        ValidationCode Code{ValidationCode::Valid};
        std::size_t    ParticleCount{0};
        std::size_t    TriangleCount{0};
        std::size_t    StretchConstraintCount{0};
        std::size_t    BendConstraintCount{0};
        std::size_t    PinnedParticleCount{0};
        std::size_t    DegenerateTriangleCount{0};  // zero-area triangles (valid indices)
        std::size_t    DegenerateConstraintCount{0}; // coincident endpoints skipped this step
        std::size_t    UnsupportedColliderCount{0};
        int            IterationsUsed{0};
        bool           Converged{true}; // max residual within ResidualTolerance after solve
        double         MaxStretchResidual{0.0};
        double         StretchResidualL2{0.0};
        double         MaxBendResidual{0.0};
        double         KineticEnergyBefore{0.0};
        double         KineticEnergyAfter{0.0};
        double         MechanicalEnergyBefore{0.0}; // kinetic + gravitational (no elastic term)
        double         MechanicalEnergyAfter{0.0};
        double         EnergyDrift{0.0};
        bool           FallbackApplied{false};
        bool           Stable{true};
    };

    struct StepResult
    {
        ClothState  State{};
        Diagnostics Diagnostics{};
    };

    [[nodiscard]] auto operator+(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator-(Vec3 value) -> Vec3;
    [[nodiscard]] auto operator*(Vec3 lhs, double rhs) -> Vec3;
    [[nodiscard]] auto operator*(double lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator/(Vec3 lhs, double rhs) -> Vec3;

    [[nodiscard]] auto Dot(Vec3 lhs, Vec3 rhs) -> double;
    [[nodiscard]] auto Cross(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto Length(Vec3 value) -> double;
    [[nodiscard]] auto IsFinite(Vec3 value) -> bool;

    [[nodiscard]] auto MakeParticle(Vec3 position, double mass) -> ParticleState;
    [[nodiscard]] auto MakePinnedParticle(Vec3 position) -> ParticleState;
    [[nodiscard]] auto MakeHalfSpaceCollider(Vec3 normal, double offset) -> Collider;
    [[nodiscard]] auto MakeSphereCollider(Vec3 center, double radius) -> Collider;

    // Builds cloth state from triangle topology: uniform particle mass,
    // structural constraints on unique edges, and bending constraints between
    // the opposite vertices of each interior edge (edge shared by exactly two
    // triangles). Rest lengths come from the input positions. Constraint
    // order is deterministic (sorted by index pair).
    [[nodiscard]] auto BuildClothFromTriangles(const std::vector<Vec3>& positions,
                                               const std::vector<Triangle>& triangles,
                                               double particleMass,
                                               double stretchCompliance,
                                               double bendCompliance) -> ClothState;

    [[nodiscard]] auto Validate(const ParticleState& particle) -> ValidationCode;
    [[nodiscard]] auto Validate(const DistanceConstraint& constraint, std::size_t particleCount) -> ValidationCode;
    [[nodiscard]] auto Validate(const Collider& collider) -> ValidationCode;
    [[nodiscard]] auto Validate(const ClothState& state, const StepParams& params) -> ValidationCode;

    [[nodiscard]] auto ComputeKineticEnergy(const ClothState& state) -> double;
    [[nodiscard]] auto ComputeMechanicalEnergy(const ClothState& state, const StepParams& params) -> double;

    [[nodiscard]] auto Step(const ClothState& state, const StepParams& params) -> StepResult;
} // namespace Intrinsic::Methods::Physics::XpbdClothReference
