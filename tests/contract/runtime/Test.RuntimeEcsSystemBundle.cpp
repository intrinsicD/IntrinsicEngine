#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EcsSystemBundle;
import Geometry.AABB;

using Extrinsic::Core::FrameGraph;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Hierarchy::Attach;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::PromotedEcsSystemBundleStats;
using Extrinsic::Runtime::RegisterPromotedEcsSystemBundle;
namespace Components = Extrinsic::ECS::Components;

namespace
{
    bool MatricesNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
    {
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                if (std::abs(a[col][row] - b[col][row]) > eps) return false;
        return true;
    }
}

TEST(RuntimeEcsSystemBundle, RegisterAddsTransformAndBoundsPasses)
{
    Registry scene;
    FrameGraph graph;

    const PromotedEcsSystemBundleStats stats =
        RegisterPromotedEcsSystemBundle(graph, scene);

    EXPECT_EQ(stats.Registered, 2u);
    EXPECT_TRUE(stats.TransformHierarchyRegistered);
    EXPECT_TRUE(stats.BoundsPropagationRegistered);
    EXPECT_EQ(graph.PassCount(), 2u);
}

TEST(RuntimeEcsSystemBundle, BundleExecutionPropagatesDirtyChildWorldMatrix)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle parent = CreateDefault(scene, "Parent");
    const EntityHandle child = CreateDefault(scene, "Child");
    Attach(raw, child, parent);

    raw.get<Components::Transform::Component>(parent).Position = glm::vec3(10.0f, 0.0f, 0.0f);
    raw.get<Components::Transform::Component>(child).Position = glm::vec3(0.0f, 4.0f, 0.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(parent);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(child);

    FrameGraph graph;
    (void)RegisterPromotedEcsSystemBundle(graph, scene);

    const auto compileResult = graph.Compile();
    ASSERT_TRUE(compileResult.has_value());
    const auto executeResult = graph.Execute();
    ASSERT_TRUE(executeResult.has_value());

    const glm::mat4 expectedParent =
        glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f));
    const glm::mat4 expectedChild =
        expectedParent * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 4.0f, 0.0f));

    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(parent).Matrix, expectedParent));
    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(child).Matrix, expectedChild));
    EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(parent));
    EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(child));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldUpdatedTag>(parent));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldUpdatedTag>(child));
}

TEST(RuntimeEcsSystemBundle, BundleExecutionPropagatesWorldBoundsAfterTransformUpdate)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle entity = CreateDefault(scene, "Bounded");

    auto& transform = raw.get<Components::Transform::Component>(entity);
    transform.Position = glm::vec3(2.0f, 0.0f, 0.0f);
    transform.Scale = glm::vec3(2.0f, 2.0f, 2.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(entity);

    auto& localBounds = raw.emplace_or_replace<Components::Culling::Local::Bounds>(entity);
    localBounds.LocalBoundingAABB.Min = glm::vec3(-1.0f, -1.0f, -1.0f);
    localBounds.LocalBoundingAABB.Max = glm::vec3(1.0f, 1.0f, 1.0f);
    localBounds.LocalBoundingSphere.Center = glm::vec3(0.0f);
    localBounds.LocalBoundingSphere.Radius = 1.0f;

    FrameGraph graph;
    (void)RegisterPromotedEcsSystemBundle(graph, scene);

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    ASSERT_TRUE(raw.all_of<Components::Culling::World::Bounds>(entity));
    const auto& world = raw.get<Components::Culling::World::Bounds>(entity);

    EXPECT_FLOAT_EQ(world.WorldBoundingOBB.Center.x, 2.0f);
    EXPECT_FLOAT_EQ(world.WorldBoundingOBB.Center.y, 0.0f);
    EXPECT_FLOAT_EQ(world.WorldBoundingOBB.Center.z, 0.0f);
    EXPECT_FLOAT_EQ(world.WorldBoundingOBB.Extents.x, 2.0f);
    EXPECT_FLOAT_EQ(world.WorldBoundingOBB.Extents.y, 2.0f);
    EXPECT_FLOAT_EQ(world.WorldBoundingOBB.Extents.z, 2.0f);
    EXPECT_FLOAT_EQ(world.WorldBoundingSphere.Radius, 2.0f);
}

TEST(RuntimeEcsSystemBundle, AppPassMutatingTransformRunsBeforeTransformHierarchy)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle entity = CreateDefault(scene, "AppMutated");

    FrameGraph graph;

    // App-style pass that runs in OnSimTick: write Transform::Component +
    // emplace IsDirtyTag so TransformHierarchy picks it up. Declaring Write
    // on Transform::Component creates a WAR edge against TransformHierarchy's
    // Read on the same component, forcing it to run first.
    bool appPassRan = false;
    graph.AddPass("App.MoveEntity",
        [](Extrinsic::Core::FrameGraphBuilder& b)
        {
            b.Write<Components::Transform::Component>();
            b.Write<Components::Transform::IsDirtyTag>();
        },
        [&raw, entity, &appPassRan]()
        {
            raw.get<Components::Transform::Component>(entity).Position =
                glm::vec3(5.0f, 6.0f, 7.0f);
            raw.emplace_or_replace<Components::Transform::IsDirtyTag>(entity);
            appPassRan = true;
        });

    (void)RegisterPromotedEcsSystemBundle(graph, scene);

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    EXPECT_TRUE(appPassRan);
    const glm::mat4 expected =
        glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 6.0f, 7.0f));
    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(entity).Matrix, expected));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
}
