#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string_view>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Extrinsic.Core.FrameGraph;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
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
import Extrinsic.Runtime.EcsSystemBundle;
import Geometry.AABB;

using Extrinsic::Core::FrameGraph;
using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Hierarchy::Attach;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::FlushPreRenderTransformState;
using Extrinsic::Runtime::PreRenderTransformFlushStats;
using Extrinsic::Runtime::PromotedEcsSystemBundleStats;
using Extrinsic::Runtime::RegisterPromotedEcsSystemBundle;
namespace Components = Extrinsic::ECS::Components;
namespace BoundsSystem = Extrinsic::ECS::Systems::BoundsPropagation;
namespace RenderSyncSystem = Extrinsic::ECS::Systems::RenderSync;
namespace TransformSystem = Extrinsic::ECS::Systems::TransformHierarchy;

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

TEST(RuntimeEcsSystemBundle, RegisterAddsTransformBoundsAndRenderSyncPasses)
{
    Registry scene;
    FrameGraph graph;

    const PromotedEcsSystemBundleStats stats =
        RegisterPromotedEcsSystemBundle(graph, scene);

    EXPECT_EQ(stats.Registered, 3u);
    EXPECT_TRUE(stats.TransformHierarchyRegistered);
    EXPECT_TRUE(stats.BoundsPropagationRegistered);
    EXPECT_TRUE(stats.RenderSyncRegistered);
    EXPECT_EQ(graph.PassCount(), 3u);
}

// BUG-105: each promoted baseline pass may add or remove components. Insert a
// reader directly after each pass so every StructuralWrite declaration is
// independently required to keep first-time EnTT storage creation off another
// reader's worker.
TEST(RuntimeEcsSystemBundle, EachBaselineStructuralWriterOrdersImmediateReader)
{
    Registry scene;
    FrameGraph graph;

    const auto addStructuralReader = [&](const std::string_view name)
    {
        graph.AddPass(
            name,
            [](Extrinsic::Core::FrameGraphBuilder& builder)
            {
                builder.StructuralRead();
            },
            []() {});
    };

    TransformSystem::RegisterSystem(graph, scene.Raw());
    addStructuralReader("Reader.AfterTransform");
    BoundsSystem::RegisterSystem(graph, scene.Raw());
    addStructuralReader("Reader.AfterBounds");
    RenderSyncSystem::RegisterSystem(graph, scene.Raw());
    addStructuralReader("Reader.AfterRenderSync");

    ASSERT_TRUE(graph.Compile().has_value());
    const auto& layers = graph.GetExecutionLayers();
    const auto findLayer = [&](const std::string_view expectedName)
    {
        for (std::size_t layer = 0; layer < layers.size(); ++layer)
        {
            for (const std::uint32_t pass : layers[layer])
            {
                if (graph.PassName(pass) == expectedName)
                    return layer;
            }
        }
        return layers.size();
    };

    const std::size_t transformLayer = findLayer(TransformSystem::PassName);
    const std::size_t transformReaderLayer = findLayer("Reader.AfterTransform");
    const std::size_t boundsLayer = findLayer(BoundsSystem::PassName);
    const std::size_t boundsReaderLayer = findLayer("Reader.AfterBounds");
    const std::size_t renderSyncLayer = findLayer(RenderSyncSystem::PassName);
    const std::size_t renderSyncReaderLayer = findLayer("Reader.AfterRenderSync");

    ASSERT_LT(transformLayer, layers.size());
    ASSERT_LT(transformReaderLayer, layers.size());
    ASSERT_LT(boundsLayer, layers.size());
    ASSERT_LT(boundsReaderLayer, layers.size());
    ASSERT_LT(renderSyncLayer, layers.size());
    ASSERT_LT(renderSyncReaderLayer, layers.size());
    EXPECT_LT(transformLayer, transformReaderLayer);
    EXPECT_LT(boundsLayer, boundsReaderLayer);
    EXPECT_LT(renderSyncLayer, renderSyncReaderLayer);
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
    // RenderSync runs at the tail of the bundle, forwards WorldUpdatedTag
    // into DirtyTransform for runtime extraction, and clears WorldUpdatedTag
    // so the producer/consumer cycle is closed within ECS.
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(parent));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(child));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(parent));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(child));
}

TEST(RuntimeEcsSystemBundle, ReplayReusesPlanAndCapturesCurrentScene)
{
    Registry firstScene;
    Registry secondScene;
    auto& firstRaw = firstScene.Raw();
    auto& secondRaw = secondScene.Raw();
    const EntityHandle firstEntity =
        CreateDefault(firstScene, "FirstReplayScene");
    const EntityHandle secondEntity =
        CreateDefault(secondScene, "SecondReplayScene");

    firstRaw.get<Components::Transform::Component>(firstEntity).Position =
        glm::vec3(1.0f, 0.0f, 0.0f);
    firstRaw.emplace_or_replace<Components::Transform::IsDirtyTag>(
        firstEntity);
    secondRaw.get<Components::Transform::Component>(secondEntity).Position =
        glm::vec3(7.0f, 0.0f, 0.0f);
    secondRaw.emplace_or_replace<Components::Transform::IsDirtyTag>(
        secondEntity);

    FrameGraph graph;
    (void)RegisterPromotedEcsSystemBundle(graph, firstScene);
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    const glm::mat4 firstExpected =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(MatricesNear(
        firstRaw.get<Components::Transform::WorldMatrix>(firstEntity).Matrix,
        firstExpected));

    // If replay keeps the previous callbacks, the second submission will
    // incorrectly settle this new mutation instead of the second scene.
    firstRaw.get<Components::Transform::Component>(firstEntity).Position =
        glm::vec3(11.0f, 0.0f, 0.0f);
    firstRaw.emplace_or_replace<Components::Transform::IsDirtyTag>(
        firstEntity);

    ASSERT_TRUE(graph.ResetForReplay().has_value());
    (void)RegisterPromotedEcsSystemBundle(graph, secondScene);
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    const auto planStats = graph.GetPlanReuseStats();
    EXPECT_EQ(planStats.CompileCallCount, 2u);
    EXPECT_EQ(planStats.PlanBuildCount, 1u);
    EXPECT_EQ(planStats.PlanReuseCount, 1u);
    EXPECT_TRUE(planStats.LastCompileReusedPlan);

    const glm::mat4 secondExpected =
        glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 0.0f, 0.0f));
    EXPECT_TRUE(MatricesNear(
        secondRaw.get<Components::Transform::WorldMatrix>(secondEntity).Matrix,
        secondExpected));
    EXPECT_FALSE(
        secondRaw.all_of<Components::Transform::IsDirtyTag>(secondEntity));

    EXPECT_TRUE(
        firstRaw.all_of<Components::Transform::IsDirtyTag>(firstEntity));
    EXPECT_TRUE(MatricesNear(
        firstRaw.get<Components::Transform::WorldMatrix>(firstEntity).Matrix,
        firstExpected));
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
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(entity));
}

// BUG-024 — a local-transform mutation made *after* the fixed-step bundle
// (the inspector-edit / gizmo-drag shape) leaves WorldMatrix stale until the
// runtime pre-render flush runs; the flush recomputes the world matrix,
// clears IsDirtyTag, and stamps DirtyTransform for extraction to drain.
TEST(RuntimeEcsSystemBundle, PreRenderFlushRefreshesPostFixedStepTransformEdit)
{
    Registry scene;
    auto& raw = scene.Raw();

    const EntityHandle entity = CreateDefault(scene, "EditedAfterFixedStep");
    raw.get<Components::Transform::Component>(entity).Position = glm::vec3(1.0f, 0.0f, 0.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(entity);

    // Scheduled fixed-step bundle runs first and settles the scene.
    FrameGraph graph;
    (void)RegisterPromotedEcsSystemBundle(graph, scene);
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());
    raw.remove<Components::DirtyTags::DirtyTransform>(entity); // extraction drained it

    // Post-fixed-step mutation (UI inspector edit / gizmo drag shape).
    raw.get<Components::Transform::Component>(entity).Position = glm::vec3(4.0f, 5.0f, 6.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(entity);

    // Stale until the flush: this is the BUG-024 failure mode.
    const glm::mat4 stale =
        glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    ASSERT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(entity).Matrix, stale));

    const PreRenderTransformFlushStats stats = FlushPreRenderTransformState(scene);

    EXPECT_EQ(stats.WorldUpdatedObserved, 1u);
    EXPECT_EQ(stats.DirtyTransformStamped, 1u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 1u);
    const glm::mat4 expected =
        glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(entity).Matrix, expected));
    EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(entity));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
    EXPECT_TRUE(raw.all_of<Components::DirtyTags::DirtyTransform>(entity));
}

// BUG-024 — a clean scene makes the pre-render flush a no-op: no world
// matrices rewritten, no DirtyTransform stamped.
TEST(RuntimeEcsSystemBundle, PreRenderFlushIsNoOpOnCleanScene)
{
    Registry scene;
    const EntityHandle entity = CreateDefault(scene, "Clean");
    (void)entity;

    FrameGraph graph;
    (void)RegisterPromotedEcsSystemBundle(graph, scene);
    ASSERT_TRUE(graph.Compile().has_value());
    ASSERT_TRUE(graph.Execute().has_value());

    const PreRenderTransformFlushStats stats = FlushPreRenderTransformState(scene);

    EXPECT_EQ(stats.WorldUpdatedObserved, 0u);
    EXPECT_EQ(stats.DirtyTransformStamped, 0u);
    EXPECT_EQ(stats.WorldUpdatedCleared, 0u);
}
