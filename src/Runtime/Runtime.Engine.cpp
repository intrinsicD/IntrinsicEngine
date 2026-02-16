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

import Core.Logging;
import Core.Tasks;
import Core.Window;
import Core.Filesystem;
import Core.Assets;
import Core.Telemetry;
import Core.FrameGraph;
import Core.Hash;
import Core.Profiling;
import Core.FeatureRegistry;
import Core.IOBackend;
import RHI;
import Graphics;
import ECS;
import Interface;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.SceneManager;
import Runtime.RenderOrchestrator;

using namespace Core::Hash;

namespace Runtime
{
    Engine::Engine(const EngineConfig& config)
    {
        Core::Tasks::Scheduler::Initialize();
        Core::Filesystem::FileWatcher::Initialize();

        Core::Log::Info("Initializing Engine...");

        // 1. SceneManager (ECS scene, entity lifecycle, GPU-reclaim hooks)
        m_SceneManager = std::make_unique<SceneManager>();

        // 2. Window
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

        // 3. GraphicsBackend (Vulkan context, device, swapchain, descriptors, transfer, textures)
#ifdef NDEBUG
        bool enableValidation = false;
#else
        bool enableValidation = true;
#endif
        GraphicsBackendConfig gfxConfig{config.AppName, enableValidation};
        m_GraphicsBackend = std::make_unique<GraphicsBackend>(*m_Window, gfxConfig);

        // 4. AssetPipeline (AssetManager, pending transfers, main-thread queue)
        m_AssetPipeline = std::make_unique<AssetPipeline>(m_GraphicsBackend->GetTransferManager());

        // 5. ImGui
        Interface::GUI::Init(*m_Window,
                             *m_GraphicsBackend->GetDevice(),
                             m_GraphicsBackend->GetSwapchain(),
                             m_GraphicsBackend->GetContext().GetInstance(),
                             m_GraphicsBackend->GetDevice()->GetGraphicsQueue());

        // 6. RenderOrchestrator (MaterialSystem, GeometryStorage, Pipelines, RenderSystem, GPUScene, FrameGraph)
        m_RenderOrchestrator = std::make_unique<RenderOrchestrator>(
            m_GraphicsBackend->GetDevice(),
            m_GraphicsBackend->GetSwapchain(),
            m_GraphicsBackend->GetRenderer(),
            m_GraphicsBackend->GetBindlessSystem(),
            m_GraphicsBackend->GetDescriptorPool(),
            m_GraphicsBackend->GetDescriptorLayout(),
            m_GraphicsBackend->GetTextureSystem(),
            GetAssetManager(),
            m_GraphicsBackend->GetDefaultTextureIndex(),
            &m_FeatureRegistry,
            config.FrameArenaSize);

        // 7. Connect EnTT on_destroy hook for immediate GPU slot reclaim (via SceneManager).
        if (m_RenderOrchestrator->GetGPUScenePtr())
        {
            m_SceneManager->ConnectGpuHooks(m_RenderOrchestrator->GetGPUScene());
        }

        // 8. Register core features in the central FeatureRegistry.
        RegisterCoreFeatures();

        // 9. I/O subsystem: backend + format registry.
        m_IOBackend = std::make_unique<Core::IO::FileIOBackend>();
        Graphics::RegisterBuiltinLoaders(m_IORegistry);

        Core::Log::Info("Engine: Constructor complete.");
    }

    Engine::~Engine()
    {
        m_GraphicsBackend->WaitIdle();

        // Disconnect entity destruction hooks before tearing down GPU systems.
        m_SceneManager->DisconnectGpuHooks();

        // Order matters!
        Core::Tasks::Scheduler::Shutdown();
        Core::Filesystem::FileWatcher::Shutdown();

        // Process material deletions before RenderOrchestrator destroys MaterialSystem.
        auto& matSys = m_RenderOrchestrator->GetMaterialSystem();
        matSys.ProcessDeletions(m_GraphicsBackend->GetDevice()->GetGlobalFrameNumber());

        Interface::GUI::Shutdown();

        m_SceneManager->Clear();
        GetAssetManager().Clear();

        m_AssetPipeline->ClearLoadedMaterials();

        // RenderOrchestrator destructor handles: GPUScene, RenderSystem, PipelineLibrary,
        // MaterialSystem, GeometryStorage, frame state.
        m_RenderOrchestrator.reset();

        // AssetPipeline destructor handles cleanup of asset state.
        m_AssetPipeline.reset();

        // SceneManager destructor handles: hook disconnect (no-op if already done), registry.
        m_SceneManager.reset();

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

        bool isModel = m_IORegistry.CanImport(ext);

        if (isModel)
        {
            Core::Log::Info("Scheduling Async Load: {}", path);

            // 1. Offload heavy parsing to Worker Thread
            Core::Tasks::Scheduler::Dispatch([this, path]()
            {
                // [Worker Thread] I/O-agnostic parsing via IORegistry
                auto loadResult = Graphics::ModelLoader::LoadAsync(
                    GetDevice(), m_GraphicsBackend->GetTransferManager(),
                    m_RenderOrchestrator->GetGeometryStorage(), path,
                    m_IORegistry, *m_IOBackend);

                if (!loadResult)
                {
                    Core::Log::Error("Failed to load model: {} ({})", path,
                                     Graphics::AssetErrorToString(loadResult.error()));
                    return;
                }

                // 2. Register Token immediately (Thread-safe)
                m_AssetPipeline->RegisterAssetLoad(Core::Assets::AssetHandle{}, loadResult->Token);

                // 3. Schedule Entity Spawning on Main Thread
                m_AssetPipeline->RunOnMainThread([this, model = std::move(loadResult->ModelData), path]() mutable
                {
                    // [Main Thread] Safe to touch Scene/Assets
                    std::filesystem::path fsPath(path);
                    std::string baseName = fsPath.filename().string();

                    static uint64_t s_AssetLoadCounter = 0;
                    std::string assetName = baseName + "::" + std::to_string(++s_AssetLoadCounter);

                    auto modelHandle = GetAssetManager().Create(assetName, std::move(model));

                    Graphics::MaterialData matData;
                    matData.AlbedoID = m_GraphicsBackend->GetDefaultTextureIndex();
                    matData.RoughnessFactor = 0.5f;

                    auto defaultMat = std::make_unique<Graphics::Material>(
                        m_RenderOrchestrator->GetMaterialSystem(), matData);

                    const std::string materialName = assetName + "::DefaultMaterial";
                    auto defaultMaterialHandle = GetAssetManager().Create(materialName, std::move(defaultMat));
                    m_AssetPipeline->TrackMaterial(defaultMaterialHandle);

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

    entt::entity Engine::SpawnModel(Core::Assets::AssetHandle modelHandle,
                                    Core::Assets::AssetHandle materialHandle,
                                    glm::vec3 position,
                                    glm::vec3 scale)
    {
        return m_SceneManager->SpawnModel(GetAssetManager(), modelHandle, materialHandle, position, scale);
    }

    void Engine::RegisterCoreFeatures()
    {
        using Cat = Core::FeatureCategory;

        // All features are registered as catalog entries for discovery and
        // runtime enable/disable.  Factories are no-ops: render passes are
        // owned by DefaultPipeline, and ECS systems are stateless free
        // functions — neither needs FeatureRegistry to create instances.
        auto noopFactory = []() -> void* { return nullptr; };
        auto noopDestroy = [](void*) {};

        auto reg = [&](const char* name, Cat cat, const char* desc) {
            Core::FeatureInfo info{};
            info.Name = name;
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = cat;
            info.Description = desc;
            info.Enabled = true;
            m_FeatureRegistry.Register(std::move(info), noopFactory, noopDestroy);
        };

        // --- Render Features ---
        reg("ForwardPass",           Cat::RenderFeature, "Main forward PBR rendering pass");
        reg("PickingPass",           Cat::RenderFeature, "Entity ID picking for mouse selection");
        reg("SelectionOutlinePass",  Cat::RenderFeature, "Selection outline overlay for selected/hovered entities");
        reg("DebugViewPass",         Cat::RenderFeature, "Render target debug visualization");
        reg("ImGuiPass",             Cat::RenderFeature, "ImGui UI overlay");

        // --- ECS Systems ---
        reg("TransformUpdate",        Cat::System, "Propagates local transforms to world matrices");
        reg("MeshRendererLifecycle",   Cat::System, "Allocates/deallocates GPU slots for mesh renderers");
        reg("GPUSceneSync",           Cat::System, "Synchronizes CPU entity data to GPU scene buffers");

        Core::Log::Info("FeatureRegistry: Registered {} core features", m_FeatureRegistry.Count());
    }

    void Engine::Run()
    {
        Core::Log::Info("Engine::Run starting...");
        OnStart();

        using Clock = std::chrono::high_resolution_clock;
        auto lastTime = Clock::now();

        // Fixed-step simulation (physics) accumulator.
        // We keep render/input updates variable-dt for responsiveness.
        double accumulator = 0.0;
        constexpr double fixedDt = 1.0 / 60.0; // 60 Hz fixed simulation
        constexpr int maxSubstepsPerFrame = 8; // clamp to avoid spiral-of-death

        while (m_Running && !m_Window->ShouldClose())
        {
            // Begin frame telemetry
            Core::Telemetry::TelemetrySystem::Get().BeginFrame();

            auto currentTime = Clock::now();
            double frameTime = std::chrono::duration<double>(currentTime - lastTime).count();
            lastTime = currentTime;

            // Prevent spiral of death from huge pauses/breakpoints
            if (frameTime > 0.25) frameTime = 0.25;

            accumulator += frameTime;

            {
                PROFILE_SCOPE("FrameArena::Reset");
                m_RenderOrchestrator->ResetFrameState();
            }

            {
                PROFILE_SCOPE("Window::OnUpdate");
                m_Window->OnUpdate();
            }

            m_AssetPipeline->ProcessMainThreadQueue();

            {
                PROFILE_SCOPE("ProcessUploads");
                m_AssetPipeline->ProcessUploads();
            }

            // Reclaim pool slots for textures that became unreachable this frame.
            m_GraphicsBackend->ProcessTextureDeletions();

            {
                auto& matSys = m_RenderOrchestrator->GetMaterialSystem();
                matSys.ProcessDeletions(m_GraphicsBackend->GetDevice()->GetGlobalFrameNumber());
            }

            // -----------------------------------------------------------------
            // Fixed-step lane (0..N substeps) for deterministic simulation
            // -----------------------------------------------------------------
            {
                PROFILE_SCOPE("FixedStep");

                int substeps = 0;
                while (accumulator >= fixedDt && substeps < maxSubstepsPerFrame)
                {
                    {
                        PROFILE_SCOPE("OnFixedUpdate");
                        OnFixedUpdate(static_cast<float>(fixedDt));
                    }

                    // Allow client/engine simulation systems to run at fixed dt.
                    // NOTE: This currently reuses the same FrameGraph instance.
                    // If you register passes here, ensure pass names are unique per tick.
                    {
                        PROFILE_SCOPE("FixedStepFrameGraph");
                        auto& fixedGraph = m_RenderOrchestrator->GetFrameGraph();
                        fixedGraph.Reset();

                        const float dtF = static_cast<float>(fixedDt);
                        OnRegisterFixedSystems(fixedGraph, dtF);

                        auto compileResult = fixedGraph.Compile();
                        if (compileResult)
                        {
                            GetAssetManager().BeginReadPhase();
                            fixedGraph.Execute();
                            GetAssetManager().EndReadPhase();
                        }
                    }

                    accumulator -= fixedDt;
                    ++substeps;
                }

                // If we hit the clamp, drop remaining accumulated time.
                // This keeps the engine responsive under extreme stalls.
                if (substeps == maxSubstepsPerFrame)
                {
                    accumulator = 0.0;
                }
            }

            {
                PROFILE_SCOPE("OnUpdate");
                // Variable-dt update for responsive input/camera
                OnUpdate(static_cast<float>(frameTime));
            }

            // --- FrameGraph: register, compile, and execute all variable-dt systems ---
            {
                PROFILE_SCOPE("FrameGraph");
                auto& frameGraph = m_RenderOrchestrator->GetFrameGraph();
                frameGraph.Reset();

                auto& registry = m_SceneManager->GetRegistry();
                const float frameDt = static_cast<float>(frameTime);

                // Client/gameplay systems first: they produce dirty state
                OnRegisterSystems(frameGraph, frameDt);

                // Core engine systems consume dirty state in pipeline order.
                // Each system is gated by the FeatureRegistry so it can be
                // toggled at runtime (e.g. for debugging or profiling).
                if (m_FeatureRegistry.IsEnabled("TransformUpdate"_id))
                    ECS::Systems::Transform::RegisterSystem(frameGraph, registry);

                auto* gpuScene = m_RenderOrchestrator->GetGPUScenePtr();
                if (gpuScene)
                {
                    auto& matSys = m_RenderOrchestrator->GetMaterialSystem();

                    if (m_FeatureRegistry.IsEnabled("MeshRendererLifecycle"_id))
                    {
                        Graphics::Systems::MeshRendererLifecycle::RegisterSystem(
                            frameGraph, registry, *gpuScene, GetAssetManager(),
                            matSys, m_RenderOrchestrator->GetGeometryStorage(),
                            m_GraphicsBackend->GetDefaultTextureIndex());
                    }

                    if (m_FeatureRegistry.IsEnabled("GPUSceneSync"_id))
                    {
                        Graphics::Systems::GPUSceneSync::RegisterSystem(
                            frameGraph, registry, *gpuScene, GetAssetManager(),
                            matSys, m_GraphicsBackend->GetDefaultTextureIndex());
                    }
                }

                auto compileResult = frameGraph.Compile();
                if (compileResult)
                {
                    GetAssetManager().BeginReadPhase();
                    frameGraph.Execute();
                    GetAssetManager().EndReadPhase();
                }
            }

            if (m_FramebufferResized)
            {
                m_GraphicsBackend->OnResize();
                m_RenderOrchestrator->OnResize();
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
