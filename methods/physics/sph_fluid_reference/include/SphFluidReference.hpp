#pragma once

#include <cstddef>
#include <vector>

namespace Intrinsic::Methods::Physics::SphFluidReference
{
    inline constexpr const char* kMethodId = "physics.sph_fluid_reference";
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
        InvalidParams,
        InvalidParticle,
        InvalidBoundary,
        NonFiniteState,
    };

    struct ParticleState
    {
        Vec3 Position{};
        Vec3 Velocity{};
    };

    // Static half-space boundary: fluid stays on dot(Normal, x) >= Offset.
    struct BoundaryPlane
    {
        Vec3   Normal{0.0, 1.0, 0.0};
        double Offset{0.0};
    };

    struct StepParams
    {
        double DeltaTime{1.0 / 240.0};
        Vec3   Gravity{0.0, -9.80665, 0.0};
        double SmoothingLength{0.1};    // kernel support radius h
        double ParticleMass{0.001};     // uniform mass (kg)
        double RestDensity{1000.0};     // rho_0 (kg/m^3)
        double Stiffness{50.0};         // gas constant k: p = k * (rho - rho_0)
        double Viscosity{0.05};         // dynamic viscosity mu
        double BoundaryRestitution{0.0}; // normal velocity scale on boundary hit
        std::size_t MaxNeighborLimit{0}; // advisory; 0 = unlimited
        std::vector<BoundaryPlane> Boundaries{};
    };

    struct Diagnostics
    {
        ValidationCode Code{ValidationCode::Valid};
        std::size_t    ParticleCount{0};
        double         TotalMass{0.0};        // ParticleMass * ParticleCount (conserved)
        double         AverageDensity{0.0};
        double         MinDensity{0.0};
        double         MaxDensity{0.0};
        double         MaxCompression{0.0};   // max(0, (rho - rho_0) / rho_0): incompressibility proxy
        double         AverageDensityError{0.0}; // mean |rho - rho_0| / rho_0
        std::size_t    MaxNeighborCount{0};
        std::size_t    NeighborOverflowCount{0}; // particles above MaxNeighborLimit (advisory)
        double         MaxVelocity{0.0};
        double         KineticEnergyBefore{0.0};
        double         KineticEnergyAfter{0.0};
        double         EnergyDrift{0.0};
        bool           FallbackApplied{false};
        bool           Stable{true};
    };

    struct StepResult
    {
        std::vector<ParticleState> Particles{};
        Diagnostics                Diagnostics{};
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

    // Smoothing kernels (Mueller et al. 2003). Zero outside r >= h.
    [[nodiscard]] auto Poly6Kernel(double r, double h) -> double;
    [[nodiscard]] auto SpikyGradientMagnitude(double r, double h) -> double;
    [[nodiscard]] auto ViscosityLaplacian(double r, double h) -> double;

    [[nodiscard]] auto MakeBoundaryPlane(Vec3 normal, double offset) -> BoundaryPlane;

    [[nodiscard]] auto Validate(const ParticleState& particle) -> ValidationCode;
    [[nodiscard]] auto Validate(const std::vector<ParticleState>& particles, const StepParams& params)
        -> ValidationCode;

    // Density at each particle from the current positions (includes the
    // self-contribution m * W(0)). Deterministic index-ordered accumulation.
    [[nodiscard]] auto ComputeDensities(const std::vector<ParticleState>& particles,
                                        const StepParams& params) -> std::vector<double>;

    [[nodiscard]] auto ComputeKineticEnergy(const std::vector<ParticleState>& particles,
                                            const StepParams& params) -> double;

    [[nodiscard]] auto Step(const std::vector<ParticleState>& particles, const StepParams& params)
        -> StepResult;
} // namespace Intrinsic::Methods::Physics::SphFluidReference
