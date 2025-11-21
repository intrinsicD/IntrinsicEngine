#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>

// Include Macros for Vulkan (VK_NO_PROTOTYPES)
#include "RHI/RHI.Vulkan.hpp"

import Runtime.Engine;
import Core.Logging;
import Core.Input;
import Runtime.Graphics.Mesh;
import Runtime.Graphics.Material;
import Runtime.Graphics.RenderSystem;
import Runtime.Graphics.ModelLoader;
import Runtime.ECS.Components;
import Runtime.RHI.Types;

using namespace Core;

// Define Data
const std::vector<Runtime::RHI::Vertex> vertices = {
    {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
    {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f}}
};
const std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

// --- The Application Class ---
class SandboxApp : public Runtime::Engine
{
public:
    SandboxApp() : Engine({"Sandbox", 1600, 900})
    {
    }

    // Store resources here so they stay alive
    std::vector<std::unique_ptr<Runtime::Graphics::Mesh>> m_Meshes;
    std::unique_ptr<Runtime::Graphics::Material> m_Material;

    // Camera State
    glm::vec3 m_CamPos = {0.0f, 2.0f, 4.0f};
    float m_CamYaw = -90.0f;
    float m_CamPitch = -20.0f;

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        // 1. Assets
        m_Meshes = Runtime::Graphics::ModelLoader::Load(GetDevice(), "assets/models/Duck.glb");
        bool success = m_Meshes.size() > 0;

        if (!success)
        {
            m_Meshes.emplace_back(std::make_unique<Runtime::Graphics::Mesh>(GetDevice(), vertices, indices));
        }

        m_Material = std::make_unique<Runtime::Graphics::Material>(
            GetDevice(), GetDescriptorPool(), GetDescriptorLayout(), "assets/textures/Checkerboard.png"
        );

        // Link Material to Global Camera UBO
        m_Material->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(Runtime::RHI::CameraBufferObject));

        // 2. Scene Setup
        if (success)
        {
            for (auto& mesh : m_Meshes) {
                auto e = m_Scene.CreateEntity("ModelPart");
                auto& t = m_Scene.GetRegistry().get<Runtime::ECS::TransformComponent>(e);
                t.Scale = glm::vec3(0.01f); // Adjust scale depending on model unit size

                auto& mr = m_Scene.GetRegistry().emplace<Runtime::ECS::MeshRendererComponent>(e);
                mr.MeshRef = mesh.get();
                mr.MaterialRef = m_Material.get();
            }
        }else
        {
            for (int i = 0; i < 10; i++)
            {
                for (int j = 0; j < 10; j++)
                {
                    auto e = m_Scene.CreateEntity("Quad");
                    auto& t = m_Scene.GetRegistry().get<Runtime::ECS::TransformComponent>(e);
                    t.Position = glm::vec3(i * 1.5f - 7.5f, j * 1.5f - 7.5f, 0.0f);

                    auto& mr = m_Scene.GetRegistry().emplace<Runtime::ECS::MeshRendererComponent>(e);
                    mr.MeshRef = m_Meshes[0].get();
                    mr.MaterialRef = m_Material.get();
                }
            }
        }
    }

    void OnUpdate(float dt) override
    {

        static float timer = 0.0f;
        static int frames = 0;
        timer += dt;
        frames++;

        if (timer >= 1.0f) {
            std::string title = "Intrinsic Engine | FPS: " + std::to_string(frames) + " | ms: " + std::to_string(1000.0f / frames);
            m_Window->SetTitle(title);
            frames = 0;
            timer = 0.0f;
        }

        // --- Free Fly Camera Control ---
        float speed = 5.0f * dt;
        if (Input::IsKeyPressed(Input::Key::LeftShift)) speed *= 2.0f;

        glm::vec3 front;
        front.x = cos(glm::radians(m_CamYaw)) * cos(glm::radians(m_CamPitch));
        front.y = sin(glm::radians(m_CamPitch));
        front.z = sin(glm::radians(m_CamYaw)) * cos(glm::radians(m_CamPitch));
        front = glm::normalize(front);

        glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
        glm::vec3 up = glm::normalize(glm::cross(right, front));

        if (Input::IsKeyPressed(Input::Key::W)) m_CamPos += front * speed;
        if (Input::IsKeyPressed(Input::Key::S)) m_CamPos -= front * speed;
        if (Input::IsKeyPressed(Input::Key::A)) m_CamPos -= right * speed;
        if (Input::IsKeyPressed(Input::Key::D)) m_CamPos += right * speed;
        if (Input::IsKeyPressed(Input::Key::Space)) m_CamPos += up * speed;

        // Camera Matrix Calculation
        Runtime::Graphics::CameraData camData{};
        camData.View = glm::lookAt(m_CamPos, m_CamPos + front, up);
        camData.Proj = glm::perspective(glm::radians(45.0f),
                                        GetSwapchain().GetExtent().width / (float)GetSwapchain().GetExtent().height,
                                        0.1f, 1000.0f);
        camData.Proj[1][1] *= -1;

        // Draw
        m_RenderSystem->OnUpdate(m_Scene, camData);
    }

    void OnRender() override
    {
        // Optional: Custom rendering if needed
    }
};

// --- Entry Point ---
int main()
{
    SandboxApp app;
    app.Run();
    return 0;
}
