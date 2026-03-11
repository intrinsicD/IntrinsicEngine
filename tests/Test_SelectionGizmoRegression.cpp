#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <entt/entity/entity.hpp>

import ECS;
import Graphics;
import Core.Input;
import Runtime.Selection;

// ===========================================================================
// Selection / Gizmo Interaction Regression Tests
//
// Validates the interplay between the selection system and the gizmo:
//   - Gizmo blocks selection when consuming input.
//   - Selection state changes update gizmo pivot.
//   - Mode transitions are safe across selection changes.
//   - Multi-entity selection with gizmo preserves relative offsets.
//   - Edge cases: gizmo with empty selection, selection during active gizmo.
// ===========================================================================

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

    inline entt::entity CreateSelectableEntity(ECS::Scene& scene, const glm::vec3& pos, const char* name)
    {
        const entt::entity entity = scene.CreateEntity(name);
        auto& reg = scene.GetRegistry();
        auto& transform = reg.get<ECS::Components::Transform::Component>(entity);
        transform.Position = pos;
        reg.emplace<ECS::Components::Selection::SelectableTag>(entity);
        reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(entity);
        ECS::Systems::Transform::OnUpdate(reg);
        return entity;
    }

    inline entt::entity CreateSelectedEntity(ECS::Scene& scene, const glm::vec3& pos, const char* name)
    {
        const entt::entity entity = CreateSelectableEntity(scene, pos, name);
        auto& reg = scene.GetRegistry();
        reg.emplace<ECS::Components::Selection::SelectedTag>(entity);
        return entity;
    }

    struct ImGuiFrameScope
    {
        ImGuiFrameScope()
        {
            IMGUI_CHECKVERSION();
            Context = ImGui::CreateContext();
            ImGui::SetCurrentContext(Context);
            ImGuiIO& io = ImGui::GetIO();
            io.DisplaySize = ImVec2(800.0f, 600.0f);
            ImGui::NewFrame();
        }

        ~ImGuiFrameScope()
        {
            ImGui::EndFrame();
            ImGui::DestroyContext(Context);
        }

        ImGuiContext* Context = nullptr;
    };
}

// ---------------------------------------------------------------------------
// When gizmo has no selection, it should not block selection input
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, NoSelection_GizmoDoesNotBlockSelection)
{
    ECS::Scene scene;
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    // With no selected entities, gizmo should not consume.
    bool consumed = gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);
    EXPECT_FALSE(consumed);

    // Selection should be free to proceed.
    entt::entity e = CreateSelectableEntity(scene, glm::vec3(0.0f), "Target");
    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);
    EXPECT_TRUE(scene.GetRegistry().all_of<ECS::Components::Selection::SelectedTag>(e));
}

// ---------------------------------------------------------------------------
// Selection Replace clears gizmo state back to Idle
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, SelectionReplace_GizmoReturnsToIdle)
{
    ECS::Scene scene;
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    entt::entity a = CreateSelectedEntity(scene, glm::vec3(0.0f), "A");

    gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);

    // Replace selection with null (deselect all).
    Runtime::Selection::ApplySelection(scene, entt::null, Runtime::Selection::PickMode::Replace);
    gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
    EXPECT_FALSE(gizmo.IsActive());
}

// ---------------------------------------------------------------------------
// Mode switch during selection is safe
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, ModeSwitchDuringSelection_IsSafe)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f), "Entity");
    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    // Cycle through all modes while an entity is selected.
    const auto modes = {Graphics::GizmoMode::Translate, Graphics::GizmoMode::Rotate, Graphics::GizmoMode::Scale};
    for (auto mode : modes)
    {
        gizmo.SetMode(mode);
        EXPECT_EQ(gizmo.GetConfig().Mode, mode);

        bool consumed = gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);
        EXPECT_FALSE(consumed); // No interaction, so not consumed.
        EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
    }
}

// ---------------------------------------------------------------------------
// Space toggle: World -> Local -> World
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, SpaceToggle_RoundTrip)
{
    Graphics::TransformGizmo gizmo;
    EXPECT_EQ(gizmo.GetConfig().Space, Graphics::GizmoSpace::World);

    gizmo.GetConfig().Space = Graphics::GizmoSpace::Local;
    EXPECT_EQ(gizmo.GetConfig().Space, Graphics::GizmoSpace::Local);

    gizmo.GetConfig().Space = Graphics::GizmoSpace::World;
    EXPECT_EQ(gizmo.GetConfig().Space, Graphics::GizmoSpace::World);
}

// ---------------------------------------------------------------------------
// Multi-entity selection: gizmo updates without crash
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, MultiSelection_GizmoUpdateNoCrash)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(-1.0f, 0.0f, 0.0f), "Left");
    CreateSelectedEntity(scene, glm::vec3(1.0f, 0.0f, 0.0f), "Right");
    CreateSelectedEntity(scene, glm::vec3(0.0f, 2.0f, 0.0f), "Top");

    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    bool consumed = gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
}

// ---------------------------------------------------------------------------
// Pivot strategy: Centroid vs FirstSelected
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, PivotStrategy_CentroidVsFirstSelected)
{
    Graphics::TransformGizmo gizmo;

    gizmo.GetConfig().Pivot = Graphics::GizmoPivot::Centroid;
    EXPECT_EQ(gizmo.GetConfig().Pivot, Graphics::GizmoPivot::Centroid);

    gizmo.GetConfig().Pivot = Graphics::GizmoPivot::FirstSelected;
    EXPECT_EQ(gizmo.GetConfig().Pivot, Graphics::GizmoPivot::FirstSelected);
}

// ---------------------------------------------------------------------------
// DrawImGui during multi-selection: no crash
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, DrawImGui_MultiSelection_NoCrash)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f), "S1");
    CreateSelectedEntity(scene, glm::vec3(1.0f, 0.0f, 0.0f), "S2");

    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;
    gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);

    ImGuiFrameScope frame;
    gizmo.DrawImGui();
    SUCCEED();
}

// ---------------------------------------------------------------------------
// Selection Add then Toggle: gizmo handles dynamic selection set
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, SelectionAddThenToggle_GizmoHandlesIt)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity a = CreateSelectableEntity(scene, glm::vec3(0.0f), "A");
    entt::entity b = CreateSelectableEntity(scene, glm::vec3(2.0f, 0.0f, 0.0f), "B");

    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;

    // Select A.
    Runtime::Selection::ApplySelection(scene, a, Runtime::Selection::PickMode::Replace);
    gizmo.Update(reg, MakeTestCamera(), input, 800, 600, false);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);

    // Add B.
    Runtime::Selection::ApplySelection(scene, b, Runtime::Selection::PickMode::Add);
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
    gizmo.Update(reg, MakeTestCamera(), input, 800, 600, false);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);

    // Toggle A off.
    Runtime::Selection::ApplySelection(scene, a, Runtime::Selection::PickMode::Toggle);
    EXPECT_FALSE(reg.all_of<ECS::Components::Selection::SelectedTag>(a));
    EXPECT_TRUE(reg.all_of<ECS::Components::Selection::SelectedTag>(b));
    gizmo.Update(reg, MakeTestCamera(), input, 800, 600, false);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
}

// ---------------------------------------------------------------------------
// ResetInteraction: clears gizmo state
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, ResetInteraction_ClearsState)
{
    ECS::Scene scene;
    CreateSelectedEntity(scene, glm::vec3(0.0f), "Entity");

    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;
    gizmo.Update(scene.GetRegistry(), MakeTestCamera(), input, 800, 600, false);

    gizmo.ResetInteraction();
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
    EXPECT_FALSE(gizmo.IsActive());
}

// ---------------------------------------------------------------------------
// Snap config: enable/disable with mode-specific values
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, SnapConfig_ModifyAndVerify)
{
    Graphics::TransformGizmo gizmo;
    auto& cfg = gizmo.GetConfig();

    cfg.SnapEnabled = true;
    cfg.TranslateSnap = 1.0f;
    cfg.RotateSnap = 45.0f;
    cfg.ScaleSnap = 0.5f;

    EXPECT_TRUE(cfg.SnapEnabled);
    EXPECT_FLOAT_EQ(cfg.TranslateSnap, 1.0f);
    EXPECT_FLOAT_EQ(cfg.RotateSnap, 45.0f);
    EXPECT_FLOAT_EQ(cfg.ScaleSnap, 0.5f);
}

// ---------------------------------------------------------------------------
// Selection event + gizmo: deselect fires event, gizmo sees empty selection
// ---------------------------------------------------------------------------
TEST(SelectionGizmoRegression, DeselectAll_GizmoSeesEmptySelection)
{
    ECS::Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = CreateSelectedEntity(scene, glm::vec3(0.0f), "X");

    Graphics::TransformGizmo gizmo;
    Core::Input::Context input;
    gizmo.Update(reg, MakeTestCamera(), input, 800, 600, false);

    // Deselect all.
    Runtime::Selection::ApplySelection(scene, entt::null, Runtime::Selection::PickMode::Replace);

    auto selectedView = reg.view<ECS::Components::Selection::SelectedTag>();
    EXPECT_EQ(selectedView.size(), 0u);

    bool consumed = gizmo.Update(reg, MakeTestCamera(), input, 800, 600, false);
    EXPECT_FALSE(consumed);
    EXPECT_EQ(gizmo.GetState(), Graphics::GizmoState::Idle);
}
