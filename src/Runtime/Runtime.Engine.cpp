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
#include <cstring>

module Runtime.Engine;

import Core;
import RHI;
import Graphics;
import ECS;
import Interface;
import Runtime.GraphicsBackend;

using namespace Core::Hash;

namespace
{
    // File-static pointer for the EnTT on_destroy hook.
    // Safe because there is exactly one Engine instance per process.
    Graphics::GPUScene* g_GpuSceneForDestroyHook = nullptr;

    void OnMeshRendererDestroyed(entt::registry& registry, entt::entity entity)
    {
        if (!g_GpuSceneForDestroyHook)
            return;

        auto& mr = registry.get<ECS::MeshRenderer::Component>(entity);
        if (mr.GpuSlot == ECS::MeshRenderer::Component::kInvalidSlot)
            return;

        // Deactivate slot (radius = 0 => culler skips it) and free.
        Graphics::GpuInstanceData inst{};
        g_GpuSceneForDestroyHook->QueueUpdate(mr.GpuSlot, inst, /*sphere*/ {0.0f, 0.0f, 0.0f, 0.0f});
        g_GpuSceneForDestroyHook->FreeSlot(mr.GpuSlot);
        mr.GpuSlot = ECS::MeshRenderer::Component::kInvalidSlot;
    }
}

namespace Runtime
{
    Engine::Engine(const EngineConfig& config) :
        m_FrameArena(config.FrameArenaSize),
        m_FrameScope(config.FrameArenaSize),
        m_FrameGraph(m_FrameScope)
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

        // 2. GraphicsBackend (Vulkan context, device, swapchain, descriptors, transfer, textures)
#ifdef NDEBUG
        bool enableValidation = false;
#else
        bool enableValidation = true;
#endif
        GraphicsBackendConfig gfxConfig{config.AppName, enableValidation};
        m_GraphicsBackend = std::make_unique<GraphicsBackend>(*m_Window, gfxConfig);

        // 3. MaterialSystem (depends on TextureSystem from GraphicsBackend + AssetManager)
        m_MaterialSystem = std::make_unique<Graphics::MaterialSystem>(
            m_GraphicsBackend->GetTextureSystem(), m_AssetManager);

        // 4. Initialize GeometryStorage with frames-in-flight for safe deferred deletion
        m_GeometryStorage.Initialize(m_GraphicsBackend->GetDevice()->GetFramesInFlight());

        // 5. ImGui
        Interface::GUI::Init(*m_Window,
                             *m_GraphicsBackend->GetDevice(),
                             m_GraphicsBackend->GetSwapchain(),
                             m_GraphicsBackend->GetContext().GetInstance(),
                             m_GraphicsBackend->GetDevice()->GetGraphicsQueue());

        // 6. Pipelines & RenderSystem
        InitPipeline();

        // 7. Retained-mode GPUScene: Engine owns it so SpawnModel/ECS can queue updates.
        if (m_PipelineLibrary && m_GraphicsBackend->GetDevice())
        {
            if (auto* p = m_PipelineLibrary->GetSceneUpdatePipeline(); p && m_PipelineLibrary->GetSceneUpdateSetLayout() != VK_NULL_HANDLE)
            {
                m_GpuScene = std::make_unique<Graphics::GPUScene>(*m_GraphicsBackend->GetDevice(),
                                                                 *p,
                                                                 m_PipelineLibrary->GetSceneUpdateSetLayout());

                // Register EnTT on_destroy hook for immediate GPU slot reclaim.
                g_GpuSceneForDestroyHook = m_GpuScene.get();
                m_Scene.GetRegistry().on_destroy<ECS::MeshRenderer::Component>()
                    .connect<&OnMeshRendererDestroyed>();

                if (m_RenderSystem)
                    m_RenderSystem->SetGpuScene(m_GpuScene.get());
            }
        }

        Core::Log::Info("Engine: InitPipeline complete.");
        Core::Log::Info("Engine: Constructor complete.");
    }

    Engine::~Engine()
    {
        m_GraphicsBackend->WaitIdle();

        // Disconnect entity destruction hooks before tearing down GPU systems.
        m_Scene.GetRegistry().on_destroy<ECS::MeshRenderer::Component>()
            .disconnect<&OnMeshRendererDestroyed>();
        g_GpuSceneForDestroyHook = nullptr;

        // Order matters!
        Core::Tasks::Scheduler::Shutdown();
        Core::Filesystem::FileWatcher::Shutdown();

        // Destroy GPU systems first while device is still alive.
        m_GpuScene.reset();
        m_RenderSystem.reset();
        m_PipelineLibrary.reset();

        Interface::GUI::Shutdown();

        m_Scene.GetRegistry().clear();
        m_AssetManager.Clear();

        m_LoadedMaterials.clear();

        if (m_MaterialSystem)
        {
            m_MaterialSystem->ProcessDeletions(m_GraphicsBackend->GetDevice()->GetGlobalFrameNumber());
            m_MaterialSystem.reset();
        }

        // Clear geometry storage before device destruction
        m_GeometryStorage.Clear();

        // Destroy per-frame transient RHI objects while VulkanDevice is still alive.
        m_FrameScope.Reset();

        // GraphicsBackend destructor handles: texture system clear, descriptors,
        // renderer, swapchain, transfer, device, surface, context.
        m_GraphicsBackend.reset();

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
                auto loadResult = Graphics::ModelLoader::LoadAsync(
                    GetDevice(), m_GraphicsBackend->GetTransferManager(),
                    m_GeometryStorage, path);

                if (!loadResult)
                {
                    Core::Log::Error("Failed to load model: {} ({})", path,
                                     Graphics::AssetErrorToString(loadResult.error()));
                    return;
                }

                // 2. Register Token immediately (Thread-safe)
                RegisterAssetLoad(Core::Assets::AssetHandle{}, loadResult->Token);

                // 3. Schedule Entity Spawning on Main Thread
                RunOnMainThread([this, model = std::move(loadResult->ModelData), path]() mutable
                {
                    // [Main Thread] Safe to touch Scene/Assets
                    std::filesystem::path fsPath(path);
                    std::string baseName = fsPath.filename().string();

                    static uint64_t s_AssetLoadCounter = 0;
                    std::string assetName = baseName + "::" + std::to_string(++s_AssetLoadCounter);

                    auto modelHandle = m_AssetManager.Create(assetName, std::move(model));

                    Graphics::MaterialData matData;
                    matData.AlbedoID = m_GraphicsBackend->GetDefaultTextureIndex();
                    matData.RoughnessFactor = 0.5f;

                    auto defaultMat = std::make_unique<Graphics::Material>(*m_MaterialSystem, matData);

                    const std::string materialName = assetName + "::DefaultMaterial";
                    auto defaultMaterialHandle = m_AssetManager.Create(materialName, std::move(defaultMat));
                    m_LoadedMaterials.push_back(defaultMaterialHandle);

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
        m_PendingLoads.push_back({handle, token, {}});
    }

    void Engine::ProcessUploads()
    {
        // 1. Cleanup Staging Memory
        m_GraphicsBackend->GarbageCollectTransfers();

        // 2. Check for completions
        std::lock_guard lock(m_LoadMutex);
        if (m_PendingLoads.empty()) return;

        auto& transferMgr = m_GraphicsBackend->GetTransferManager();

        // Use erase-remove idiom to process finished loads
        auto it = std::remove_if(m_PendingLoads.begin(), m_PendingLoads.end(),
                                 [&](PendingLoad& load)
                                 {
                                     if (transferMgr.IsCompleted(load.Token))
                                     {
                                         if (load.OnComplete.Valid()) load.OnComplete();

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
        // Shader policy (data-driven)
        m_ShaderRegistry.Register("Forward.Vert"_id, "shaders/triangle.vert.spv");
        m_ShaderRegistry.Register("Forward.Frag"_id, "shaders/triangle.frag.spv");
        m_ShaderRegistry.Register("Picking.Vert"_id, "shaders/pick_id.vert.spv");
        m_ShaderRegistry.Register("Picking.Frag"_id, "shaders/pick_id.frag.spv");
        m_ShaderRegistry.Register("Debug.Vert"_id, "shaders/debug_view.vert.spv");
        m_ShaderRegistry.Register("Debug.Frag"_id, "shaders/debug_view.frag.spv");
        m_ShaderRegistry.Register("Debug.Comp"_id, "shaders/debug_view.comp.spv");

        // Stage 3 compute
        m_ShaderRegistry.Register("Cull.Comp"_id, "shaders/instance_cull_multigeo.comp.spv");

        // GPUScene scatter update
        m_ShaderRegistry.Register("SceneUpdate.Comp"_id, "shaders/scene_update.comp.spv");

        // Pipeline library (owns PSOs)
        m_PipelineLibrary = std::make_unique<Graphics::PipelineLibrary>(
            m_GraphicsBackend->GetDevice(),
            m_GraphicsBackend->GetBindlessSystem(),
            m_GraphicsBackend->GetDescriptorLayout());
        m_PipelineLibrary->BuildDefaults(m_ShaderRegistry,
                                         m_GraphicsBackend->GetSwapchain().GetImageFormat(),
                                         RHI::VulkanImage::FindDepthFormat(*m_GraphicsBackend->GetDevice()));

        // RenderSystem (borrows PSOs via PipelineLibrary)
        Core::Log::Info("About to create RenderSystem...");
        Graphics::RenderSystemConfig rsConfig{};
        m_RenderSystem = std::make_unique<Graphics::RenderSystem>(
            rsConfig,
            m_GraphicsBackend->GetDevice(),
            m_GraphicsBackend->GetSwapchain(),
            m_GraphicsBackend->GetRenderer(),
            m_GraphicsBackend->GetBindlessSystem(),
            m_GraphicsBackend->GetDescriptorPool(),
            m_GraphicsBackend->GetDescriptorLayout(),
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

        // Default Render Pipeline (hot-swappable)
        m_RenderSystem->RequestPipelineSwap(std::make_unique<Graphics::DefaultPipeline>());
    }

    entt::entity Engine::SpawnModel(Core::Assets::AssetHandle modelHandle,
                                    Core::Assets::AssetHandle materialHandle,
                                    glm::vec3 position,
                                    glm::vec3 scale)
    {
        // 1. Resolve Model
        if (auto* model = m_AssetManager.TryGet<Graphics::Model>(modelHandle))
        {
            // 2. Create Root
            std::string name = "Model";

            entt::entity root = m_Scene.CreateEntity(name);
            auto& t = m_Scene.GetRegistry().get<ECS::Components::Transform::Component>(root);
            t.Position = position;
            t.Scale = scale;

            // Assign stable pick IDs (monotonic, never reused during runtime).
            static uint32_t s_NextPickId = 1u;
            if (!m_Scene.GetRegistry().all_of<ECS::Components::Selection::PickID>(root))
            {
                m_Scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(root, s_NextPickId++);
            }

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

                // Add Selectable Tag
                m_Scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(targetEntity);

                // Stable pick ID for each selectable entity.
                if (!m_Scene.GetRegistry().all_of<ECS::Components::Selection::PickID>(targetEntity))
                {
                    m_Scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(targetEntity, s_NextPickId++);
                }
            }

            return root;
        }

        Core::Log::Error("Cannot spawn model: Asset not ready or invalid.");
        return entt::null;
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
            m_GraphicsBackend->ProcessTextureDeletions();

            if (m_MaterialSystem)
            {
                m_MaterialSystem->ProcessDeletions(m_GraphicsBackend->GetDevice()->GetGlobalFrameNumber());
            }

            {
                PROFILE_SCOPE("OnUpdate");
                // Update with variable dt for responsive input/camera
                OnUpdate(static_cast<float>(frameTime));
            }

            // --- FrameGraph: register, compile, and execute all systems ---
            {
                PROFILE_SCOPE("FrameGraph");
                m_FrameGraph.Reset();

                auto& registry = m_Scene.GetRegistry();
                const float frameDt = static_cast<float>(frameTime);

                // Client/gameplay systems first: they produce dirty state
                OnRegisterSystems(m_FrameGraph, frameDt);

                // Core engine systems consume dirty state in pipeline order.
                ECS::Systems::Transform::RegisterSystem(m_FrameGraph, registry);

                if (m_GpuScene && m_MaterialSystem)
                {
                    Graphics::Systems::MeshRendererLifecycle::RegisterSystem(
                        m_FrameGraph, registry, *m_GpuScene, m_AssetManager,
                        *m_MaterialSystem, m_GeometryStorage,
                        m_GraphicsBackend->GetDefaultTextureIndex());

                    Graphics::Systems::GPUSceneSync::RegisterSystem(
                        m_FrameGraph, registry, *m_GpuScene, m_AssetManager,
                        *m_MaterialSystem, m_GraphicsBackend->GetDefaultTextureIndex());
                }

                auto compileResult = m_FrameGraph.Compile();
                if (compileResult)
                {
                    m_AssetManager.BeginReadPhase();
                    m_FrameGraph.Execute();
                    m_AssetManager.EndReadPhase();
                }
            }

            if (m_FramebufferResized)
            {
                m_GraphicsBackend->OnResize();
                if (m_RenderSystem)
                {
                    m_RenderSystem->OnResize();
                }
                m_FramebufferResized = false;
            }

            m_GraphicsBackend->GarbageCollectTransfers();

            {
                PROFILE_SCOPE("OnRender");
                OnRender();
            }

            // End frame telemetry
            Core::Telemetry::TelemetrySystem::Get().EndFrame();
        }

        Core::Tasks::Scheduler::WaitForAll();
        m_GraphicsBackend->WaitIdle();

        // Final flush: destroy any RHI resources that were deferred via VulkanDevice::SafeDestroy().
        m_GraphicsBackend->FlushDeletionQueues();
    }
}
