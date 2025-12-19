module;

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

module Runtime.Graphics.Camera;
import Core.Input;

namespace Runtime::Graphics
{
    void UpdateMatrices(CameraComponent& camera, float aspectRatio)
    {
        // View
        glm::mat4 rotate = glm::toMat4(glm::conjugate(camera.Orientation));
        glm::mat4 translate = glm::translate(glm::mat4(1.0f), -camera.Position);
        camera.ViewMatrix = rotate * translate;

        // Projection (Vulkan: Y flipped, Depth 0..1)
        camera.ProjectionMatrix = glm::perspective(glm::radians(camera.Fov), aspectRatio, camera.Near, camera.Far);
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
        float, bool disableInput)
    {
        if (disableInput) return;

        using namespace Core::Input;

        // Zoom logic (Simple W/S for distance for now, mouse scroll later)
        // Recalculate distance based on current position to stay in sync
        glm::vec3 offset = camera.Position - orbitControlComponent.Target;

        // Handle Rotation
        if (inputContext.IsMouseButtonPressed(0)) // Left Click
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

            // --- Trackball Logic ---
            // 1. Get Camera Basis Vectors
            glm::vec3 camRight = camera.GetRight();
            glm::vec3 camUp = camera.GetUp();

            // 2. Create rotation quaternions
            // World Up for stability, OR camUp for free-tumble
            // For "continuous in any direction" like PMP, usually we rotate around Screen Axes (CamUp/CamRight).
            // Let's use CamUp to allow tumbling over the pole cleanly.
            glm::quat yawRot = glm::angleAxis(glm::radians(-xDelta), camUp);

            glm::quat pitchRot = glm::angleAxis(glm::radians(-yDelta), camRight);

            // 3. Combine rotations (Pitch then Yaw is standard for trackball feel)
            glm::quat rotation = yawRot * pitchRot;

            // 4. Apply rotation to offset and orientation
            offset = rotation * offset;
            camera.Orientation = glm::normalize(rotation * camera.Orientation);

            orbitControlComponent.LastX = pos.x;
            orbitControlComponent.LastY = pos.y;
        }
        else
        {
            orbitControlComponent.FirstMouse = true;
        }

        // Apply Zoom/Movement (Optional: Add wheel support here in future)
        // Just ensure position is updated based on Target + Rotated Offset
        camera.Position = orbitControlComponent.Target + offset;
    }
}