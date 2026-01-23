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

using namespace Core::Hash;

namespace Runtime
{
    entt::entity Engine::SpawnModel(Core::Assets::AssetHandle modelHandle,
                                    Core::Assets::AssetHandle materialHandle,
                                    glm::vec3 position,
                                    glm::vec3 scale)
    {
        // 1. Resolve Model
        auto modelResult = m_AssetManager.Get<Graphics::Model>(modelHandle);
        if (!modelResult)
        {
            Core::Log::Error("Cannot spawn model: Asset not ready or invalid.");
            return entt::null;
        }
        const auto& model = *modelResult;

        // 2. Create Root
        // We use the Asset Name as the entity name base
        std::string name = "Model";
        // (Optional: fetch name from AssetManager metadata if available)

        entt::entity root = m_Scene.CreateEntity(name);
        auto& t = m_Scene.GetRegistry().get<ECS::Components::Transform::Component>(root);
        t.Position = position;
        t.Scale = scale;

        // 3. Create Submeshes
        for (size_t i = 0; i < model->Meshes.size(); i++)
        {
            entt::entity targetEntity = root;

            // If complex model, create children. If single mesh, put it on root.
            if (model->Meshes.size() > 1)
            {
                targetEntity = m_Scene.CreateEntity(model->Meshes[i]->Name);
                ECS::Components::Hierarchy::Attach(m_Scene.GetRegistry(), targetEntity, root);
            }

            // Add Renderer
            auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRenderer::Component>(targetEntity);
            mr.Geometry = model->Meshes[i]->Handle;
            mr.Material = materialHandle;

            // Add Collider
            if (model->Meshes[i]->CollisionGeometry)
            {
                auto& col = m_Scene.GetRegistry().emplace<ECS::MeshCollider::Component>(targetEntity);
                col.CollisionRef = model->Meshes[i]->CollisionGeometry;
                col.WorldOBB.Center = col.CollisionRef->LocalAABB.GetCenter();
            }

            // Add Selectable Tag (THE CRITICAL FIX)
            m_Scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(targetEntity);
        }

        return root;
    }

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

        // Bindless + TextureSystem
        m_BindlessSystem = std::make_unique<RHI::BindlessDescriptorSystem>(*m_Device);
        m_TextureSystem = std::make_unique<RHI::TextureSystem>(*m_Device, *m_BindlessSystem);
        m_MaterialSystem = std::make_unique<Graphics::MaterialSystem>(*m_TextureSystem, m_AssetManager);
        // Create and register an engine-owned default 1x1 white texture.
        // Keep it alive for the lifetime of the engine so slots never go stale.
        {
            std::vector<uint8_t> whitePixel = {255, 255, 255, 255};
            m_DefaultTexture = std::make_shared<RHI::Texture>(*m_TextureSystem, *m_Device, whitePixel, 1, 1);
            m_DefaultTextureIndex = m_DefaultTexture->GetBindlessIndex();
        }

        // Initialize GeometryStorage with frames-in-flight for safe deferred deletion
        m_GeometryStorage.Initialize(m_Device->GetFramesInFlight());

        // 4. Swapchain & Renderer
        m_Swapchain = std::make_unique<RHI::VulkanSwapchain>(m_Device, *m_Window);
        m_Renderer = std::make_unique<RHI::SimpleRenderer>(m_Device, *m_Swapchain);
        m_TransferManager = std::make_unique<RHI::TransferManager>(*m_Device);
        Interface::GUI::Init(*m_Window, *m_Device, *m_Swapchain, m_Context->GetInstance(),
                             m_Device->GetGraphicsQueue());


        InitPipeline();
        Core::Log::Info("Engine: InitPipeline complete.");
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

        // Destroy GPU systems first while device is still alive.
        m_RenderSystem.reset();
        m_PipelineLibrary.reset();

        Interface::GUI::Shutdown();

        m_Scene.GetRegistry().clear();
        m_AssetManager.Clear();

        m_DefaultTexture.reset();
        m_LoadedMaterials.clear();

        if (m_MaterialSystem)
        {
            // Process any final deletions from the clear above
            m_MaterialSystem->ProcessDeletions(m_Device->GetGlobalFrameNumber());
            m_MaterialSystem.reset();
        }

        // Clear geometry storage before pipeline/device destruction
        m_GeometryStorage.Clear();

        // Critical: destroy any per-frame transient RHI objects (RenderGraph images, pass closures, etc.)
        // while the VulkanDevice is still alive.
        m_FrameScope.Reset();

        // Ensure any deferred texture pool deletions are processed and the pool is cleared
        // while the device is still valid and idle.
        if (m_TextureSystem)
        {
            m_TextureSystem->ProcessDeletions();
            m_TextureSystem->Clear();
        }

        m_BindlessSystem.reset();
        m_DescriptorPool.reset();
        m_DescriptorLayout.reset();
        m_Renderer.reset();
        m_Swapchain.reset();
        m_TransferManager.reset();

        // Destroy the texture system before the device.
        m_TextureSystem.reset();

        // Flush any remaining deferred SafeDestroy work now (must be done before VulkanDevice teardown).
        if (m_Device)
        {
            m_Device->FlushAllDeletionQueues();
        }

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
                    Core::Log::Error("Failed to load model: {} ({})", path,
                                     Graphics::AssetErrorToString(loadResult.error()));
                    return;
                }

                // 2. Register Token immediately (Thread-safe)
                // This ensures the Engine knows GPU work is pending before we even try to spawn.
                RegisterAssetLoad(Core::Assets::AssetHandle{}, loadResult->Token);

                // 3. Schedule Entity Spawning on Main Thread
                // We CANNOT touch m_Scene, m_AssetManager, or m_LoadedMaterials here.
                RunOnMainThread([this, model = std::move(loadResult->ModelData), path]() mutable
                {
                    // [Main Thread] Safe to touch Scene/Assets
                    std::filesystem::path fsPath(path);
                    std::string assetName = fsPath.filename().string();

                    // Transfer ownership of the model into the AssetManager (zero-copy).
                    // Keep a raw pointer for the rest of this scope (AssetManager now owns lifetime).
                    auto modelHandle = m_AssetManager.Create(assetName, std::move(model));

                    // --- Setup Material (Requires AssetManager) ---
                    // Define local loader lambda inside the main thread task
                    auto textureLoader = [this](const std::string& pathStr, Core::Assets::AssetHandle handle)
                        -> std::shared_ptr<RHI::Texture>
                    {
                        std::filesystem::path texPath(pathStr);
                        auto result = Graphics::TextureLoader::LoadAsync(
                            texPath, *GetDevice(), *m_TextureSystem);
                        if (result)
                        {
                            // This path currently creates a fully uploaded texture synchronously.
                            // Keep the AssetManager state machine consistent.
                            m_AssetManager.MoveToProcessing(handle);
                            RegisterAssetLoad(handle, result->Token);
                            return std::move(result->Texture);
                        }

                        Core::Log::Warn("Texture load failed: {} ({})", pathStr,
                                        Graphics::AssetErrorToString(result.error()));
                        return {};
                    };

                    // Load Default Texture
                    auto texHandle = m_AssetManager.Load<RHI::Texture>(
                        Core::Filesystem::GetAssetPath("textures/Parameterization.jpg"), textureLoader);

                    Graphics::MaterialData matData;
                    matData.AlbedoID = m_DefaultTextureIndex;
                    matData.RoughnessFactor = 0.5f;

                    // Create RAII Wrapper (Allocates in Pool)
                    auto defaultMat = std::make_unique<Graphics::Material>(*m_MaterialSystem, matData);

                    // Link Texture Asset (Setup Listener)
                    defaultMat->SetAlbedoTexture(texHandle);

                    auto defaultMaterialHandle = m_AssetManager.Create("DefaultMaterial", std::move(defaultMat));
                    m_LoadedMaterials.push_back(defaultMaterialHandle);

                    // --- Spawn Entities ---
                    SpawnModel(modelHandle, defaultMaterialHandle, glm::vec3(0.0f), glm::vec3(0.01f));

                    Core::Log::Info("Successfully spawned: {}", assetName);
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

    void Engine::ProcessMainThreadQueue()
    {
        std::vector<Core::Tasks::LocalTask> tasks;
        {
            std::lock_guard lock(m_MainThreadQueueMutex);
            if (m_MainThreadQueue.empty()) return;
            tasks.swap(m_MainThreadQueue);
        }

        for (auto& task : tasks)
        {
            task();
        }
    }

    void Engine::InitPipeline()
    {
        m_DescriptorLayout = std::make_unique<RHI::DescriptorLayout>(*m_Device);
        m_DescriptorPool = std::make_unique<RHI::DescriptorAllocator>(*m_Device);

        // ---------------------------------------------------------------------
        // Shader policy (data-driven)
        // ---------------------------------------------------------------------
        m_ShaderRegistry.Register("Forward.Vert"_id, "shaders/triangle.vert.spv");
        m_ShaderRegistry.Register("Forward.Frag"_id, "shaders/triangle.frag.spv");
        m_ShaderRegistry.Register("Picking.Vert"_id, "shaders/pick_id.vert.spv");
        m_ShaderRegistry.Register("Picking.Frag"_id, "shaders/pick_id.frag.spv");
        m_ShaderRegistry.Register("Debug.Vert"_id, "shaders/debug_view.vert.spv");
        m_ShaderRegistry.Register("Debug.Frag"_id, "shaders/debug_view.frag.spv");
        m_ShaderRegistry.Register("Debug.Comp"_id, "shaders/debug_view.comp.spv");

        // ---------------------------------------------------------------------
        // Pipeline library (owns PSOs)
        // ---------------------------------------------------------------------
        m_PipelineLibrary = std::make_unique<Graphics::PipelineLibrary>(m_Device, *m_BindlessSystem, *m_DescriptorLayout);
        m_PipelineLibrary->BuildDefaults(m_ShaderRegistry,
                                         m_Swapchain->GetImageFormat(),
                                         RHI::VulkanImage::FindDepthFormat(*m_Device));

        // ---------------------------------------------------------------------
        // RenderSystem (borrows PSOs via PipelineLibrary)
        // ---------------------------------------------------------------------
        Core::Log::Info("About to create RenderSystem...");
        Graphics::RenderSystemConfig rsConfig{};
        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            rsConfig,
            m_Device,
            *m_Swapchain,
            *m_Renderer,
            *m_BindlessSystem,
            *m_DescriptorPool,
            *m_DescriptorLayout,
            *m_PipelineLibrary,
            m_ShaderRegistry,
            m_FrameArena,
            m_FrameScope,
            m_GeometryStorage,
            *m_MaterialSystem
        );
        Core::Log::Info("RenderSystem created successfully.");

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

            // Reclaim pool slots for textures that became unreachable this frame.
            if (m_TextureSystem)
            {
                m_TextureSystem->ProcessDeletions();
            }

            if (m_MaterialSystem)
            {
                m_MaterialSystem->ProcessDeletions(m_Device->GetGlobalFrameNumber());
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

        // Final flush: destroy any RHI resources that were deferred via VulkanDevice::SafeDestroy().
        // This is required because SafeDestroy is keyed off frame indices; at shutdown we want deterministic teardown.
        m_Device->FlushAllDeletionQueues();
    }
}
