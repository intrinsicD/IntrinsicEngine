// src/Runtime/Graphics/Graphics.Camera.cppm
module;

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

export module Runtime.Graphics.Camera;

import Core.Input;

export namespace Runtime::Graphics
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
}
