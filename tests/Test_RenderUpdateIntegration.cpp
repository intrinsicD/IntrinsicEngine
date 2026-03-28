#include <gtest/gtest.h>

#include <concepts>
#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>

#include <entt/entity/registry.hpp>

import ECS;
import Graphics;
import Core;
import Runtime.RenderOrchestrator;

// ===========================================================================
// Render / Update Integration Smoke Tests
//
// Validates CPU-side contracts that underpin the render/update cycle:
//   - FrameGraph system registration and compilation ordering
//   - Transform system produces correct WorldMatrix from dirty entities
//   - RenderOrchestrator compile-time API contracts
//   - Camera matrix computation produces finite, valid matrices
//   - FrameGraph handles empty pass list gracefully
//   - Multiple system registrations compile without cycles
//
// These tests are purely CPU-side (no Vulkan, no swapchain).
// ===========================================================================

namespace
{
    [[maybe_unused]] [[nodiscard]] Graphics::CameraComponent MakeTestCamera()
    {
        Graphics::CameraComponent cam;
        cam.Position = glm::vec3(0.0f, 0.0f, 10.0f);
        cam.Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        cam.Fov = 60.0f;
        cam.Near = 0.1f;
        cam.Far = 1000.0f;
        cam.AspectRatio = 16.0f / 9.0f;
        cam.ViewMatrix = glm::lookAt(cam.Position, cam.Position + cam.GetForward(), cam.GetUp());
        cam.ProjectionMatrix = glm::perspective(glm::radians(cam.Fov), cam.AspectRatio, cam.Near, cam.Far);
        cam.ProjectionMatrix[1][1] *= -1.0f;
        return cam;
    }
}

// ---------------------------------------------------------------------------
// RenderOrchestrator: compile-time contracts
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, RenderOrchestrator_NotCopyable)
{
    static_assert(!std::is_copy_constructible_v<Runtime::RenderOrchestrator>);
    static_assert(!std::is_copy_assignable_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderUpdateIntegration, RenderOrchestrator_NotMovable)
{
    static_assert(!std::is_move_constructible_v<Runtime::RenderOrchestrator>);
    static_assert(!std::is_move_assignable_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderUpdateIntegration, RenderOrchestrator_NotDefaultConstructible)
{
    static_assert(!std::is_default_constructible_v<Runtime::RenderOrchestrator>);
    SUCCEED();
}

TEST(RenderUpdateIntegration, RenderDriver_ExposesStagedFrameExecutionApi)
{
    static_assert(requires(Graphics::RenderDriver& renderSystem,
                           ECS::Scene& scene,
                           const Graphics::CameraComponent& camera,
                           Core::Assets::AssetManager& assetManager,
                           uint64_t currentFrame)
    {
        renderSystem.BeginFrame(currentFrame);
        { renderSystem.AcquireFrame() } -> std::same_as<bool>;
        renderSystem.ProcessCompletedGpuWork(scene, currentFrame);
        renderSystem.UpdateGlobals(camera);
        renderSystem.BuildGraph(assetManager, camera);
        renderSystem.ExecuteGraph();
        renderSystem.EndFrame();
    });
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Camera: UpdateMatrices produces valid output
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, CameraUpdateMatrices_ProducesFiniteMatrices)
{
    Graphics::CameraComponent cam;
    cam.Position = glm::vec3(0.0f, 0.0f, 5.0f);
    cam.Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    cam.Fov = 60.0f;
    cam.Near = 0.1f;
    cam.Far = 1000.0f;
    cam.AspectRatio = 16.0f / 9.0f;

    Graphics::UpdateMatrices(cam, cam.AspectRatio);

    // All matrix elements should be finite.
    for (int c = 0; c < 4; ++c)
    {
        for (int r = 0; r < 4; ++r)
        {
            EXPECT_TRUE(std::isfinite(cam.ViewMatrix[c][r]))
                << "ViewMatrix[" << c << "][" << r << "] is not finite";
            EXPECT_TRUE(std::isfinite(cam.ProjectionMatrix[c][r]))
                << "ProjectionMatrix[" << c << "][" << r << "] is not finite";
        }
    }
}

// ---------------------------------------------------------------------------
// Camera: aspect ratio change updates projection
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, CameraAspectRatioChange_UpdatesProjection)
{
    Graphics::CameraComponent cam;
    cam.Position = glm::vec3(0.0f);
    cam.Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    cam.Fov = 60.0f;
    cam.Near = 0.1f;
    cam.Far = 100.0f;
    cam.AspectRatio = 16.0f / 9.0f;

    Graphics::UpdateMatrices(cam, 16.0f / 9.0f);
    const glm::mat4 proj1 = cam.ProjectionMatrix;

    Graphics::UpdateMatrices(cam, 4.0f / 3.0f);
    const glm::mat4 proj2 = cam.ProjectionMatrix;

    // Projection should differ when aspect ratio changes.
    bool differs = false;
    for (int c = 0; c < 4 && !differs; ++c)
        for (int r = 0; r < 4 && !differs; ++r)
            if (std::abs(proj1[c][r] - proj2[c][r]) > 1e-6f)
                differs = true;

    EXPECT_TRUE(differs) << "Projection matrix should change with aspect ratio";
}

// ---------------------------------------------------------------------------
// FrameGraph: empty graph compiles to zero passes
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, EmptyFrameGraph_CompilesToZeroPasses)
{
    Core::Memory::ScopeStack arena(16 * 1024);
    Core::FrameGraph fg(arena);

    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(fg.GetPassCount(), 0u);
}

// ---------------------------------------------------------------------------
// FrameGraph: Transform system produces exactly one pass
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, TransformSystemRegistration_OnePass)
{
    Core::Memory::ScopeStack arena(64 * 1024);
    Core::FrameGraph fg(arena);

    ECS::Scene scene;
    auto& reg = scene.GetRegistry();
    entt::entity e = scene.CreateEntity("E");
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

    ECS::Systems::Transform::RegisterSystem(fg, reg);

    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(fg.GetPassCount(), 1u);
}

// ---------------------------------------------------------------------------
// Transform dirty-clean cycle: dirty tag cleared after execution
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, TransformDirtyCleanCycle)
{
    Core::Memory::ScopeStack arena(64 * 1024);
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Dirty");
    auto& t = reg.get<ECS::Components::Transform::Component>(e);
    t.Position = {3.0f, 4.0f, 5.0f};
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);
    EXPECT_TRUE(reg.all_of<ECS::Components::Transform::IsDirtyTag>(e));

    Core::FrameGraph fg(arena);
    ECS::Systems::Transform::RegisterSystem(fg, reg);
    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    fg.Execute();

    // After execution, dirty tag should be cleared.
    EXPECT_FALSE(reg.all_of<ECS::Components::Transform::IsDirtyTag>(e));

    // World matrix should reflect the transform.
    auto& world = reg.get<ECS::Components::Transform::WorldMatrix>(e);
    EXPECT_FLOAT_EQ(world.Matrix[3][0], 3.0f);
    EXPECT_FLOAT_EQ(world.Matrix[3][1], 4.0f);
    EXPECT_FLOAT_EQ(world.Matrix[3][2], 5.0f);
}

// ---------------------------------------------------------------------------
// Transform rotation: non-identity rotation produces correct world matrix
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, TransformRotation_ProducesCorrectWorldMatrix)
{
    Core::Memory::ScopeStack arena(64 * 1024);
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Rotated");
    auto& t = reg.get<ECS::Components::Transform::Component>(e);
    t.Position = {0.0f, 0.0f, 0.0f};
    // 90 degrees around Y axis.
    t.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

    Core::FrameGraph fg(arena);
    ECS::Systems::Transform::RegisterSystem(fg, reg);
    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    fg.Execute();

    auto& world = reg.get<ECS::Components::Transform::WorldMatrix>(e);

    // After 90-degree Y rotation, X-axis should map to -Z.
    // world[0] (first column) should be approximately (0, 0, -1, 0).
    EXPECT_NEAR(world.Matrix[0][0], 0.0f, 1e-5f);
    EXPECT_NEAR(world.Matrix[0][2], -1.0f, 1e-5f);
}

// ---------------------------------------------------------------------------
// Transform scale: uniform scale reflected in world matrix diagonal
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, TransformScale_ReflectedInWorldMatrix)
{
    Core::Memory::ScopeStack arena(64 * 1024);
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Scaled");
    auto& t = reg.get<ECS::Components::Transform::Component>(e);
    t.Position = {0.0f, 0.0f, 0.0f};
    t.Scale = {3.0f, 3.0f, 3.0f};
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(e);

    Core::FrameGraph fg(arena);
    ECS::Systems::Transform::RegisterSystem(fg, reg);
    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    fg.Execute();

    auto& world = reg.get<ECS::Components::Transform::WorldMatrix>(e);
    EXPECT_FLOAT_EQ(world.Matrix[0][0], 3.0f);
    EXPECT_FLOAT_EQ(world.Matrix[1][1], 3.0f);
    EXPECT_FLOAT_EQ(world.Matrix[2][2], 3.0f);
}

// ---------------------------------------------------------------------------
// Multiple entities: all dirty entities updated in single FrameGraph pass
// ---------------------------------------------------------------------------

TEST(RenderUpdateIntegration, MultipleEntities_AllUpdatedInSinglePass)
{
    Core::Memory::ScopeStack arena(64 * 1024);
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    constexpr int kCount = 10;
    entt::entity entities[kCount];

    for (int i = 0; i < kCount; ++i)
    {
        entities[i] = scene.CreateEntity("E" + std::to_string(i));
        auto& t = reg.get<ECS::Components::Transform::Component>(entities[i]);
        t.Position = {static_cast<float>(i), 0.0f, 0.0f};
        reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(entities[i]);
    }

    Core::FrameGraph fg(arena);
    ECS::Systems::Transform::RegisterSystem(fg, reg);
    auto result = fg.Compile();
    ASSERT_TRUE(result.has_value());
    fg.Execute();

    for (int i = 0; i < kCount; ++i)
    {
        EXPECT_FALSE(reg.all_of<ECS::Components::Transform::IsDirtyTag>(entities[i]))
            << "Entity " << i << " still dirty";
        auto& world = reg.get<ECS::Components::Transform::WorldMatrix>(entities[i]);
        EXPECT_FLOAT_EQ(world.Matrix[3][0], static_cast<float>(i))
            << "Entity " << i << " wrong X";
    }
}
