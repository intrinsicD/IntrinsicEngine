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
