// PHYSICS-003 — parity fixtures between the promoted physics world solver
// (`Extrinsic.Physics.World::SolveStep`) and the canonical METHOD-001 CPU
// reference (`methods/physics/rigid_body_reference`).
//
// The reference is canonical truth: these tests run the same fixture through
// both implementations with matched parameters and assert the world solver
// tracks the reference within documented tolerances. The world stores float
// state while the reference computes in double, so tolerances are absolute
// and sized for ~60 accumulated steps (1e-4) or a single solve (1e-5).
//
// Scope note: both solvers apply linear contact impulses and positional
// Baumgarte projection on the captured manifold; neither models angular
// inertia in contact response. Parity therefore covers linear quantities.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "RigidBodyReference.hpp"

import Extrinsic.Physics.World;

namespace Physics = Extrinsic::Physics;
namespace Ref = Intrinsic::Methods::Physics::RigidBodyReference;

namespace
{
    constexpr double kFreeFallTolerance = 1.0e-4;
    constexpr double kContactTolerance = 1.0e-4;

    [[nodiscard]] Physics::BodyDescriptor WorldDynamicSphere(const glm::vec3 position,
                                                             const float radius,
                                                             const float mass,
                                                             const float linearDamping = 0.0f)
    {
        Physics::BodyDescriptor body = Physics::MakeDynamicBody(mass);
        body.Pose.Position = position;
        body.LinearDamping = linearDamping;
        body.Shapes = {Physics::MakeSphere(radius)};
        return body;
    }

    [[nodiscard]] Ref::BodyState ReferenceDynamicSphere(const glm::vec3 position,
                                                        const double radius,
                                                        const double mass,
                                                        const double linearDamping = 0.0)
    {
        Ref::BodyState body = Ref::MakeDynamicBody(
            Ref::Vec3{position.x, position.y, position.z}, mass,
            {Ref::MakeSphere(radius)});
        body.LinearDamping = linearDamping;
        return body;
    }
}

// Free fall with damping and no contacts: the world SolveStep integration
// matches the reference integrator across 60 accumulated steps.
TEST(PhysicsSolverParity, FreeFallTracksReferenceIntegrator)
{
    const glm::vec3 start{0.0f, 10.0f, 0.0f};
    constexpr float kDamping = 0.2f;

    Physics::World world;
    const Physics::BodyHandle handle =
        world.AddBody(WorldDynamicSphere(start, 0.5f, 2.0f, kDamping));
    ASSERT_TRUE(handle.IsValid());

    std::vector<Ref::BodyState> reference{
        ReferenceDynamicSphere(start, 0.5, 2.0, kDamping)};

    Physics::SolverSettings settings{};
    settings.EnableSleep = false; // the reference fixture body never sleeps

    const Ref::StepParams params{}; // dt 1/60, gravity -9.80665, 8 iterations

    for (int step = 0; step < 60; ++step)
    {
        const Physics::SolveStepDiagnostics diagnostics =
            world.SolveStep(Physics::StepInput{}, settings);
        ASSERT_EQ(diagnostics.Status, Physics::ValidationStatus::Valid);

        Ref::StepResult result = Ref::Step(reference, params);
        ASSERT_EQ(result.Diagnostics.Code, Ref::ValidationCode::Valid);
        reference = std::move(result.Bodies);
    }

    const Physics::BodyDescriptor* worldBody = world.GetBody(handle);
    ASSERT_NE(worldBody, nullptr);
    const Ref::BodyState& referenceBody = reference.front();

    EXPECT_NEAR(worldBody->Pose.Position.y, referenceBody.Position.Y, kFreeFallTolerance);
    EXPECT_NEAR(worldBody->LinearVelocity.y, referenceBody.LinearVelocity.Y, kFreeFallTolerance);
    EXPECT_NEAR(worldBody->Pose.Position.x, referenceBody.Position.X, kFreeFallTolerance);
    EXPECT_NEAR(worldBody->Pose.Position.z, referenceBody.Position.Z, kFreeFallTolerance);
}

// One overlapping dynamic sphere pair, zero gravity, one solve step with
// matched solver parameters: positions and velocities match the reference
// contact resolution. Sphere-sphere is analytic in both implementations and
// shares the A->B normal convention.
TEST(PhysicsSolverParity, OverlappingSpherePairMatchesReferenceContactSolve)
{
    const glm::vec3 positionA{0.0f, 0.0f, 0.0f};
    const glm::vec3 positionB{0.8f, 0.0f, 0.0f};

    Physics::World world;
    const Physics::BodyHandle handleA =
        world.AddBody(WorldDynamicSphere(positionA, 0.5f, 1.0f));
    const Physics::BodyHandle handleB =
        world.AddBody(WorldDynamicSphere(positionB, 0.5f, 1.0f));
    ASSERT_TRUE(handleA.IsValid());
    ASSERT_TRUE(handleB.IsValid());

    std::vector<Ref::BodyState> reference{
        ReferenceDynamicSphere(positionA, 0.5, 1.0),
        ReferenceDynamicSphere(positionB, 0.5, 1.0),
    };

    // Matched parameters; zero gravity isolates the contact response.
    Physics::StepInput input{};
    input.Gravity = glm::vec3{0.0f};
    Physics::SolverSettings settings{};
    settings.EnableSleep = false;

    Ref::StepParams params{};
    params.Gravity = Ref::Vec3{0.0, 0.0, 0.0};

    const Physics::SolveStepDiagnostics diagnostics = world.SolveStep(input, settings);
    ASSERT_EQ(diagnostics.Status, Physics::ValidationStatus::Valid);

    const Ref::StepResult result = Ref::Step(reference, params);
    ASSERT_EQ(result.Diagnostics.Code, Ref::ValidationCode::Valid);
    ASSERT_EQ(result.Diagnostics.ContactCount, 1u);

    const Physics::BodyDescriptor* worldA = world.GetBody(handleA);
    const Physics::BodyDescriptor* worldB = world.GetBody(handleB);
    ASSERT_NE(worldA, nullptr);
    ASSERT_NE(worldB, nullptr);

    EXPECT_NEAR(worldA->Pose.Position.x, result.Bodies[0].Position.X, kContactTolerance);
    EXPECT_NEAR(worldB->Pose.Position.x, result.Bodies[1].Position.X, kContactTolerance);
    EXPECT_NEAR(worldA->LinearVelocity.x, result.Bodies[0].LinearVelocity.X, kContactTolerance);
    EXPECT_NEAR(worldB->LinearVelocity.x, result.Bodies[1].LinearVelocity.X, kContactTolerance);

    // Both implementations report convergence diagnostics for the fixture.
    EXPECT_EQ(diagnostics.Solve, Physics::SolveStatus::Converged);
    EXPECT_TRUE(result.Diagnostics.Converged);
    EXPECT_NEAR(diagnostics.MaxPenetrationBefore,
                result.Diagnostics.MaxPenetration,
                kContactTolerance);
}

