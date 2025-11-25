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
import Core.Assets;
import Runtime.Graphics.Model;
import Runtime.Graphics.Material;
import Runtime.Graphics.RenderSystem;
import Runtime.Graphics.ModelLoader;
import Runtime.ECS.Components;
import Runtime.RHI.Types;

using namespace Core;

// --- The Application Class ---
class SandboxApp : public Runtime::Engine
{
public:
    SandboxApp() : Engine({"Sandbox", 1600, 900})
    {
    }

    // Resources
    Assets::AssetHandle m_DuckHandle;
    std::shared_ptr<Runtime::Graphics::Material> m_Material;

    // State to track if we have spawned the entity yet
    bool m_IsEntitySpawned = false;

    // Camera State
    glm::vec3 m_CamPos = {0.0f, 2.0f, 4.0f};
    float m_CamYaw = -90.0f;
    float m_CamPitch = -20.0f;

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        // 1. Define the Loader Lambda
        auto modelLoader = [&](const std::string& path)
        {
            return Runtime::Graphics::ModelLoader::Load(GetDevice(), path);
        };

        // 2. Start Async Load (Returns immediately)
        // We use "assets/models/Duck.glb" assuming your folder structure
        m_DuckHandle = m_AssetManager.Load<Runtime::Graphics::Model>("assets/models/Duck.glb", modelLoader);

        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        m_Material = std::make_shared<Runtime::Graphics::Material>(
            GetDevice(), GetDescriptorPool(), GetDescriptorLayout(), "assets/textures/DuckCM.png"
        );
        m_Material->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(Runtime::RHI::CameraBufferObject));

        Log::Info("Asset Load Requested. Waiting for background thread...");
    }

    void OnUpdate(float dt) override
    {
        // --- FPS Counter ---
        static float timer = 0.0f;
        static int frames = 0;
        timer += dt;
        frames++;
        if (timer >= 1.0f)
        {
            std::string title = "Intrinsic Engine | FPS: " + std::to_string(frames) + " | ms: " + std::to_string(
                1000.0f / frames);
            m_Window->SetTitle(title);
            frames = 0;
            timer = 0.0f;
        }

        // --- ASSET SYSTEM POLLING ---
        // Check if the entity is not yet spawned, and if the asset is finally ready
        if (!m_IsEntitySpawned)
        {
            using namespace Core::Assets;

            LoadState state = m_AssetManager.GetState(m_DuckHandle);

            if (state == LoadState::Ready)
            {
                // Retrieve the loaded model safely
                auto model = m_AssetManager.Get<Runtime::Graphics::Model>(m_DuckHandle);

                if (model && !model->Meshes.empty())
                {
                    // Create Entity
                    auto e = m_Scene.CreateEntity("Duck");
                    auto& t = m_Scene.GetRegistry().get<Runtime::ECS::TransformComponent>(e);
                    t.Scale = glm::vec3(0.01f); // GLTF units are often large

                    auto& mr = m_Scene.GetRegistry().emplace<Runtime::ECS::MeshRendererComponent>(e);
                    // Now safe to access Meshes[0]
                    mr.MeshRef = model->Meshes[0];
                    mr.MaterialRef = m_Material;

                    m_IsEntitySpawned = true;
                    Log::Info("Duck Asset Ready! Entity Spawned.");
                }
            }
            else if (state == LoadState::Failed)
            {
                Log::Error("Duck failed to load!");
                m_IsEntitySpawned = true; // Stop trying
            }
        }

        // --- Rotate Entities ---
        auto view = m_Scene.GetRegistry().view<Runtime::ECS::TransformComponent>();
        for (auto [entity, transform] : view.each())
        {
            transform.Rotation.y += 45.0f * dt;
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
    }
};

int main()
{
    SandboxApp app;
    app.Run();
    return 0;
}
