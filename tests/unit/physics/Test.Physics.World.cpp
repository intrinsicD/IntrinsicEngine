#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

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
    ASSERT_TRUE(world.AddBody(nonUniform).IsValid());

    const Physics::CollisionResult collisions = world.ComputeCollisionContacts();
    EXPECT_EQ(collisions.Diagnostics.FilteredBodiesSkipped, 1u);
    EXPECT_EQ(collisions.Diagnostics.DynamicNonUniformScaleRejects, 1u);
    EXPECT_EQ(collisions.Diagnostics.LastRejectReason, Physics::CollisionRejectReason::NonUniformDynamicScale);
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
// convention regardless of the geometry kernel's per-pair orientation
// (the kernel's sphere-box analytic and GJK/EPA fallback paths currently
// return B->A normals; tracked as BUG-025). A sphere above a static box
// must report a downward normal so the solver separates instead of
// tunnelling.
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

TEST(PhysicsWorld, SolveStepSeparatesOverlappingSpheresAndConverges)
{
    Physics::World world;
    const Physics::BodyHandle a = world.AddBody(DynamicSphereAt({0.0f, 0.0f, 0.0f}));
    const Physics::BodyHandle b = world.AddBody(DynamicSphereAt({0.8f, 0.0f, 0.0f}));
    ASSERT_TRUE(a.IsValid() && b.IsValid());

    const Physics::SolveStepDiagnostics diagnostics = world.SolveStep(ZeroGravityStep());

    EXPECT_EQ(diagnostics.Status, Physics::ValidationStatus::Valid);
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

    // A sleeping body is not integrated by SolveStep, even under gravity.
    const glm::vec3 positionBefore = world.GetBody(handle)->Pose.Position;
    (void)world.SolveStep(Physics::StepInput{}, settings);
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
    // Small iteration budget keeps the captured-manifold positional
    // projection close to the true penetration so the resting sphere is not
    // catapulted; two passes fully clear the slop-adjusted depth.
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
