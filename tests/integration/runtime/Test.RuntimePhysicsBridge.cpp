#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

import Extrinsic.ECS.Component.Collider;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.RigidBody;
import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Physics.World;
import Extrinsic.Runtime.PhysicsBridge;

namespace Collider = Extrinsic::ECS::Components::Collider;
namespace RigidBody = Extrinsic::ECS::Components::RigidBody;
namespace Transform = Extrinsic::ECS::Components::Transform;
namespace Components = Extrinsic::ECS::Components;
namespace Physics = Extrinsic::Physics;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::PhysicsBridge;
using Extrinsic::Runtime::PhysicsBridgeFixedStepConfig;

namespace
{
    [[nodiscard]] Components::StableId Id(std::uint64_t low)
    {
        return Components::StableId{0xA11CEu, low};
    }

    [[nodiscard]] EntityHandle CreateAuthoredBody(Registry& scene,
                                                  Components::StableId stableId,
                                                  RigidBody::Component rigidBody,
                                                  glm::vec3 position = glm::vec3(0.0f))
    {
        EntityHandle entity = scene.Create();
        auto& raw = scene.Raw();
        raw.emplace<Components::StableId>(entity, stableId);
        Transform::Component transform{};
        transform.Position = position;
        raw.emplace<Transform::Component>(entity, transform);
        raw.emplace<Collider::Component>(entity, Collider::Component{{Collider::MakeSphere(0.5f)}, true});
        raw.emplace<RigidBody::Component>(entity, rigidBody);
        return entity;
    }
}

TEST(RuntimePhysicsBridge, SyncAuthoringCreatesPhysicsBodyFromStableEntity)
{
    Registry scene;
    PhysicsBridge bridge;
    const Components::StableId stableId = Id(1u);
    (void)CreateAuthoredBody(scene, stableId, RigidBody::MakeDynamic(2.0f), glm::vec3(0.0f, 3.0f, 0.0f));

    const auto& diagnostics = bridge.SyncAuthoring(scene);
    EXPECT_EQ(diagnostics.SidecarCount, 1u);
    EXPECT_EQ(diagnostics.BodiesCreated, 1u);
    EXPECT_EQ(bridge.GetWorld().BodyCount(), 1u);

    const std::optional<Physics::BodyHandle> handle = bridge.ResolveBody(stableId);
    ASSERT_TRUE(handle.has_value());
    const Physics::BodyDescriptor* body = bridge.GetWorld().GetBody(*handle);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Motion, Physics::MotionType::Dynamic);
    EXPECT_FLOAT_EQ(body->Mass, 2.0f);
    EXPECT_FLOAT_EQ(body->Pose.Position.y, 3.0f);
}

TEST(RuntimePhysicsBridge, StableIdentityReusesHandleAndRemovalDestroysWorldBody)
{
    Registry scene;
    PhysicsBridge bridge;
    const Components::StableId stableId = Id(2u);
    const EntityHandle entity = CreateAuthoredBody(scene, stableId, RigidBody::MakeDynamic(1.0f));

    bridge.SyncAuthoring(scene);
    const std::optional<Physics::BodyHandle> first = bridge.ResolveBody(stableId);
    ASSERT_TRUE(first.has_value());

    bridge.SyncAuthoring(scene);
    const std::optional<Physics::BodyHandle> second = bridge.ResolveBody(stableId);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
    EXPECT_EQ(bridge.GetWorld().BodyCount(), 1u);

    scene.Destroy(entity);
    bridge.SyncAuthoring(scene);
    EXPECT_FALSE(bridge.ResolveBody(stableId).has_value());
    EXPECT_EQ(bridge.GetWorld().BodyCount(), 0u);
    EXPECT_GE(bridge.GetDiagnostics().BodiesRemoved, 1u);
}

TEST(RuntimePhysicsBridge, FixedStepRunsSyncThenStepThenDynamicWriteback)
{
    Registry scene;
    PhysicsBridge bridge;
    const Components::StableId stableId = Id(3u);
    RigidBody::Component body = RigidBody::MakeDynamic(1.0f);
    body.LinearVelocity = glm::vec3(0.0f, 0.0f, 0.0f);
    const EntityHandle entity = CreateAuthoredBody(scene, stableId, body, glm::vec3(0.0f, 10.0f, 0.0f));

    PhysicsBridgeFixedStepConfig config{};
    config.FixedDeltaSeconds = 1.0f / 60.0f;
    config.Gravity = glm::vec3(0.0f, -9.6f, 0.0f);

    const auto& diagnostics = bridge.TickFixedStep(scene, 1.0f / 60.0f, config);
    EXPECT_EQ(diagnostics.FixedSteps, 1u);
    EXPECT_LT(diagnostics.LastSyncOrder, diagnostics.LastStepOrder);
    EXPECT_LT(diagnostics.LastStepOrder, diagnostics.LastWritebackOrder);
    EXPECT_EQ(diagnostics.DynamicWritebacks, 1u);

    const auto& transform = scene.Raw().get<Transform::Component>(entity);
    EXPECT_LT(transform.Position.y, 10.0f);
    EXPECT_TRUE(scene.Raw().all_of<Transform::IsDirtyTag>(entity));
    EXPECT_TRUE(scene.Raw().all_of<Transform::WorldUpdatedTag>(entity));
}

TEST(RuntimePhysicsBridge, WritebackSkipsStaticAndKinematicBodies)
{
    Registry scene;
    PhysicsBridge bridge;
    const EntityHandle staticEntity = CreateAuthoredBody(scene, Id(4u), RigidBody::MakeStatic(), glm::vec3(0.0f));
    RigidBody::Component kinematic = RigidBody::MakeKinematic();
    kinematic.LinearVelocity = glm::vec3(0.0f, 5.0f, 0.0f);
    const EntityHandle kinematicEntity = CreateAuthoredBody(scene, Id(5u), kinematic, glm::vec3(0.0f));

    bridge.TickFixedStep(scene, 1.0f / 30.0f);

    EXPECT_FLOAT_EQ(scene.Raw().get<Transform::Component>(staticEntity).Position.y, 0.0f);
    EXPECT_FLOAT_EQ(scene.Raw().get<Transform::Component>(kinematicEntity).Position.y, 0.0f);
    EXPECT_GE(bridge.GetDiagnostics().StaticWritebacksSkipped, 1u);
    EXPECT_GE(bridge.GetDiagnostics().KinematicWritebacksSkipped, 1u);
}

TEST(RuntimePhysicsBridge, InvalidAndMissingAuthoringDiagnosticsAreReported)
{
    Registry scene;
    PhysicsBridge bridge;
    auto& raw = scene.Raw();

    EntityHandle missingCollider = scene.Create();
    raw.emplace<Components::StableId>(missingCollider, Id(6u));
    raw.emplace<Transform::Component>(missingCollider);
    raw.emplace<RigidBody::Component>(missingCollider, RigidBody::MakeDynamic(1.0f));

    EntityHandle missingStable = scene.Create();
    raw.emplace<Transform::Component>(missingStable);
    raw.emplace<Collider::Component>(missingStable, Collider::Component{{Collider::MakeSphere(0.5f)}, true});

    bridge.SyncAuthoring(scene);

    EXPECT_EQ(bridge.GetDiagnostics().MissingAuthoringComponents, 1u);
    EXPECT_EQ(bridge.GetDiagnostics().MissingStableIds, 1u);
    EXPECT_EQ(bridge.GetWorld().BodyCount(), 0u);
}
