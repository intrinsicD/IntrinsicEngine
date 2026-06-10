#pragma once

#include <cstddef>
#include <vector>

namespace Intrinsic::Methods::Physics::ParticleSpringReference
{
    inline constexpr const char* kMethodId = "physics.particle_spring_reference";
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
        InvalidParticle,
        InvalidSpring,
        NonFiniteState,
    };

    // Inverse mass of 0 pins the particle: it never integrates, and forces on
    // it are discarded. Pinned particles may still be moved by the caller
    // between steps (kinematic authoring).
    struct ParticleState
    {
        Vec3   Position{};
        Vec3   Velocity{};
        double InverseMass{1.0};
    };

    // Hooke spring with axial damping (Provot-style):
    //   F_on_A = (k * (|d| - L0) + c * dot(v_B - v_A, dhat)) * dhat
    //   F_on_B = -F_on_A,  d = x_B - x_A, dhat = d / |d|
    struct SpringRecord
    {
        std::size_t ParticleA{0};
        std::size_t ParticleB{0};
        double      RestLength{1.0};
        double      Stiffness{0.0};
        double      Damping{0.0};
    };

    struct SystemState
    {
        std::vector<ParticleState> Particles{};
        std::vector<SpringRecord>  Springs{};
    };

    struct StepParams
    {
        double DeltaTime{1.0 / 60.0};
        Vec3   Gravity{0.0, -9.80665, 0.0};
        double GlobalDamping{0.0}; // velocity *= max(0, 1 - GlobalDamping * dt)
    };

    struct Diagnostics
    {
        ValidationCode Code{ValidationCode::Valid};
        std::size_t    ParticleCount{0};
        std::size_t    SpringCount{0};
        std::size_t    PinnedParticleCount{0};
        std::size_t    DegenerateSpringCount{0};
        double         MaxSpringResidual{0.0}; // max |length - rest| after step
        double         SpringResidualL2{0.0};  // L2 norm of post-step residuals
        double         KineticEnergyBefore{0.0};
        double         KineticEnergyAfter{0.0};
        double         TotalEnergyBefore{0.0}; // kinetic + elastic + gravitational
        double         TotalEnergyAfter{0.0};
        double         EnergyDrift{0.0};       // TotalEnergyAfter - TotalEnergyBefore
        double         MaxStiffnessDtRatio{0.0}; // max over springs of omega * dt
        bool           StabilityLimitExceeded{false}; // omega * dt >= 2 for some spring
        bool           FallbackApplied{false}; // post-step state was non-finite; input returned
        bool           Stable{true};
    };

    struct StepResult
    {
        SystemState State{};
        Diagnostics Diagnostics{};
    };

    [[nodiscard]] auto operator+(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator-(Vec3 lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator-(Vec3 value) -> Vec3;
    [[nodiscard]] auto operator*(Vec3 lhs, double rhs) -> Vec3;
    [[nodiscard]] auto operator*(double lhs, Vec3 rhs) -> Vec3;
    [[nodiscard]] auto operator/(Vec3 lhs, double rhs) -> Vec3;

    [[nodiscard]] auto Dot(Vec3 lhs, Vec3 rhs) -> double;
    [[nodiscard]] auto Length(Vec3 value) -> double;
    [[nodiscard]] auto IsFinite(Vec3 value) -> bool;

    [[nodiscard]] auto MakeParticle(Vec3 position, double mass) -> ParticleState;
    [[nodiscard]] auto MakePinnedParticle(Vec3 position) -> ParticleState;
    [[nodiscard]] auto MakeSpring(std::size_t particleA,
                                  std::size_t particleB,
                                  double restLength,
                                  double stiffness,
                                  double damping = 0.0) -> SpringRecord;

    [[nodiscard]] auto Validate(const ParticleState& particle) -> ValidationCode;
    [[nodiscard]] auto Validate(const SpringRecord& spring, std::size_t particleCount) -> ValidationCode;
    [[nodiscard]] auto Validate(const SystemState& state, const StepParams& params) -> ValidationCode;

    [[nodiscard]] auto ComputeKineticEnergy(const SystemState& state) -> double;
    [[nodiscard]] auto ComputeTotalEnergy(const SystemState& state, const StepParams& params) -> double;

    [[nodiscard]] auto Step(const SystemState& state, const StepParams& params) -> StepResult;
} // namespace Intrinsic::Methods::Physics::ParticleSpringReference
