module;
#include <chrono>
#include <queue>
#include <mutex>
#include <filesystem>
#include <system_error> // for std::error_code
#include <algorithm> // for std::transform
#include <cctype>    // for std::tolower
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include "RHI.Vulkan.hpp"
#include "Core.Profiling.Macros.hpp"

module Runtime.Engine;

import Core;
import RHI;
import Graphics;
import ECS;
import Interface;

namespace Runtime
{
    Engine::Engine(const EngineConfig& config) :
        m_FrameArena(config.FrameArenaSize),
        m_FrameScope(config.FrameArenaSize)
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

        // Initialize GeometryStorage with frames-in-flight for safe deferred deletion
        m_GeometryStorage.Initialize(m_Device->GetFramesInFlight());

        // 4. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(m_Device, *m_Window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(m_Device, *m_Swapchain);
        m_TransferManager = std::make_unique<RHI::TransferManager>(m_Device);
        Interface::GUI::Init(*m_Window, *m_Device, *m_Swapchain, m_Context->GetInstance(),
                             m_Device->GetGraphicsQueue());

        InitPipeline();
        Core::Log::Info("Engine: InitPipeline complete, creating default texture...");
        std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
        m_DefaultTexture = std::make_shared<RHI::Texture>(m_Device, whitePixel, 1, 1);
        Core::Log::Info("Engine: Constructor complete.");
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

        m_RenderSystem.reset();

        // Clear geometry storage before pipeline/device destruction
        m_GeometryStorage.Clear();

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
            Core::Log::Info("Scheduling Async Load: {}", path);

            // 1. Offload heavy parsing to Worker Thread
            Core::Tasks::Scheduler::Dispatch([this, path]()
            {
                // [Worker Thread] Heavy OBJ/GLTF Parsing
                auto loadResult = Graphics::ModelLoader::LoadAsync(GetDevice(), *m_TransferManager, m_GeometryStorage,
                                                                   path);

                if (!loadResult)
                {
                    Core::Log::Error("Failed to load model: {}", path);
                    return;
                }

                // 2. Register Token immediately (Thread-safe)
                // This ensures the Engine knows GPU work is pending before we even try to spawn.
                RegisterAssetLoad(Core::Assets::AssetHandle{}, loadResult->Token);

                // 3. Schedule Entity Spawning on Main Thread
                // We CANNOT touch m_Scene, m_AssetManager, or m_LoadedMaterials here.
                RunOnMainThread([this, model = loadResult->ModelData, path]()
                {
                    // [Main Thread] Safe to touch Scene/Assets
                    std::filesystem::path fsPath(path);
                    std::string assetName = fsPath.filename().string();

                    // This increments the ref-count of the shared_ptr, preventing ~Model() from running
                    m_AssetManager.Create(assetName, model);

                    // --- Setup Material (Requires AssetManager) ---
                    // Define local loader lambda inside the main thread task
                    auto textureLoader = [this](const std::string& pathStr, Core::Assets::AssetHandle handle)
                        -> std::shared_ptr<RHI::Texture>
                    {
                        std::filesystem::path texPath(pathStr);
                        auto result = Graphics::TextureLoader::LoadAsync(texPath, GetDevice(), *m_TransferManager);
                        if (result)
                        {
                            m_AssetManager.MoveToProcessing(handle);
                            RegisterAssetLoad(handle, result->Token);
                            return result->Resource;
                        }
                        return nullptr;
                    };

                    // Load Default Texture
                    auto texHandle = m_AssetManager.Load<RHI::Texture>(
                        Core::Filesystem::GetAssetPath("textures/Parameterization.jpg"), textureLoader);

                    auto defaultMat = std::make_shared<Graphics::Material>(
                        GetDevice(), *m_BindlessSystem,
                        texHandle, m_DefaultTexture, m_AssetManager
                    );
                    m_LoadedMaterials.push_back(defaultMat);

                    auto defaultMaterialHandle = m_AssetManager.Create("DefaultMaterial", defaultMat);

                    // --- Spawn Entities ---
                    std::string entityName = fsPath.stem().string();
                    entt::entity rootEntity = m_Scene.CreateEntity(entityName);

                    auto& t = m_Scene.GetRegistry().get<ECS::Components::Transform::Component>(rootEntity);
                    t.Scale = glm::vec3(0.01f);

                    for (size_t i = 0; i < model->Size(); i++)
                    {
                        entt::entity targetEntity = rootEntity;
                        if (model->Size() > 1)
                        {
                            targetEntity = m_Scene.CreateEntity(entityName + "_" + std::to_string(i));
                             // NOTE: Parent-child relationships could be added here via a Hierarchy component
                            // when the ECS supports transform hierarchies (e.g., ECS::Hierarchy::Component).
                        }

                        auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRenderer::Component>(targetEntity);
                        mr.Geometry = model->Meshes[i]->Handle;
                        mr.Material = defaultMaterialHandle;

                        // Add Collider
                        if (model->Meshes[i]->CollisionGeometry)
                        {
                            auto& col = m_Scene.GetRegistry().emplace<ECS::MeshCollider::Component>(targetEntity);
                            col.CollisionRef = model->Meshes[i]->CollisionGeometry;
                            col.WorldOBB.Center = col.CollisionRef->LocalAABB.GetCenter(); // Init center
                        }
                    }
                    Core::Log::Info("Successfully spawned: {}", entityName);
                });
            });
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

    void Engine::RunOnMainThread(std::function<void()> task)
    {
        std::lock_guard lock(m_MainThreadQueueMutex);
        m_MainThreadQueue.emplace_back(std::move(task));
    }

    void Engine::ProcessMainThreadQueue()
    {
        std::vector<std::function<void()>> tasks;
        {
            std::lock_guard lock(m_MainThreadQueueMutex);
            if (m_MainThreadQueue.empty()) return;
            tasks.swap(m_MainThreadQueue);
        }

        for (const auto& task : tasks)
        {
            task();
        }
    }

    void Engine::InitPipeline()
    {
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(m_Device);
        m_BindlessSystem = std::make_shared<RHI::BindlessDescriptorSystem>(m_Device);

        // Resolve shader paths robustly. Depending on how the app is launched, the CWD may be
        // the repo root, the build dir, or bin/. Prefer the common dev layout: <bin>/shaders/*.spv.

        RHI::ShaderModule vert(m_Device, Core::Filesystem::GetShaderPath("shaders/triangle.vert.spv"),
                               RHI::ShaderStage::Vertex);
        RHI::ShaderModule frag(m_Device, Core::Filesystem::GetShaderPath("shaders/triangle.frag.spv"),
                               RHI::ShaderStage::Fragment);

        // --- NEW: Using Builder ---
        RHI::PipelineBuilder builder(m_Device);

        // 1. Shaders
        builder.SetShaders(&vert, &frag);

        // 2. Vertex Layout (Previously hardcoded inside RHI::Types)
        builder.SetInputLayout(RHI::StandardLayoutFactory::Get());

        // 3. Render Targets (Dynamic Rendering)
        builder.SetColorFormats({m_Swapchain->GetImageFormat()});
        builder.SetDepthFormat(RHI::VulkanImage::FindDepthFormat(*m_Device));

        // 4. Layouts
        builder.AddDescriptorSetLayout(m_DescriptorLayout->GetHandle());
        builder.AddDescriptorSetLayout(m_BindlessSystem->GetLayout());

        // 5. Push Constants
        VkPushConstantRange pushConstant{};
        pushConstant.offset = 0;
        pushConstant.size = sizeof(RHI::MeshPushConstants);
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        builder.AddPushConstantRange(pushConstant);

        // Build
        auto pipelineResult = builder.Build();
        if (pipelineResult)
        {
            m_Pipeline = std::move(*pipelineResult);
            Core::Log::Info("Pipeline built successfully using Builder.");

            Core::Log::Info("About to create RenderSystem...");
            m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
                m_Device,
                *m_Swapchain,
                *m_Renderer,
                *m_BindlessSystem,
                *m_DescriptorPool,
                *m_DescriptorLayout,
                *m_Pipeline,
                m_FrameArena,
                m_FrameScope,
                m_GeometryStorage
            );
            Core::Log::Info("RenderSystem created successfully.");
        }
        else
        {
            Core::Log::Error("Failed to build pipeline: {}", (int)pipelineResult.error());
            std::exit(1);
        }

        if (!m_RenderSystem)
        {
            Core::Log::Error("Failed to create RenderSystem!");
            std::exit(1);
        }
    }

    void Engine::Run()
    {
        Core::Log::Info("Engine::Run starting...");
        OnStart();

        using Clock = std::chrono::high_resolution_clock;
        auto lastTime = Clock::now();
        double accumulator = 0.0;
        const double dt = 1.0 / 60.0; // Fixed 60hz physics

        while (m_Running && !m_Window->ShouldClose())
        {
            // Begin frame telemetry
            Core::Telemetry::TelemetrySystem::Get().BeginFrame();

            auto currentTime = Clock::now();
            double frameTime = std::chrono::duration<double>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Prevent spiral of death
            if (frameTime > 0.25) frameTime = 0.25;

            accumulator += frameTime;

            {
                PROFILE_SCOPE("FrameArena::Reset");
                m_FrameScope.Reset();
                m_FrameArena.Reset();
            }

            {
                PROFILE_SCOPE("Window::OnUpdate");
                m_Window->OnUpdate();
            }

            ProcessMainThreadQueue();

            {
                PROFILE_SCOPE("ProcessUploads");
                ProcessUploads();
            }

            {
                PROFILE_SCOPE("OnUpdate");
                // Update with variable dt for responsive input/camera
                OnUpdate(static_cast<float>(frameTime));
            }

            // Fixed timestep for physics simulation
            {
                PROFILE_SCOPE("PhysicsUpdate");
                while (accumulator >= dt)
                {
                    ECS::Systems::Transform::OnUpdate(m_Scene.GetRegistry());
                    accumulator -= dt;
                }
            }

            if (m_FramebufferResized)
            {
                m_Renderer->OnResize();
                if (m_RenderSystem)
                {
                    m_RenderSystem->OnResize();
                }
                m_FramebufferResized = false;
            }

            m_TransferManager->GarbageCollect();

            {
                PROFILE_SCOPE("OnRender");
                OnRender();
            }

            // End frame telemetry
            Core::Telemetry::TelemetrySystem::Get().EndFrame();
        }

        Core::Tasks::Scheduler::WaitForAll();
        vkDeviceWaitIdle(m_Device->GetLogicalDevice());
    }
}
