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
import Core.Filesystem;
import Core.Profiling;
import Runtime.RHI.Shader;
import Runtime.RHI.Texture;
import Runtime.Graphics.ModelLoader;
import Runtime.Graphics.TextureLoader;
import Runtime.Graphics.Material;
import Runtime.ECS.Components;
import Runtime.RHI.Types;
import Runtime.Interface.GUI;

namespace Runtime
{
    Engine::Engine(const EngineConfig& config) : m_FrameArena(config.FrameArenaSize)
    {
        Core::Tasks::Scheduler::Initialize();
        Core::Filesystem::FileWatcher::Initialize();

        Core::Log::Info("Initializing Engine...");

        // 1. Window
        Core::Windowing::WindowProps props{config.AppName, config.Width, config.Height};
        m_Window = std::make_unique<Core::Windowing::Window>(props);

        if (!m_Window->IsValid())
        {
            Core::Log::Error("FATAL: Window initialization failed");
            std::exit(-1);
        }

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
        m_Device = std::make_shared<RHI::VulkanDevice>(*m_Context, m_Surface);

        // 4. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(m_Device, *m_Window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(m_Device, *m_Swapchain);
        m_TransferManager = std::make_unique<RHI::TransferManager>(m_Device);
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
        Core::Tasks::Scheduler::Shutdown();
        Core::Filesystem::FileWatcher::Shutdown();

        Interface::GUI::Shutdown();

        m_Scene.GetRegistry().clear();
        m_AssetManager.Clear();
        m_DefaultTexture.reset();

        m_LoadedMaterials.clear();
        m_LoadedGeometries.clear();

        m_RenderSystem.reset();
        m_Pipeline.reset();
        m_BindlessSystem.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_Renderer.reset();
        m_Swapchain.reset();
        m_TransferManager.reset();
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
            auto textureLoader = [this](const std::string& pathStr, Core::Assets::AssetHandle handle)
                -> std::shared_ptr<RHI::Texture>
            {
                std::filesystem::path path(pathStr);
                auto result = Graphics::TextureLoader::LoadAsync(path, GetDevice(), *m_TransferManager);

                if (result)
                {
                    // 1. Notify AssetManager: "Don't set Ready yet, I'm processing"
                    m_AssetManager.MoveToProcessing(handle);

                    // 2. Notify Engine: "Wake up AssetManager when this token is done"
                    RegisterAssetLoad(handle, result->Token);

                    return result->Resource;
                }
                return nullptr;
            };

            auto texHandle = m_AssetManager.Load<RHI::Texture>(
                Core::Filesystem::GetAssetPath("textures/Parameterization.jpg"), textureLoader);

            auto defaultMat = std::make_shared<Graphics::Material>(
                GetDevice(), *m_BindlessSystem, // Pass bindless system
                texHandle, m_DefaultTexture, m_AssetManager
            );
            m_LoadedMaterials.push_back(defaultMat);

            // --- Spawn Entity ---
            std::string entityName = fsPath.stem().string();

            // Create a Parent Entity if multiple meshes, or just one if single
            entt::entity rootEntity = m_Scene.CreateEntity(entityName);

            // Setup Transform for Root
            auto& t = m_Scene.GetRegistry().get<ECS::Transform::Component>(rootEntity);
            t.Scale = glm::vec3(0.01f);

            // If it's a point cloud, we might want to scale it differently or center it?
            // For now, keep at origin.

            for (size_t i = 0; i < model->Size(); i++)
            {
                // Store in engine cache to keep alive (optional, shared_ptr handles this mostly via components)
                m_LoadedGeometries.push_back(model->Meshes[i]->GpuGeometry);

                entt::entity targetEntity = rootEntity;

                // If multi-mesh, create children (simple hierarchy simulation)
                if (model->Size() > 1)
                {
                    targetEntity = m_Scene.CreateEntity(entityName + "_" + std::to_string(i));
                    // TODO: Parenting system
                }

                auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRenderer::Component>(targetEntity);
                mr.GeometryRef = model->Meshes[i]->GpuGeometry;
                mr.MaterialRef = defaultMat;
            }

            Core::Log::Info("Successfully spawned: {}", entityName);
        }
        else
        {
            Core::Log::Warn("Unsupported file extension: {}", ext);
        }
    }

    void Engine::RegisterAssetLoad(Core::Assets::AssetHandle handle, RHI::TransferToken token)
    {
        std::lock_guard lock(m_LoadMutex);
        m_PendingLoads.push_back({handle, token});
    }

    void Engine::ProcessUploads()
    {
        // 1. Cleanup Staging Memory
        m_TransferManager->GarbageCollect();

        // 2. Check for completions
        std::lock_guard lock(m_LoadMutex);
        if (m_PendingLoads.empty()) return;

        // Use erase-remove idiom to process finished loads
        auto it = std::remove_if(m_PendingLoads.begin(), m_PendingLoads.end(),
                                 [&](const PendingLoad& load)
                                 {
                                     if (m_TransferManager->IsCompleted(load.Token))
                                     {
                                         // Signal Core that the "External Processing" is done
                                         m_AssetManager.FinalizeLoad(load.Handle);
                                         return true; // Remove from list
                                     }
                                     return false; // Keep waiting
                                 });

        m_PendingLoads.erase(it, m_PendingLoads.end());
    }

    void Engine::InitPipeline()
    {
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorPool>(m_Device);
        m_BindlessSystem = std::make_unique<RHI::BindlessDescriptorSystem>(m_Device);

        RHI::ShaderModule vert(m_Device, "shaders/triangle.vert.spv", RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(m_Device, "shaders/triangle.frag.spv", RHI::ShaderStage::Fragment);

        RHI::PipelineConfig config{&vert, &frag};
        std::vector<VkDescriptorSetLayout> layouts = {
            m_DescriptorLayout->GetHandle(),
            m_BindlessSystem->GetLayout()
        };

        m_Pipeline = std::make_unique<RHI::GraphicsPipeline>(m_Device, *m_Swapchain, config, layouts);

        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            m_Device,
            *m_Swapchain,
            *m_Renderer,
            *m_BindlessSystem, // <--- Passed
            *m_DescriptorPool, // <--- Passed
            *m_DescriptorLayout, // <--- Passed
            *m_Pipeline,
            m_FrameArena
        );
    }

    void Engine::Run()
    {
        Core::Profiling::ScopedTimer timer("Engine::Run");
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

            ProcessUploads();
            m_TransferManager->GarbageCollect();

            auto currentTime = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(currentTime - lastTime).count();
            lastTime = currentTime;

            OnUpdate(dt);
            OnRender();
        }

        Core::Tasks::Scheduler::WaitForAll();
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    }
}
