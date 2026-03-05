module;

#include <cstdint>
#include <vector>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

export module Graphics:TransformGizmo;

import :DebugDraw;
import :Camera;
import Core.Input;

export namespace Graphics
{
    // =========================================================================
    // TransformGizmo — 3D manipulation gizmo system
    // =========================================================================
    //
    // Renders translate/rotate/scale gizmos via the DebugDraw overlay path
    // (LinePass transient, no depth test). Handles mouse interaction with
    // deterministic picking priority.
    //
    // Contract:
    //  - Call Update() once per frame before DebugDraw is consumed.
    //  - Reads from the ECS registry to find selected entities.
    //  - Writes transform changes directly to ECS Transform components.
    //  - Main-thread only.

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
        All  = 7  // Uniform scale only
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
        float TranslateSnap      = 0.25f;  // World units
        float RotateSnap         = 15.0f;  // Degrees
        float ScaleSnap          = 0.1f;   // Scale factor

        float HandleLength       = 1.0f;   // Base length (scaled by distance)
        float HandleThickness    = 3.0f;   // Line width in pixels
        float PickRadius         = 0.08f;  // NDC pick tolerance

        // Mouse button used for gizmo interaction (0=LMB).
        int   MouseButton        = 0;
    };

    class TransformGizmo
    {
    public:
        TransformGizmo() = default;

        // Per-frame update: handles input, updates state, emits debug geometry.
        // Returns true if the gizmo consumed the mouse input (blocks selection).
        bool Update(entt::registry& registry,
                    DebugDraw& debugDraw,
                    const CameraComponent& camera,
                    const Core::Input::Context& input,
                    uint32_t viewportWidth,
                    uint32_t viewportHeight,
                    bool uiCapturesMouse);

        [[nodiscard]] GizmoConfig& GetConfig() { return m_Config; }
        [[nodiscard]] const GizmoConfig& GetConfig() const { return m_Config; }

        [[nodiscard]] GizmoState GetState() const { return m_State; }
        [[nodiscard]] GizmoAxis  GetHoveredAxis() const { return m_HoveredAxis; }
        [[nodiscard]] GizmoAxis  GetActiveAxis() const { return m_ActiveAxis; }

        // Returns true if the gizmo is currently being dragged.
        [[nodiscard]] bool IsActive() const { return m_State == GizmoState::Active; }

    private:
        GizmoConfig m_Config;
        GizmoState  m_State       = GizmoState::Idle;
        GizmoAxis   m_HoveredAxis = GizmoAxis::None;
        GizmoAxis   m_ActiveAxis  = GizmoAxis::None;

        // Interaction state
        glm::vec3   m_DragStart{0.0f};    // World-space drag start point
        glm::vec3   m_PivotPosition{0.0f};
        glm::quat   m_PivotRotation{1.0f, 0.0f, 0.0f, 0.0f};
        float       m_InitialRotateAngle = 0.0f;
        glm::vec3   m_InitialScale{1.0f};

        // Cache for multi-selection initial transforms
        struct EntityTransformCache
        {
            entt::entity Entity = entt::null;
            glm::vec3    InitialPosition{0.0f};
            glm::quat    InitialRotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3    InitialScale{1.0f};
        };
        std::vector<EntityTransformCache> m_CachedTransforms;

        // Compute the pivot point from selected entities.
        [[nodiscard]] bool ComputePivot(entt::registry& registry);

        // Hit-test the mouse against gizmo axes.
        [[nodiscard]] GizmoAxis HitTest(const glm::vec2& mouseNDC,
                                        const CameraComponent& camera,
                                        float handleScale) const;

        // Compute world-space handle scale (constant screen size).
        [[nodiscard]] float ComputeHandleScale(const CameraComponent& camera) const;

        // Get the axis direction in gizmo space (world or local).
        [[nodiscard]] glm::vec3 GetAxisDirection(GizmoAxis axis) const;

        // Project mouse to a line/plane for dragging.
        [[nodiscard]] glm::vec3 ProjectMouseToAxis(const glm::vec2& mouseNDC,
                                                    const CameraComponent& camera,
                                                    GizmoAxis axis,
                                                    float handleScale) const;

        [[nodiscard]] float ProjectMouseToRotation(const glm::vec2& mouseNDC,
                                                    const CameraComponent& camera,
                                                    GizmoAxis axis,
                                                    float handleScale) const;

        [[nodiscard]] float ProjectMouseToScale(const glm::vec2& mouseNDC,
                                                const CameraComponent& camera,
                                                GizmoAxis axis,
                                                float handleScale) const;

        // Apply snap to a value.
        [[nodiscard]] float ApplySnap(float value, float snapIncrement) const;

        // Emit gizmo geometry to DebugDraw.
        void DrawTranslateGizmo(DebugDraw& dd, float handleScale) const;
        void DrawRotateGizmo(DebugDraw& dd, float handleScale) const;
        void DrawScaleGizmo(DebugDraw& dd, float handleScale) const;

        // Get color for axis (highlighted when hovered/active).
        [[nodiscard]] uint32_t GetAxisColor(GizmoAxis axis) const;
    };
}
