module;

#include <cstdint>
#include <optional>
#include <vector>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Graphics.TransformGizmo;

import Graphics.Camera;
import Core.Input;
import ECS;

export namespace Graphics
{
    enum class GizmoMode : uint8_t
    {
        Translate = 0,
        Rotate    = 1,
        Scale     = 2
    };

    enum class GizmoSpace : uint8_t
    {
        World = 0,
        Local = 1
    };

    enum class GizmoAxis : uint8_t
    {
        None = 0,
        X    = 1,
        Y    = 2,
        Z    = 3,
        XY   = 4,
        XZ   = 5,
        YZ   = 6,
        All  = 7
    };

    enum class GizmoPivot : uint8_t
    {
        Centroid      = 0,
        FirstSelected = 1
    };

    enum class GizmoState : uint8_t
    {
        Idle    = 0,
        Hovered = 1,
        Active  = 2
    };

    struct GizmoConfig
    {
        GizmoMode  Mode          = GizmoMode::Translate;
        GizmoSpace Space         = GizmoSpace::World;
        GizmoPivot Pivot         = GizmoPivot::Centroid;

        bool  SnapEnabled        = false;
        float TranslateSnap      = 0.25f;
        float RotateSnap         = 15.0f;
        float ScaleSnap          = 0.1f;

        float HandleLength       = 1.0f;
        float HandleThickness    = 3.0f;

        // ImGuizmo uses LMB internally; keep this as an engine/editor contract.
        int   MouseButton        = 0;
    };

    class TransformGizmo
    {
    public:
        struct TransformChange
        {
            entt::entity Entity = entt::null;
            ECS::Components::Transform::Component Before{};
            ECS::Components::Transform::Component After{};
        };

        struct CompletedInteraction
        {
            GizmoMode Mode = GizmoMode::Translate;
            std::vector<TransformChange> Changes{};
        };

        TransformGizmo() = default;

        // Captures selection/camera/viewport state before rendering.
        // Returns true when the gizmo should block scene interaction for this frame.
        bool Update(entt::registry& registry,
                    const CameraComponent& camera,
                    const Core::Input::Context& input,
                    uint32_t viewportWidth,
                    uint32_t viewportHeight,
                    bool uiCapturesMouse);

        // Executes the actual ImGuizmo draw/manipulate path during the active ImGui frame.
        void DrawImGui();

        void SetMode(GizmoMode mode);
        void ResetInteraction();

        [[nodiscard]] GizmoConfig& GetConfig() { return m_Config; }
        [[nodiscard]] const GizmoConfig& GetConfig() const { return m_Config; }

        [[nodiscard]] GizmoState GetState() const { return m_State; }
        [[nodiscard]] GizmoAxis  GetHoveredAxis() const { return m_HoveredAxis; }
        [[nodiscard]] GizmoAxis  GetActiveAxis() const { return m_ActiveAxis; }
        [[nodiscard]] bool IsActive() const { return m_State == GizmoState::Active; }
        [[nodiscard]] std::optional<CompletedInteraction> ConsumeCompletedInteraction();

    private:
        struct EntityTransformCache
        {
            entt::entity Entity = entt::null;
            ECS::Components::Transform::Component InitialLocalTransform{};
            glm::mat4    InitialWorldMatrix{1.0f};
            glm::mat4    InitialParentWorldMatrix{1.0f};
            bool         HasInitialParentWorldMatrix = false;
        };

        GizmoConfig m_Config{};
        GizmoState  m_State       = GizmoState::Idle;
        GizmoAxis   m_HoveredAxis = GizmoAxis::None;
        GizmoAxis   m_ActiveAxis  = GizmoAxis::None;

        entt::registry* m_Registry = nullptr;
        CameraComponent m_Camera{};
        uint32_t        m_ViewportWidth = 0;
        uint32_t        m_ViewportHeight = 0;
        bool            m_UICapturesMouse = false;
        bool            m_HasSelection = false;

        glm::mat4       m_PivotMatrix{1.0f};
        glm::mat4       m_ManipulationStartPivotMatrix{1.0f};
        glm::mat4       m_CurrentManipulatedPivotMatrix{1.0f};
        std::vector<EntityTransformCache> m_CachedTransforms{};
        std::optional<CompletedInteraction> m_CompletedInteraction{};

        [[nodiscard]] bool ComputeSelectionSnapshot(entt::registry& registry, bool refreshCache);
        void ApplyManipulatedPivotMatrix(const glm::mat4& manipulatedPivotMatrix);
    };
}
