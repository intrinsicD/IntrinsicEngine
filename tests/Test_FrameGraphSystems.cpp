#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include <entt/entity/registry.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

import Core;
import ECS;

using namespace Core;
using Core::Hash::operator""_id;

// -------------------------------------------------------------------------
// Stub types for dependency-graph testing (avoid linking Graphics module)
// -------------------------------------------------------------------------
namespace Stubs
{
    struct MeshRendererComponent {};
}

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
namespace
{
    ptrdiff_t IndexOf(const std::vector<std::string>& log, const std::string& name)
    {
        auto it = std::find(log.begin(), log.end(), name);
        return (it != log.end()) ? std::distance(log.begin(), it) : -1;
    }

    void ExpectOrder(const std::vector<std::string>& log,
                     const std::string& before, const std::string& after)
    {
        auto a = IndexOf(log, before);
        auto b = IndexOf(log, after);
        ASSERT_GE(a, 0) << before << " not found in log";
        ASSERT_GE(b, 0) << after << " not found in log";
        EXPECT_LT(a, b) << before << " should execute before " << after;
    }
}

// =========================================================================
// Test: ECS Systems register into FrameGraph with correct dependencies
// =========================================================================
TEST(FrameGraphSystems, TransformSystem_RegistersAndExecutes)
{
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    // Create an entity with a dirty transform to verify the system actually runs.
    entt::entity e = scene.CreateEntity("TestEntity");
    auto& t = registry.get<ECS::Components::Transform::Component>(e);
    t.Position = {5.0f, 0.0f, 0.0f};
    registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

    // Register via the system's self-describing RegisterSystem function.
    ECS::Systems::Transform::RegisterSystem(graph, registry);

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value()) << "Compile failed";
    EXPECT_EQ(graph.GetPassCount(), 1u);

    graph.Execute();

    // Verify the system actually ran: WorldMatrix should be updated.
    auto& world = registry.get<ECS::Components::Transform::WorldMatrix>(e);
    EXPECT_FLOAT_EQ(world.Matrix[3][0], 5.0f);

    // Dirty tag should be cleared.
    EXPECT_FALSE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e));
}

TEST(FrameGraphSystems, AxisRotator_RegistersAndExecutes)
{
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Spinner");
    registry.emplace<ECS::Components::AxisRotator::Component>(e,
        ECS::Components::AxisRotator::Component::Y());

    auto originalRot = registry.get<ECS::Components::Transform::Component>(e).Rotation;

    ECS::Systems::AxisRotator::RegisterSystem(graph, registry, 1.0f / 60.0f);

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(graph.GetPassCount(), 1u);

    graph.Execute();

    // Rotation should have changed.
    auto& t = registry.get<ECS::Components::Transform::Component>(e);
    EXPECT_NE(t.Rotation, originalRot);

    // AxisRotator should have marked the transform as dirty.
    EXPECT_TRUE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e));
}

// =========================================================================
// Test: AxisRotator → TransformUpdate ordering via FrameGraph dependencies
// =========================================================================
TEST(FrameGraphSystems, AxisRotator_RunsBeforeTransformUpdate)
{
    // AxisRotator writes Transform::Component and IsDirtyTag.
    // TransformUpdate also writes IsDirtyTag (clears it) and reads Transform::Component.
    // The FrameGraph should enforce: AxisRotator before TransformUpdate.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Spinner");
    registry.emplace<ECS::Components::AxisRotator::Component>(e,
        ECS::Components::AxisRotator::Component::Y());

    // Register gameplay systems first (they produce dirty state),
    // then core pipeline systems (they consume it). This mirrors Engine::Run().
    ECS::Systems::AxisRotator::RegisterSystem(graph, registry, 1.0f / 60.0f);
    ECS::Systems::Transform::RegisterSystem(graph, registry);

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(graph.GetPassCount(), 2u);

    // Verify layer structure: AxisRotator must be in an earlier layer than TransformUpdate.
    const auto& layers = graph.GetExecutionLayers();
    ASSERT_GE(layers.size(), 2u);

    // Find which layer each pass is in.
    uint32_t axisRotatorLayer = ~0u;
    uint32_t transformLayer = ~0u;

    for (uint32_t li = 0; li < layers.size(); ++li)
    {
        for (uint32_t pi : layers[li])
        {
            if (graph.GetPassName(pi) == "AxisRotator") axisRotatorLayer = li;
            if (graph.GetPassName(pi) == "TransformUpdate") transformLayer = li;
        }
    }

    EXPECT_LT(axisRotatorLayer, transformLayer)
        << "AxisRotator (layer " << axisRotatorLayer
        << ") must run before TransformUpdate (layer " << transformLayer << ")";
}

// =========================================================================
// Test: Full pipeline ordering - AxisRotator → Transform → (Lifecycle || GPUSceneSync)
// =========================================================================
TEST(FrameGraphSystems, FullPipeline_CorrectLayerStructure)
{
    // Simulate the full engine system registration using only the dependency
    // declarations (not the actual execute functions, since those need GPU resources).
    // We use the FrameGraph's type-token system directly.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    std::vector<std::string> log;

    // Simulate AxisRotator registration
    graph.AddPass("AxisRotator",
        [](FrameGraphBuilder& builder)
        {
            builder.Read<ECS::Components::AxisRotator::Component>();
            builder.Write<ECS::Components::Transform::Component>();
            builder.Write<ECS::Components::Transform::IsDirtyTag>();
        },
        [&]() { log.emplace_back("AxisRotator"); });

    // Simulate TransformUpdate registration
    graph.AddPass("TransformUpdate",
        [](FrameGraphBuilder& builder)
        {
            builder.Read<ECS::Components::Transform::Component>();
            builder.Read<ECS::Components::Hierarchy::Component>();
            builder.Write<ECS::Components::Transform::WorldMatrix>();
            builder.Write<ECS::Components::Transform::IsDirtyTag>();
            builder.Write<ECS::Components::Transform::WorldUpdatedTag>();
            builder.Signal("TransformUpdate"_id);
        },
        [&]() { log.emplace_back("TransformUpdate"); });

    // Simulate MeshRendererLifecycle registration
    graph.AddPass("MeshRendererLifecycle",
        [](FrameGraphBuilder& builder)
        {
            builder.Read<ECS::Components::Transform::WorldMatrix>();
            builder.Write<Stubs::MeshRendererComponent>();
            builder.WaitFor("TransformUpdate"_id);
        },
        [&]() { log.emplace_back("MeshRendererLifecycle"); });

    // Simulate GPUSceneSync registration
    graph.AddPass("GPUSceneSync",
        [](FrameGraphBuilder& builder)
        {
            builder.Read<ECS::Components::Transform::WorldMatrix>();
            builder.Read<Stubs::MeshRendererComponent>();
            builder.Write<ECS::Components::Transform::WorldUpdatedTag>();
            builder.WaitFor("TransformUpdate"_id);
            builder.Signal("GPUSceneReady"_id);
        },
        [&]() { log.emplace_back("GPUSceneSync"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value()) << "Compile failed";

    [[maybe_unused]] const auto& layers = graph.GetExecutionLayers();

    // Expected ordering:
    //   Layer 0: AxisRotator (writes Transform::Component, IsDirtyTag)
    //   Layer 1: TransformUpdate (reads Component, writes WorldMatrix, clears IsDirtyTag)
    //   Layer 2: MeshRendererLifecycle (reads WorldMatrix, waits TransformUpdate)
    //   Layer 3: GPUSceneSync (reads WorldMatrix + MeshRenderer, writes WorldUpdatedTag, waits TransformUpdate)
    //
    // Note: MeshRendererLifecycle and GPUSceneSync could potentially be in the same layer
    // depending on exact dependency analysis. GPUSceneSync reads Surface::Component
    // while MeshRendererLifecycle writes it, so GPUSceneSync must come after.

    // Execute and verify ordering.
    Tasks::Scheduler::Initialize(2);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    ASSERT_EQ(log.size(), 4u);

    // Core ordering constraints:
    ExpectOrder(log, "AxisRotator", "TransformUpdate");
    ExpectOrder(log, "TransformUpdate", "MeshRendererLifecycle");
    ExpectOrder(log, "TransformUpdate", "GPUSceneSync");
    ExpectOrder(log, "MeshRendererLifecycle", "GPUSceneSync");
}

// =========================================================================
// AxisRotator: Direct OnUpdate unit tests
// =========================================================================

TEST(AxisRotator, RotationAngleIsCorrect)
{
    // After dt = 1.0s at 45 deg/s around Y, the rotation should be 45 degrees.
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(e);
    registry.emplace<ECS::Components::AxisRotator::Component>(e,
        ECS::Components::AxisRotator::Component::Y());

    ECS::Systems::AxisRotator::OnUpdate(registry, 1.0f);

    const auto& t = registry.get<ECS::Components::Transform::Component>(e);
    // Expected: angleAxis(radians(45), Y) applied to identity quaternion.
    const glm::quat expected = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f});
    EXPECT_NEAR(t.Rotation.x, expected.x, 1e-5f);
    EXPECT_NEAR(t.Rotation.y, expected.y, 1e-5f);
    EXPECT_NEAR(t.Rotation.z, expected.z, 1e-5f);
    EXPECT_NEAR(t.Rotation.w, expected.w, 1e-5f);
}

TEST(AxisRotator, DirtyTagIsEmitted)
{
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(e);
    registry.emplace<ECS::Components::AxisRotator::Component>(e,
        ECS::Components::AxisRotator::Component::Y());

    EXPECT_FALSE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e));

    ECS::Systems::AxisRotator::OnUpdate(registry, 1.0f / 60.0f);

    EXPECT_TRUE(registry.all_of<ECS::Components::Transform::IsDirtyTag>(e));
}

TEST(AxisRotator, ZeroSpeedProducesNoRotation)
{
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(e);

    ECS::Components::AxisRotator::Component rotator;
    rotator.Speed = 0.0f;
    rotator.axis = {0.0f, 1.0f, 0.0f};
    registry.emplace<ECS::Components::AxisRotator::Component>(e, rotator);

    const auto origRot = registry.get<ECS::Components::Transform::Component>(e).Rotation;

    ECS::Systems::AxisRotator::OnUpdate(registry, 1.0f);

    const auto& t = registry.get<ECS::Components::Transform::Component>(e);
    // With zero speed, the delta quaternion is identity — rotation unchanged.
    EXPECT_NEAR(t.Rotation.x, origRot.x, 1e-6f);
    EXPECT_NEAR(t.Rotation.y, origRot.y, 1e-6f);
    EXPECT_NEAR(t.Rotation.z, origRot.z, 1e-6f);
    EXPECT_NEAR(t.Rotation.w, origRot.w, 1e-6f);
}

TEST(AxisRotator, MultipleEntitiesRotateIndependently)
{
    entt::registry registry;

    auto eX = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(eX);
    registry.emplace<ECS::Components::AxisRotator::Component>(eX,
        ECS::Components::AxisRotator::Component::X());

    auto eZ = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(eZ);
    registry.emplace<ECS::Components::AxisRotator::Component>(eZ,
        ECS::Components::AxisRotator::Component::Z());

    ECS::Systems::AxisRotator::OnUpdate(registry, 1.0f);

    const auto& tX = registry.get<ECS::Components::Transform::Component>(eX);
    const auto& tZ = registry.get<ECS::Components::Transform::Component>(eZ);

    // Each should have rotated around its own axis.
    const glm::quat expectedX = glm::angleAxis(glm::radians(45.0f), glm::vec3{1.0f, 0.0f, 0.0f});
    const glm::quat expectedZ = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 0.0f, 1.0f});

    EXPECT_NEAR(tX.Rotation.x, expectedX.x, 1e-5f);
    EXPECT_NEAR(tX.Rotation.y, expectedX.y, 1e-5f);
    EXPECT_NEAR(tX.Rotation.z, expectedX.z, 1e-5f);
    EXPECT_NEAR(tX.Rotation.w, expectedX.w, 1e-5f);

    EXPECT_NEAR(tZ.Rotation.x, expectedZ.x, 1e-5f);
    EXPECT_NEAR(tZ.Rotation.y, expectedZ.y, 1e-5f);
    EXPECT_NEAR(tZ.Rotation.z, expectedZ.z, 1e-5f);
    EXPECT_NEAR(tZ.Rotation.w, expectedZ.w, 1e-5f);
}

TEST(AxisRotator, AccumulatesOverMultipleUpdates)
{
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<ECS::Components::Transform::Component>(e);
    registry.emplace<ECS::Components::AxisRotator::Component>(e,
        ECS::Components::AxisRotator::Component::Y());

    // Two updates of 0.5s at 45 deg/s = two 22.5-degree increments = 45 degrees total.
    ECS::Systems::AxisRotator::OnUpdate(registry, 0.5f);
    ECS::Systems::AxisRotator::OnUpdate(registry, 0.5f);

    const auto& t = registry.get<ECS::Components::Transform::Component>(e);
    const glm::quat expected = glm::angleAxis(glm::radians(45.0f), glm::vec3{0.0f, 1.0f, 0.0f});

    EXPECT_NEAR(t.Rotation.x, expected.x, 1e-4f);
    EXPECT_NEAR(t.Rotation.y, expected.y, 1e-4f);
    EXPECT_NEAR(t.Rotation.z, expected.z, 1e-4f);
    EXPECT_NEAR(t.Rotation.w, expected.w, 1e-4f);
}

TEST(AxisRotator, EntityWithoutTransform_IsSkipped)
{
    // AxisRotator queries for entities with both Transform::Component and
    // AxisRotator::Component. An entity with only the rotator should be skipped.
    entt::registry registry;
    auto e = registry.create();
    registry.emplace<ECS::Components::AxisRotator::Component>(e,
        ECS::Components::AxisRotator::Component::Y());

    // Should not crash — the view simply yields no matches.
    ECS::Systems::AxisRotator::OnUpdate(registry, 1.0f);
}

// =========================================================================
// Test: Multi-frame reset and re-registration
// =========================================================================
TEST(FrameGraphSystems, MultiFrame_ResetAndReRegister)
{
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    ECS::Scene scene;
    auto& registry = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Entity");
    auto& t = registry.get<ECS::Components::Transform::Component>(e);

    for (int frame = 0; frame < 5; ++frame)
    {
        scope.Reset();
        graph.Reset();

        // Move entity each frame
        t.Position.x = static_cast<float>(frame) * 10.0f;
        registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

        ECS::Systems::Transform::RegisterSystem(graph, registry);

        auto result = graph.Compile();
        ASSERT_TRUE(result.has_value()) << "Frame " << frame;

        graph.Execute();

        auto& world = registry.get<ECS::Components::Transform::WorldMatrix>(e);
        EXPECT_FLOAT_EQ(world.Matrix[3][0], static_cast<float>(frame) * 10.0f)
            << "Frame " << frame;
    }
}
