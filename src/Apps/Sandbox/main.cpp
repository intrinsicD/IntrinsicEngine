#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>

// Include Macros for Vulkan (VK_NO_PROTOTYPES)
#include "tiny_gltf.h"
#include "RHI/RHI.Vulkan.hpp"

import Runtime.Engine;
import Core.Logging;
import Core.Input;
import Core.Assets;
import Runtime.Graphics.Model;
import Runtime.Graphics.Material;
import Runtime.Graphics.RenderSystem;
import Runtime.Graphics.ModelLoader;
import Runtime.Graphics.Camera;
import Runtime.ECS.Components;
import Runtime.RHI.Types;
import Runtime.RHI.Texture;
import Runtime.Interface.GUI;

using namespace Core;
using namespace Runtime;

// --- The Application Class ---
class SandboxApp : public Engine
{
public:
    SandboxApp() : Engine({"Sandbox", 1600, 900})
    {
    }

    // Resources
    Assets::AssetHandle m_DuckModel;
    Assets::AssetHandle m_DuckTexture;
    std::shared_ptr<Graphics::Material> m_DuckMaterial;

    // State to track if we have spawned the entity yet
    bool m_IsEntitySpawned = false;

    // Camera State
    Graphics::Camera m_Camera;
    std::unique_ptr<Graphics::CameraController> m_CameraController;

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        m_Camera.Position = {0.0f, 0.0f, 4.0f};
        m_CameraController = std::make_unique<Graphics::OrbitCameraController>();

        auto textureLoader = [&](const std::string& path)
        {
            // Note: RHI::Texture constructor does IO + GPU upload synchronously here
            // In a real engine, IO is separate from Upload.
            return std::make_shared<RHI::Texture>(GetDevice(), path);
        };
        m_DuckTexture = m_AssetManager.Load<RHI::Texture>("assets/textures/DuckCM.png", textureLoader);

        // 1. Define the Loader Lambda
        auto modelLoader = [&](const std::string& path)
        {
            return Graphics::ModelLoader::Load(GetDevice(), path);
        };

        // 2. Start Async Load (Returns immediately)
        // We use "assets/models/Duck.glb" assuming your folder structure
        m_DuckModel = m_AssetManager.Load<Graphics::Model>("assets/models/Duck.glb", modelLoader);

        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        m_DuckMaterial = std::make_shared<Graphics::Material>(
            GetDevice(), GetDescriptorPool(), GetDescriptorLayout(), m_DuckTexture
        );
        m_DuckMaterial->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(RHI::CameraBufferObject));

        Log::Info("Asset Load Requested. Waiting for background thread...");
    }

    void OnUpdate(float dt) override
    {
        bool mouseCaptured = Interface::GUI::WantCaptureMouse();

        if (m_CameraController)
        {
            m_CameraController->OnUpdate(m_Camera, dt, mouseCaptured);
        }


        if (m_Window->GetWidth() != 0 && m_Window->GetHeight() != 0)
        {
            m_CameraController->OnResize(m_Camera, m_Window->GetWidth(), m_Window->GetHeight());
        }

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
            if (m_AssetManager.GetState(m_DuckModel) == Assets::LoadState::Ready)
            {
                auto model = m_AssetManager.Get<Graphics::Model>(m_DuckModel);
                if (model && !model->Meshes.empty())
                {
                    auto e = m_Scene.CreateEntity("Duck");
                    auto& t = m_Scene.GetRegistry().get<ECS::TransformComponent>(e);
                    t.Scale = glm::vec3(0.01f);

                    auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(e);
                    mr.MeshRef = model->Meshes[0];
                    mr.MaterialRef = m_DuckMaterial; // Assign material with handle

                    m_IsEntitySpawned = true;
                    Log::Info("Duck Entity Spawned.");
                }
            }
        }

        // --- Rotate Entities ---
        auto view = m_Scene.GetRegistry().view<ECS::TransformComponent>();
        for (auto [entity, transform] : view.each())
        {
            ECS::TransformRotating rotator;
            rotator.OnUpdate(transform, dt);
        }

        //if (Input::IsKeyPressed(Input::Key::Tab)) {
        // Simple toggle logic (debounce omitted for brevity)
        //}

        // Draw
        m_RenderSystem->OnUpdate(m_Scene, m_Camera, m_AssetManager);
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
