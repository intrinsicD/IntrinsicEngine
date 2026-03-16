#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <entt/entity/entity.hpp>

#include "TestImGuiFrameScope.hpp"

import ECS;
import Graphics;
import Core.Input;

namespace
{
    [[nodiscard]] Graphics::CameraComponent MakeTestCamera()
    {
        Graphics::CameraComponent cam;
        cam.Position = glm::vec3(0.0f, 0.0f, 5.0f);
        cam.Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        cam.Fov = 45.0f;
        cam.Near = 0.1f;
        cam.Far = 100.0f;
        cam.AspectRatio = 16.0f / 9.0f;
        cam.ViewMatrix = glm::lookAt(cam.Position, cam.Position + cam.GetForward(), cam.GetUp());
        cam.ProjectionMatrix = glm::perspective(glm::radians(cam.Fov), cam.AspectRatio, cam.Near, cam.Far);
        cam.ProjectionMatrix[1][1] *= -1.0f;
        return cam;
    }

    inline entt::entity CreateSelectedEntity(ECS::Scene& scene, const glm::vec3& pos)
    {
        const entt::entity entity = scene.CreateEntity("TestEntity");
        auto& reg = scene.GetRegistry();
        auto& transform = reg.get<ECS::Components::Transform::Component>(entity);
        transform.Position = pos;
        reg.emplace<ECS::Components::Selection::SelectableTag>(entity);
        reg.emplace<ECS::Components::Selection::SelectedTag>(entity);
        reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(entity);
        ECS::Systems::Transform::OnUpdate(reg);
        return entity;
    }

}

TEST(TransformGizmo, NoSelectedEntity_ReturnsNotConsumed)
{
    ECS::Scene scene;
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    const bool consumed = gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);

    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
    EXPECT_FALSE(gizmo.IsActive());
}

TEST(TransformGizmo, SelectedEntity_UpdateDoesNotConsumeWithoutInteraction)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f));
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    const bool consumed = gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);

    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
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

TEST(TransformGizmo, DefaultMouseButton_IsLmb)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_EQ(gizmo.GetConfig().MouseButton, 0);
}

TEST(TransformGizmo, SnapConfig_DefaultValues)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_FALSE(gizmo.GetConfig().SnapEnabled);
    EXPECT_FLOAT_EQ(gizmo.GetConfig().TranslateSnap, 0.25f);
    EXPECT_FLOAT_EQ(gizmo.GetConfig().RotateSnap, 15.0f);
    EXPECT_FLOAT_EQ(gizmo.GetConfig().ScaleSnap, 0.1f);
}

TEST(TransformGizmo, AppearanceConfig_DefaultValues)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_FLOAT_EQ(gizmo.GetConfig().HandleLength, 1.0f);
    EXPECT_FLOAT_EQ(gizmo.GetConfig().HandleThickness, 3.0f);
}

TEST(TransformGizmo, SetMode_UpdatesConfigAndKeepsIdleState)
{
    Graphics::TransformGizmo gizmo;
    gizmo.SetMode(Graphics::GizmoMode::Rotate);
    EXPECT_EQ(gizmo.GetConfig().Mode, Graphics::GizmoMode::Rotate);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
    gizmo.SetMode(Graphics::GizmoMode::Scale);
    EXPECT_EQ(gizmo.GetConfig().Mode, Graphics::GizmoMode::Scale);
}

TEST(TransformGizmo, UICapturesMouse_BlocksInteraction)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f));
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    const bool consumed = gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, true);

    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
}

TEST(TransformGizmo, DrawImGui_NoSelection_NoCrash)
{
    Graphics::TransformGizmo gizmo;
    TestSupport::ImGuiFrameScope frame;
    gizmo.DrawImGui();
    SUCCEED();
}

TEST(TransformGizmo, DrawImGui_WithSelection_NoCrash)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f));
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;
    ASSERT_FALSE(gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false));

    TestSupport::ImGuiFrameScope frame;
    gizmo.DrawImGui();
    SUCCEED();
}
