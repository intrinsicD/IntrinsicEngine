// METHOD-011 — SPH particle fluid CPU reference backend tests.
//
// Pins the Mueller 2003 kernel closed forms and normalization, uniform-grid
// density recovery, exact symmetric-pair momentum conservation, viscosity
// smoothing, the toy fluid-column drop with boundary planes, neighbor
// overflow reporting, invalid-input validation, instability fail-closed
// fallback, and repeated-step determinism.
#include "SphFluidReference.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace Sph = Intrinsic::Methods::Physics::SphFluidReference;

namespace
{
    constexpr double kPi = 3.141592653589793238462643383279502884;

    // Uniform cubic lattice with spacing chosen so particle mass equals
    // rest density * spacing^3 and h = 2 * spacing.
    [[nodiscard]] auto UniformGrid(std::size_t side, double spacing) -> std::vector<Sph::ParticleState>
    {
        std::vector<Sph::ParticleState> particles;
        particles.reserve(side * side * side);
        for (std::size_t z = 0; z < side; ++z)
        {
            for (std::size_t y = 0; y < side; ++y)
            {
                for (std::size_t x = 0; x < side; ++x)
                {
                    Sph::ParticleState particle{};
                    particle.Position = Sph::Vec3{static_cast<double>(x) * spacing,
                                                  static_cast<double>(y) * spacing,
                                                  static_cast<double>(z) * spacing};
                    particles.push_back(particle);
                }
            }
        }
        return particles;
    }

    [[nodiscard]] auto GridParams(double spacing) -> Sph::StepParams
    {
        Sph::StepParams params{};
        params.SmoothingLength = 2.0 * spacing;
        params.RestDensity = 1000.0;
        params.ParticleMass = params.RestDensity * spacing * spacing * spacing;
        params.Gravity = Sph::Vec3{};
        params.DeltaTime = 1.0 / 240.0;
        return params;
    }

    void ExpectParticlesEqual(const std::vector<Sph::ParticleState>& actual,
                              const std::vector<Sph::ParticleState>& expected)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            EXPECT_EQ(actual[i].Position.X, expected[i].Position.X);
            EXPECT_EQ(actual[i].Position.Y, expected[i].Position.Y);
            EXPECT_EQ(actual[i].Position.Z, expected[i].Position.Z);
            EXPECT_EQ(actual[i].Velocity.X, expected[i].Velocity.X);
            EXPECT_EQ(actual[i].Velocity.Y, expected[i].Velocity.Y);
            EXPECT_EQ(actual[i].Velocity.Z, expected[i].Velocity.Z);
        }
    }
}

TEST(SphFluidReference, Poly6KernelMatchesClosedFormAndSupport)
{
    const double h = 0.2;
    // W(0) = 315 / (64 pi h^3) for the poly6 kernel.
    EXPECT_NEAR(Sph::Poly6Kernel(0.0, h), 315.0 / (64.0 * kPi * h * h * h), 1.0e-9);
    EXPECT_EQ(Sph::Poly6Kernel(h, h), 0.0);
    EXPECT_EQ(Sph::Poly6Kernel(2.0 * h, h), 0.0);
    EXPECT_GT(Sph::Poly6Kernel(0.5 * h, h), 0.0);
}

TEST(SphFluidReference, Poly6KernelIntegratesToOneOverSupport)
{
    // Radial integration: int_0^h W(r) 4 pi r^2 dr == 1 (midpoint rule).
    const double h = 0.13;
    const int    samples = 20000;
    const double dr = h / samples;
    double       integral = 0.0;
    for (int i = 0; i < samples; ++i)
    {
        const double r = (i + 0.5) * dr;
        integral += Sph::Poly6Kernel(r, h) * 4.0 * kPi * r * r * dr;
    }
    EXPECT_NEAR(integral, 1.0, 1.0e-4);
}

TEST(SphFluidReference, SpikyAndViscosityKernelsMatchClosedForms)
{
    const double h = 0.2;
    const double r = 0.5 * h;
    const double h6 = h * h * h * h * h * h;
    EXPECT_NEAR(Sph::SpikyGradientMagnitude(r, h), 45.0 / (kPi * h6) * (h - r) * (h - r), 1.0e-9);
    EXPECT_NEAR(Sph::ViscosityLaplacian(r, h), 45.0 / (kPi * h6) * (h - r), 1.0e-9);
    EXPECT_EQ(Sph::SpikyGradientMagnitude(h, h), 0.0);
    EXPECT_EQ(Sph::ViscosityLaplacian(h, h), 0.0);
}

TEST(SphFluidReference, UniformGridInteriorDensityRecoversRestDensity)
{
    constexpr double kSpacing = 0.05;
    const auto       particles = UniformGrid(5, kSpacing);
    const auto       params = GridParams(kSpacing);

    const std::vector<double> densities = Sph::ComputeDensities(particles, params);
    // Center particle of the 5x5x5 grid: index (2,2,2).
    const std::size_t center = (2 * 5 + 2) * 5 + 2;
    EXPECT_NEAR(densities[center], params.RestDensity, 0.05 * params.RestDensity);
}

TEST(SphFluidReference, SymmetricPairConservesMomentumExactly)
{
    // Two identical particles inside each other's support, approaching:
    // densities and pressures are bitwise equal by symmetry, so the
    // symmetrized pressure and viscosity forces cancel exactly.
    std::vector<Sph::ParticleState> particles(2);
    particles[0].Position = Sph::Vec3{-0.02, 0.0, 0.0};
    particles[1].Position = Sph::Vec3{0.02, 0.0, 0.0};
    particles[0].Velocity = Sph::Vec3{0.5, 0.0, 0.0};
    particles[1].Velocity = Sph::Vec3{-0.5, 0.0, 0.0};

    Sph::StepParams params{};
    params.SmoothingLength = 0.1;
    params.ParticleMass = 0.5; // forces a positive compression so pressure acts
    params.RestDensity = 1000.0;
    params.Stiffness = 50.0;
    params.Viscosity = 0.1;
    params.Gravity = Sph::Vec3{};
    params.DeltaTime = 1.0 / 240.0;

    for (int i = 0; i < 20; ++i)
    {
        const Sph::StepResult result = Sph::Step(particles, params);
        ASSERT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);
        particles = result.Particles;
    }

    const Sph::Vec3 momentum = particles[0].Velocity + particles[1].Velocity;
    EXPECT_NEAR(Sph::Length(momentum), 0.0, 1.0e-12);
}

TEST(SphFluidReference, ViscosityReducesRelativeVelocity)
{
    std::vector<Sph::ParticleState> particles(2);
    particles[0].Position = Sph::Vec3{-0.02, 0.0, 0.0};
    particles[1].Position = Sph::Vec3{0.02, 0.0, 0.0};
    particles[0].Velocity = Sph::Vec3{-1.0, 0.0, 0.0};
    particles[1].Velocity = Sph::Vec3{1.0, 0.0, 0.0};

    Sph::StepParams params{};
    params.SmoothingLength = 0.1;
    params.ParticleMass = 0.001;
    params.RestDensity = 1000.0;
    params.Stiffness = 0.0; // isolate viscosity
    params.Viscosity = 0.5;
    params.Gravity = Sph::Vec3{};
    params.DeltaTime = 1.0 / 240.0;

    const Sph::StepResult result = Sph::Step(particles, params);
    ASSERT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);

    const double relativeBefore = 2.0;
    const double relativeAfter =
        Sph::Length(result.Particles[1].Velocity - result.Particles[0].Velocity);
    EXPECT_LT(relativeAfter, relativeBefore);
    const Sph::Vec3 momentum = result.Particles[0].Velocity + result.Particles[1].Velocity;
    EXPECT_NEAR(Sph::Length(momentum), 0.0, 1.0e-12);
}

TEST(SphFluidReference, SingleParticleFreeFallsWithZeroPressure)
{
    // An isolated particle is far below rest density, so the pressure clamp
    // keeps it force-free and gravity integration matches the closed form.
    std::vector<Sph::ParticleState> particles(1);

    Sph::StepParams params{};
    params.SmoothingLength = 0.1;
    params.ParticleMass = 0.001;
    params.RestDensity = 1000.0;
    params.DeltaTime = 0.1;
    params.Gravity = Sph::Vec3{0.0, -10.0, 0.0};

    for (int i = 0; i < 10; ++i)
    {
        const Sph::StepResult result = Sph::Step(particles, params);
        ASSERT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);
        particles = result.Particles;
    }

    EXPECT_NEAR(particles[0].Velocity.Y, -10.0, 1.0e-12);
    EXPECT_NEAR(particles[0].Position.Y, -5.5, 1.0e-12);
}

TEST(SphFluidReference, ToyColumnDropStaysStableAboveFloor)
{
    constexpr double kSpacing = 0.05;
    auto             particles = UniformGrid(3, kSpacing);
    for (Sph::ParticleState& particle : particles)
    {
        particle.Position.Y += 0.2; // start the column above the floor
    }

    Sph::StepParams params = GridParams(kSpacing);
    params.Gravity = Sph::Vec3{0.0, -9.80665, 0.0};
    params.Viscosity = 0.2;
    params.Boundaries = {Sph::MakeBoundaryPlane(Sph::Vec3{0.0, 1.0, 0.0}, 0.0)};

    Sph::Diagnostics lastDiagnostics{};
    for (int i = 0; i < 60; ++i)
    {
        const Sph::StepResult result = Sph::Step(particles, params);
        ASSERT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);
        ASSERT_TRUE(result.Diagnostics.Stable);
        lastDiagnostics = result.Diagnostics;
        particles = result.Particles;
        for (const Sph::ParticleState& particle : particles)
        {
            EXPECT_GE(particle.Position.Y, -1.0e-9);
        }
    }

    EXPECT_EQ(lastDiagnostics.ParticleCount, 27u);
    EXPECT_NEAR(lastDiagnostics.TotalMass, params.ParticleMass * 27.0, 1.0e-15);
    EXPECT_GT(lastDiagnostics.AverageDensity, 0.0);
    EXPECT_GT(lastDiagnostics.MaxNeighborCount, 0u);
    EXPECT_GE(lastDiagnostics.MaxCompression, 0.0);
}

TEST(SphFluidReference, BoundaryPlaneWithNonUnitNormalProjectsAndReflectsExactly)
{
    // Directly-authored planes pass Validate with any finite non-zero
    // normal; projection and the restitution reflection must scale by
    // dot(N, N) (review finding on METHOD-011: {0,2,0}/0 used to project
    // y=-1 to y=3 and over-reflect the normal velocity).
    std::vector<Sph::ParticleState> particles(1);
    particles[0].Position = Sph::Vec3{0.0, -1.0, 0.0};
    particles[0].Velocity = Sph::Vec3{0.0, -3.0, 0.0};

    Sph::StepParams params{};
    params.SmoothingLength = 0.1;
    params.ParticleMass = 0.001;
    params.RestDensity = 1000.0;
    params.Stiffness = 0.0;
    params.Gravity = Sph::Vec3{};
    params.DeltaTime = 0.01;
    params.Boundaries = {Sph::BoundaryPlane{Sph::Vec3{0.0, 2.0, 0.0}, 0.0}};

    const Sph::StepResult inelastic = Sph::Step(particles, params);
    ASSERT_EQ(inelastic.Diagnostics.Code, Sph::ValidationCode::Valid);
    EXPECT_NEAR(inelastic.Particles[0].Position.Y, 0.0, 1.0e-12);
    EXPECT_NEAR(inelastic.Particles[0].Velocity.Y, 0.0, 1.0e-12);

    params.BoundaryRestitution = 1.0;
    const Sph::StepResult bouncy = Sph::Step(particles, params);
    ASSERT_EQ(bouncy.Diagnostics.Code, Sph::ValidationCode::Valid);
    EXPECT_NEAR(bouncy.Particles[0].Position.Y, 0.0, 1.0e-12);
    EXPECT_NEAR(bouncy.Particles[0].Velocity.Y, 3.0, 1.0e-12);
}

TEST(SphFluidReference, NeighborOverflowReportedAdvisory)
{
    std::vector<Sph::ParticleState> particles(3);
    particles[0].Position = Sph::Vec3{0.0, 0.0, 0.0};
    particles[1].Position = Sph::Vec3{0.01, 0.0, 0.0};
    particles[2].Position = Sph::Vec3{0.0, 0.01, 0.0};

    Sph::StepParams params{};
    params.SmoothingLength = 0.1;
    params.ParticleMass = 0.001;
    params.RestDensity = 1000.0;
    params.Gravity = Sph::Vec3{};
    params.MaxNeighborLimit = 1;

    const Sph::StepResult result = Sph::Step(particles, params);
    ASSERT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.MaxNeighborCount, 2u);
    EXPECT_EQ(result.Diagnostics.NeighborOverflowCount, 3u);
}

TEST(SphFluidReference, InvalidInputsFailClosedWithExplicitCodes)
{
    std::vector<Sph::ParticleState> particles(1);
    Sph::StepParams                 valid{};
    valid.Gravity = Sph::Vec3{};

    {
        Sph::StepParams params = valid;
        params.DeltaTime = 0.0;
        const Sph::StepResult result = Sph::Step(particles, params);
        EXPECT_EQ(result.Diagnostics.Code, Sph::ValidationCode::InvalidTimeStep);
        ExpectParticlesEqual(result.Particles, particles);
    }
    {
        Sph::StepParams params = valid;
        params.SmoothingLength = 0.0;
        EXPECT_EQ(Sph::Step(particles, params).Diagnostics.Code, Sph::ValidationCode::InvalidParams);
    }
    {
        Sph::StepParams params = valid;
        params.ParticleMass = -1.0;
        EXPECT_EQ(Sph::Step(particles, params).Diagnostics.Code, Sph::ValidationCode::InvalidParams);
    }
    {
        Sph::StepParams params = valid;
        params.RestDensity = 0.0;
        EXPECT_EQ(Sph::Step(particles, params).Diagnostics.Code, Sph::ValidationCode::InvalidParams);
    }
    {
        Sph::StepParams params = valid;
        params.Viscosity = -0.1;
        EXPECT_EQ(Sph::Step(particles, params).Diagnostics.Code, Sph::ValidationCode::InvalidParams);
    }
    {
        Sph::StepParams params = valid;
        params.BoundaryRestitution = 1.5;
        EXPECT_EQ(Sph::Step(particles, params).Diagnostics.Code, Sph::ValidationCode::InvalidParams);
    }
    {
        Sph::StepParams params = valid;
        params.Boundaries.push_back(Sph::BoundaryPlane{Sph::Vec3{}, 0.0}); // zero normal
        EXPECT_EQ(Sph::Step(particles, params).Diagnostics.Code, Sph::ValidationCode::InvalidBoundary);
    }
    {
        std::vector<Sph::ParticleState> badParticles(1);
        badParticles[0].Position.X = std::numeric_limits<double>::quiet_NaN();
        EXPECT_EQ(Sph::Step(badParticles, valid).Diagnostics.Code, Sph::ValidationCode::InvalidParticle);
    }
}

TEST(SphFluidReference, RepeatedStepsAreDeterministic)
{
    constexpr double kSpacing = 0.05;
    const auto       initial = UniformGrid(3, kSpacing);

    Sph::StepParams params = GridParams(kSpacing);
    params.Gravity = Sph::Vec3{0.0, -9.80665, 0.0};
    params.Viscosity = 0.1;
    params.Boundaries = {Sph::MakeBoundaryPlane(Sph::Vec3{0.0, 1.0, 0.0}, -0.1)};

    auto run = [&]() -> std::vector<Sph::ParticleState>
    {
        std::vector<Sph::ParticleState> particles = initial;
        for (int i = 0; i < 30; ++i)
        {
            const Sph::StepResult result = Sph::Step(particles, params);
            EXPECT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);
            particles = result.Particles;
        }
        return particles;
    };

    const auto first = run();
    const auto second = run();
    ExpectParticlesEqual(first, second);
}

TEST(SphFluidReference, NonFiniteExplosionFailsClosedToInputState)
{
    // Compressed pair with an overflow-inducing (but finite) gas constant:
    // the pressure force overflows and the step must fail closed.
    std::vector<Sph::ParticleState> particles(2);
    particles[0].Position = Sph::Vec3{0.0, 0.0, 0.0};
    particles[1].Position = Sph::Vec3{0.01, 0.0, 0.0};

    Sph::StepParams params{};
    params.SmoothingLength = 0.1;
    params.ParticleMass = 1.0; // strongly over rest density
    params.RestDensity = 1000.0;
    params.Stiffness = 1.0e306;
    params.Gravity = Sph::Vec3{};

    const Sph::StepResult result = Sph::Step(particles, params);
    EXPECT_EQ(result.Diagnostics.Code, Sph::ValidationCode::NonFiniteState);
    EXPECT_TRUE(result.Diagnostics.FallbackApplied);
    EXPECT_FALSE(result.Diagnostics.Stable);
    ExpectParticlesEqual(result.Particles, particles);
}

TEST(SphFluidReference, DiagnosticsPopulatedOnNominalStep)
{
    constexpr double kSpacing = 0.05;
    const auto       particles = UniformGrid(3, kSpacing);
    Sph::StepParams  params = GridParams(kSpacing);
    params.Gravity = Sph::Vec3{0.0, -9.80665, 0.0};

    const Sph::StepResult result = Sph::Step(particles, params);
    ASSERT_EQ(result.Diagnostics.Code, Sph::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.ParticleCount, 27u);
    EXPECT_GT(result.Diagnostics.AverageDensity, 0.0);
    EXPECT_GT(result.Diagnostics.MaxDensity, result.Diagnostics.MinDensity);
    EXPECT_GE(result.Diagnostics.AverageDensityError, 0.0);
    EXPECT_GT(result.Diagnostics.MaxVelocity, 0.0);
    EXPECT_TRUE(result.Diagnostics.Stable);
    EXPECT_FALSE(result.Diagnostics.FallbackApplied);
}
