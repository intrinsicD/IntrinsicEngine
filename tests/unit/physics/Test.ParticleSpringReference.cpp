// METHOD-009 — particle and mass-spring CPU reference backend tests.
//
// Pins the deterministic integrator policy (semi-implicit Euler), analytic
// two-particle spring behavior, exact conservation properties of the
// symmetric force application, pinned-particle and invalid-input handling,
// instability fail-closed fallback, and repeated-step determinism.
#include "ParticleSpringReference.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace Psr = Intrinsic::Methods::Physics::ParticleSpringReference;

namespace
{
    [[nodiscard]] auto NoGravityParams(double dt) -> Psr::StepParams
    {
        Psr::StepParams params{};
        params.DeltaTime = dt;
        params.Gravity = Psr::Vec3{};
        return params;
    }

    [[nodiscard]] auto SymmetricStretchedPair(double stiffness, double damping = 0.0) -> Psr::SystemState
    {
        Psr::SystemState state{};
        state.Particles = {
            Psr::MakeParticle(Psr::Vec3{-1.0, 0.0, 0.0}, 1.0),
            Psr::MakeParticle(Psr::Vec3{1.0, 0.0, 0.0}, 1.0),
        };
        state.Springs = {Psr::MakeSpring(0, 1, 1.5, stiffness, damping)};
        return state;
    }

    void ExpectStateEqual(const Psr::SystemState& actual, const Psr::SystemState& expected)
    {
        ASSERT_EQ(actual.Particles.size(), expected.Particles.size());
        for (std::size_t i = 0; i < actual.Particles.size(); ++i)
        {
            EXPECT_EQ(actual.Particles[i].Position.X, expected.Particles[i].Position.X);
            EXPECT_EQ(actual.Particles[i].Position.Y, expected.Particles[i].Position.Y);
            EXPECT_EQ(actual.Particles[i].Position.Z, expected.Particles[i].Position.Z);
            EXPECT_EQ(actual.Particles[i].Velocity.X, expected.Particles[i].Velocity.X);
            EXPECT_EQ(actual.Particles[i].Velocity.Y, expected.Particles[i].Velocity.Y);
            EXPECT_EQ(actual.Particles[i].Velocity.Z, expected.Particles[i].Velocity.Z);
        }
    }
}

TEST(ParticleSpringReference, FreeFallUsesDeterministicSemiImplicitEuler)
{
    Psr::SystemState state{};
    state.Particles = {Psr::MakeParticle(Psr::Vec3{}, 2.0)};

    Psr::StepParams params{};
    params.DeltaTime = 0.1;
    params.Gravity = Psr::Vec3{0.0, -10.0, 0.0};

    for (int i = 0; i < 10; ++i)
    {
        const Psr::StepResult result = Psr::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
        state = result.State;
    }

    // Semi-implicit Euler closed form: v_n = g*dt*n, x_n = g*dt^2 * n(n+1)/2.
    EXPECT_NEAR(state.Particles[0].Velocity.Y, -10.0, 1.0e-12);
    EXPECT_NEAR(state.Particles[0].Position.Y, -5.5, 1.0e-12);
}

TEST(ParticleSpringReference, RestLengthSpringProducesNoMotion)
{
    Psr::SystemState state{};
    state.Particles = {
        Psr::MakeParticle(Psr::Vec3{0.0, 0.0, 0.0}, 1.0),
        Psr::MakeParticle(Psr::Vec3{1.5, 0.0, 0.0}, 1.0),
    };
    state.Springs = {Psr::MakeSpring(0, 1, 1.5, 250.0, 5.0)};

    const Psr::StepResult result = Psr::Step(state, NoGravityParams(0.01));
    ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
    ExpectStateEqual(result.State, state);
    EXPECT_EQ(result.Diagnostics.MaxSpringResidual, 0.0);
    EXPECT_EQ(result.Diagnostics.EnergyDrift, 0.0);
}

TEST(ParticleSpringReference, StretchedSpringPullsParticlesTogetherSymmetrically)
{
    const Psr::SystemState state = SymmetricStretchedPair(50.0);
    const Psr::StepResult  result = Psr::Step(state, NoGravityParams(0.01));

    ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
    EXPECT_GT(result.State.Particles[0].Velocity.X, 0.0);
    EXPECT_LT(result.State.Particles[1].Velocity.X, 0.0);
    EXPECT_EQ(result.State.Particles[0].Velocity.X, -result.State.Particles[1].Velocity.X);
    EXPECT_LT(result.Diagnostics.MaxSpringResidual, 0.5); // moved toward rest from |ext| = 0.5
}

TEST(ParticleSpringReference, TwoParticleSpringConservesMomentumAndCenterOfMass)
{
    Psr::SystemState state = SymmetricStretchedPair(100.0);

    const Psr::StepParams params = NoGravityParams(0.001);
    for (int i = 0; i < 500; ++i)
    {
        const Psr::StepResult result = Psr::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
        state = result.State;
    }

    const Psr::Vec3 momentum = state.Particles[0].Velocity + state.Particles[1].Velocity;
    const Psr::Vec3 centerOfMass = state.Particles[0].Position + state.Particles[1].Position;
    EXPECT_NEAR(Psr::Length(momentum), 0.0, 1.0e-14);
    EXPECT_NEAR(Psr::Length(centerOfMass), 0.0, 1.0e-14);
}

TEST(ParticleSpringReference, HarmonicOscillatorEnergyStaysBounded)
{
    Psr::SystemState      state = SymmetricStretchedPair(100.0);
    const Psr::StepParams params = NoGravityParams(0.001);

    const double initialEnergy = Psr::ComputeTotalEnergy(state, params);
    ASSERT_GT(initialEnergy, 0.0); // stretched spring stores elastic energy

    // ~2.25 periods at omega = sqrt(2 * k / m) ≈ 14.14 rad/s.
    for (int i = 0; i < 1000; ++i)
    {
        const Psr::StepResult result = Psr::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
        ASSERT_TRUE(result.Diagnostics.Stable);
        state = result.State;
    }

    const double finalEnergy = Psr::ComputeTotalEnergy(state, params);
    EXPECT_NEAR(finalEnergy, initialEnergy, 0.05 * initialEnergy);
}

TEST(ParticleSpringReference, DampedHangingSpringReachesAnalyticEquilibrium)
{
    // Pinned anchor with a unit mass hanging on a spring: the analytic
    // equilibrium extension is m*g/k below the rest length.
    constexpr double kStiffness = 100.0;
    constexpr double kGravityMagnitude = 10.0;
    constexpr double kRestLength = 1.0;

    Psr::SystemState state{};
    state.Particles = {
        Psr::MakePinnedParticle(Psr::Vec3{0.0, 0.0, 0.0}),
        Psr::MakeParticle(Psr::Vec3{0.0, -kRestLength, 0.0}, 1.0),
    };
    state.Springs = {Psr::MakeSpring(0, 1, kRestLength, kStiffness, 2.0)};

    Psr::StepParams params{};
    params.DeltaTime = 0.005;
    params.Gravity = Psr::Vec3{0.0, -kGravityMagnitude, 0.0};

    for (int i = 0; i < 4000; ++i)
    {
        const Psr::StepResult result = Psr::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
        state = result.State;
    }

    const double expectedLength = kRestLength + kGravityMagnitude / kStiffness;
    EXPECT_NEAR(-state.Particles[1].Position.Y, expectedLength, 1.0e-3);
    EXPECT_NEAR(Psr::Length(state.Particles[1].Velocity), 0.0, 1.0e-3);
}

TEST(ParticleSpringReference, PinnedParticleNeverMovesAndDiscardsForces)
{
    Psr::SystemState state{};
    state.Particles = {
        Psr::MakePinnedParticle(Psr::Vec3{0.0, 2.0, 0.0}),
        Psr::MakeParticle(Psr::Vec3{0.0, 0.0, 0.0}, 1.0),
    };
    state.Springs = {Psr::MakeSpring(0, 1, 1.0, 80.0)};

    Psr::StepParams params{};
    params.DeltaTime = 0.01;
    params.Gravity = Psr::Vec3{0.0, -9.80665, 0.0};

    for (int i = 0; i < 25; ++i)
    {
        const Psr::StepResult result = Psr::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
        EXPECT_EQ(result.Diagnostics.PinnedParticleCount, 1u);
        state = result.State;
    }

    EXPECT_EQ(state.Particles[0].Position.X, 0.0);
    EXPECT_EQ(state.Particles[0].Position.Y, 2.0);
    EXPECT_EQ(state.Particles[0].Position.Z, 0.0);
    EXPECT_NE(state.Particles[1].Position.Y, 0.0); // free particle moved
}

TEST(ParticleSpringReference, RepeatedStepsAreDeterministic)
{
    const Psr::SystemState initial = SymmetricStretchedPair(120.0, 1.5);

    Psr::StepParams params{};
    params.DeltaTime = 0.004;
    params.Gravity = Psr::Vec3{0.3, -9.0, 0.1};
    params.GlobalDamping = 0.05;

    auto run = [&]() -> Psr::SystemState
    {
        Psr::SystemState state = initial;
        for (int i = 0; i < 50; ++i)
        {
            const Psr::StepResult result = Psr::Step(state, params);
            EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
            state = result.State;
        }
        return state;
    };

    const Psr::SystemState first = run();
    const Psr::SystemState second = run();
    ExpectStateEqual(first, second);
}

TEST(ParticleSpringReference, CoincidentParticlesReportDegenerateSpring)
{
    Psr::SystemState state{};
    state.Particles = {
        Psr::MakeParticle(Psr::Vec3{1.0, 1.0, 1.0}, 1.0),
        Psr::MakeParticle(Psr::Vec3{1.0, 1.0, 1.0}, 1.0),
    };
    state.Springs = {Psr::MakeSpring(0, 1, 1.0, 100.0)};

    const Psr::StepResult result = Psr::Step(state, NoGravityParams(0.01));
    ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.DegenerateSpringCount, 1u);
    EXPECT_TRUE(result.Diagnostics.Stable);
    ExpectStateEqual(result.State, state); // skipped force, no gravity: unchanged
}

TEST(ParticleSpringReference, InvalidInputsFailClosedWithExplicitCodes)
{
    const Psr::StepParams params = NoGravityParams(0.01);

    {
        Psr::SystemState state{};
        state.Particles = {Psr::MakeParticle(Psr::Vec3{}, 1.0)};
        const Psr::StepResult result = Psr::Step(state, NoGravityParams(0.0));
        EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::InvalidTimeStep);
        ExpectStateEqual(result.State, state);
    }
    {
        Psr::SystemState state{};
        state.Particles = {Psr::MakeParticle(Psr::Vec3{}, 1.0)};
        state.Particles[0].Position.X = std::numeric_limits<double>::quiet_NaN();
        const Psr::StepResult result = Psr::Step(state, params);
        EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::InvalidParticle);
    }
    {
        Psr::SystemState state{};
        state.Particles = {Psr::MakeParticle(Psr::Vec3{}, 1.0)};
        state.Particles[0].InverseMass = -1.0;
        const Psr::StepResult result = Psr::Step(state, params);
        EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::InvalidParticle);
    }
    {
        Psr::SystemState state{};
        state.Particles = {Psr::MakeParticle(Psr::Vec3{}, 1.0)};
        state.Springs = {Psr::MakeSpring(0, 7, 1.0, 10.0)}; // index out of range
        const Psr::StepResult result = Psr::Step(state, params);
        EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::InvalidSpring);
        ExpectStateEqual(result.State, state);
    }
    {
        Psr::SystemState state{};
        state.Particles = {
            Psr::MakeParticle(Psr::Vec3{}, 1.0),
            Psr::MakeParticle(Psr::Vec3{1, 0, 0}, 1.0),
        };
        state.Springs = {Psr::MakeSpring(1, 1, 1.0, 10.0)}; // self spring
        const Psr::StepResult result = Psr::Step(state, params);
        EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::InvalidSpring);
    }
    {
        Psr::SystemState state{};
        state.Particles = {
            Psr::MakeParticle(Psr::Vec3{}, 1.0),
            Psr::MakeParticle(Psr::Vec3{1, 0, 0}, 1.0),
        };
        state.Springs = {Psr::MakeSpring(0, 1, 1.0, -5.0)}; // negative stiffness
        const Psr::StepResult result = Psr::Step(state, params);
        EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::InvalidSpring);
    }
}

TEST(ParticleSpringReference, StabilityRatioReportedWithoutFailingFiniteSteps)
{
    Psr::SystemState state = SymmetricStretchedPair(1.0e6);

    const Psr::StepResult result = Psr::Step(state, NoGravityParams(0.1));
    ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
    // omega * dt = 0.1 * sqrt(2e6) ≈ 141 — far beyond the limit of 2.
    EXPECT_TRUE(result.Diagnostics.StabilityLimitExceeded);
    EXPECT_GT(result.Diagnostics.MaxStiffnessDtRatio, 2.0);
    EXPECT_FALSE(result.Diagnostics.FallbackApplied);
}

TEST(ParticleSpringReference, NonFiniteExplosionFailsClosedToInputState)
{
    Psr::SystemState state{};
    state.Particles = {
        Psr::MakeParticle(Psr::Vec3{-10.0, 0.0, 0.0}, 1.0),
        Psr::MakeParticle(Psr::Vec3{10.0, 0.0, 0.0}, 1.0),
    };
    // Finite but overflow-inducing stiffness: force = k * extension overflows
    // to infinity, so the post-step state is non-finite and must fail closed.
    state.Springs = {Psr::MakeSpring(0, 1, 1.0, 1.0e308)};

    const Psr::StepResult result = Psr::Step(state, NoGravityParams(0.01));
    EXPECT_EQ(result.Diagnostics.Code, Psr::ValidationCode::NonFiniteState);
    EXPECT_TRUE(result.Diagnostics.FallbackApplied);
    EXPECT_FALSE(result.Diagnostics.Stable);
    ExpectStateEqual(result.State, state);
}

TEST(ParticleSpringReference, DiagnosticsPopulatedOnNominalStep)
{
    Psr::SystemState state = SymmetricStretchedPair(100.0, 0.5);

    const Psr::StepResult result = Psr::Step(state, NoGravityParams(0.005));
    ASSERT_EQ(result.Diagnostics.Code, Psr::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.ParticleCount, 2u);
    EXPECT_EQ(result.Diagnostics.SpringCount, 1u);
    EXPECT_EQ(result.Diagnostics.PinnedParticleCount, 0u);
    EXPECT_GT(result.Diagnostics.MaxSpringResidual, 0.0);
    EXPECT_GT(result.Diagnostics.SpringResidualL2, 0.0);
    EXPECT_GT(result.Diagnostics.KineticEnergyAfter, result.Diagnostics.KineticEnergyBefore);
    EXPECT_GT(result.Diagnostics.TotalEnergyBefore, 0.0);
    EXPECT_TRUE(result.Diagnostics.Stable);
}
