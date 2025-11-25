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
    struct Camera
    {
        glm::vec3 Position{0.0f, 2.0f, 4.0f};
        glm::quat Orientation{1.0f, 0.0f, 0.0f, 0.0f}; // Identity
        // float m_CamYaw = -90.0f;
        // float m_CamPitch = -20.0f;

        float Fov = 45.0f;
        float AspectRatio = 1.77f;
        float Near = 0.1f;
        float Far = 1000.0f;

        glm::mat4 ViewMatrix{1.0f};
        glm::mat4 ProjectionMatrix{1.0f};

        void UpdateMatrices()
        {
            // View
            glm::mat4 rotate = glm::toMat4(glm::conjugate(Orientation));
            glm::mat4 translate = glm::translate(glm::mat4(1.0f), -Position);
            ViewMatrix = rotate * translate;

            // Projection (Vulkan: Y flipped, Depth 0..1)
            ProjectionMatrix = glm::perspective(glm::radians(Fov), AspectRatio, Near, Far);
            ProjectionMatrix[1][1] *= -1;
        }

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

    // --- Controllers ---

    class CameraController
    {
    public:
        virtual ~CameraController() = default;
        virtual void OnUpdate(Camera& camera, float dt) = 0;

        virtual void OnResize(Camera& camera, uint32_t width, uint32_t height)
        {
            if (height > 0) camera.AspectRatio = (float)width / (float)height;
        }
    };

    class FPSCameraController : public CameraController
    {
    public:
        float MoveSpeed = 5.0f;
        float MouseSensitivity = 0.1f;

        void OnUpdate(Camera& camera, float dt) override
        {
            using namespace Core::Input;

            // 1. Mouse Look
            if (IsMouseButtonPressed(1)) // Right Click to look
            {
                double x = GetMouseX();
                double y = GetMouseY();

                if (m_FirstMouse)
                {
                    m_LastX = x;
                    m_LastY = y;
                    m_FirstMouse = false;
                }

                float xOffset = static_cast<float>(x - m_LastX) * MouseSensitivity;
                float yOffset = static_cast<float>(y - m_LastY) * MouseSensitivity;

                m_Yaw -= xOffset;
                m_Pitch -= yOffset;

                // Clamp Pitch
                if (m_Pitch > 89.0f) m_Pitch = 89.0f;
                if (m_Pitch < -89.0f) m_Pitch = -89.0f;

                m_LastX = x;
                m_LastY = y;
            }
            else
            {
                m_FirstMouse = true;
            }

            // Reconstruct Orientation from Yaw/Pitch
            glm::quat qPitch = glm::angleAxis(glm::radians(m_Pitch), glm::vec3(1, 0, 0));
            glm::quat qYaw = glm::angleAxis(glm::radians(m_Yaw), glm::vec3(0, 1, 0));
            camera.Orientation = glm::normalize(qYaw * qPitch);

            // 2. Movement
            float velocity = MoveSpeed * dt;
            if (IsKeyPressed(Key::LeftShift)) velocity *= 2.0f;

            if (IsKeyPressed(Key::W)) camera.Position += camera.GetForward() * velocity;
            if (IsKeyPressed(Key::S)) camera.Position -= camera.GetForward() * velocity;
            if (IsKeyPressed(Key::D)) camera.Position += camera.GetRight() * velocity;
            if (IsKeyPressed(Key::A)) camera.Position -= camera.GetRight() * velocity;
            if (IsKeyPressed(Key::Space)) camera.Position += glm::vec3(0, 1, 0) * velocity;

            // 3. Update Matrices
            camera.UpdateMatrices();
        }

    private:
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;
        double m_LastX = 0.0;
        double m_LastY = 0.0;
        bool m_FirstMouse = true;
    };

    // Simple Orbit Camera for inspecting models
    class OrbitCameraController : public CameraController
    {
    public:
        glm::vec3 Target{0.0f};
        float Radius = 5.0f;
        float Sensitivity = 0.2f;

        void OnUpdate(Camera& camera, float dt) override
        {
            using namespace Core::Input;
            if (IsMouseButtonPressed(0)) // Left Click
            {
                double x = GetMouseX();
                double y = GetMouseY();

                if (m_FirstMouse)
                {
                    m_LastX = x;
                    m_LastY = y;
                    m_FirstMouse = false;
                }

                float xDelta = static_cast<float>(x - m_LastX) * Sensitivity;
                float yDelta = static_cast<float>(y - m_LastY) * Sensitivity;

                m_Theta -= xDelta; // Azimuth
                m_Phi += yDelta; // Elevation

                // Clamp Phi to avoid Gimbal lock (approx)
                if (m_Phi > 89.0f) m_Phi = 89.0f;
                if (m_Phi < -89.0f) m_Phi = -89.0f;

                m_LastX = x;
                m_LastY = y;
            }
            else
            {
                m_FirstMouse = true;
            }

            // Convert Spherical to Cartesian
            float radPhi = glm::radians(m_Phi);
            float radTheta = glm::radians(m_Theta);

            float x = Radius * cos(radPhi) * cos(radTheta);
            float y = Radius * sin(radPhi);
            float z = Radius * cos(radPhi) * sin(radTheta);

            camera.Position = Target + glm::vec3(x, y, z);
            camera.Orientation = glm::quatLookAt(glm::normalize(Target - camera.Position), glm::vec3(0, 1, 0));
            camera.UpdateMatrices();
        }

    private:
        float m_Theta = 90.0f;
        float m_Phi = 0.0f;
        double m_LastX = 0.0;
        double m_LastY = 0.0;
        bool m_FirstMouse = true;
    };
}
