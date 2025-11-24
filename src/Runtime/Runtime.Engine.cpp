module;
#include <chrono>
#include <queue>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <RHI/RHI.Vulkan.hpp>
#include <glm/glm.hpp>

module Runtime.Engine;

import Core.Logging;
import Core.Input;
import Core.Window;
import Core.Memory;
import Runtime.RHI.Shader;
import Runtime.Graphics.ModelLoader;
import Runtime.Graphics.Material;
import Runtime.ECS.Components;
import Runtime.RHI.Types;

namespace Runtime
{
    Engine::Engine(const EngineConfig& config) : m_FrameArena(1024 * 1024) // 1 MB per frame
    {
        Core::Log::Info("Initializing Engine...");

        // 1. Window
        Core::Windowing::WindowProps props{config.AppName, config.Width, config.Height};
        m_Window = std::make_unique<Core::Windowing::Window>(props);

        Core::Input::Initialize(m_Window->GetNativeHandle());
        m_Window->SetEventCallback([this](const Core::Windowing::Event& e)
        {
            if (e.Type == Core::Windowing::EventType::WindowClose) m_Running = false;
            if (e.Type == Core::Windowing::EventType::KeyPressed && e.KeyCode == 256) m_Running = false;
            if (e.Type == Core::Windowing::EventType::WindowResize) { m_FramebufferResized = true; }
            if (e.Type == Core::Windowing::EventType::WindowDrop)
            {
                for (const auto& path : e.Paths)
                {
                    LoadDroppedAsset(path);
                }
            }
        });

        // 2. Vulkan Context & Surface
#ifdef NDEBUG
        bool enableValidation = false;
#else
        bool enableValidation = true;
#endif
        RHI::ContextConfig rhiConfig{config.AppName, enableValidation};
        m_Context = std::make_unique<RHI::VulkanContext>(rhiConfig);

        if (!m_Window->CreateSurface(m_Context->GetInstance(), nullptr, &m_Surface))
        {
            Core::Log::Error("FATAL: Failed to create Vulkan Surface");
            std::exit(-1); // Hard fail without exceptions
        }

        // 3. Device
        m_Device = std::make_unique<RHI::VulkanDevice>(*m_Context, m_Surface);

        // 4. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(*m_Device, *m_Window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(*m_Device, *m_Swapchain);

        InitPipeline();
    }

    Engine::~Engine()
    {
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());

        // Order matters!
        m_Scene.GetRegistry().clear();
        m_RenderSystem.reset();
        m_Pipeline.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_Renderer.reset();
        m_Swapchain.reset();
        m_Device.reset();

        vkDestroySurfaceKHR(m_Context->GetInstance(), m_Surface, nullptr);
        m_Context.reset();
        m_Window.reset();
    }

    void Engine::LoadDroppedAsset(const std::string& path)
    {
        std::filesystem::path fsPath(path);
        std::filesystem::path canonical = std::filesystem::canonical(fsPath);
        std::filesystem::path assetDir = std::filesystem::canonical("assets/");

        if (!canonical.string().starts_with(assetDir.string()))
        {
            Core::Log::Error("Dropped file is outside of assets directory: {}", path);
            return;
        }
        std::string ext = fsPath.extension().string();

        // Convert to lower case for comparison
        for (auto& c : ext) c = tolower(c);

        if (ext == ".gltf" || ext == ".glb")
        {
            Core::Log::Info("Drop Detected: Loading Model {}", path);

            // TODO: Move this to Async Task Graph to prevent frame freeze
            auto meshes = Graphics::ModelLoader::Load(GetDevice(), path);

            if (meshes.empty())
            {
                Core::Log::Error("Failed to load model or model was empty.");
                return;
            }

            // Create a default material (Pink debug to show if textures fail)
            // In a real scenario, ModelLoader should return Materials too.
            static auto defaultMat = std::make_shared<Graphics::Material>(
                GetDevice(), GetDescriptorPool(), GetDescriptorLayout(), "assets/textures/DuckCM.png"
            );
            defaultMat->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(RHI::CameraBufferObject));

            // Create Entity
            auto entity = m_Scene.CreateEntity(fsPath.stem().string());

            auto& t = m_Scene.GetRegistry().get<ECS::TransformComponent>(entity);
            t.Scale = glm::vec3(1.0f);
            t.Position = glm::vec3(0.0f, 0.0f, 0.0f); // Reset position

            // If we loaded multiple meshes (submeshes), we might need multiple entities
            // For simplicity here, we attach the first mesh to the entity.
            // A better approach is to create children entities for each mesh.

            for (size_t i = 0; i < meshes.size(); i++)
            {
                if (i == 0)
                {
                    auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(entity);
                    mr.MeshRef = meshes[i];
                    mr.MaterialRef = defaultMat;
                }
                else
                {
                    // Create child entities for submeshes
                    auto child = m_Scene.CreateEntity(fsPath.stem().string() + "_" + std::to_string(i));
                    auto& ct = m_Scene.GetRegistry().get<ECS::TransformComponent>(child);
                    // In a real loader, we would apply local transforms from GLTF nodes here

                    auto& cmr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(child);
                    cmr.MeshRef = meshes[i];
                    cmr.MaterialRef = defaultMat;
                }
            }
            Core::Log::Info("Model instantiated into Scene.");
        }
        else
        {
            Core::Log::Warn("Unsupported file extension dropped: {}", ext);
        }
    }

    void Engine::InitPipeline()
    {
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorPool>(*m_Device);

        RHI::ShaderModule vert(*m_Device, "shaders/triangle.vert.spv", RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(*m_Device, "shaders/triangle.frag.spv", RHI::ShaderStage::Fragment);

        RHI::PipelineConfig config{&vert, &frag};
        m_Pipeline = std::make_unique<RHI::GraphicsPipeline>(*m_Device, *m_Swapchain, config,
                                                             m_DescriptorLayout->GetHandle());

        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            *m_Device, *m_Swapchain, *m_Renderer, *m_Pipeline, m_FrameArena);
    }

    void Engine::Run()
    {
        OnStart(); // User setup

        auto lastTime = std::chrono::high_resolution_clock::now();

        std::deque<float> frameTimes;
        const size_t FRAME_SAMPLES = 10;

        while (m_Running && !m_Window->ShouldClose())
        {
            m_FrameArena.Reset(); // Clear per-frame allocations

            m_Window->OnUpdate();

            if (m_FramebufferResized)
            {
                m_Renderer->OnResize();
                m_FramebufferResized = false;
            }

            auto currentTime = std::chrono::high_resolution_clock::now();
            float rawDt = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            frameTimes.push_back(rawDt);
            if (frameTimes.size() > FRAME_SAMPLES) frameTimes.pop_front();

            float dt = 0.0f;
            for (float t : frameTimes) dt += t;
            dt /= frameTimes.size();

            // Cap dt to prevent spiral of death during lag spikes
            if (dt > 0.1f) dt = 0.1f;

            OnUpdate(dt); // User Logic

            // Currently RenderSystem::OnUpdate handles the draw, so OnRender is optional hook
            // In future, OnRender might manipulate the RenderGraph
            OnRender();
        }

        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    }
}
