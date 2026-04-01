module;

#include <algorithm>
#include <array>
#include <cmath>

#include <imgui.h>
#include <ImGuizmo.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <entt/entity/registry.hpp>

module Graphics.TransformGizmo;

import Graphics.Camera;
import ECS;
import Core.Input;

namespace
{
    [[nodiscard]] ECS::Components::Transform::Component ResolveWorldTransform(entt::registry& registry, entt::entity entity)
    {
        ECS::Components::Transform::Component worldTransform{};
        if (const auto* world = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity);
            world && ECS::Components::Transform::TryDecomposeMatrix(world->Matrix, worldTransform))
        {
            return worldTransform;
        }

        if (const auto* local = registry.try_get<ECS::Components::Transform::Component>(entity))
            return *local;

        return worldTransform;
    }

    [[nodiscard]] glm::mat4 ResolveWorldMatrix(entt::registry& registry, entt::entity entity)
    {
        if (const auto* world = registry.try_get<ECS::Components::Transform::WorldMatrix>(entity))
            return world->Matrix;
        if (const auto* local = registry.try_get<ECS::Components::Transform::Component>(entity))
            return ECS::Components::Transform::GetMatrix(*local);
        return glm::mat4(1.0f);
    }

    [[nodiscard]] bool TryResolveParentWorldMatrix(entt::registry& registry,
                                                   entt::entity entity,
                                                   glm::mat4& outParentWorldMatrix)
    {
        const auto* hierarchy = registry.try_get<ECS::Components::Hierarchy::Component>(entity);
        if (!hierarchy || hierarchy->Parent == entt::null || !registry.valid(hierarchy->Parent))
            return false;

        outParentWorldMatrix = ResolveWorldMatrix(registry, hierarchy->Parent);
        return true;
    }

    [[nodiscard]] glm::mat4 ComposePivotMatrix(const glm::vec3& position, const glm::quat& rotation)
    {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m *= glm::mat4_cast(rotation);
        return m;
    }

    [[nodiscard]] ImGuizmo::OPERATION ToImGuizmoOperation(Graphics::GizmoMode mode)
    {
        switch (mode)
        {
        case Graphics::GizmoMode::Translate: return ImGuizmo::TRANSLATE;
        case Graphics::GizmoMode::Rotate:    return ImGuizmo::ROTATE;
        case Graphics::GizmoMode::Scale:     return ImGuizmo::SCALE;
        default:                             return ImGuizmo::TRANSLATE;
        }
    }

    [[nodiscard]] ImGuizmo::MODE ToImGuizmoMode(Graphics::GizmoSpace space)
    {
        return (space == Graphics::GizmoSpace::Local) ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    }

    [[nodiscard]] Graphics::GizmoAxis DetectHoveredAxis(Graphics::GizmoMode mode)
    {
        switch (mode)
        {
        case Graphics::GizmoMode::Translate:
            if (ImGuizmo::IsOver(ImGuizmo::TRANSLATE_X)) return Graphics::GizmoAxis::X;
            if (ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Y)) return Graphics::GizmoAxis::Y;
            if (ImGuizmo::IsOver(ImGuizmo::TRANSLATE_Z)) return Graphics::GizmoAxis::Z;
            if (ImGuizmo::IsOver(ImGuizmo::TRANSLATE))   return Graphics::GizmoAxis::All;
            return Graphics::GizmoAxis::None;
        case Graphics::GizmoMode::Rotate:
            if (ImGuizmo::IsOver(ImGuizmo::ROTATE_X)) return Graphics::GizmoAxis::X;
            if (ImGuizmo::IsOver(ImGuizmo::ROTATE_Y)) return Graphics::GizmoAxis::Y;
            if (ImGuizmo::IsOver(ImGuizmo::ROTATE_Z)) return Graphics::GizmoAxis::Z;
            if (ImGuizmo::IsOver(ImGuizmo::ROTATE_SCREEN)) return Graphics::GizmoAxis::All;
            if (ImGuizmo::IsOver(ImGuizmo::ROTATE)) return Graphics::GizmoAxis::All;
            return Graphics::GizmoAxis::None;
        case Graphics::GizmoMode::Scale:
            if (ImGuizmo::IsOver(ImGuizmo::SCALE_X) || ImGuizmo::IsOver(ImGuizmo::SCALE_XU)) return Graphics::GizmoAxis::X;
            if (ImGuizmo::IsOver(ImGuizmo::SCALE_Y) || ImGuizmo::IsOver(ImGuizmo::SCALE_YU)) return Graphics::GizmoAxis::Y;
            if (ImGuizmo::IsOver(ImGuizmo::SCALE_Z) || ImGuizmo::IsOver(ImGuizmo::SCALE_ZU)) return Graphics::GizmoAxis::Z;
            if (ImGuizmo::IsOver(ImGuizmo::SCALE) || ImGuizmo::IsOver(ImGuizmo::SCALEU)) return Graphics::GizmoAxis::All;
            return Graphics::GizmoAxis::None;
        }
        return Graphics::GizmoAxis::None;
    }
}

namespace Graphics
{
    void TransformGizmo::ResetInteraction()
    {
        m_State = GizmoState::Idle;
        m_HoveredAxis = GizmoAxis::None;
        m_ActiveAxis = GizmoAxis::None;
        m_CurrentManipulatedPivotMatrix = m_PivotMatrix;
        m_ManipulationStartPivotMatrix = m_PivotMatrix;
    }

    void TransformGizmo::SetMode(GizmoMode mode)
    {
        if (m_Config.Mode == mode)
            return;

        m_Config.Mode = mode;
        ResetInteraction();
    }

    bool TransformGizmo::ComputeSelectionSnapshot(entt::registry& registry, bool refreshCache)
    {
        auto view = registry.view<ECS::Components::Selection::SelectedTag,
                                  ECS::Components::Transform::Component>();

        if (refreshCache)
            m_CachedTransforms.clear();

        bool first = true;
        glm::vec3 centroid{0.0f};
        glm::vec3 pivotPosition{0.0f};
        glm::quat pivotRotation{1.0f, 0.0f, 0.0f, 0.0f};
        int count = 0;

        for (auto [entity, localTransform] : view.each())
        {
            const ECS::Components::Transform::Component worldTransform = ResolveWorldTransform(registry, entity);
            centroid += worldTransform.Position;
            ++count;

            if (first)
            {
                pivotPosition = worldTransform.Position;
                pivotRotation = worldTransform.Rotation;
                first = false;
            }

            if (refreshCache)
            {
                EntityTransformCache cache;
                cache.Entity = entity;
                cache.InitialWorldMatrix = ResolveWorldMatrix(registry, entity);
                cache.HasInitialParentWorldMatrix = TryResolveParentWorldMatrix(registry, entity, cache.InitialParentWorldMatrix);
                m_CachedTransforms.push_back(cache);
            }
        }

        if (count == 0)
            return false;

        if (m_Config.Pivot == GizmoPivot::Centroid)
            pivotPosition = centroid / static_cast<float>(count);

        if (m_Config.Space == GizmoSpace::World)
            pivotRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

        m_PivotMatrix = ComposePivotMatrix(pivotPosition, pivotRotation);
        if (refreshCache || m_State != GizmoState::Active)
            m_CurrentManipulatedPivotMatrix = m_PivotMatrix;
        return true;
    }

    bool TransformGizmo::Update(entt::registry& registry,
                                const CameraComponent& camera,
                                const Core::Input::Context& input,
                                uint32_t viewportWidth,
                                uint32_t viewportHeight,
                                bool uiCapturesMouse)
    {
        m_Registry = &registry;
        m_Camera = camera;
        m_ViewportWidth = viewportWidth;
        m_ViewportHeight = viewportHeight;
        m_UICapturesMouse = uiCapturesMouse;

        const bool refreshCache = (m_State != GizmoState::Active);
        m_HasSelection = ComputeSelectionSnapshot(registry, refreshCache);
        if (!m_HasSelection)
        {
            m_CachedTransforms.clear();
            ResetInteraction();
            return false;
        }

        const bool mouseDown = input.IsMouseButtonPressed(m_Config.MouseButton);
        if (m_State == GizmoState::Active && !mouseDown)
        {
            m_State = GizmoState::Idle;
            m_ActiveAxis = GizmoAxis::None;
        }

        return !uiCapturesMouse && (m_State == GizmoState::Active || m_State == GizmoState::Hovered);
    }

    void TransformGizmo::ApplyManipulatedPivotMatrix(const glm::mat4& manipulatedPivotMatrix)
    {
        if (!m_Registry || m_CachedTransforms.empty())
            return;

        const float det = glm::determinant(m_ManipulationStartPivotMatrix);
        if (!std::isfinite(det) || std::abs(det) < 1e-8f)
            return;

        const glm::mat4 delta = manipulatedPivotMatrix * glm::inverse(m_ManipulationStartPivotMatrix);

        for (const auto& cache : m_CachedTransforms)
        {
            if (!m_Registry->valid(cache.Entity))
                continue;

            auto* localTransform = m_Registry->try_get<ECS::Components::Transform::Component>(cache.Entity);
            if (!localTransform)
                continue;

            const glm::mat4 targetWorldMatrix = delta * cache.InitialWorldMatrix;

            if (cache.HasInitialParentWorldMatrix)
            {
                ECS::Components::Transform::Component solvedLocal{};
                if (!ECS::Components::Transform::TryComputeLocalTransform(
                        targetWorldMatrix,
                        cache.InitialParentWorldMatrix,
                        solvedLocal))
                {
                    continue;
                }
                *localTransform = solvedLocal;
            }
            else
            {
                ECS::Components::Transform::Component solvedWorld{};
                if (!ECS::Components::Transform::TryDecomposeMatrix(targetWorldMatrix, solvedWorld))
                    continue;
                *localTransform = solvedWorld;
            }

            m_Registry->emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(cache.Entity);
        }
    }

    void TransformGizmo::DrawImGui()
    {
        if (!m_HasSelection || !m_Registry || m_ViewportWidth == 0 || m_ViewportHeight == 0)
            return;

        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::BeginFrame();
        ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
        ImGuizmo::SetRect(0.0f, 0.0f,
                          static_cast<float>(m_ViewportWidth),
                          static_cast<float>(m_ViewportHeight));
        ImGuizmo::SetOrthographic(m_Camera.ProjectionType == CameraProjectionType::Orthographic);
        ImGuizmo::AllowAxisFlip(true);
        ImGuizmo::SetGizmoSizeClipSpace(0.12f * m_Config.HandleLength);

        auto& style = ImGuizmo::GetStyle();
        style.TranslationLineThickness = m_Config.HandleThickness;
        style.RotationLineThickness = m_Config.HandleThickness;
        style.RotationOuterLineThickness = m_Config.HandleThickness;
        style.ScaleLineThickness = m_Config.HandleThickness;
        style.HatchedAxisLineThickness = std::max(1.0f, m_Config.HandleThickness * 0.5f);

        const ImGuizmo::OPERATION operation = ToImGuizmoOperation(m_Config.Mode);
        const ImGuizmo::MODE mode = ToImGuizmoMode(m_Config.Space);

        std::array<float, 3> snap3 = {m_Config.TranslateSnap, m_Config.TranslateSnap, m_Config.TranslateSnap};
        std::array<float, 1> snap1 = {m_Config.RotateSnap};
        const float* snap = nullptr;
        if (m_Config.SnapEnabled)
        {
            if (m_Config.Mode == GizmoMode::Translate)
                snap = snap3.data();
            else if (m_Config.Mode == GizmoMode::Rotate)
                snap = snap1.data();
            else
                snap = snap3.data();
        }

        glm::mat4 matrix = (m_State == GizmoState::Active) ? m_CurrentManipulatedPivotMatrix : m_PivotMatrix;
        const bool wasActive = (m_State == GizmoState::Active);

        // CameraComponent::ProjectionMatrix is Vulkan render-space (Y flipped).
        // ImGuizmo operates in ImGui/editor viewport space, so we must undo that
        // flip locally or manipulation appears mirrored relative to the visible axes.
        glm::mat4 gizmoProjection = m_Camera.ProjectionMatrix;
        gizmoProjection[1][1] *= -1.0f;

        const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(m_Camera.ViewMatrix),
                                                      glm::value_ptr(gizmoProjection),
                                                      operation,
                                                      mode,
                                                      glm::value_ptr(matrix),
                                                      nullptr,
                                                      snap);

        const GizmoAxis hoveredAxis = DetectHoveredAxis(m_Config.Mode);
        const bool isUsing = ImGuizmo::IsUsing();
        const bool isOver = ImGuizmo::IsOver();

        m_HoveredAxis = hoveredAxis;

        if (isUsing)
        {
            if (!wasActive)
                m_ManipulationStartPivotMatrix = m_PivotMatrix;

            m_CurrentManipulatedPivotMatrix = matrix;
            ApplyManipulatedPivotMatrix(matrix);
            m_State = GizmoState::Active;
            m_ActiveAxis = (hoveredAxis != GizmoAxis::None) ? hoveredAxis : GizmoAxis::All;
        }
        else
        {
            m_CurrentManipulatedPivotMatrix = m_PivotMatrix;
            m_ActiveAxis = GizmoAxis::None;
            m_State = isOver ? GizmoState::Hovered : GizmoState::Idle;
        }

        (void)manipulated;
    }
}
