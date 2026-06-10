// METHOD-010 — XPBD cloth and shell CPU reference backend tests.
//
// Pins the deterministic XPBD step (predict, compliant constraint
// projection, half-space collision, position-derived velocities), the
// triangle-topology constraint builder, pinned-vertex behavior, degenerate
// and invalid-input diagnostics, and repeated-step determinism.
#include "XpbdClothReference.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace Cloth = Intrinsic::Methods::Physics::XpbdClothReference;

namespace
{
    [[nodiscard]] auto NoGravityParams(double dt, int iterations = 20) -> Cloth::StepParams
    {
        Cloth::StepParams params{};
        params.DeltaTime = dt;
        params.Gravity = Cloth::Vec3{};
        params.Iterations = iterations;
        return params;
    }

    // 3x3 vertex grid in the XZ plane (spacing 0.25), 8 triangles, top row
    // (z = 0) pinned. Index layout: row-major, index = z * 3 + x.
    [[nodiscard]] auto PinnedPatch(double stretchCompliance = 0.0, double bendCompliance = 0.0)
        -> Cloth::ClothState
    {
        constexpr double kSpacing = 0.25;
        std::vector<Cloth::Vec3> positions;
        std::vector<Cloth::Triangle> triangles;
        for (std::size_t z = 0; z < 3; ++z)
        {
            for (std::size_t x = 0; x < 3; ++x)
            {
                positions.push_back(Cloth::Vec3{static_cast<double>(x) * kSpacing, 0.0,
                                                static_cast<double>(z) * kSpacing});
            }
        }
        for (std::size_t z = 0; z < 2; ++z)
        {
            for (std::size_t x = 0; x < 2; ++x)
            {
                const std::size_t i = z * 3 + x;
                triangles.push_back(Cloth::Triangle{i, i + 1, i + 3});
                triangles.push_back(Cloth::Triangle{i + 1, i + 4, i + 3});
            }
        }
        Cloth::ClothState state =
            Cloth::BuildClothFromTriangles(positions, triangles, 0.1, stretchCompliance, bendCompliance);
        for (std::size_t x = 0; x < 3; ++x)
        {
            state.Particles[x].InverseMass = 0.0; // pin top row
        }
        return state;
    }

    void ExpectStateEqual(const Cloth::ClothState& actual, const Cloth::ClothState& expected)
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

TEST(XpbdClothReference, BuildClothFromTrianglesCreatesUniqueEdgesAndBendPairs)
{
    // Two triangles sharing a diagonal: 5 unique edges, 1 interior edge with
    // one bend pair between the opposite corners.
    const std::vector<Cloth::Vec3> positions{
        {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}};
    const std::vector<Cloth::Triangle> triangles{{0, 1, 2}, {1, 3, 2}};

    const Cloth::ClothState state = Cloth::BuildClothFromTriangles(positions, triangles, 1.0, 0.0, 0.0);

    EXPECT_EQ(state.StretchConstraints.size(), 5u);
    ASSERT_EQ(state.BendConstraints.size(), 1u);
    EXPECT_EQ(state.BendConstraints[0].ParticleA, 0u);
    EXPECT_EQ(state.BendConstraints[0].ParticleB, 3u);
    // Rest lengths come from the input positions.
    EXPECT_NEAR(state.BendConstraints[0].RestLength, std::sqrt(2.0), 1.0e-12);
}

TEST(XpbdClothReference, ZeroComplianceConstraintProjectsRigidly)
{
    // Single stretched constraint between two free particles with rigid
    // (zero-compliance) projection: one iteration restores the rest length
    // and preserves the midpoint for equal masses.
    Cloth::ClothState state{};
    state.Particles = {
        Cloth::MakeParticle(Cloth::Vec3{-1.0, 0.0, 0.0}, 1.0),
        Cloth::MakeParticle(Cloth::Vec3{1.0, 0.0, 0.0}, 1.0),
    };
    state.StretchConstraints = {Cloth::DistanceConstraint{0, 1, 1.0, 0.0}};

    const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01, 1));
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);

    const Cloth::Vec3 d = result.State.Particles[0].Position - result.State.Particles[1].Position;
    EXPECT_NEAR(Cloth::Length(d), 1.0, 1.0e-12);
    const Cloth::Vec3 mid = (result.State.Particles[0].Position + result.State.Particles[1].Position) * 0.5;
    EXPECT_NEAR(Cloth::Length(mid), 0.0, 1.0e-12);
    EXPECT_TRUE(result.Diagnostics.Converged);
}

TEST(XpbdClothReference, PinnedPatchUnderGravityHangsAndConverges)
{
    Cloth::ClothState state = PinnedPatch();

    Cloth::StepParams params{};
    params.DeltaTime = 1.0 / 60.0;
    params.Gravity = Cloth::Vec3{0.0, -9.80665, 0.0};
    params.Iterations = 20;
    params.ResidualTolerance = 5.0e-3;

    for (int i = 0; i < 60; ++i)
    {
        const Cloth::StepResult result = Cloth::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
        ASSERT_TRUE(result.Diagnostics.Stable);
        state = result.State;
    }

    // Pinned row unchanged; free rows hang below their start height.
    for (std::size_t x = 0; x < 3; ++x)
    {
        EXPECT_EQ(state.Particles[x].Position.Y, 0.0);
    }
    for (std::size_t i = 3; i < 9; ++i)
    {
        EXPECT_LT(state.Particles[i].Position.Y, -1.0e-3);
    }

    const Cloth::StepResult settled = Cloth::Step(state, params);
    EXPECT_TRUE(settled.Diagnostics.Converged)
        << "max stretch residual " << settled.Diagnostics.MaxStretchResidual;
}

TEST(XpbdClothReference, BendConstraintResistsFolding)
{
    // Two triangles folded 90 degrees across the shared edge. With a rigid
    // bend constraint the opposite vertices are pushed back toward their
    // flat-configuration distance.
    const std::vector<Cloth::Vec3> flat{
        {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {1, 0, 1}};
    const std::vector<Cloth::Triangle> triangles{{0, 1, 2}, {1, 3, 2}};
    Cloth::ClothState state = Cloth::BuildClothFromTriangles(flat, triangles, 1.0, 0.0, 0.0);

    // Fold vertex 3 up out of the plane (rotate about the 1-2 diagonal edge).
    state.Particles[3].Position = Cloth::Vec3{1.0, 0.7, 0.3};
    const double foldedDistance =
        Cloth::Length(state.Particles[3].Position - state.Particles[0].Position);
    const double restDistance = state.BendConstraints.at(0).RestLength;
    ASSERT_LT(foldedDistance, restDistance);

    const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01, 20));
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);

    const double solvedDistance =
        Cloth::Length(result.State.Particles[3].Position - result.State.Particles[0].Position);
    EXPECT_GT(solvedDistance, foldedDistance);
    EXPECT_LT(result.Diagnostics.MaxBendResidual, restDistance - foldedDistance);
}

TEST(XpbdClothReference, PinnedVerticesNeverMove)
{
    Cloth::ClothState state = PinnedPatch();
    Cloth::StepParams params{};
    params.DeltaTime = 1.0 / 60.0;
    params.Gravity = Cloth::Vec3{0.3, -9.80665, 0.2};
    params.Iterations = 10;

    for (int i = 0; i < 30; ++i)
    {
        const Cloth::StepResult result = Cloth::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
        EXPECT_EQ(result.Diagnostics.PinnedParticleCount, 3u);
        state = result.State;
    }

    EXPECT_EQ(state.Particles[0].Position.X, 0.0);
    EXPECT_EQ(state.Particles[1].Position.X, 0.25);
    EXPECT_EQ(state.Particles[2].Position.X, 0.5);
    for (std::size_t x = 0; x < 3; ++x)
    {
        EXPECT_EQ(state.Particles[x].Position.Y, 0.0);
        EXPECT_EQ(state.Particles[x].Position.Z, 0.0);
    }
}

TEST(XpbdClothReference, HalfSpaceColliderKeepsParticlesAbovePlane)
{
    Cloth::ClothState state = PinnedPatch();
    // Unpin everything so the patch free-falls onto the floor.
    for (Cloth::ParticleState& particle : state.Particles)
    {
        particle.InverseMass = 1.0 / 0.1;
    }

    Cloth::StepParams params{};
    params.DeltaTime = 1.0 / 60.0;
    params.Gravity = Cloth::Vec3{0.0, -9.80665, 0.0};
    params.Iterations = 10;
    params.Colliders = {Cloth::MakeHalfSpaceCollider(Cloth::Vec3{0.0, 1.0, 0.0}, -0.2)};

    for (int i = 0; i < 120; ++i)
    {
        const Cloth::StepResult result = Cloth::Step(state, params);
        ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
        state = result.State;
        for (const Cloth::ParticleState& particle : state.Particles)
        {
            EXPECT_GE(particle.Position.Y, -0.2 - 1.0e-9);
        }
    }
}

TEST(XpbdClothReference, HalfSpaceColliderWithNonUnitNormalProjectsToPlane)
{
    // Directly-authored colliders pass Validate with any finite non-zero
    // normal; the projection must land on the geometric plane
    // dot(N, x) = Offset, not overshoot by |N|^2 (review finding on
    // METHOD-010: {0,2,0}/0 used to project y=-1 to y=3 instead of y=0).
    Cloth::ClothState state{};
    state.Particles = {Cloth::MakeParticle(Cloth::Vec3{0.0, -1.0, 0.0}, 1.0)};

    Cloth::StepParams params = NoGravityParams(0.01, 1);
    Cloth::Collider collider{};
    collider.Kind = Cloth::ColliderKind::HalfSpace;
    collider.Normal = Cloth::Vec3{0.0, 2.0, 0.0};
    collider.Offset = 0.0;
    params.Colliders = {collider};

    const Cloth::StepResult result = Cloth::Step(state, params);
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_NEAR(result.State.Particles[0].Position.Y, 0.0, 1.0e-12);

    // Scaled offset defines the same plane family: dot((0,2,0), x) >= 4 is
    // the half-space y >= 2.
    state.Particles[0].Position = Cloth::Vec3{0.0, 1.0, 0.0};
    params.Colliders[0].Offset = 4.0;
    const Cloth::StepResult offsetResult = Cloth::Step(state, params);
    ASSERT_EQ(offsetResult.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_NEAR(offsetResult.State.Particles[0].Position.Y, 2.0, 1.0e-12);
}

TEST(XpbdClothReference, SphereColliderReportedAsUnsupportedAndSkipped)
{
    Cloth::ClothState state = PinnedPatch();
    Cloth::StepParams params = NoGravityParams(0.01, 5);
    params.Colliders = {Cloth::MakeSphereCollider(Cloth::Vec3{0.0, -1.0, 0.0}, 0.5)};

    const Cloth::StepResult result = Cloth::Step(state, params);
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.UnsupportedColliderCount, 1u);
}

TEST(XpbdClothReference, DegenerateTriangleReportedNotFatal)
{
    // Distinct indices but collinear positions: zero-area triangle.
    Cloth::ClothState state{};
    state.Particles = {
        Cloth::MakeParticle(Cloth::Vec3{0, 0, 0}, 1.0),
        Cloth::MakeParticle(Cloth::Vec3{1, 0, 0}, 1.0),
        Cloth::MakeParticle(Cloth::Vec3{2, 0, 0}, 1.0),
    };
    state.Triangles = {Cloth::Triangle{0, 1, 2}};

    const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01, 1));
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.DegenerateTriangleCount, 1u);
}

TEST(XpbdClothReference, CoincidentConstraintEndpointsSkippedAndCounted)
{
    Cloth::ClothState state{};
    state.Particles = {
        Cloth::MakeParticle(Cloth::Vec3{1, 1, 1}, 1.0),
        Cloth::MakeParticle(Cloth::Vec3{1, 1, 1}, 1.0),
    };
    state.StretchConstraints = {Cloth::DistanceConstraint{0, 1, 0.5, 0.0}};

    const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01, 4));
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.DegenerateConstraintCount, 1u); // counted once per step
    EXPECT_TRUE(result.Diagnostics.Stable);
}

TEST(XpbdClothReference, InvalidInputsFailClosedWithExplicitCodes)
{
    const Cloth::ClothState valid = PinnedPatch();

    {
        const Cloth::StepResult result = Cloth::Step(valid, NoGravityParams(0.0));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidTimeStep);
        ExpectStateEqual(result.State, valid);
    }
    {
        const Cloth::StepResult result = Cloth::Step(valid, NoGravityParams(0.01, 0));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidIterations);
    }
    {
        Cloth::ClothState state = valid;
        state.Particles[4].Position.Y = std::numeric_limits<double>::quiet_NaN();
        const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidParticle);
    }
    {
        Cloth::ClothState state = valid;
        state.StretchConstraints.push_back(Cloth::DistanceConstraint{0, 99, 1.0, 0.0});
        const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidConstraint);
    }
    {
        Cloth::ClothState state = valid;
        state.BendConstraints.push_back(Cloth::DistanceConstraint{1, 1, 1.0, 0.0}); // self
        const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidConstraint);
    }
    {
        Cloth::ClothState state = valid;
        state.StretchConstraints[0].Compliance = -1.0;
        const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidConstraint);
    }
    {
        Cloth::ClothState state = valid;
        state.Triangles.push_back(Cloth::Triangle{0, 0, 1}); // repeated index
        const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidTopology);
    }
    {
        Cloth::ClothState state = valid;
        state.Triangles.push_back(Cloth::Triangle{0, 1, 99}); // out of range
        const Cloth::StepResult result = Cloth::Step(state, NoGravityParams(0.01));
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidTopology);
    }
    {
        Cloth::StepParams params = NoGravityParams(0.01);
        params.Colliders = {Cloth::Collider{}};
        params.Colliders[0].Normal = Cloth::Vec3{}; // zero normal half-space
        const Cloth::StepResult result = Cloth::Step(valid, params);
        EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::InvalidCollider);
    }
}

TEST(XpbdClothReference, NonConvergenceReportedWhenIterationsInsufficient)
{
    // A heavily stretched patch with a single iteration cannot reach the
    // tolerance; the diagnostics must say so rather than claiming success.
    Cloth::ClothState state = PinnedPatch();
    for (std::size_t i = 3; i < 9; ++i)
    {
        state.Particles[i].Position.Z *= 3.0; // stretch the free rows
    }

    Cloth::StepParams params = NoGravityParams(0.01, 1);
    params.ResidualTolerance = 1.0e-6;

    const Cloth::StepResult oneIteration = Cloth::Step(state, params);
    ASSERT_EQ(oneIteration.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_FALSE(oneIteration.Diagnostics.Converged);
    EXPECT_GT(oneIteration.Diagnostics.MaxStretchResidual, 1.0e-6);

    params.Iterations = 50;
    const Cloth::StepResult manyIterations = Cloth::Step(state, params);
    EXPECT_LT(manyIterations.Diagnostics.MaxStretchResidual,
              oneIteration.Diagnostics.MaxStretchResidual);
}

TEST(XpbdClothReference, RepeatedStepsAreDeterministic)
{
    const Cloth::ClothState initial = PinnedPatch(1.0e-6, 1.0e-4);

    Cloth::StepParams params{};
    params.DeltaTime = 1.0 / 60.0;
    params.Gravity = Cloth::Vec3{0.1, -9.80665, 0.05};
    params.Iterations = 12;
    params.GlobalDamping = 0.02;
    params.Colliders = {Cloth::MakeHalfSpaceCollider(Cloth::Vec3{0.0, 1.0, 0.0}, -1.0)};

    auto run = [&]() -> Cloth::ClothState
    {
        Cloth::ClothState state = initial;
        for (int i = 0; i < 40; ++i)
        {
            const Cloth::StepResult result = Cloth::Step(state, params);
            EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
            state = result.State;
        }
        return state;
    };

    const Cloth::ClothState first = run();
    const Cloth::ClothState second = run();
    ExpectStateEqual(first, second);
}

TEST(XpbdClothReference, NonFiniteExplosionFailsClosedToInputState)
{
    Cloth::ClothState state{};
    state.Particles = {
        Cloth::MakeParticle(Cloth::Vec3{-1.0, 0.0, 0.0}, 1.0),
        Cloth::MakeParticle(Cloth::Vec3{1.0, 0.0, 0.0}, 1.0),
    };
    // Finite but overflow-inducing velocity: prediction overflows position.
    state.Particles[0].Velocity = Cloth::Vec3{1.0e308, 0.0, 0.0};

    Cloth::StepParams params = NoGravityParams(1.0e10, 1);

    const Cloth::StepResult result = Cloth::Step(state, params);
    EXPECT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::NonFiniteState);
    EXPECT_TRUE(result.Diagnostics.FallbackApplied);
    EXPECT_FALSE(result.Diagnostics.Stable);
    ExpectStateEqual(result.State, state);
}

TEST(XpbdClothReference, DiagnosticsPopulatedOnNominalStep)
{
    Cloth::ClothState state = PinnedPatch();

    Cloth::StepParams params{};
    params.DeltaTime = 1.0 / 60.0;
    params.Gravity = Cloth::Vec3{0.0, -9.80665, 0.0};
    params.Iterations = 8;

    const Cloth::StepResult result = Cloth::Step(state, params);
    ASSERT_EQ(result.Diagnostics.Code, Cloth::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.ParticleCount, 9u);
    EXPECT_EQ(result.Diagnostics.TriangleCount, 8u);
    EXPECT_EQ(result.Diagnostics.StretchConstraintCount, 16u); // 2x(3x2 grid edges) + 4 diagonals
    EXPECT_EQ(result.Diagnostics.BendConstraintCount, result.State.BendConstraints.size());
    EXPECT_GT(result.Diagnostics.BendConstraintCount, 0u);
    EXPECT_EQ(result.Diagnostics.IterationsUsed, 8);
    EXPECT_TRUE(result.Diagnostics.Stable);
    EXPECT_GT(result.Diagnostics.KineticEnergyAfter, 0.0);
}
