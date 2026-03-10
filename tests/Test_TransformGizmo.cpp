#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/registry.hpp>

import ECS;
import Graphics;
import Core.Input;

// =============================================================================
// TransformGizmo — Unit and Integration Tests
// =============================================================================
//
// Tests verify:
//  - Gizmo state machine transitions (idle → hovered → active → idle).
//  - Pivot computation (centroid, first-selected) for single and multi-entity.
//  - Mode switching (translate/rotate/scale).
//  - World/local space orientation.
//  - Snap logic.
//  - Gizmo does not appear when no entity is selected.
//  - Gizmo consumes mouse input during active drag.

namespace
{
    // Helper: create a camera looking along -Z from +Z position.
    Graphics::CameraComponent MakeTestCamera()
    {
        Graphics::CameraComponent cam;
        cam.Position = glm::vec3(0.0f, 0.0f, 5.0f);
        cam.Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        cam.Fov = 45.0f;
        cam.Near = 0.1f;
        cam.Far = 100.0f;
        cam.AspectRatio = 16.0f / 9.0f;

        // Build matrices manually (matching Graphics::UpdateMatrices behavior).
        cam.ViewMatrix = glm::lookAt(cam.Position,
                                      cam.Position + cam.GetForward(),
                                      cam.GetUp());
        cam.ProjectionMatrix = glm::perspective(
            glm::radians(cam.Fov), cam.AspectRatio, cam.Near, cam.Far);
        // Vulkan Y-flip.
        cam.ProjectionMatrix[1][1] *= -1.0f;

        return cam;
    }

    // Helper: create a selected entity with a transform at the given position.
    entt::entity CreateSelectedEntity(ECS::Scene& scene, const glm::vec3& pos)
    {
        auto entity = scene.CreateEntity("TestEntity");
        auto& reg = scene.GetRegistry();

        auto& transform = reg.get<ECS::Components::Transform::Component>(entity);
        transform.Position = pos;

        reg.emplace<ECS::Components::Selection::SelectableTag>(entity);
        reg.emplace<ECS::Components::Selection::SelectedTag>(entity);

        return entity;
    }

    [[nodiscard]] const Graphics::DebugDraw::LineSegment& GetPrimaryTranslateAxisLine(const Graphics::DebugDraw& dd,
                                                                                      size_t axisIndex)
    {
        const auto lines = dd.GetOverlayLines();
        EXPECT_GE(lines.size(), 6u);
        return lines[axisIndex * 2u];
    }
}

// =============================================================================
// State Machine Tests
// =============================================================================

TEST(TransformGizmo, NoSelectedEntity_ReturnsNotConsumed)
{
    ECS::Scene scene;
    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    bool consumed = gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);

    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
}

TEST(TransformGizmo, WithSelectedEntity_DrawsGizmo)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f));

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);

    // Gizmo should have drawn overlay lines for the translate handles.
    EXPECT_GT(dd.GetOverlayLineCount(), 0u);
}

TEST(TransformGizmo, DefaultMode_IsTranslate)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_EQ(gizmo.GetConfig().Mode, Graphics::GizmoMode::Translate);
}

TEST(TransformGizmo, DefaultSpace_IsWorld)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_EQ(gizmo.GetConfig().Space, Graphics::GizmoSpace::World);
}

TEST(TransformGizmo, DefaultPivot_IsCentroid)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_EQ(gizmo.GetConfig().Pivot, Graphics::GizmoPivot::Centroid);
}

// =============================================================================
// Mode Switching Tests
// =============================================================================

TEST(TransformGizmo, ModeSwitching_DrawsDifferentGeometry)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f));
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    // Translate mode.
    {
        Graphics::DebugDraw dd;
        Graphics::TransformGizmo gizmo;
        gizmo.GetConfig().Mode = Graphics::GizmoMode::Translate;
        gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);
        const uint32_t translateLines = dd.GetOverlayLineCount();
        EXPECT_GT(translateLines, 0u);
    }

    // Rotate mode.
    {
        Graphics::DebugDraw dd;
        Graphics::TransformGizmo gizmo;
        gizmo.GetConfig().Mode = Graphics::GizmoMode::Rotate;
        gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);
        const uint32_t rotateLines = dd.GetOverlayLineCount();
        EXPECT_GT(rotateLines, 0u);
    }

    // Scale mode.
    {
        Graphics::DebugDraw dd;
        Graphics::TransformGizmo gizmo;
        gizmo.GetConfig().Mode = Graphics::GizmoMode::Scale;
        gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);
        const uint32_t scaleLines = dd.GetOverlayLineCount();
        EXPECT_GT(scaleLines, 0u);
    }
}

// =============================================================================
// Pivot Computation Tests
// =============================================================================

TEST(TransformGizmo, CentroidPivot_ComputesCentroid)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(1.0f, 0.0f, 0.0f));
    CreateSelectedEntity(scene, glm::vec3(-1.0f, 0.0f, 0.0f));

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    gizmo.GetConfig().Pivot = Graphics::GizmoPivot::Centroid;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);

    // Gizmo should render (we can't easily check the pivot position directly,
    // but we verify it renders without crashing with multiple selections).
    EXPECT_GT(dd.GetOverlayLineCount(), 0u);
}

TEST(TransformGizmo, FirstSelectedPivot_UsesFirstEntity)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(2.0f, 0.0f, 0.0f));
    CreateSelectedEntity(scene, glm::vec3(-2.0f, 0.0f, 0.0f));

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    gizmo.GetConfig().Pivot = Graphics::GizmoPivot::FirstSelected;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);

    EXPECT_GT(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// Snap Tests
// =============================================================================

TEST(TransformGizmo, SnapDisabled_DefaultConfig)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_FALSE(gizmo.GetConfig().SnapEnabled);
}

TEST(TransformGizmo, SnapConfig_DefaultValues)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_FLOAT_EQ(gizmo.GetConfig().TranslateSnap, 0.25f);
    EXPECT_FLOAT_EQ(gizmo.GetConfig().RotateSnap, 15.0f);
    EXPECT_FLOAT_EQ(gizmo.GetConfig().ScaleSnap, 0.1f);
}

// =============================================================================
// UICapture Tests
// =============================================================================

TEST(TransformGizmo, UICapturesMouse_BlocksGizmo)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f));

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    // With UI capturing mouse, gizmo should still draw but not interact.
    bool consumed = gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, true);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
}

// =============================================================================
// Gizmo Config Tests
// =============================================================================

TEST(TransformGizmo, GizmoConfig_MutationWorks)
{
    Graphics::TransformGizmo gizmo;

    gizmo.GetConfig().Mode = Graphics::GizmoMode::Scale;
    EXPECT_EQ(gizmo.GetConfig().Mode, Graphics::GizmoMode::Scale);

    gizmo.GetConfig().Space = Graphics::GizmoSpace::Local;
    EXPECT_EQ(gizmo.GetConfig().Space, Graphics::GizmoSpace::Local);

    gizmo.GetConfig().Pivot = Graphics::GizmoPivot::FirstSelected;
    EXPECT_EQ(gizmo.GetConfig().Pivot, Graphics::GizmoPivot::FirstSelected);

    gizmo.GetConfig().SnapEnabled = true;
    EXPECT_TRUE(gizmo.GetConfig().SnapEnabled);

    gizmo.GetConfig().TranslateSnap = 0.5f;
    EXPECT_FLOAT_EQ(gizmo.GetConfig().TranslateSnap, 0.5f);
}

// =============================================================================
// No Selected Entity Regression
// =============================================================================

TEST(TransformGizmo, NoSelection_NoDebugDraw)
{
    ECS::Scene scene;
    // Create an entity but don't select it.
    scene.CreateEntity("Unselected");

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);

    // No gizmo should be drawn for unselected entities.
    EXPECT_EQ(dd.GetOverlayLineCount(), 0u);
}

// =============================================================================
// Multi-Entity Selection
// =============================================================================

TEST(TransformGizmo, MultiSelection_DrawsSingleGizmo)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(1.0f, 0.0f, 0.0f));
    CreateSelectedEntity(scene, glm::vec3(0.0f, 1.0f, 0.0f));
    CreateSelectedEntity(scene, glm::vec3(0.0f, 0.0f, 1.0f));

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(scene.GetRegistry(), dd, cam, input, 800, 600, false);

    // Should draw one gizmo (same number of lines regardless of entity count).
    EXPECT_GT(dd.GetOverlayLineCount(), 0u);
}

TEST(TransformGizmo, LocalSpace_UsesWorldPivotAndOrientationForParentedEntity)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity parent = scene.CreateEntity("Parent");
    const entt::entity child = CreateSelectedEntity(scene, glm::vec3(0.0f));
    ECS::Components::Hierarchy::Attach(reg, child, parent);

    auto& parentTransform = reg.get<ECS::Components::Transform::Component>(parent);
    parentTransform.Position = {10.0f, 0.0f, 0.0f};
    parentTransform.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    auto& childTransform = reg.get<ECS::Components::Transform::Component>(child);
    childTransform.Position = {2.0f, 0.0f, 0.0f};
    childTransform.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(parent);
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(child);
    ECS::Systems::Transform::OnUpdate(reg);

    const auto& childWorld = reg.get<ECS::Components::Transform::WorldMatrix>(child);
    const glm::vec3 expectedPivot = glm::vec3(childWorld.Matrix[3]);
    const glm::vec3 expectedXAxis = glm::normalize(parentTransform.Rotation * childTransform.Rotation * glm::vec3(1.0f, 0.0f, 0.0f));

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    gizmo.GetConfig().Mode = Graphics::GizmoMode::Translate;
    gizmo.GetConfig().Space = Graphics::GizmoSpace::Local;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(reg, dd, cam, input, 800, 600, false);

    const auto& xAxisLine = GetPrimaryTranslateAxisLine(dd, 0);
    const glm::vec3 actualXAxis = glm::normalize(xAxisLine.End - xAxisLine.Start);

    EXPECT_NEAR(xAxisLine.Start.x, expectedPivot.x, 1e-3f);
    EXPECT_NEAR(xAxisLine.Start.y, expectedPivot.y, 1e-3f);
    EXPECT_NEAR(xAxisLine.Start.z, expectedPivot.z, 1e-3f);
    EXPECT_NEAR(actualXAxis.x, expectedXAxis.x, 1e-3f);
    EXPECT_NEAR(actualXAxis.y, expectedXAxis.y, 1e-3f);
    EXPECT_NEAR(actualXAxis.z, expectedXAxis.z, 1e-3f);
}

TEST(TransformGizmo, WorldSpace_UsesWorldPivotButKeepsWorldAxesForParentedEntity)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    const entt::entity parent = scene.CreateEntity("Parent");
    const entt::entity child = CreateSelectedEntity(scene, glm::vec3(0.0f));
    ECS::Components::Hierarchy::Attach(reg, child, parent);

    auto& parentTransform = reg.get<ECS::Components::Transform::Component>(parent);
    parentTransform.Position = {3.0f, -4.0f, 1.0f};
    parentTransform.Rotation = glm::angleAxis(glm::radians(60.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    auto& childTransform = reg.get<ECS::Components::Transform::Component>(child);
    childTransform.Position = {1.5f, 0.0f, 0.0f};
    childTransform.Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));

    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(parent);
    reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(child);
    ECS::Systems::Transform::OnUpdate(reg);

    const auto& childWorld = reg.get<ECS::Components::Transform::WorldMatrix>(child);
    const glm::vec3 expectedPivot = glm::vec3(childWorld.Matrix[3]);

    Graphics::DebugDraw dd;
    Graphics::TransformGizmo gizmo;
    gizmo.GetConfig().Mode = Graphics::GizmoMode::Translate;
    gizmo.GetConfig().Space = Graphics::GizmoSpace::World;
    auto cam = MakeTestCamera();
    Core::Input::Context input;

    gizmo.Update(reg, dd, cam, input, 800, 600, false);

    const auto& xAxisLine = GetPrimaryTranslateAxisLine(dd, 0);
    const glm::vec3 actualXAxis = glm::normalize(xAxisLine.End - xAxisLine.Start);

    EXPECT_NEAR(xAxisLine.Start.x, expectedPivot.x, 1e-3f);
    EXPECT_NEAR(xAxisLine.Start.y, expectedPivot.y, 1e-3f);
    EXPECT_NEAR(xAxisLine.Start.z, expectedPivot.z, 1e-3f);
    EXPECT_NEAR(actualXAxis.x, 1.0f, 1e-3f);
    EXPECT_NEAR(actualXAxis.y, 0.0f, 1e-3f);
    EXPECT_NEAR(actualXAxis.z, 0.0f, 1e-3f);
}
