module;
#include <chrono>
#include <queue>
#include <filesystem>
#include <system_error> // for std::error_code
#include <algorithm> // for std::transform
#include <cctype>    // for std::tolower
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <entt/fwd.hpp>
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
    Engine::Engine(const EngineConfig& config) : m_FrameArena(config.FrameArenaSize)
    {
        Core::Tasks::Scheduler::Initialize();

        Core::Log::Info("Initializing Engine...");

        // 1. Window
        Core::Windowing::WindowProps props{config.AppName, config.Width, config.Height};
        m_Window = std::make_unique<Core::Windowing::Window>(props);

        if (!m_Window->IsValid())
        {
            Core::Log::Error("FATAL: Window initialization failed");
            std::exit(-1);
        }

        Core::Input::Initialize(m_Window->GetNativeHandle());
        m_Window->SetEventCallback([this](const Core::Windowing::Event& e)
        {
            std::visit([this](auto&& event)
            {
                using T = std::decay_t<decltype(event)>;

                if constexpr (std::is_same_v<T, Core::Windowing::WindowCloseEvent>)
                {
                    m_Running = false;
                }
                else if constexpr (std::is_same_v<T, Core::Windowing::WindowResizeEvent>)
                {
                    m_FramebufferResized = true;
                }
                else if constexpr (std::is_same_v<T, Core::Windowing::KeyEvent>)
                {
                    if (Interface::GUI::WantCaptureKeyboard()) return;
                    if (event.IsPressed && event.KeyCode == 256) m_Running = false;
                }
                else if constexpr (std::is_same_v<T, Core::Windowing::WindowDropEvent>)
                {
                    for (const auto& path : event.Paths)
                    {
                        LoadDroppedAsset(path);
                    }
                }
            }, e);
        });

        // 2. Vulkan Context & Surface
#ifdef NDEBUG
        bool enableValidation = false;
#else
        bool enableValidation = true;
#endif
        RHI::ContextConfig ctxConfig{config.AppName, enableValidation};
        m_Context = std::make_unique<RHI::VulkanContext>(ctxConfig);

        if (!m_Window->CreateSurface(m_Context->GetInstance(), nullptr, &m_Surface))
        {
            Core::Log::Error("FATAL: Failed to create Vulkan Surface");
            std::exit(-1); // Hard fail without exceptions
        }

        // 3. Device
        m_Device = std::make_unique<RHI::VulkanDevice>(*m_Context, m_Surface);

        // 4. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(m_Device, *m_Window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(m_Device, *m_Swapchain);

        Interface::GUI::Init(*m_Window, *m_Device, *m_Swapchain, m_Context->GetInstance(),
                             m_Device->GetGraphicsQueue());

        InitPipeline();
        std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
        m_DefaultTexture = std::make_shared<RHI::Texture>(m_Device, whitePixel, 1, 1);
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
        m_DefaultTexture.reset();

        m_LoadedMaterials.clear();
        m_LoadedGeometries.clear();

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
            Core::Log::Warn("Assets directory not found or inaccessible");
        }

        auto relativePath = canonical.lexically_relative(assetDir);
        if (relativePath.empty() || relativePath.native().starts_with(".."))
        {
            Core::Log::Warn("Dropped file is outside of assets directory: {}", path);
        }

        std::string ext = fsPath.extension().string();

        // Convert to lower case for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        bool isModel = (ext == ".gltf" || ext == ".glb" ||
            ext == ".obj" || ext == ".ply" ||
            ext == ".xyz" || ext == ".pcd" ||
            ext == ".tgf");

        if (isModel)
        {
            Core::Log::Info("Loading Model: {}", path);

            // TODO: Move to Async Task
            auto model = Graphics::ModelLoader::Load(GetDevice(), path);

            if (!model || !model->IsValid())
            {
                Core::Log::Error("Failed to load model: {}", path);
                return;
            }

            // --- Setup Default Material ---
            auto textureLoader = [&](const std::string& p) { return std::make_shared<RHI::Texture>(GetDevice(), p); };
            auto texHandle = m_AssetManager.Load<RHI::Texture>("assets/textures/Parameterization.jpg", textureLoader);

            auto defaultMat = std::make_shared<Graphics::Material>(
                GetDevice(), GetDescriptorPool(), GetDescriptorLayout(),
                texHandle, m_DefaultTexture, m_AssetManager
            );
            defaultMat->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(RHI::CameraBufferObject));
            m_LoadedMaterials.push_back(defaultMat);

            // --- Spawn Entity ---
            std::string entityName = fsPath.stem().string();

            // Create a Parent Entity if multiple meshes, or just one if single
            entt::entity rootEntity = m_Scene.CreateEntity(entityName);

            // Setup Transform for Root
            auto& t = m_Scene.GetRegistry().get<ECS::TransformComponent>(rootEntity);
            t.Scale = glm::vec3(1.0f);

            // If it's a point cloud, we might want to scale it differently or center it?
            // For now, keep at origin.

            for (size_t i = 0; i < model->Size(); i++)
            {
                // Store in engine cache to keep alive (optional, shared_ptr handles this mostly via components)
                m_LoadedGeometries.push_back(model->Meshes[i]);

                entt::entity targetEntity = rootEntity;

                // If multi-mesh, create children (simple hierarchy simulation)
                if (model->Size() > 1)
                {
                    targetEntity = m_Scene.CreateEntity(entityName + "_" + std::to_string(i));
                    // TODO: Parenting system
                }

                auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(targetEntity);
                mr.GeometryRef = model->Meshes[i]; // <--- USING NEW FIELD
                mr.MaterialRef = defaultMat;
            }

            Core::Log::Info("Successfully spawned: {}", entityName);
        }
        else
        {
            Core::Log::Warn("Unsupported file extension: {}", ext);
        }
    }

    void Engine::InitPipeline()
    {
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorPool>(m_Device);

        RHI::ShaderModule vert(m_Device, "shaders/triangle.vert.spv", RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(m_Device, "shaders/triangle.frag.spv", RHI::ShaderStage::Fragment);

        RHI::PipelineConfig config{&vert, &frag};
        m_Pipeline = std::make_unique<RHI::GraphicsPipeline>(m_Device, *m_Swapchain, config,
                                                             m_DescriptorLayout->GetHandle());

        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            m_Device, *m_Swapchain, *m_Renderer, *m_Pipeline, m_FrameArena);
    }

    void Engine::Run()
    {
        OnStart();
        auto lastTime = std::chrono::high_resolution_clock::now();

        while (m_Running && !m_Window->ShouldClose())
        {
            m_FrameArena.Reset();
            m_Window->OnUpdate();
            if (m_FramebufferResized)
            {
                m_Renderer->OnResize();
                m_FramebufferResized = false;
            }

            auto currentTime = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            OnUpdate(dt);
            OnRender();
        }

        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    }
}
