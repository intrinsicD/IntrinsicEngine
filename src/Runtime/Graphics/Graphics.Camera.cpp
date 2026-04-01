module;

#include <algorithm>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

module Graphics.Camera;
import Core.Input;

namespace Graphics
{
    void UpdateMatrices(CameraComponent& camera, float aspectRatio)
    {
        camera.AspectRatio = aspectRatio;

        // View
        glm::mat4 rotate = glm::toMat4(glm::conjugate(camera.Orientation));
        glm::mat4 translate = glm::translate(glm::mat4(1.0f), -camera.Position);
        camera.ViewMatrix = rotate * translate;

        const float safeAspect = (aspectRatio > 0.0f) ? aspectRatio : 1.0f;
        const float safeNear = std::max(1e-4f, camera.Near);
        const float safeFar = std::max(safeNear + 1e-3f, camera.Far);

        if (camera.ProjectionType == CameraProjectionType::Orthographic)
        {
            const float orthoHeight = std::max(1e-4f, camera.OrthographicHeight);
            const float halfHeight = 0.5f * orthoHeight;
            const float halfWidth = halfHeight * safeAspect;
            camera.ProjectionMatrix = glm::ortho(-halfWidth, halfWidth, -halfHeight, halfHeight, safeNear, safeFar);
        }
        else
        {
            // Projection (Vulkan: Y flipped, Depth 0..1)
            const float safeFov = glm::clamp(camera.Fov, 1.0f, 179.0f);
            camera.ProjectionMatrix = glm::perspective(glm::radians(safeFov), safeAspect, safeNear, safeFar);
        }

        camera.ProjectionMatrix[1][1] *= -1;
    }

    void OnResize(CameraComponent& camera, uint32_t width, uint32_t height)
    {
        if (height > 0) camera.AspectRatio = (float)width / (float)height;
    }

    void OnUpdate(CameraComponent& camera,
        FlyControlComponent& flyControlComponent,
        const Core::Input::Context &inputContext,
        float dt, bool disableInput)
    {
        if (disableInput) return;

        using namespace Core::Input;

        // 1. Mouse Look
        if (inputContext.IsMouseButtonPressed(1)) // Right Click to look
        {
            glm::vec2 pos = inputContext.GetMousePosition();

            if (flyControlComponent.FirstMouse)
            {
                flyControlComponent.LastX = pos.x;
                flyControlComponent.LastY = pos.y;
                flyControlComponent.FirstMouse = false;
            }

            float xOffset = static_cast<float>(pos.x - flyControlComponent.LastX) * flyControlComponent.MouseSensitivity;
            float yOffset = static_cast<float>(pos.y - flyControlComponent.LastY) * flyControlComponent.MouseSensitivity;

            flyControlComponent.Yaw -= xOffset;
            flyControlComponent.Pitch -= yOffset;

            // Clamp Pitch
            if (flyControlComponent.Pitch > 89.0f) flyControlComponent.Pitch = 89.0f;
            if (flyControlComponent.Pitch < -89.0f) flyControlComponent.Pitch = -89.0f;

            flyControlComponent.LastX = pos.x;
            flyControlComponent.LastY = pos.y;
        }
        else
        {
            flyControlComponent.FirstMouse = true;
        }

        // Reconstruct Orientation from Yaw/Pitch
        glm::quat qPitch = glm::angleAxis(glm::radians(flyControlComponent.Pitch), glm::vec3(1, 0, 0));
        glm::quat qYaw = glm::angleAxis(glm::radians(flyControlComponent.Yaw), glm::vec3(0, 1, 0));
        camera.Orientation = glm::normalize(qYaw * qPitch);

        // 2. Movement
        float velocity = flyControlComponent.MoveSpeed * dt;
        if (inputContext.IsKeyPressed(Key::LeftShift)) velocity *= 2.0f;
        if (inputContext.IsKeyPressed(Key::W)) camera.Position += camera.GetForward() * velocity;
        if (inputContext.IsKeyPressed(Key::S)) camera.Position -= camera.GetForward() * velocity;
        if (inputContext.IsKeyPressed(Key::D)) camera.Position += camera.GetRight() * velocity;
        if (inputContext.IsKeyPressed(Key::A)) camera.Position -= camera.GetRight() * velocity;
        if (inputContext.IsKeyPressed(Key::Space)) camera.Position += glm::vec3(0, 1, 0) * velocity;
    }

    // Trackball / Free Orbit Camera

    void OnUpdate(CameraComponent& camera,
        OrbitControlComponent& orbitControlComponent,
        const Core::Input::Context &inputContext,
        float dt, bool disableInput)
    {
        if (disableInput) return;

        using namespace Core::Input;

        glm::vec3 offset = camera.Position - orbitControlComponent.Target;

        // --- 1. Handle Rotation (RMB drag) ---
        if (inputContext.IsMouseButtonPressed(1)) // Right Click
        {
            glm::vec2 pos = inputContext.GetMousePosition();

            if (orbitControlComponent.FirstMouse)
            {
                orbitControlComponent.LastX = pos.x;
                orbitControlComponent.LastY = pos.y;
                orbitControlComponent.FirstMouse = false;
            }

            float xDelta = static_cast<float>(pos.x - orbitControlComponent.LastX) * orbitControlComponent.Sensitivity;
            float yDelta = static_cast<float>(pos.y - orbitControlComponent.LastY) * orbitControlComponent.Sensitivity;

            glm::vec3 camRight = camera.GetRight();
            glm::vec3 camUp = camera.GetUp();

            glm::quat yawRot = glm::angleAxis(glm::radians(-xDelta), camUp);
            glm::quat pitchRot = glm::angleAxis(glm::radians(-yDelta), camRight);
            glm::quat rotation = yawRot * pitchRot;

            offset = rotation * offset;
            camera.Orientation = glm::normalize(rotation * camera.Orientation);

            orbitControlComponent.LastX = pos.x;
            orbitControlComponent.LastY = pos.y;
        }
        else
        {
            orbitControlComponent.FirstMouse = true;
        }

        // --- 2. Scroll Zoom ---
        glm::vec2 scroll = inputContext.GetScrollDelta();
        if (scroll.y != 0.0f)
        {
            float currentDist = glm::length(offset);
            // Proportional zoom: zoom amount scales with current distance for natural feel.
            float zoomAmount = scroll.y * orbitControlComponent.ZoomSpeed * currentDist * 0.1f;
            float newDist = currentDist - zoomAmount;
            newDist = glm::clamp(newDist, orbitControlComponent.MinDistance, orbitControlComponent.MaxDistance);

            if (currentDist > 0.0001f)
            {
                offset = glm::normalize(offset) * newDist;
            }
            orbitControlComponent.Distance = newDist;
        }

        // --- 3. WASD Panning (moves both target and camera) ---
        {
            float velocity = orbitControlComponent.PanSpeed * dt;
            if (inputContext.IsKeyPressed(Key::LeftShift)) velocity *= 2.5f;

            // Project camera forward onto the horizontal plane for W/S movement.
            glm::vec3 forward = camera.GetForward();
            glm::vec3 horizontalForward = glm::normalize(glm::vec3(forward.x, 0.0f, forward.z));
            // If camera is looking straight down/up, use the camera's up projected instead.
            if (glm::length(glm::vec3(forward.x, 0.0f, forward.z)) < 0.001f)
            {
                glm::vec3 up = camera.GetUp();
                horizontalForward = glm::normalize(glm::vec3(up.x, 0.0f, up.z));
            }

            glm::vec3 right = camera.GetRight();
            glm::vec3 horizontalRight = glm::normalize(glm::vec3(right.x, 0.0f, right.z));

            glm::vec3 panDelta{0.0f};
            if (inputContext.IsKeyPressed(Key::W)) panDelta += horizontalForward * velocity;
            if (inputContext.IsKeyPressed(Key::S)) panDelta -= horizontalForward * velocity;
            if (inputContext.IsKeyPressed(Key::D)) panDelta += horizontalRight * velocity;
            if (inputContext.IsKeyPressed(Key::A)) panDelta -= horizontalRight * velocity;

            orbitControlComponent.Target += panDelta;
        }

        // --- 4. Update camera position from target + rotated offset ---
        camera.Position = orbitControlComponent.Target + offset;
    }
}
