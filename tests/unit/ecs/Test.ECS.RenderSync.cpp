#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.System.BoundsPropagation;
import Extrinsic.ECS.System.RenderSync;
import Extrinsic.ECS.System.TransformHierarchy;
import Extrinsic.ECS.Component.Culling.World;
import Geometry.AABB;
import Geometry.Sphere;

using Extrinsic::Core::FrameGraph;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Hierarchy::Attach;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::Registry;
namespace Components = Extrinsic::ECS::Components;
namespace BoundsSystem = Extrinsic::ECS::Systems::BoundsPropagation;
namespace RenderSyncSystem = Extrinsic::ECS::Systems::RenderSync;
namespace TransformSystem = Extrinsic::ECS::Systems::TransformHierarchy;

TEST(ECSRenderSync, ForwardsWorldUpdatedTagToDirtyTransformAndClearsSource)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle entity = CreateDefault(scene, "Forwarded");
    raw.emplace_or_replace<Components::Transform::WorldUpdatedTag>(entity);

    RenderSyncSystem::Stats stats{};
    RenderSyncSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.WorldUpdatedObserved, 1u);
    EXPECT_EQ(stats.DirtyTransformStamped, 1u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 1u);
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(entity));
}

TEST(ECSRenderSync, IsNoOpWhenNoWorldUpdatedTagsArePresent)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle entity = CreateDefault(scene, "Untouched");

    RenderSyncSystem::Stats stats{};
    RenderSyncSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.WorldUpdatedObserved, 0u);
    EXPECT_EQ(stats.DirtyTransformStamped, 0u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 0u);
    EXPECT_FALSE(raw.all_of<Components::DirtyTags::DirtyTransform>(entity));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
}

TEST(ECSRenderSync, RefreshesExistingDirtyTransformWithoutDuplicating)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle entity = CreateDefault(scene, "Already");
    raw.emplace_or_replace<Components::DirtyTags::DirtyTransform>(entity);
    raw.emplace_or_replace<Components::Transform::WorldUpdatedTag>(entity);

    RenderSyncSystem::Stats stats{};
    RenderSyncSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.WorldUpdatedObserved, 1u);
    EXPECT_EQ(stats.DirtyTransformStamped, 1u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 1u);
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(entity));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
}

TEST(ECSRenderSync, LeavesDirtyTransformOnEntitiesWithoutWorldUpdatedTag)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle producer = CreateDefault(scene, "Producer");
    const EntityHandle preexisting = CreateDefault(scene, "Preexisting");

    raw.emplace_or_replace<Components::Transform::WorldUpdatedTag>(producer);
    raw.emplace_or_replace<Components::DirtyTags::DirtyTransform>(preexisting);

    RenderSyncSystem::Stats stats{};
    RenderSyncSystem::OnUpdate(raw, stats);

    // The producer entry is forwarded; the unrelated DirtyTransform is left
    // alone for whichever consumer eventually drains it.
    EXPECT_EQ(stats.WorldUpdatedObserved, 1u);
    EXPECT_EQ(stats.DirtyTransformStamped, 1u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 1u);
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(producer));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(preexisting));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(producer));
}

TEST(ECSRenderSync, AccumulatesStatsAcrossMultipleEntities)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle a = CreateDefault(scene, "A");
    const EntityHandle b = CreateDefault(scene, "B");
    const EntityHandle c = CreateDefault(scene, "C");

    raw.emplace_or_replace<Components::Transform::WorldUpdatedTag>(a);
    raw.emplace_or_replace<Components::Transform::WorldUpdatedTag>(b);
    raw.emplace_or_replace<Components::Transform::WorldUpdatedTag>(c);

    RenderSyncSystem::Stats stats{};
    RenderSyncSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.WorldUpdatedObserved, 3u);
    EXPECT_EQ(stats.DirtyTransformStamped, 3u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 3u);
    for (const EntityHandle e : {a, b, c})
    {
        EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(e));
        EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(e));
    }
}

TEST(ECSRenderSync, FrameGraphPipelineForwardsTagsAfterTransformAndBoundsPasses)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle parent = CreateDefault(scene, "Parent");
    const EntityHandle child = CreateDefault(scene, "Child");
    Attach(raw, child, parent);

    raw.get<Components::Transform::Component>(parent).Position = glm::vec3(1.0f, 0.0f, 0.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(parent);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(child);

    auto& bounds = raw.emplace<Components::Culling::Local::Bounds>(parent);
    bounds.LocalBoundingAABB.Min = glm::vec3(-1.0f);
    bounds.LocalBoundingAABB.Max = glm::vec3(1.0f);
    bounds.LocalBoundingSphere.Center = glm::vec3(0.0f);
    bounds.LocalBoundingSphere.Radius = 1.0f;

    FrameGraph graph;
    TransformSystem::RegisterSystem(graph, raw);
    BoundsSystem::RegisterSystem(graph, raw);
    RenderSyncSystem::RegisterSystem(graph, raw);

    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    // BoundsPropagation observed the WorldUpdatedTag before RenderSync
    // cleared it: the parent has a freshly recomputed world bounds.
    EXPECT_TRUE(raw.all_of<Components::Culling::World::Bounds>(parent));

    // RenderSync forwarded the producer signal into DirtyTransform and
    // cleared WorldUpdatedTag for the next substep.
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(parent));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(child));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(parent));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(child));
}
