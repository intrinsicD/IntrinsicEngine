// src/Runtime/Graphics/Graphics.Camera.cppm
module;

#include <limits>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

export module Graphics:Camera;

import Core.Input;

export namespace Graphics
{
    // --- Pure Data Class ---
    struct CameraComponent
    {
        glm::vec3 Position{0.0f, 0.0f, 4.0f};
        glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f}; // Identity
        // float m_CamYaw = -90.0f;
        // float m_CamPitch = -20.0f;

        float Fov = 45.0f;
        float AspectRatio = 1.77f;
        float Near = 0.1f;
        float Far = 1000.0f;

        glm::mat4 ViewMatrix{1.0f};
        glm::mat4 ProjectionMatrix{1.0f};

        [[nodiscard]] glm::vec3 GetForward() const
        {
            return glm::rotate(Orientation, glm::vec3(0.0f, 0.0f, -1.0f));
        }

        [[nodiscard]] glm::vec3 GetRight() const
        {
            return glm::rotate(Orientation, glm::vec3(1.0f, 0.0f, 0.0f));
        }

        [[nodiscard]] glm::vec3 GetUp() const
        {
            return glm::rotate(Orientation, glm::vec3(0.0f, 1.0f, 0.0f));
        }
    };

    void UpdateMatrices(CameraComponent& camera, float aspectRatio);

    struct OrbitControlComponent
    {
        glm::vec3 Target{0.0f};
        float Sensitivity = 0.2f;
        float Distance = 5.0f;

        // WASD panning speed (world units per second).
        float PanSpeed = 3.0f;

        // Scroll zoom parameters.
        float ZoomSpeed = 1.0f;
        float MinDistance = 0.1f;
        float MaxDistance = 500.0f;

        double LastX = 0.0;
        double LastY = 0.0;
        bool FirstMouse = true;
    };

    // State for a Flying/FPS Camera (POD)
    struct FlyControlComponent
    {
        float MoveSpeed = 5.0f;
        float MouseSensitivity = 0.1f;
        float Yaw = 0.0f;
        float Pitch = 0.0f;

        double LastX = 0.0;
        double LastY = 0.0;
        bool FirstMouse = true;
    };

    // --- Controllers ---

    void OnResize(CameraComponent& camera, uint32_t width, uint32_t height);

    void OnUpdate(CameraComponent& camera,
        FlyControlComponent& flyControlComponent,
        const Core::Input::Context &inputContext,
        float dt, bool disableInput);

    void OnUpdate(CameraComponent& camera,
        OrbitControlComponent& orbitControlComponent,
        const Core::Input::Context &inputContext,
        float dt, bool disableInput);

    // =========================================================================
    // Coordinate-Space Transformation Utilities
    // =========================================================================
    //
    // Coordinate spaces:
    //
    //  World   — 3-D world coordinates (standard basis, no transform applied)
    //  View    — camera-relative; V = ViewMatrix * world
    //  Clip    — homogeneous post-projection; C = ProjectionMatrix * view
    //  NDC     — normalised device coordinates; ndc = clip.xyz / clip.w
    //              x,y ∈ [-1,+1], z ∈ [0,+1] (Vulkan depth convention)
    //  Screen  — pixel coordinates; x ∈ [0,W), y ∈ [0,H)
    //             NOTE: Y=0 is the *top* of the viewport (matches Vulkan/ImGui)
    //  Object  — entity-local; requires caller-supplied modelMatrix
    //
    // Convention note: Vulkan Y-flip is baked into CameraComponent::ProjectionMatrix
    // (ProjectionMatrix[1][1] *= -1), so NDC y=+1 is the top of the screen.
    //
    // Point transforms use homogeneous division (perspective-correct).
    // Direction/vector transforms use the upper-3x3 of the relevant matrix
    // (no translation) and skip the w divide.
    // =========================================================================

    // ---- World ↔ View -------------------------------------------------------

    /// Transform a world-space point into view/camera space.
    [[nodiscard]] inline glm::vec3 WorldToView(const CameraComponent& cam,
                                                const glm::vec3& worldPoint)
    {
        const glm::vec4 v = cam.ViewMatrix * glm::vec4(worldPoint, 1.0f);
        return glm::vec3(v);  // w == 1 for affine view matrix
    }

    /// Transform a view-space point back into world space.
    [[nodiscard]] inline glm::vec3 ViewToWorld(const CameraComponent& cam,
                                                const glm::vec3& viewPoint)
    {
        const glm::vec4 w = glm::inverse(cam.ViewMatrix) * glm::vec4(viewPoint, 1.0f);
        return glm::vec3(w);
    }

    /// Transform a world-space direction (no translation) into view space.
    [[nodiscard]] inline glm::vec3 WorldDirToView(const CameraComponent& cam,
                                                   const glm::vec3& worldDir)
    {
        return glm::mat3(cam.ViewMatrix) * worldDir;
    }

    /// Transform a view-space direction back into world space.
    [[nodiscard]] inline glm::vec3 ViewDirToWorld(const CameraComponent& cam,
                                                   const glm::vec3& viewDir)
    {
        return glm::mat3(glm::inverse(cam.ViewMatrix)) * viewDir;
    }

    // ---- World ↔ Clip -------------------------------------------------------

    /// Transform a world-space point into homogeneous clip space (w not divided).
    [[nodiscard]] inline glm::vec4 WorldToClip(const CameraComponent& cam,
                                                const glm::vec3& worldPoint)
    {
        return cam.ProjectionMatrix * cam.ViewMatrix * glm::vec4(worldPoint, 1.0f);
    }

    // ---- World ↔ NDC --------------------------------------------------------

    /// Project a world-space point into NDC [-1,+1]² × [0,1] (returns {nan,nan,nan}
    /// if the point is behind the near plane or on the projection singularity).
    [[nodiscard]] inline glm::vec3 WorldToNDC(const CameraComponent& cam,
                                               const glm::vec3& worldPoint)
    {
        const glm::vec4 clip = WorldToClip(cam, worldPoint);
        if (clip.w == 0.0f)
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            return glm::vec3(nan);
        }
        return glm::vec3(clip) / clip.w;
    }

    /// Unproject an NDC point back into world space.
    /// z should be in [0,1] (Vulkan depth); z=0 → near plane, z=1 → far plane.
    [[nodiscard]] inline glm::vec3 NDCToWorld(const CameraComponent& cam,
                                               const glm::vec3& ndc)
    {
        const glm::mat4 invVP = glm::inverse(cam.ProjectionMatrix * cam.ViewMatrix);
        const glm::vec4 h = invVP * glm::vec4(ndc, 1.0f);
        return glm::vec3(h) / h.w;
    }

    // ---- World ↔ Screen -----------------------------------------------------

    /// Project a world-space point to screen-pixel coordinates.
    /// Returns {x, y} with y=0 at the top (Vulkan/ImGui convention).
    /// Returns {nan,nan} if the point is behind the camera.
    [[nodiscard]] inline glm::vec2 WorldToScreen(const CameraComponent& cam,
                                                  const glm::vec3& worldPoint,
                                                  float viewportWidth,
                                                  float viewportHeight)
    {
        const glm::vec4 clip = WorldToClip(cam, worldPoint);
        if (clip.w <= 0.0f)
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            return glm::vec2(nan);
        }
        const glm::vec2 ndc = glm::vec2(clip) / clip.w;
        // NDC x: [-1,+1] → [0, W]
        // NDC y: [-1,+1] → [H, 0]  (flip because Vulkan NDC y=+1 is top)
        return {
            (ndc.x * 0.5f + 0.5f) * viewportWidth,
            (0.5f - ndc.y * 0.5f) * viewportHeight
        };
    }

    /// Unproject a screen-pixel coordinate (y=0 at top) at a given NDC depth
    /// back into world space.
    [[nodiscard]] inline glm::vec3 ScreenToWorld(const CameraComponent& cam,
                                                  const glm::vec2& screenPos,
                                                  float viewportWidth,
                                                  float viewportHeight,
                                                  float ndcDepth = 0.0f)
    {
        const float ndcX =  (screenPos.x / viewportWidth)  * 2.0f - 1.0f;
        const float ndcY = -(screenPos.y / viewportHeight) * 2.0f + 1.0f; // flip Y
        return NDCToWorld(cam, glm::vec3(ndcX, ndcY, ndcDepth));
    }

    // ---- NDC ↔ Screen -------------------------------------------------------

    /// Convert NDC xy [-1,+1] to screen-pixel coordinates (y=0 at top).
    [[nodiscard]] inline glm::vec2 NDCToScreen(const glm::vec2& ndc,
                                                float viewportWidth,
                                                float viewportHeight)
    {
        return {
            (ndc.x * 0.5f + 0.5f) * viewportWidth,
            (0.5f - ndc.y * 0.5f) * viewportHeight
        };
    }

    /// Convert screen-pixel coordinates (y=0 at top) to NDC xy [-1,+1].
    [[nodiscard]] inline glm::vec2 ScreenToNDC(const glm::vec2& screenPos,
                                                float viewportWidth,
                                                float viewportHeight)
    {
        return {
             (screenPos.x / viewportWidth)  * 2.0f - 1.0f,
            -(screenPos.y / viewportHeight) * 2.0f + 1.0f
        };
    }

    // ---- View ↔ NDC ---------------------------------------------------------

    /// Transform a view-space point to NDC.
    [[nodiscard]] inline glm::vec3 ViewToNDC(const CameraComponent& cam,
                                              const glm::vec3& viewPoint)
    {
        const glm::vec4 clip = cam.ProjectionMatrix * glm::vec4(viewPoint, 1.0f);
        if (clip.w == 0.0f)
        {
            const float nan = std::numeric_limits<float>::quiet_NaN();
            return glm::vec3(nan);
        }
        return glm::vec3(clip) / clip.w;
    }

    /// Unproject NDC back to view space.
    [[nodiscard]] inline glm::vec3 NDCToView(const CameraComponent& cam,
                                              const glm::vec3& ndc)
    {
        const glm::mat4 invProj = glm::inverse(cam.ProjectionMatrix);
        const glm::vec4 h = invProj * glm::vec4(ndc, 1.0f);
        return glm::vec3(h) / h.w;
    }

    // ---- World ↔ Object (local) space ---------------------------------------

    /// Transform a world-space point into object-local space given the model matrix.
    [[nodiscard]] inline glm::vec3 WorldToObject(const glm::mat4& modelMatrix,
                                                  const glm::vec3& worldPoint)
    {
        const glm::vec4 local = glm::inverse(modelMatrix) * glm::vec4(worldPoint, 1.0f);
        return glm::vec3(local) / local.w;
    }

    /// Transform an object-local point to world space.
    [[nodiscard]] inline glm::vec3 ObjectToWorld(const glm::mat4& modelMatrix,
                                                  const glm::vec3& localPoint)
    {
        const glm::vec4 w = modelMatrix * glm::vec4(localPoint, 1.0f);
        return glm::vec3(w) / w.w;
    }

    /// Transform a world-space direction into object-local space (no translation).
    [[nodiscard]] inline glm::vec3 WorldDirToObject(const glm::mat4& modelMatrix,
                                                     const glm::vec3& worldDir)
    {
        return glm::mat3(glm::inverse(modelMatrix)) * worldDir;
    }

    /// Transform an object-local direction to world space (no translation).
    [[nodiscard]] inline glm::vec3 ObjectDirToWorld(const glm::mat4& modelMatrix,
                                                     const glm::vec3& localDir)
    {
        return glm::mat3(modelMatrix) * localDir;
    }

    // ---- Camera-ray factory -------------------------------------------------

    /// Build a world-space ray from the camera through a screen-pixel position.
    /// Uses the same combined-VP-inverse construction as ImGuizmo::ComputeCameraRay.
    struct CameraRay
    {
        glm::vec3 Origin;    ///< World-space ray origin (near-plane point)
        glm::vec3 Direction; ///< Normalised world-space direction
    };

    [[nodiscard]] inline CameraRay RayFromScreen(const CameraComponent& cam,
                                                  const glm::vec2& screenPos,
                                                  float viewportWidth,
                                                  float viewportHeight)
    {
        const glm::vec2 ndc = ScreenToNDC(screenPos, viewportWidth, viewportHeight);
        const glm::mat4 invVP = glm::inverse(cam.ProjectionMatrix * cam.ViewMatrix);

        glm::vec4 nearH = invVP * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
        nearH /= nearH.w;
        glm::vec4 farH  = invVP * glm::vec4(ndc.x, ndc.y, 1.0f - 1e-6f, 1.0f);
        farH  /= farH.w;

        const glm::vec3 origin = glm::vec3(nearH);
        const glm::vec3 dir    = glm::normalize(glm::vec3(farH) - origin);
        return {origin, dir};
    }

    /// Same as RayFromScreen but accepts NDC directly (convenience for gizmo math).
    [[nodiscard]] inline CameraRay RayFromNDC(const CameraComponent& cam,
                                               const glm::vec2& ndc)
    {
        const glm::mat4 invVP = glm::inverse(cam.ProjectionMatrix * cam.ViewMatrix);

        glm::vec4 nearH = invVP * glm::vec4(ndc.x, ndc.y, 0.0f, 1.0f);
        nearH /= nearH.w;
        glm::vec4 farH  = invVP * glm::vec4(ndc.x, ndc.y, 1.0f - 1e-6f, 1.0f);
        farH  /= farH.w;

        const glm::vec3 origin = glm::vec3(nearH);
        const glm::vec3 dir    = glm::normalize(glm::vec3(farH) - origin);
        return {origin, dir};
    }
}
