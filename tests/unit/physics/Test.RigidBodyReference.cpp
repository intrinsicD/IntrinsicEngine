#include "RigidBodyReference.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace Rigid = Intrinsic::Methods::Physics::RigidBodyReference;

namespace
{
    constexpr double kPi = 3.141592653589793238462643383279502884;

    [[nodiscard]] auto NoGravityParams() -> Rigid::StepParams
    {
        Rigid::StepParams params{};
        params.Gravity = Rigid::Vec3{};
        params.SolverIterations = 8;
        params.PenetrationSlop = 0.0;
        params.PositionCorrectionPercent = 1.0;
        return params;
    }
}

TEST(RigidBodyReference, FreeFallUsesDeterministicSemiImplicitEuler)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{0.0, 0.0, 0.0}, 2.0, {Rigid::MakeSphere(0.5)}),
    };

    Rigid::StepParams params{};
    params.DeltaTime = 0.1;
    params.Gravity = Rigid::Vec3{0.0, -10.0, 0.0};
    params.SolverIterations = 1;

    for (int i = 0; i < 10; ++i)
    {
        const Rigid::StepResult result = Rigid::Step(bodies, params);
        ASSERT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
        bodies = result.Bodies;
    }

    EXPECT_NEAR(bodies[0].LinearVelocity.Y, -10.0, 1.0e-12);
    EXPECT_NEAR(bodies[0].Position.Y, -5.5, 1.0e-12);
}

TEST(RigidBodyReference, ConstantAccelerationAccumulatesPositionAndVelocity)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{1.0, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(0.25)}),
    };
    bodies[0].LinearVelocity = Rigid::Vec3{2.0, 0.0, 0.0};

    Rigid::StepParams params{};
    params.DeltaTime = 0.25;
    params.Gravity = Rigid::Vec3{4.0, 0.0, 0.0};
    params.SolverIterations = 1;

    for (int i = 0; i < 4; ++i)
    {
        bodies = Rigid::Step(bodies, params).Bodies;
    }

    EXPECT_NEAR(bodies[0].LinearVelocity.X, 6.0, 1.0e-12);
    EXPECT_NEAR(bodies[0].Position.X, 5.5, 1.0e-12);
}

TEST(RigidBodyReference, AngularIntegrationUsesUnitQuaternion)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{}, 1.0, {Rigid::MakeSphere(0.25)}),
    };
    bodies[0].AngularVelocity = Rigid::Vec3{0.0, 0.0, kPi};

    Rigid::StepParams params = NoGravityParams();
    params.DeltaTime = 0.5;

    const Rigid::StepResult result = Rigid::Step(bodies, params);

    ASSERT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_NEAR(result.Bodies[0].Orientation.W, std::sqrt(0.5), 1.0e-12);
    EXPECT_NEAR(result.Bodies[0].Orientation.Z, std::sqrt(0.5), 1.0e-12);
    EXPECT_NEAR(Rigid::Length(Rigid::Vec3{result.Bodies[0].Orientation.X, result.Bodies[0].Orientation.Y, result.Bodies[0].Orientation.Z}),
                std::sqrt(0.5),
                1.0e-12);
}

TEST(RigidBodyReference, ElasticSphereCollisionSwapsLinearVelocities)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{-0.75, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(1.0)}),
        Rigid::MakeDynamicBody(Rigid::Vec3{0.75, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(1.0)}),
    };
    bodies[0].LinearVelocity = Rigid::Vec3{1.0, 0.0, 0.0};
    bodies[1].LinearVelocity = Rigid::Vec3{-1.0, 0.0, 0.0};

    Rigid::StepParams params = NoGravityParams();
    params.DeltaTime = 0.01;
    params.Restitution = 1.0;

    const Rigid::StepResult result = Rigid::Step(bodies, params);

    ASSERT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    ASSERT_EQ(result.Diagnostics.ContactCount, 1u);
    EXPECT_NEAR(result.Bodies[0].LinearVelocity.X, -1.0, 1.0e-12);
    EXPECT_NEAR(result.Bodies[1].LinearVelocity.X, 1.0, 1.0e-12);
    EXPECT_NEAR(result.Diagnostics.EnergyDrift, 0.0, 1.0e-12);
}

TEST(RigidBodyReference, InelasticSphereCollisionSettlesAtSharedNormalVelocity)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{-0.75, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(1.0)}),
        Rigid::MakeDynamicBody(Rigid::Vec3{0.75, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(1.0)}),
    };
    bodies[0].LinearVelocity = Rigid::Vec3{1.0, 0.0, 0.0};
    bodies[1].LinearVelocity = Rigid::Vec3{-1.0, 0.0, 0.0};

    Rigid::StepParams params = NoGravityParams();
    params.DeltaTime = 0.01;
    params.Restitution = 0.0;

    const Rigid::StepResult result = Rigid::Step(bodies, params);

    ASSERT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_NEAR(result.Bodies[0].LinearVelocity.X, 0.0, 1.0e-12);
    EXPECT_NEAR(result.Bodies[1].LinearVelocity.X, 0.0, 1.0e-12);
    EXPECT_LT(result.Diagnostics.KineticEnergyAfter, result.Diagnostics.KineticEnergyBefore);
}

TEST(RigidBodyReference, RestingContactPushesDynamicSphereOutOfStaticBox)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{0.0, 0.8, 0.0}, 1.0, {Rigid::MakeSphere(1.0)}),
        Rigid::MakeStaticBody(Rigid::Vec3{0.0, 0.0, 0.0}, {Rigid::MakeBox(Rigid::Vec3{5.0, 0.25, 5.0})}),
    };
    bodies[0].LinearVelocity = Rigid::Vec3{0.0, -1.0, 0.0};

    Rigid::StepParams params = NoGravityParams();
    params.DeltaTime = 0.01;

    const Rigid::StepResult result = Rigid::Step(bodies, params);

    ASSERT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    ASSERT_EQ(result.Diagnostics.ContactCount, 1u);
    EXPECT_GE(result.Bodies[0].Position.Y, 1.25);
    EXPECT_NEAR(result.Bodies[0].LinearVelocity.Y, 0.0, 1.0e-12);
    EXPECT_NEAR(result.Diagnostics.ResidualPenetration, 0.0, 1.0e-12);
}

TEST(RigidBodyReference, RepeatabilityProducesIdenticalReferenceState)
{
    std::vector<Rigid::BodyState> initial{
        Rigid::MakeDynamicBody(Rigid::Vec3{-0.2, 1.0, 0.0}, 2.0, {Rigid::MakeSphere(0.5)}),
        Rigid::MakeStaticBody(Rigid::Vec3{0.0, 0.0, 0.0}, {Rigid::MakeBox(Rigid::Vec3{3.0, 0.1, 3.0})}),
    };
    initial[0].LinearVelocity = Rigid::Vec3{0.25, -0.5, 0.1};
    initial[0].AngularVelocity = Rigid::Vec3{0.0, 0.25, 0.0};

    Rigid::StepParams params{};
    params.DeltaTime = 1.0 / 120.0;
    params.PositionCorrectionPercent = 0.75;

    auto run = [&](std::vector<Rigid::BodyState> bodies) {
        for (int i = 0; i < 32; ++i)
        {
            bodies = Rigid::Step(bodies, params).Bodies;
        }
        return bodies;
    };

    const auto a = run(initial);
    const auto b = run(initial);

    EXPECT_DOUBLE_EQ(a[0].Position.X, b[0].Position.X);
    EXPECT_DOUBLE_EQ(a[0].Position.Y, b[0].Position.Y);
    EXPECT_DOUBLE_EQ(a[0].LinearVelocity.Y, b[0].LinearVelocity.Y);
    EXPECT_DOUBLE_EQ(a[0].Orientation.W, b[0].Orientation.W);
}

TEST(RigidBodyReference, DegenerateInputsReturnExplicitDiagnostics)
{
    Rigid::StepParams params = NoGravityParams();
    params.DeltaTime = 0.0;
    EXPECT_EQ(Rigid::Validate(std::vector<Rigid::BodyState>{}, params), Rigid::ValidationCode::InvalidTimeStep);

    params = NoGravityParams();
    params.SolverIterations = 0;
    EXPECT_EQ(Rigid::Validate(std::vector<Rigid::BodyState>{}, params), Rigid::ValidationCode::InvalidSolverIterations);

    params = NoGravityParams();
    EXPECT_EQ(Rigid::Step({Rigid::MakeDynamicBody(Rigid::Vec3{}, 0.0)}, params).Diagnostics.Code, Rigid::ValidationCode::InvalidMass);

    Rigid::BodyState invalidInertia = Rigid::MakeDynamicBody(Rigid::Vec3{}, 1.0, {Rigid::MakeSphere(0.5)});
    invalidInertia.Inertia.X = 0.0;
    EXPECT_EQ(Rigid::Step({invalidInertia}, params).Diagnostics.Code, Rigid::ValidationCode::InvalidInertia);

    Rigid::BodyState nonFinite = Rigid::MakeDynamicBody(Rigid::Vec3{}, 1.0, {Rigid::MakeSphere(0.5)});
    nonFinite.Position.X = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(Rigid::Step({nonFinite}, params).Diagnostics.Code, Rigid::ValidationCode::NonFiniteState);

    EXPECT_EQ(Rigid::Step({Rigid::MakeDynamicBody(Rigid::Vec3{}, 1.0, {Rigid::MakeSphere(0.0)})}, params).Diagnostics.Code,
              Rigid::ValidationCode::InvalidShape);
}

TEST(RigidBodyReference, FiniteScaleExtremesRemainValid)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeDynamicBody(Rigid::Vec3{1.0e6, 1.0e-6, 0.0}, 1.0e-3, {Rigid::MakeSphere(1.0e-3)}),
    };

    Rigid::StepParams params = NoGravityParams();
    params.DeltaTime = 1.0e-4;

    const Rigid::StepResult result = Rigid::Step(bodies, params);

    EXPECT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_TRUE(Rigid::IsFinite(result.Bodies[0].Position));
}

TEST(RigidBodyReference, OverlappingStartRecordsPenetrationDiagnostics)
{
    std::vector<Rigid::BodyState> bodies{
        Rigid::MakeStaticBody(Rigid::Vec3{-0.25, 0.0, 0.0}, {Rigid::MakeSphere(1.0)}),
        Rigid::MakeStaticBody(Rigid::Vec3{0.25, 0.0, 0.0}, {Rigid::MakeSphere(1.0)}),
    };

    Rigid::StepParams params = NoGravityParams();
    const Rigid::StepResult result = Rigid::Step(bodies, params);

    ASSERT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.ContactCount, 1u);
    EXPECT_GT(result.Diagnostics.MaxPenetration, 1.0);
    EXPECT_FALSE(result.Diagnostics.Converged);
}

TEST(RigidBodyReference, SphereCapsuleAndSphereBoxContactsAreFirstPhaseSupported)
{
    Rigid::StepParams params = NoGravityParams();

    const Rigid::StepResult capsuleResult = Rigid::Step(
        {
            Rigid::MakeDynamicBody(Rigid::Vec3{1.25, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(0.5)}),
            Rigid::MakeStaticBody(Rigid::Vec3{0.0, 0.0, 0.0}, {Rigid::MakeCapsule(1.0, 1.0)}),
        },
        params);
    ASSERT_EQ(capsuleResult.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_EQ(capsuleResult.Diagnostics.ContactCount, 1u);
    EXPECT_TRUE(capsuleResult.Contacts[0].Supported);

    const Rigid::StepResult boxResult = Rigid::Step(
        {
            Rigid::MakeDynamicBody(Rigid::Vec3{1.2, 0.0, 0.0}, 1.0, {Rigid::MakeSphere(0.5)}),
            Rigid::MakeStaticBody(Rigid::Vec3{0.0, 0.0, 0.0}, {Rigid::MakeBox(Rigid::Vec3{1.0, 1.0, 1.0})}),
        },
        params);
    ASSERT_EQ(boxResult.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_EQ(boxResult.Diagnostics.ContactCount, 1u);
    EXPECT_TRUE(boxResult.Contacts[0].Supported);
}

TEST(RigidBodyReference, UnsupportedDynamicShapePairsAreReportedButDoNotFailValidation)
{
    const Rigid::StepResult result = Rigid::Step(
        {
            Rigid::MakeDynamicBody(Rigid::Vec3{0.0, 0.0, 0.0}, 1.0, {Rigid::MakeCapsule(0.5, 1.0)}),
            Rigid::MakeDynamicBody(Rigid::Vec3{0.25, 0.0, 0.0}, 1.0, {Rigid::MakeCapsule(0.5, 1.0)}),
        },
        NoGravityParams());

    EXPECT_EQ(result.Diagnostics.Code, Rigid::ValidationCode::Valid);
    EXPECT_EQ(result.Diagnostics.ContactCount, 0u);
    EXPECT_GE(result.Diagnostics.UnsupportedPairCount, 1u);
}
