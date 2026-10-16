#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.Physics.World;

namespace Physics = Extrinsic::Physics;

namespace
{
    [[nodiscard]] bool Near(float a, float b, float eps = 1.0e-5f)
    {
        return std::abs(a - b) <= eps;
    }

    [[nodiscard]] bool NearVec(const glm::vec3& a, const glm::vec3& b, float eps = 1.0e-5f)
    {
        return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
    }

    [[nodiscard]] Physics::BodyDescriptor DynamicSphere(float mass = 1.0f)
    {
        Physics::BodyDescriptor body = Physics::MakeDynamicBody(mass);
        body.Shapes = {Physics::MakeSphere(0.5f)};
        return body;
    }
}

TEST(PhysicsWorld, LifecycleCreatesDestroysAndRejectsStaleHandles)
{
    Physics::World world;

    const Physics::BodyHandle first = world.AddBody(DynamicSphere());
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(world.BodyCount(), 1u);
    EXPECT_TRUE(world.Contains(first));

    EXPECT_TRUE(world.DestroyBody(first));
    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_FALSE(world.Contains(first));
    EXPECT_FALSE(world.DestroyBody(first));

    const Physics::BodyHandle second = world.AddBody(DynamicSphere());
    ASSERT_TRUE(second.IsValid());
    EXPECT_EQ(second.Index, first.Index);
    EXPECT_NE(second.Generation, first.Generation);
    EXPECT_FALSE(world.UpdateBody(first, DynamicSphere()));
    EXPECT_TRUE(world.UpdateBody(second, DynamicSphere(2.0f)));

    const Physics::WorldDiagnostics& diagnostics = world.GetDiagnostics();
    EXPECT_EQ(diagnostics.BodiesCreated, 2u);
    EXPECT_EQ(diagnostics.BodiesDestroyed, 1u);
    EXPECT_GE(diagnostics.StaleHandleRejects, 2u);
    EXPECT_EQ(diagnostics.DescriptorUpdates, 1u);
}

TEST(PhysicsWorld, InvalidDescriptorsAreRejected)
{
    Physics::World world;
    Physics::BodyDescriptor invalid = DynamicSphere(-1.0f);

    const Physics::BodyHandle handle = world.AddBody(invalid);
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_EQ(world.GetDiagnostics().InvalidDescriptorsRejected, 1u);

    invalid.Mass = 1.0f;
    invalid.Shapes.clear();
    EXPECT_EQ(Physics::Validate(invalid), Physics::ValidationStatus::EmptyShapeList);
}

TEST(PhysicsWorld, DescriptorUpdateReplacesPoseAndVelocity)
{
    Physics::World world;
    Physics::BodyDescriptor body = DynamicSphere();
    const Physics::BodyHandle handle = world.AddBody(body);
    ASSERT_TRUE(handle.IsValid());

    body.Pose.Position = glm::vec3(3.0f, 4.0f, 5.0f);
    body.LinearVelocity = glm::vec3(1.0f, 2.0f, 3.0f);
    ASSERT_TRUE(world.UpdateBody(handle, body));

    const Physics::BodyDescriptor* stored = world.GetBody(handle);
    ASSERT_NE(stored, nullptr);
    EXPECT_TRUE(Near(stored->Pose.Position.x, 3.0f));
    EXPECT_TRUE(Near(stored->LinearVelocity.z, 3.0f));
}

TEST(PhysicsWorld, StepIntegratesDynamicAndCountsStaticKinematicBodies)
{
    Physics::World world;
    Physics::BodyDescriptor staticBody = Physics::MakeStaticBody();
    Physics::BodyDescriptor kinematicBody = Physics::MakeKinematicBody();
    Physics::BodyDescriptor dynamicBody = DynamicSphere();

    kinematicBody.LinearVelocity = glm::vec3(0.0f, 2.0f, 0.0f);
    dynamicBody.Pose.Position = glm::vec3(0.0f, 10.0f, 0.0f);

    const Physics::BodyHandle staticHandle = world.AddBody(staticBody);
    const Physics::BodyHandle kinematicHandle = world.AddBody(kinematicBody);
    const Physics::BodyHandle dynamicHandle = world.AddBody(dynamicBody);
    ASSERT_TRUE(staticHandle.IsValid());
    ASSERT_TRUE(kinematicHandle.IsValid());
    ASSERT_TRUE(dynamicHandle.IsValid());

    const Physics::StepDiagnostics diagnostics = world.Step({0.5f, glm::vec3(0.0f, -10.0f, 0.0f)});
    EXPECT_EQ(diagnostics.Status, Physics::ValidationStatus::Valid);
    EXPECT_EQ(diagnostics.BodiesVisited, 3u);
    EXPECT_EQ(diagnostics.StaticBodiesSkipped, 1u);
    EXPECT_EQ(diagnostics.KinematicBodiesIntegrated, 1u);
    EXPECT_EQ(diagnostics.DynamicBodiesIntegrated, 1u);

    const Physics::BodyDescriptor* kinematic = world.GetBody(kinematicHandle);
    const Physics::BodyDescriptor* dynamic = world.GetBody(dynamicHandle);
    ASSERT_NE(kinematic, nullptr);
    ASSERT_NE(dynamic, nullptr);
    EXPECT_TRUE(Near(kinematic->Pose.Position.y, 1.0f));
    EXPECT_TRUE(dynamic->Pose.Position.y < 10.0f);
    EXPECT_EQ(world.GetDiagnostics().StepsExecuted, 1u);
}

TEST(PhysicsWorld, RepeatedWorldsStepDeterministically)
{
    Physics::World a;
    Physics::World b;
    Physics::BodyDescriptor body = DynamicSphere();
    body.Pose.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    body.LinearVelocity = glm::vec3(0.25f, 0.5f, -0.75f);
    body.LinearDamping = 0.1f;

    const Physics::BodyHandle ha = a.AddBody(body);
    const Physics::BodyHandle hb = b.AddBody(body);
    ASSERT_TRUE(ha.IsValid());
    ASSERT_TRUE(hb.IsValid());

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(a.Step({1.0f / 120.0f, glm::vec3(0.0f, -9.8f, 0.0f)}).Status,
                  Physics::ValidationStatus::Valid);
        ASSERT_EQ(b.Step({1.0f / 120.0f, glm::vec3(0.0f, -9.8f, 0.0f)}).Status,
                  Physics::ValidationStatus::Valid);
    }

    const Physics::BodyDescriptor* ba = a.GetBody(ha);
    const Physics::BodyDescriptor* bb = b.GetBody(hb);
    ASSERT_NE(ba, nullptr);
    ASSERT_NE(bb, nullptr);
    EXPECT_TRUE(Near(ba->Pose.Position.x, bb->Pose.Position.x));
    EXPECT_TRUE(Near(ba->Pose.Position.y, bb->Pose.Position.y));
    EXPECT_TRUE(Near(ba->LinearVelocity.y, bb->LinearVelocity.y));
}

TEST(PhysicsWorld, CollisionGeneratesSphereSphereContactAndCandidate)
{
    Physics::World world;
    Physics::BodyDescriptor a = DynamicSphere();
    Physics::BodyDescriptor b = DynamicSphere();
    a.Pose.Position = glm::vec3(0.0f, 0.0f, 0.0f);
    b.Pose.Position = glm::vec3(0.75f, 0.0f, 0.0f);

    const Physics::BodyHandle ha = world.AddBody(a);
    const Physics::BodyHandle hb = world.AddBody(b);
    ASSERT_TRUE(ha.IsValid());
    ASSERT_TRUE(hb.IsValid());

    const Physics::CollisionResult collisions = world.ComputeCollisionContacts();
    ASSERT_EQ(collisions.Candidates.size(), 1u);
    ASSERT_EQ(collisions.Contacts.size(), 1u);
    EXPECT_EQ(collisions.Diagnostics.BroadphasePairs, 1u);
    EXPECT_EQ(collisions.Diagnostics.ContactsGenerated, 1u);

    const Physics::ContactRecord& contact = collisions.Contacts.front();
    EXPECT_EQ(contact.A.Body.Index, ha.Index);
    EXPECT_EQ(contact.B.Body.Index, hb.Index);
    EXPECT_TRUE(NearVec(contact.Normal, glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(Near(contact.PenetrationDepth, 0.25f));
}

TEST(PhysicsWorld, CollisionKeepsSeparatedPairsAsBroadphaseCandidates)
{
    Physics::World world;
    Physics::BodyDescriptor a = DynamicSphere();
    Physics::BodyDescriptor b = DynamicSphere();
    b.Pose.Position = glm::vec3(5.0f, 0.0f, 0.0f);

    ASSERT_TRUE(world.AddBody(a).IsValid());
    ASSERT_TRUE(world.AddBody(b).IsValid());

    const Physics::CollisionResult collisions = world.ComputeCollisionContacts();
    EXPECT_EQ(collisions.Candidates.size(), 1u);
    EXPECT_TRUE(collisions.Contacts.empty());
    EXPECT_EQ(collisions.Diagnostics.BroadphasePairs, 1u);
    EXPECT_EQ(collisions.Diagnostics.ContactsGenerated, 0u);
}

TEST(PhysicsWorld, CollisionCandidateOrderingIsDeterministic)
{
    Physics::World world;
    const Physics::BodyHandle a = world.AddBody(DynamicSphere());
    const Physics::BodyHandle b = world.AddBody(DynamicSphere());
    const Physics::BodyHandle c = world.AddBody(DynamicSphere());
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());
    ASSERT_TRUE(c.IsValid());

    const Physics::CollisionResult collisions = world.ComputeCollisionContacts();
    ASSERT_EQ(collisions.Candidates.size(), 3u);
    EXPECT_EQ(collisions.Candidates[0].A.Body.Index, a.Index);
    EXPECT_EQ(collisions.Candidates[0].B.Body.Index, b.Index);
    EXPECT_EQ(collisions.Candidates[1].A.Body.Index, a.Index);
    EXPECT_EQ(collisions.Candidates[1].B.Body.Index, c.Index);
    EXPECT_EQ(collisions.Candidates[2].A.Body.Index, b.Index);
    EXPECT_EQ(collisions.Candidates[2].B.Body.Index, c.Index);
}

TEST(PhysicsWorld, CollisionSupportsFirstPhasePrimitivePairs)
{
    Physics::World world;

    Physics::BodyDescriptor sphere = DynamicSphere();
    sphere.Pose.Position = glm::vec3(0.0f, 0.0f, 0.0f);

    Physics::BodyDescriptor capsule = Physics::MakeDynamicBody();
    capsule.Pose.Position = glm::vec3(0.75f, 0.0f, 0.0f);
    capsule.Shapes = {Physics::MakeCapsule(0.25f, 0.5f)};

    Physics::BodyDescriptor boxA = Physics::MakeDynamicBody();
    boxA.Pose.Position = glm::vec3(0.0f, 1.25f, 0.0f);
    boxA.Shapes = {Physics::MakeBox(glm::vec3(0.5f))};

    Physics::BodyDescriptor boxB = Physics::MakeDynamicBody();
    boxB.Pose.Position = glm::vec3(0.25f, 1.25f, 0.0f);
    boxB.Shapes = {Physics::MakeBox(glm::vec3(0.5f))};

    ASSERT_TRUE(world.AddBody(sphere).IsValid());
    ASSERT_TRUE(world.AddBody(capsule).IsValid());
    ASSERT_TRUE(world.AddBody(boxA).IsValid());
    ASSERT_TRUE(world.AddBody(boxB).IsValid());

    const Physics::CollisionResult collisions = world.ComputeCollisionContacts();
    EXPECT_EQ(collisions.Diagnostics.BroadphasePairs, 6u);
    EXPECT_GE(collisions.Diagnostics.ContactsGenerated, 2u);
    EXPECT_EQ(collisions.Diagnostics.UnsupportedPairs, 0u);
}

TEST(PhysicsWorld, CollisionReportsFilteringTriggersAndDynamicNonUniformScale)
{
    Physics::World world;

    Physics::BodyDescriptor solid = DynamicSphere();
    Physics::BodyDescriptor trigger = DynamicSphere();
    trigger.Shapes.front().IsTrigger = true;
    trigger.Pose.Position = glm::vec3(0.25f, 0.0f, 0.0f);

    Physics::BodyDescriptor filtered = DynamicSphere();
    filtered.ParticipatesInContacts = false;

    Physics::BodyDescriptor nonUniform = DynamicSphere();
    nonUniform.Pose.Scale = glm::vec3(2.0f, 1.0f, 1.0f);

    ASSERT_TRUE(world.AddBody(solid).IsValid());
    ASSERT_TRUE(world.AddBody(trigger).IsValid());
    ASSERT_TRUE(world.AddBody(filtered).IsValid());
    EXPECT_FALSE(world.AddBody(nonUniform).IsValid());
    EXPECT_EQ(world.GetDiagnostics().InvalidDescriptorsRejected, 1u);

    const Physics::CollisionResult collisions = world.ComputeCollisionContacts();
    EXPECT_EQ(collisions.Diagnostics.FilteredBodiesSkipped, 1u);
    ASSERT_EQ(collisions.Contacts.size(), 1u);
    EXPECT_TRUE(collisions.Contacts.front().IsTrigger);
    EXPECT_EQ(collisions.Diagnostics.TriggerContacts, 1u);
}

// ── PHYSICS-003: islands, sleep, and solver diagnostics ─────────────────────

namespace
{
    [[nodiscard]] Physics::BodyDescriptor DynamicSphereAt(const glm::vec3 position,
                                                          float mass = 1.0f)
    {
        Physics::BodyDescriptor body = DynamicSphere(mass);
        body.Pose.Position = position;
        return body;
    }

    [[nodiscard]] Physics::BodyDescriptor StaticBoxAt(const glm::vec3 position,
                                                      const glm::vec3 halfExtents)
    {
        Physics::BodyDescriptor body = Physics::MakeStaticBody();
        body.Pose.Position = position;
        body.Shapes = {Physics::MakeBox(halfExtents)};
        return body;
    }

    // Zero-gravity step input so contact/sleep behavior is isolated from
    // integration unless a test wants gravity.
    [[nodiscard]] Physics::StepInput ZeroGravityStep()
    {
        Physics::StepInput input{};
        input.Gravity = glm::vec3{0.0f};
        return input;
    }
}

// The physics-owned ContactRecord enforces the documented A->B normal
// convention, including the geometry sphere-box and GJK/EPA paths.
// A sphere above a static box must report a downward normal so the solver
// separates instead of tunnelling.
TEST(PhysicsWorld, ContactRecordNormalFollowsAToBConvention)
{
    Physics::World world;
    ASSERT_TRUE(world.AddBody(DynamicSphereAt({0.0f, 0.45f, 0.0f})).IsValid());
    ASSERT_TRUE(world
                    .AddBody(StaticBoxAt({0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f}))
                    .IsValid());

    const Physics::CollisionResult collision = world.ComputeCollisionContacts();
    ASSERT_EQ(collision.Contacts.size(), 1u);
    const Physics::ContactRecord& contact = collision.Contacts.front();
    EXPECT_EQ(contact.A.Body.Index, 0u);
    EXPECT_EQ(contact.B.Body.Index, 1u);
    // Normal points from A (sphere, above) toward B (box, below).
    EXPECT_LT(contact.Normal.y, -0.9f);
    EXPECT_NEAR(contact.PenetrationDepth, 0.05f, 0.01f);
}

TEST(PhysicsWorld, IslandsGroupContactConnectedDynamicBodiesDeterministically)
{
    Physics::World world;

    // Pair A-B overlap; C is isolated; D rests on a static anchor.
    const Physics::BodyHandle a = world.AddBody(DynamicSphereAt({0.0f, 0.0f, 0.0f}));
    const Physics::BodyHandle b = world.AddBody(DynamicSphereAt({0.8f, 0.0f, 0.0f}));
    const Physics::BodyHandle c = world.AddBody(DynamicSphereAt({10.0f, 0.0f, 0.0f}));
    const Physics::BodyHandle d = world.AddBody(DynamicSphereAt({20.0f, 0.4f, 0.0f}));
    const Physics::BodyHandle anchor =
        world.AddBody(StaticBoxAt({20.0f, -0.5f, 0.0f}, {1.0f, 0.5f, 1.0f}));
    ASSERT_TRUE(a.IsValid() && b.IsValid() && c.IsValid() && d.IsValid() && anchor.IsValid());

    const Physics::CollisionResult collision = world.ComputeCollisionContacts();
    const Physics::IslandBuildResult islands = world.BuildIslands(collision);

    ASSERT_EQ(islands.Islands.size(), 3u);
    EXPECT_EQ(islands.Diagnostics.IslandCount, 3u);
    EXPECT_EQ(islands.Diagnostics.IslandedDynamicBodies, 4u);
    EXPECT_GE(islands.Diagnostics.StaticAnchoredContacts, 1u);

    // Deterministic ordering: islands by smallest member index, members
    // sorted ascending.
    ASSERT_EQ(islands.Islands[0].Bodies.size(), 2u);
    EXPECT_EQ(islands.Islands[0].Bodies[0], a);
    EXPECT_EQ(islands.Islands[0].Bodies[1], b);
    ASSERT_EQ(islands.Islands[1].Bodies.size(), 1u);
    EXPECT_EQ(islands.Islands[1].Bodies[0], c);
    ASSERT_EQ(islands.Islands[2].Bodies.size(), 1u);
    EXPECT_EQ(islands.Islands[2].Bodies[0], d);

    // The A-B contact belongs to island 0; the D-anchor contact to island 2;
    // isolated C carries no contacts.
    EXPECT_FALSE(islands.Islands[0].ContactIndices.empty());
    EXPECT_TRUE(islands.Islands[1].ContactIndices.empty());
    EXPECT_FALSE(islands.Islands[2].ContactIndices.empty());

    // Rebuilding from the same state yields the same grouping.
    const Physics::IslandBuildResult again = world.BuildIslands(world.ComputeCollisionContacts());
    ASSERT_EQ(again.Islands.size(), islands.Islands.size());
    for (std::size_t i = 0; i < again.Islands.size(); ++i)
        EXPECT_EQ(again.Islands[i].Bodies, islands.Islands[i].Bodies);
}

TEST(PhysicsWorld, IslandsExcludeTriggerContacts)
{
    Physics::World world;

    Physics::BodyDescriptor trigger = DynamicSphere();
    trigger.Shapes.front().IsTrigger = true;
    const Physics::BodyHandle a = world.AddBody(DynamicSphereAt({0.0f, 0.0f, 0.0f}));
    const Physics::BodyHandle b = world.AddBody(trigger);
    ASSERT_TRUE(a.IsValid() && b.IsValid());

    const Physics::CollisionResult collision = world.ComputeCollisionContacts();
    ASSERT_EQ(collision.Contacts.size(), 1u);
    ASSERT_TRUE(collision.Contacts.front().IsTrigger);

    const Physics::IslandBuildResult islands = world.BuildIslands(collision);
    EXPECT_EQ(islands.Diagnostics.TriggerContactsExcluded, 1u);
    // Two singleton islands: the trigger contact creates no constraint edge.
    ASSERT_EQ(islands.Islands.size(), 2u);
    EXPECT_TRUE(islands.Islands[0].ContactIndices.empty());
    EXPECT_TRUE(islands.Islands[1].ContactIndices.empty());
}

TEST(PhysicsWorld, SolveStepSeparatesOverlappingSpheresWithoutOvershootingSlop)
{
    Physics::World world;
    const Physics::BodyHandle a = world.AddBody(DynamicSphereAt({0.0f, 0.0f, 0.0f}));
    const Physics::BodyHandle b = world.AddBody(DynamicSphereAt({0.8f, 0.0f, 0.0f}));
    ASSERT_TRUE(a.IsValid() && b.IsValid());

    const Physics::SolveStepDiagnostics diagnostics = world.SolveStep(ZeroGravityStep());

    EXPECT_EQ(diagnostics.Status, Physics::ValidationStatus::Valid);
    // Residual depth is approximately .0010005; its excess above .001 slop
    // is below the existing .001 error tolerance.
    EXPECT_EQ(diagnostics.Solve, Physics::SolveStatus::Converged);
    EXPECT_GT(diagnostics.MaxPenetrationBefore, 0.1f);
    EXPECT_LE(diagnostics.MaxPenetrationAfter, 0.001f + 1.0e-6f);
    EXPECT_GT(diagnostics.IterationsUsed, 0u);
    EXPECT_GT(diagnostics.ContactsSolved, 0u);
    EXPECT_EQ(diagnostics.NonConvergedIslands, 0u);

    // Symmetric separation along the contact normal (equal masses).
    const Physics::BodyDescriptor* bodyA = world.GetBody(a);
    const Physics::BodyDescriptor* bodyB = world.GetBody(b);
    ASSERT_NE(bodyA, nullptr);
    ASSERT_NE(bodyB, nullptr);
    EXPECT_LT(bodyA->Pose.Position.x, 0.0f);
    EXPECT_GT(bodyB->Pose.Position.x, 0.8f);
    EXPECT_TRUE(Near(bodyA->Pose.Position.x + bodyB->Pose.Position.x, 0.8f, 1.0e-4f));
}

TEST(PhysicsWorld, SolveStepReportsNonConvergenceUnderStarvedIterationBudget)
{
    Physics::World world;
    // Deep overlap with a starved budget and a tiny correction percent so a
    // single pass cannot reach the tolerance.
    const Physics::BodyHandle a = world.AddBody(DynamicSphereAt({0.0f, 0.0f, 0.0f}));
    const Physics::BodyHandle b = world.AddBody(DynamicSphereAt({0.1f, 0.0f, 0.0f}));
    ASSERT_TRUE(a.IsValid() && b.IsValid());

    Physics::SolverSettings settings{};
    settings.MaxIterations = 1u;
    settings.PositionCorrectionPercent = 0.05f;

    const Physics::SolveStepDiagnostics diagnostics =
        world.SolveStep(ZeroGravityStep(), settings);

    EXPECT_EQ(diagnostics.Status, Physics::ValidationStatus::Valid);
    EXPECT_EQ(diagnostics.Solve, Physics::SolveStatus::MaxIterationsReached);
    EXPECT_EQ(diagnostics.IterationsUsed, 1u);
    EXPECT_GT(diagnostics.MaxPenetrationAfter, 0.001f);
    EXPECT_GE(diagnostics.NonConvergedIslands, 1u);
}

TEST(PhysicsWorld, SolveStepRejectsInvalidSolverSettings)
{
    Physics::World world;
    ASSERT_TRUE(world.AddBody(DynamicSphere()).IsValid());

    Physics::SolverSettings settings{};
    settings.MaxIterations = 0u;

    const Physics::SolveStepDiagnostics diagnostics =
        world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_EQ(diagnostics.Status, Physics::ValidationStatus::InvalidSolverSettings);
    EXPECT_EQ(world.GetDiagnostics().SolveStepsExecuted, 0u);
}

TEST(PhysicsWorld, SleepPolicyTransitionsLowMotionBodyAndWakeOnUpdate)
{
    Physics::World world;
    const Physics::BodyHandle handle = world.AddBody(DynamicSphereAt({0.0f, 0.0f, 0.0f}));
    ASSERT_TRUE(handle.IsValid());

    Physics::SolverSettings settings{};
    settings.TimeToSleepSeconds = 3.0f / 60.0f; // sleeps on the third step

    Physics::SolveStepDiagnostics diagnostics{};
    for (int i = 0; i < 3; ++i)
        diagnostics = world.SolveStep(ZeroGravityStep(), settings);

    EXPECT_TRUE(world.IsBodyAsleep(handle));
    EXPECT_EQ(diagnostics.Sleep.SleepTransitions, 1u);
    EXPECT_EQ(diagnostics.Sleep.SleepingDynamicBodies, 1u);
    EXPECT_EQ(diagnostics.Sleep.AwakeDynamicBodies, 0u);

    // Unchanged forces keep a sleeping body out of integration.
    const glm::vec3 positionBefore = world.GetBody(handle)->Pose.Position;
    const auto sleeping = world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_EQ(sleeping.Integration.DynamicBodiesIntegrated, 0u);
    EXPECT_TRUE(NearVec(world.GetBody(handle)->Pose.Position, positionBefore));
    EXPECT_TRUE(world.IsBodyAsleep(handle));

    // A descriptor update is an external mutation and wakes the body.
    Physics::BodyDescriptor descriptor = *world.GetBody(handle);
    descriptor.LinearVelocity = glm::vec3{1.0f, 0.0f, 0.0f};
    ASSERT_TRUE(world.UpdateBody(handle, descriptor));
    EXPECT_FALSE(world.IsBodyAsleep(handle));

    const Physics::SolveStepDiagnostics moving =
        world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_EQ(moving.Sleep.AwakeDynamicBodies, 1u);
    EXPECT_GT(world.GetBody(handle)->Pose.Position.x, 0.0f);
}

TEST(PhysicsWorld, SolveStepIsDeterministicForRepeatedIdenticalInputs)
{
    const auto buildWorld = [](Physics::World& world)
    {
        ASSERT_TRUE(world.AddBody(DynamicSphereAt({0.0f, 0.5f, 0.0f})).IsValid());
        ASSERT_TRUE(world.AddBody(DynamicSphereAt({0.6f, 0.5f, 0.0f})).IsValid());
        ASSERT_TRUE(world
                        .AddBody(StaticBoxAt({0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f}))
                        .IsValid());
    };

    Physics::World first;
    Physics::World second;
    buildWorld(first);
    buildWorld(second);

    for (int step = 0; step < 30; ++step)
    {
        const Physics::SolveStepDiagnostics da = first.SolveStep();
        const Physics::SolveStepDiagnostics db = second.SolveStep();
        EXPECT_EQ(da.Solve, db.Solve);
        EXPECT_EQ(da.ContactsSolved, db.ContactsSolved);
        EXPECT_EQ(da.MaxPenetrationAfter, db.MaxPenetrationAfter);
    }

    for (std::uint32_t index = 0u; index < 2u; ++index)
    {
        const Physics::BodyHandle handle{index, 1u};
        const Physics::BodyDescriptor* bodyA = first.GetBody(handle);
        const Physics::BodyDescriptor* bodyB = second.GetBody(handle);
        ASSERT_NE(bodyA, nullptr);
        ASSERT_NE(bodyB, nullptr);
        // Bitwise-identical evolution: same code path, same iteration order.
        EXPECT_EQ(bodyA->Pose.Position.x, bodyB->Pose.Position.x);
        EXPECT_EQ(bodyA->Pose.Position.y, bodyB->Pose.Position.y);
        EXPECT_EQ(bodyA->Pose.Position.z, bodyB->Pose.Position.z);
        EXPECT_EQ(bodyA->LinearVelocity.x, bodyB->LinearVelocity.x);
        EXPECT_EQ(bodyA->LinearVelocity.y, bodyB->LinearVelocity.y);
        EXPECT_EQ(bodyA->LinearVelocity.z, bodyB->LinearVelocity.z);
    }
}

TEST(PhysicsWorld, SolveStepEnergyDriftIsReportedAndBoundedForRestingContact)
{
    Physics::World world;
    ASSERT_TRUE(world.AddBody(DynamicSphereAt({0.0f, 0.45f, 0.0f})).IsValid());
    ASSERT_TRUE(world
                    .AddBody(StaticBoxAt({0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f}))
                    .IsValid());

    Physics::SolverSettings settings{};
    settings.EnableSleep = false;
    // Two remaining-depth corrections approach slop without overshooting.
    // Repeated gravity leaves a steady residual of approximately .0011135.
    settings.MaxIterations = 2u;

    Physics::SolveStepDiagnostics diagnostics{};
    for (int step = 0; step < 60; ++step)
        diagnostics = world.SolveStep(Physics::StepInput{}, settings);

    // Resting contact: the solver cancels the gravity-injected approach
    // velocity each step, so the reported energy after the solve stays
    // bounded near zero instead of accumulating.
    EXPECT_EQ(diagnostics.Solve, Physics::SolveStatus::Converged);
    EXPECT_LT(diagnostics.KineticEnergyAfter, 0.05f);
    EXPECT_TRUE(std::isfinite(diagnostics.EnergyDrift));
    EXPECT_EQ(world.GetDiagnostics().SolveStepsExecuted, 60u);
    EXPECT_EQ(world.GetDiagnostics().LastSolveStep.Solve, diagnostics.Solve);
}

TEST(PhysicsWorld, SolveStepCorrectsAnalyticBoxPenetrationFromShortGjkSimplex)
{
    Physics::World world;
    auto first = Physics::MakeDynamicBody(1.0f);
    first.Shapes = {Physics::MakeBox(glm::vec3(1.0f))};
    auto second = first;
    second.Pose.Position = {1.5f, 0.0f, 0.0f};
    const auto a = world.AddBody(first);
    const auto b = world.AddBody(second);
    ASSERT_TRUE(a.IsValid());
    ASSERT_TRUE(b.IsValid());

    Physics::SolverSettings settings{};
    settings.MaxIterations = 1;
    settings.PositionCorrectionPercent = 1.0f;
    settings.PenetrationSlop = 0.01f;
    settings.EnableSleep = false;
    // Gravity and both velocities are zero: integration cannot move either box.
    // One equal-mass projection must split the analytic (0.5 - 0.01) correction.
    const auto result = world.SolveStep(ZeroGravityStep(), settings);
    ASSERT_EQ(result.Status, Physics::ValidationStatus::Valid);
    EXPECT_NEAR(result.MaxPenetrationBefore, 0.5f, 1.0e-4f);
    EXPECT_EQ(result.IterationsUsed, 1u);
    EXPECT_EQ(result.ContactsSolved, 1u);
    const auto* bodyA = world.GetBody(a);
    const auto* bodyB = world.GetBody(b);
    ASSERT_NE(bodyA, nullptr);
    ASSERT_NE(bodyB, nullptr);
    EXPECT_TRUE(NearVec(bodyA->Pose.Position, {-0.245f, 0.0f, 0.0f}, 1.0e-4f));
    EXPECT_TRUE(NearVec(bodyB->Pose.Position, {1.745f, 0.0f, 0.0f}, 1.0e-4f));
    EXPECT_TRUE(NearVec(bodyA->LinearVelocity, glm::vec3(0.0f)));
    EXPECT_TRUE(NearVec(bodyB->LinearVelocity, glm::vec3(0.0f)));
}


TEST(PhysicsWorld, ClearKeepsOldHandlesInvalidAcrossSlotReuse)
{
    Physics::World world;
    const auto first = world.AddBody(DynamicSphere());
    const auto previouslyDestroyed = world.AddBody(DynamicSphere());
    ASSERT_TRUE(first.IsValid());
    ASSERT_TRUE(previouslyDestroyed.IsValid());
    ASSERT_TRUE(world.DestroyBody(previouslyDestroyed));
    world.Clear();
    world.Clear();
    EXPECT_EQ(world.BodyCount(), 0u);
    EXPECT_FALSE(world.Contains(first));
    EXPECT_EQ(world.GetBody(first), nullptr);

    const auto replacement = world.AddBody(DynamicSphere(2.0f));
    ASSERT_TRUE(replacement.IsValid());
    EXPECT_EQ(replacement.Index, first.Index);
    ASSERT_NE(replacement.Generation, first.Generation);
    EXPECT_FALSE(world.Contains(first));
    EXPECT_EQ(world.GetBody(first), nullptr);
    EXPECT_FALSE(world.UpdateBody(first, DynamicSphere(3.0f)));
    EXPECT_FALSE(world.DestroyBody(first));
    EXPECT_FALSE(world.Contains(previouslyDestroyed));
    EXPECT_EQ(world.GetBody(previouslyDestroyed), nullptr);
    ASSERT_NE(world.GetBody(replacement), nullptr);
    EXPECT_FLOAT_EQ(world.GetBody(replacement)->Mass, 2.0f);
    EXPECT_EQ(world.BodyCount(), 1u);

    world.Clear();
    const auto third = world.AddBody(DynamicSphere(4.0f));
    ASSERT_TRUE(third.IsValid());
    EXPECT_EQ(third.Index, replacement.Index);
    EXPECT_NE(third.Generation, replacement.Generation);
    EXPECT_NE(third.Generation, first.Generation);
    EXPECT_FALSE(world.Contains(first));
    EXPECT_FALSE(world.Contains(replacement));
    EXPECT_EQ(world.BodyCount(), 1u);
}

TEST(PhysicsWorld, BodyQueriesRequireValidatedUpdatesForMutation)
{
    using QueriedBody = decltype(std::declval<Physics::World&>().GetBody(Physics::BodyHandle{}));
    EXPECT_TRUE((std::is_same_v<QueriedBody, const Physics::BodyDescriptor*>));
}


TEST(PhysicsWorld, CollisionScratchIsResetBetweenIndependentEpaPairs)
{
    // Each EPA seed alone reserves more than 2 KiB. Forty separated pairs
    // therefore require arena reuse within one collision query, not just
    // between queries; removing the reset exhausts the 64 KiB workspace.
    constexpr unsigned pairCount = 40;
    const auto boxAt = [](float x)
    {
        auto body = Physics::MakeStaticBody();
        body.Pose.Position.x = x;
        body.Shapes = {Physics::MakeBox(glm::vec3(1.0f))};
        return body;
    };
    Physics::World combined;
    for (unsigned pair = 0; pair < pairCount; ++pair)
    {
        ASSERT_TRUE(combined.AddBody(boxAt(10.0f * pair)).IsValid());
        ASSERT_TRUE(combined.AddBody(boxAt(10.0f * pair + 1.5f)).IsValid());
    }
    const auto contacts = combined.ComputeCollisionContacts();
    ASSERT_EQ(contacts.Contacts.size(), pairCount);
    EXPECT_EQ(contacts.Diagnostics.ContactsGenerated, pairCount);
    EXPECT_EQ(contacts.Diagnostics.BroadphasePairs, 80u * 79u / 2u);
    for (unsigned pair = 0; pair < pairCount; ++pair)
    {
        SCOPED_TRACE(pair);
        Physics::World separate;
        ASSERT_TRUE(separate.AddBody(boxAt(10.0f * pair)).IsValid());
        ASSERT_TRUE(separate.AddBody(boxAt(10.0f * pair + 1.5f)).IsValid());
        const auto reference = separate.ComputeCollisionContacts();
        ASSERT_EQ(reference.Contacts.size(), 1u);
        const auto& actual = contacts.Contacts[pair];
        const auto& expected = reference.Contacts.front();
        EXPECT_EQ(actual.A.Body.Index, 2u * pair);
        EXPECT_EQ(actual.B.Body.Index, 2u * pair + 1u);
        EXPECT_NEAR(actual.PenetrationDepth, 0.5f, 1.0e-4f);
        EXPECT_FLOAT_EQ(actual.PenetrationDepth, expected.PenetrationDepth);
        EXPECT_TRUE(NearVec(actual.Normal, expected.Normal));
        EXPECT_TRUE(NearVec(actual.ContactPointA, expected.ContactPointA));
        EXPECT_TRUE(NearVec(actual.ContactPointB, expected.ContactPointB));
    }
}


TEST(PhysicsWorld, PositionProjectionUsesRemainingDepthAtEveryIteration)
{
    for (const unsigned iterations : {1u, 2u, 8u})
    {
        SCOPED_TRACE(iterations);
        Physics::World world;
        const auto a = world.AddBody(DynamicSphereAt(glm::vec3(0.0f)));
        const auto b = world.AddBody(DynamicSphereAt(glm::vec3(0.8f, 0.0f, 0.0f)));
        ASSERT_TRUE(a.IsValid() && b.IsValid());
        Physics::SolverSettings settings;
        settings.MaxIterations = iterations;
        settings.EnableSleep = false;
        const auto result = world.SolveStep(ZeroGravityStep(), settings);
        ASSERT_EQ(result.Status, Physics::ValidationStatus::Valid);
        const auto* bodyA = world.GetBody(a);
        const auto* bodyB = world.GetBody(b);
        ASSERT_NE(bodyA, nullptr);
        ASSERT_NE(bodyB, nullptr);
        const double separation = bodyB->Pose.Position.x - bodyA->Pose.Position.x;
        const double expected = 1.0 - 0.001 - (0.2 - 0.001) * std::pow(0.2, iterations);
        EXPECT_NEAR(separation, expected, 2.0e-5);
        EXPECT_LE(separation, 1.0);
        EXPECT_NEAR(bodyA->Pose.Position.x + bodyB->Pose.Position.x, 0.8, 2.0e-5);
        EXPECT_LE(result.MaxPenetrationAfter, result.MaxPenetrationBefore);
        EXPECT_EQ(result.Solve, iterations == 8u ? Physics::SolveStatus::Converged : Physics::SolveStatus::MaxIterationsReached);
    }
}

TEST(PhysicsWorld, IntegrationOverflowPreservesPreviousFiniteState)
{
    for (const bool solve : {false, true})
    {
        SCOPED_TRACE(solve);
        Physics::World world;
        auto descriptor = DynamicSphereAt(glm::vec3(1.0f, 2.0f, 3.0f));
        descriptor.GravityScale = 2.0f;
        const auto handle = world.AddBody(descriptor);
        ASSERT_TRUE(handle.IsValid());
        Physics::StepInput input;
        input.DeltaSeconds = 1.0f;
        input.Gravity = glm::vec3(0.0f, -std::numeric_limits<float>::max(), 0.0f);
        if (solve)
        {
            Physics::SolverSettings settings;
            settings.EnableSleep = false;
            const auto result = world.SolveStep(input, settings);
            EXPECT_EQ(result.Status, Physics::ValidationStatus::Valid);
            EXPECT_NE(result.Integration.Status, Physics::ValidationStatus::Valid);
            EXPECT_EQ(result.Solve, Physics::SolveStatus::Degraded);
        }
        else
        {
            EXPECT_NE(world.Step(input).Status, Physics::ValidationStatus::Valid);
        }
        const auto* body = world.GetBody(handle);
        ASSERT_NE(body, nullptr);
        EXPECT_TRUE(Physics::IsFinite(body->Pose.Position));
        EXPECT_TRUE(Physics::IsFinite(body->LinearVelocity));
        EXPECT_TRUE(Physics::IsFinite(body->AngularVelocity));
        EXPECT_TRUE(NearVec(body->Pose.Position, descriptor.Pose.Position));
        EXPECT_TRUE(NearVec(body->LinearVelocity, descriptor.LinearVelocity));
        EXPECT_TRUE(NearVec(body->AngularVelocity, descriptor.AngularVelocity));
    }
}

TEST(PhysicsWorld, NumericalFailureRestoresFiniteContactIsland)
{
    Physics::World world;
    const auto descriptor = DynamicSphereAt(glm::vec3(0.0f), 1.0e-40f);
    const auto dynamic = world.AddBody(descriptor);
    auto anchor = Physics::MakeStaticBody();
    anchor.Pose.Position.x = 0.8f;
    const auto fixed = world.AddBody(anchor);
    ASSERT_TRUE(dynamic.IsValid() && fixed.IsValid());
    Physics::SolverSettings settings;
    settings.EnableSleep = false;
    const auto result = world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_EQ(result.Solve, Physics::SolveStatus::Degraded);
    const auto* body = world.GetBody(dynamic);
    const auto* staticBody = world.GetBody(fixed);
    ASSERT_NE(body, nullptr);
    ASSERT_NE(staticBody, nullptr);
    EXPECT_TRUE(Physics::IsFinite(body->Pose.Position));
    EXPECT_TRUE(Physics::IsFinite(body->LinearVelocity));
    EXPECT_TRUE(NearVec(body->Pose.Position, descriptor.Pose.Position));
    EXPECT_TRUE(NearVec(body->LinearVelocity, descriptor.LinearVelocity));
    EXPECT_TRUE(NearVec(staticBody->Pose.Position, anchor.Pose.Position));
    EXPECT_TRUE(NearVec(staticBody->LinearVelocity, anchor.LinearVelocity));
    const auto collision = world.ComputeCollisionContacts();
    ASSERT_EQ(collision.Contacts.size(), 1u);
    EXPECT_TRUE(std::isfinite(collision.Contacts[0].PenetrationDepth));
}

TEST(PhysicsWorld, NonDynamicContactsDoNotCountAsSolverResidual)
{
    for (const bool kinematic : {false, true})
    {
        SCOPED_TRACE(kinematic);
        Physics::World world;
        auto a = Physics::MakeStaticBody();
        auto b = kinematic ? Physics::MakeKinematicBody() : Physics::MakeStaticBody();
        b.Pose.Position.x = 0.8f;
        ASSERT_TRUE(world.AddBody(a).IsValid());
        ASSERT_TRUE(world.AddBody(b).IsValid());
        ASSERT_EQ(world.ComputeCollisionContacts().Contacts.size(), 1u);
        const auto result = world.SolveStep(ZeroGravityStep());
        EXPECT_EQ(result.Solve, Physics::SolveStatus::Converged);
        EXPECT_EQ(result.IterationsUsed, 0u);
        EXPECT_EQ(result.NonConvergedIslands, 0u);
        EXPECT_EQ(result.Islands.IslandedDynamicBodies, 0u);
        EXPECT_FLOAT_EQ(result.MaxPenetrationBefore, 0.0f);
        EXPECT_FLOAT_EQ(result.MaxPenetrationAfter, 0.0f);
        EXPECT_FLOAT_EQ(result.MaxNormalVelocityResidual, 0.0f);
    }
}


TEST(PhysicsWorld, ChildRotationComposesAfterBodyScaleForCollisionGeometry)
{
    auto box = StaticBoxAt(glm::vec3(0.0f), glm::vec3(1.0f, 0.25f, 0.25f));
    box.Pose.Scale = glm::vec3(2.0f, 1.0f, 1.0f);
    box.Shapes[0].Local.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    for (const bool inside : {true, false})
    {
        SCOPED_TRACE(inside);
        Physics::World world;
        auto probe = Physics::MakeStaticBody();
        probe.Shapes = {Physics::MakeSphere(0.1f)};
        probe.Pose.Position = inside ? glm::vec3(0.4f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.5f, 0.0f);
        ASSERT_TRUE(world.AddBody(box).IsValid());
        ASSERT_TRUE(world.AddBody(probe).IsValid());
        EXPECT_EQ(world.ComputeCollisionContacts().Contacts.size(), inside ? 1u : 0u);
    }
}

TEST(PhysicsWorld, UnsupportedComposedShearIsRejectedBeforeBodyMutation)
{
    Physics::World world;
    auto descriptor = StaticBoxAt(glm::vec3(0.0f), glm::vec3(1.0f));
    descriptor.Pose.Scale = glm::vec3(2.0f, 1.0f, 1.0f);
    const auto handle = world.AddBody(descriptor);
    ASSERT_TRUE(handle.IsValid());
    auto sheared = descriptor;
    sheared.Shapes[0].Local.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    EXPECT_EQ(Physics::Validate(sheared), Physics::ValidationStatus::UnsupportedShapeTransform);
    EXPECT_FALSE(world.AddBody(sheared).IsValid());
    EXPECT_FALSE(world.UpdateBody(handle, sheared));
    EXPECT_EQ(world.BodyCount(), 1u);
    const auto* stored = world.GetBody(handle);
    ASSERT_NE(stored, nullptr);
    ASSERT_EQ(stored->Shapes.size(), 1u);
    EXPECT_EQ(stored->Shapes[0].Local.Rotation, descriptor.Shapes[0].Local.Rotation);
    EXPECT_EQ(stored->Pose.Scale, descriptor.Pose.Scale);
}

TEST(PhysicsWorld, DynamicNonUniformScaleIsRejectedBeforeBodyMutation)
{
    Physics::World world;
    const auto descriptor = DynamicSphere();
    const auto handle = world.AddBody(descriptor);
    ASSERT_TRUE(handle.IsValid());
    auto unsupported = descriptor;
    unsupported.Pose.Scale = glm::vec3(1.0f, 2.0f, 1.0f);
    EXPECT_EQ(Physics::Validate(unsupported), Physics::ValidationStatus::UnsupportedShapeTransform);
    EXPECT_FALSE(world.AddBody(unsupported).IsValid());
    EXPECT_FALSE(world.UpdateBody(handle, unsupported));
    EXPECT_EQ(world.BodyCount(), 1u);
    const auto* stored = world.GetBody(handle);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->Pose.Scale, descriptor.Pose.Scale);
}

TEST(PhysicsWorld, DisablingSleepWakesBeforeIntegration)
{
    Physics::World world;
    const auto handle = world.AddBody(DynamicSphere());
    ASSERT_TRUE(handle.IsValid());
    Physics::SolverSettings settings;
    settings.TimeToSleepSeconds = 0.0f;
    (void)world.SolveStep(ZeroGravityStep(), settings);
    ASSERT_TRUE(world.IsBodyAsleep(handle));
    settings.EnableSleep = false;
    const auto result = world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_FALSE(world.IsBodyAsleep(handle));
    EXPECT_EQ(result.Sleep.WakeTransitions, 1u);
    EXPECT_EQ(result.Integration.DynamicBodiesIntegrated, 1u);
    EXPECT_EQ(result.Sleep.AwakeDynamicBodies, 1u);
}

TEST(PhysicsWorld, ChangedGravityWakesBeforeIntegration)
{
    Physics::World world;
    const auto handle = world.AddBody(DynamicSphere());
    ASSERT_TRUE(handle.IsValid());
    Physics::SolverSettings settings;
    settings.TimeToSleepSeconds = 0.0f;
    (void)world.SolveStep(ZeroGravityStep(), settings);
    ASSERT_TRUE(world.IsBodyAsleep(handle));
    auto input = ZeroGravityStep();
    input.DeltaSeconds = 1.0f;
    input.Gravity.y = -1.0f;
    const auto result = world.SolveStep(input, settings);
    EXPECT_FALSE(world.IsBodyAsleep(handle));
    EXPECT_EQ(result.Sleep.WakeTransitions, 1u);
    EXPECT_EQ(result.Integration.DynamicBodiesIntegrated, 1u);
    const auto* body = world.GetBody(handle);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Pose.Position, glm::vec3(0.0f, -1.0f, 0.0f));
    EXPECT_EQ(body->LinearVelocity, glm::vec3(0.0f, -1.0f, 0.0f));
}

TEST(PhysicsWorld, MovingKinematicContactWakesSleepingDynamic)
{
    Physics::World world;
    const auto dynamic = world.AddBody(DynamicSphere());
    auto pusher = Physics::MakeKinematicBody();
    pusher.Pose.Position.x = 1.1f;
    pusher.LinearVelocity.x = -1.0f;
    ASSERT_TRUE(world.AddBody(pusher).IsValid());
    ASSERT_TRUE(dynamic.IsValid());
    Physics::SolverSettings settings;
    settings.TimeToSleepSeconds = 0.0f;
    auto input = ZeroGravityStep();
    input.DeltaSeconds = 0.01f;
    (void)world.SolveStep(input, settings);
    ASSERT_TRUE(world.IsBodyAsleep(dynamic));
    ASSERT_TRUE(world.ComputeCollisionContacts().Contacts.empty());
    input.DeltaSeconds = 0.2f;
    const auto result = world.SolveStep(input, settings);
    EXPECT_FALSE(world.IsBodyAsleep(dynamic));
    EXPECT_EQ(result.Sleep.WakeTransitions, 1u);
    EXPECT_GT(result.ContactsSolved, 0u);
    const auto* body = world.GetBody(dynamic);
    ASSERT_NE(body, nullptr);
    EXPECT_LT(body->LinearVelocity.x, -0.5f);
}

TEST(PhysicsWorld, AnchorMutationsWakeSleepingDynamics)
{
    for (const int mutation : {0, 1, 2})
    {
        SCOPED_TRACE(mutation);
        Physics::World world;
        auto anchor = StaticBoxAt(glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(0.5f));
        const auto anchorHandle = world.AddBody(anchor);
        const auto dynamic = world.AddBody(DynamicSphere());
        ASSERT_TRUE(anchorHandle.IsValid() && dynamic.IsValid());
        Physics::SolverSettings settings;
        settings.TimeToSleepSeconds = 0.0f;
        (void)world.SolveStep(ZeroGravityStep(), settings);
        ASSERT_TRUE(world.IsBodyAsleep(dynamic));
        anchor.Pose.Position.x = 101.0f;
        if (mutation == 0)
            ASSERT_TRUE(world.AddBody(anchor).IsValid());
        else if (mutation == 1)
            ASSERT_TRUE(world.UpdateBody(anchorHandle, anchor));
        else
            ASSERT_TRUE(world.DestroyBody(anchorHandle));
        EXPECT_FALSE(world.IsBodyAsleep(dynamic));
        settings.TimeToSleepSeconds = 1.0f;
        const auto first = world.SolveStep(ZeroGravityStep(), settings);
        EXPECT_FALSE(world.IsBodyAsleep(dynamic));
        EXPECT_EQ(first.Integration.DynamicBodiesIntegrated, 1u);
    }
}

TEST(PhysicsWorld, RemovingSupportWakesSleeperUnderUnchangedGravity)
{
    Physics::World world;
    const auto support = world.AddBody(StaticBoxAt(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(5.0f, 0.5f, 5.0f)));
    const auto dynamic = world.AddBody(DynamicSphereAt(glm::vec3(0.0f, 0.5f, 0.0f)));
    ASSERT_TRUE(support.IsValid() && dynamic.IsValid());
    Physics::SolverSettings settings;
    settings.TimeToSleepSeconds = 0.0f;
    auto input = ZeroGravityStep();
    input.DeltaSeconds = 0.01f;
    input.Gravity.y = -1.0f;
    (void)world.SolveStep(input, settings);
    ASSERT_TRUE(world.IsBodyAsleep(dynamic));
    const auto* before = world.GetBody(dynamic);
    ASSERT_NE(before, nullptr);
    const float previousY = before->Pose.Position.y;
    ASSERT_TRUE(world.DestroyBody(support));
    input.DeltaSeconds = 1.0f;
    const auto result = world.SolveStep(input, settings);
    EXPECT_FALSE(world.IsBodyAsleep(dynamic));
    EXPECT_EQ(result.Integration.DynamicBodiesIntegrated, 1u);
    const auto* body = world.GetBody(dynamic);
    ASSERT_NE(body, nullptr);
    EXPECT_FLOAT_EQ(body->Pose.Position.y, previousY - 1.0f);
    EXPECT_EQ(body->LinearVelocity, glm::vec3(0.0f, -1.0f, 0.0f));
}


TEST(PhysicsWorld, DegradedIslandDoesNotShortenIndependentIslandSolve)
{
    Physics::World world;
    const auto tiny = world.AddBody(DynamicSphereAt(glm::vec3(100.0f, 0.0f, 0.0f), 1.0e-40f));
    auto anchor = Physics::MakeStaticBody();
    anchor.Pose.Position.x = 100.8f;
    ASSERT_TRUE(world.AddBody(anchor).IsValid());
    const auto a = world.AddBody(DynamicSphereAt(glm::vec3(0.0f)));
    const auto b = world.AddBody(DynamicSphereAt(glm::vec3(0.8f, 0.0f, 0.0f)));
    ASSERT_TRUE(tiny.IsValid() && a.IsValid() && b.IsValid());
    Physics::SolverSettings settings;
    settings.MaxIterations = 8u;
    settings.EnableSleep = false;
    const auto result = world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_EQ(result.Status, Physics::ValidationStatus::Valid);
    EXPECT_EQ(result.Solve, Physics::SolveStatus::Degraded);
    EXPECT_EQ(result.IterationsUsed, 8u);
    const auto* bodyA = world.GetBody(a);
    const auto* bodyB = world.GetBody(b);
    const auto* failed = world.GetBody(tiny);
    ASSERT_NE(bodyA, nullptr);
    ASSERT_NE(bodyB, nullptr);
    ASSERT_NE(failed, nullptr);
    EXPECT_NEAR(bodyB->Pose.Position.x - bodyA->Pose.Position.x, 0.99899949056, 2.0e-5);
    EXPECT_EQ(failed->Pose.Position, glm::vec3(100.0f, 0.0f, 0.0f));
    EXPECT_EQ(failed->LinearVelocity, glm::vec3(0.0f));
}


TEST(PhysicsWorld, SleepingIslandsDoNotReportUnsolvedResiduals)
{
    Physics::World world;
    const auto a = world.AddBody(DynamicSphereAt(glm::vec3(0.0f)));
    const auto b = world.AddBody(DynamicSphereAt(glm::vec3(0.8f, 0.0f, 0.0f)));
    ASSERT_TRUE(a.IsValid() && b.IsValid());
    Physics::SolverSettings settings;
    settings.MaxIterations = 1u;
    settings.TimeToSleepSeconds = 0.0f;
    (void)world.SolveStep(ZeroGravityStep(), settings);
    ASSERT_TRUE(world.IsBodyAsleep(a) && world.IsBodyAsleep(b));
    ASSERT_FALSE(world.ComputeCollisionContacts().Contacts.empty());
    const auto positionA = world.GetBody(a)->Pose.Position;
    const auto positionB = world.GetBody(b)->Pose.Position;
    const auto result = world.SolveStep(ZeroGravityStep(), settings);
    EXPECT_EQ(result.Integration.DynamicBodiesIntegrated, 0u);
    EXPECT_EQ(result.IterationsUsed, 0u);
    EXPECT_EQ(result.ContactsSolved, 0u);
    EXPECT_EQ(result.NonConvergedIslands, 0u);
    EXPECT_EQ(result.Solve, Physics::SolveStatus::Converged);
    EXPECT_FLOAT_EQ(result.MaxPenetrationBefore, 0.0f);
    EXPECT_FLOAT_EQ(result.MaxPenetrationAfter, 0.0f);
    EXPECT_FLOAT_EQ(result.MaxNormalVelocityResidual, 0.0f);
    EXPECT_EQ(world.GetBody(a)->Pose.Position, positionA);
    EXPECT_EQ(world.GetBody(b)->Pose.Position, positionB);
}

TEST(PhysicsWorld, DynamicSupportRemovalOrTeleportWakesSleepingStack)
{
    for (const bool teleport : {false, true})
    {
        SCOPED_TRACE(teleport);
        Physics::World world;
        ASSERT_TRUE(world.AddBody(StaticBoxAt(glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(5.0f, 0.5f, 5.0f))).IsValid());
        const auto bottom = world.AddBody(DynamicSphereAt(glm::vec3(0.0f, 0.5f, 0.0f)));
        const auto top = world.AddBody(DynamicSphereAt(glm::vec3(0.0f, 1.5f, 0.0f)));
        ASSERT_TRUE(bottom.IsValid() && top.IsValid());
        Physics::SolverSettings settings;
        settings.TimeToSleepSeconds = 0.0f;
        auto input = ZeroGravityStep();
        input.DeltaSeconds = 0.01f;
        input.Gravity.y = -1.0f;
        (void)world.SolveStep(input, settings);
        ASSERT_TRUE(world.IsBodyAsleep(bottom) && world.IsBodyAsleep(top));
        const float previousY = world.GetBody(top)->Pose.Position.y;
        if (teleport)
        {
            auto descriptor = *world.GetBody(bottom);
            descriptor.Pose.Position.x = 100.0f;
            ASSERT_TRUE(world.UpdateBody(bottom, descriptor));
        }
        else
            ASSERT_TRUE(world.DestroyBody(bottom));
        EXPECT_FALSE(world.IsBodyAsleep(top));
        input.DeltaSeconds = 0.1f;
        const auto result = world.SolveStep(input, settings);
        EXPECT_GE(result.Integration.DynamicBodiesIntegrated, 1u);
        EXPECT_FALSE(world.IsBodyAsleep(top));
        EXPECT_NEAR(world.GetBody(top)->Pose.Position.y, previousY - 0.01f, 1.0e-6f);
        EXPECT_NEAR(world.GetBody(top)->LinearVelocity.y, -0.1f, 1.0e-6f);
    }
}


TEST(PhysicsWorld, AdmittedChildQuaternionTolerancePreservesRepresentableShapeTransforms)
{
    // Descriptor validation accepts this small norm error. Shape admission must
    // use the normalized rotation, including at scales that amplify float noise.
    const auto rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f)) *
        std::sqrt(1.0009f);
    for (const int configuration : {0, 1, 2})
    {
        SCOPED_TRACE(configuration);
        auto descriptor = configuration == 0
            ? Physics::MakeStaticBody() : Physics::MakeDynamicBody();
        descriptor.Shapes = {Physics::MakeBox(glm::vec3(0.5f))};
        descriptor.Shapes[0].Local.Rotation = rotation;
        descriptor.Pose.Scale = configuration == 0 ? glm::vec3(2.0f, 1.0f, 1.0f) :
            glm::vec3(configuration == 1 ? 1.0f : 1.0e6f);
        EXPECT_EQ(Physics::Validate(descriptor), Physics::ValidationStatus::Valid);
        Physics::World world;
        EXPECT_TRUE(world.AddBody(descriptor).IsValid());
        EXPECT_EQ(world.BodyCount(), 1u);
    }
}


TEST(PhysicsWorld, IntegrationRejectsFinitePoseWithOverflowedChildWorldPosition)
{
    for (const bool solve : {false, true})
    {
        for (const bool overflow : {false, true})
        {
            SCOPED_TRACE(solve);
            SCOPED_TRACE(overflow);
            Physics::World world;
            auto descriptor = DynamicSphere();
            const float limit = std::numeric_limits<float>::max();
            descriptor.Shapes[0].Local.Position.x = limit * 0.75f;
            descriptor.LinearVelocity.x = limit * (overflow ? 0.75f : 0.125f);
            const auto handle = world.AddBody(descriptor);
            ASSERT_TRUE(handle.IsValid());
            auto input = ZeroGravityStep();
            input.DeltaSeconds = 1.0f;
            const auto expected = overflow ? Physics::ValidationStatus::NumericalFailure : Physics::ValidationStatus::Valid;
            if (solve)
            {
                Physics::SolverSettings settings;
                settings.EnableSleep = false;
                const auto result = world.SolveStep(input, settings);
                EXPECT_EQ(result.Status, Physics::ValidationStatus::Valid);
                EXPECT_EQ(result.Integration.Status, expected);
                EXPECT_EQ(result.Solve, overflow ? Physics::SolveStatus::Degraded : Physics::SolveStatus::Converged);
            }
            else
                EXPECT_EQ(world.Step(input).Status, expected);
            const auto* body = world.GetBody(handle);
            ASSERT_NE(body, nullptr);
            EXPECT_TRUE(Physics::IsFinite(body->Pose.Position));
            EXPECT_EQ(body->Pose.Position.x, overflow ? 0.0f : descriptor.LinearVelocity.x);
            EXPECT_EQ(body->LinearVelocity, descriptor.LinearVelocity);
            EXPECT_EQ(Physics::Validate(*body), Physics::ValidationStatus::Valid);
        }
    }
}
