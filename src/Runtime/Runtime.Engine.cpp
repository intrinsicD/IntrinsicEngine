module;
#include <chrono>
#include <queue>
#include <filesystem>
#include <system_error> // CRITICAL FIX: for std::error_code
#include <algorithm> // for std::transform
#include <cctype>    // for std::tolower
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "RHI/RHI.Vulkan.hpp"

module Runtime.Engine;

import Core.Logging;
import Core.Input;
import Core.Window;
import Core.Memory;
import Core.Tasks;
import Runtime.RHI.Shader;
import Runtime.RHI.Texture;
import Runtime.Graphics.ModelLoader;
import Runtime.Graphics.Material;
import Runtime.ECS.Components;
import Runtime.RHI.Types;
import Runtime.Interface.GUI;

namespace Runtime
{
    Engine::Engine(const EngineConfig& config) : m_FrameArena(config.FrameArenaSize) // CRITICAL FIX: Use configured size
    {
        Core::Tasks::Scheduler::Initialize();

        Core::Log::Info("Initializing Engine...");

        // 1. Window
        Core::Windowing::WindowProps props{config.AppName, config.Width, config.Height};
        m_Window = std::make_unique<Core::Windowing::Window>(props);

        // CRITICAL FIX: Check if window initialization succeeded
        if (!m_Window->IsValid())
        {
            Core::Log::Error("FATAL: Window initialization failed");
            std::exit(-1);
        }

        Core::Input::Initialize(m_Window->GetNativeHandle());
        m_Window->SetEventCallback([this](const Core::Windowing::Event& e)
        {
            if (e.Type == Core::Windowing::EventType::KeyPressed ||
                e.Type == Core::Windowing::EventType::KeyReleased)
            {
                if (Interface::GUI::WantCaptureKeyboard()) return; // STOP here
            }
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

        Interface::GUI::Init(
            *m_Window,
            *m_Device,
            *m_Swapchain,
            m_Context->GetInstance(),
            m_Device->GetGraphicsQueue()
        );

        InitPipeline();
    }

    Engine::~Engine()
    {
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device->GetLogicalDevice());
        }

        // Order matters!
        Interface::GUI::Shutdown();
        Core::Tasks::Scheduler::Shutdown();
        m_Scene.GetRegistry().clear();
        m_AssetManager.Clear();
        m_RenderSystem.reset();
        m_Pipeline.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_Renderer.reset();
        m_Swapchain.reset();
        m_Device.reset();

        if (m_Context)
        {
            vkDestroySurfaceKHR(m_Context->GetInstance(), m_Surface, nullptr);
        }
        m_Context.reset();
        m_Window.reset();
    }

    void Engine::LoadDroppedAsset(const std::string& path)
    {
        // CRITICAL FIX: Use error_code to avoid exceptions (build has -fno-exceptions)
        std::error_code ec;
        std::filesystem::path fsPath(path);

        // Check if file exists before canonicalizing
        if (!std::filesystem::exists(fsPath, ec) || ec)
        {
            Core::Log::Error("Dropped file does not exist: {}", path);
            return;
        }

        std::filesystem::path canonical = std::filesystem::canonical(fsPath, ec);
        if (ec)
        {
            Core::Log::Error("Failed to resolve canonical path for: {}", path);
            return;
        }

        std::filesystem::path assetDir = std::filesystem::canonical("assets/", ec);
        if (ec)
        {
            Core::Log::Error("Assets directory not found or inaccessible");
            return;
        }

        // CRITICAL FIX: Use lexically_relative for safer path containment check
        auto relativePath = canonical.lexically_relative(assetDir);
        if (relativePath.empty() || relativePath.native().starts_with(".."))
        {
            Core::Log::Error("Dropped file is outside of assets directory: {}", path);
            return;
        }

        std::string ext = fsPath.extension().string();

        // Convert to lower case for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (ext == ".gltf" || ext == ".glb")
        {
            Core::Log::Info("Drop Detected: Loading Model {}", path);

            // TODO: Move this to Async Task Graph to prevent frame freeze
            auto model = Graphics::ModelLoader::Load(GetDevice(), path);

            if (model->Meshes.empty())
            {
                Core::Log::Error("Failed to load model or model was empty.");
                return;
            }

            // --- MATERIAL & ASSET LOGIC START ---

            // 1. Define Texture Loader
            // This lambda creates the actual RHI::Texture when the AssetTask runs.
            auto textureLoader = [&](const std::string& path)
            {
                return std::make_shared<RHI::Texture>(GetDevice(), path);
            };

            // 2. Load the Fallback Texture (Parameterization.png)
            // We use the AssetManager. If it's already loaded, we get the handle immediately.
            auto texHandle = m_AssetManager.Load<RHI::Texture>("assets/textures/Parameterization.png", textureLoader);

            // 3. Create the Default Material
            // We now pass the AssetHandle instead of a raw string path.
            auto defaultMat = std::make_shared<Graphics::Material>(
                GetDevice(), GetDescriptorPool(), GetDescriptorLayout(), texHandle
            );

            // 4. Request Descriptor Write
            // The material will wait until texHandle is 'Ready' inside RenderSystem::OnUpdate
            defaultMat->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(RHI::CameraBufferObject));

            m_LoadedMaterials.push_back(defaultMat);

            // --- MATERIAL & ASSET LOGIC END ---

            // Create Entity
            auto entity = m_Scene.CreateEntity(fsPath.stem().string());

            auto& t = m_Scene.GetRegistry().get<ECS::TransformComponent>(entity);
            t.Scale = glm::vec3(1.0f);
            t.Position = glm::vec3(0.0f, 0.0f, 0.0f); // Reset position

            // If we loaded multiple meshes (submeshes), we might need multiple entities
            // For simplicity here, we attach the first mesh to the entity.
            // A better approach is to create children entities for each mesh.

            for (size_t i = 0; i < model->Size(); i++)
            {
                if (i == 0)
                {
                    auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(entity);
                    mr.MeshRef = model->Meshes[i];
                    mr.MaterialRef = defaultMat;
                }
                else
                {
                    // Create child entities for submeshes
                    auto child = m_Scene.CreateEntity(fsPath.stem().string() + "_" + std::to_string(i));
                    //auto& ct = m_Scene.GetRegistry().get<ECS::TransformComponent>(child);
                    // In a real loader, we would apply local transforms from GLTF nodes here

                    auto& cmr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(child);
                    cmr.MeshRef = model->Meshes[i];
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
