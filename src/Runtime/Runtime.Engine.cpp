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

import Core.Logging;
import Core.Tasks;
import Core.Window;
import Core.Filesystem;
import Core.Assets;
import Core.Telemetry;
import Core.Benchmark;
import Core.FrameGraph;
import Core.Hash;
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
import Runtime.PointCloudKMeans;
import Runtime.SystemBundles;

using namespace Core::Hash;

namespace
{
    struct FrameGraphTimingTotals
    {
        uint64_t CompileNsTotal = 0;
        uint64_t ExecuteNsTotal = 0;
        uint64_t CriticalPathNsTotal = 0;
    };

    struct FrameGraphExecutor
    {
        Core::Assets::AssetManager& AssetManager;
        FrameGraphTimingTotals& Timings;

        void Execute(Core::FrameGraph& graph) const
        {
            const auto compileResult = graph.Compile();
            Timings.CompileNsTotal += graph.GetLastCompileTimeNs();
            if (!compileResult)
            {
                return;
            }

            AssetManager.BeginReadPhase();
            graph.Execute();
            AssetManager.EndReadPhase();

            Timings.ExecuteNsTotal += graph.GetLastExecuteTimeNs();
            Timings.CriticalPathNsTotal += graph.GetLastCriticalPathTimeNs();
        }
    };

    struct StreamingLaneCoordinator
    {
        Runtime::AssetPipeline& AssetPipeline;
        Runtime::GraphicsBackend& GraphicsBackend;
        Graphics::MaterialSystem& MaterialSystem;

        void BeginFrame() const
        {
            AssetPipeline.ProcessMainThreadQueue();

            {
                PROFILE_SCOPE("ProcessUploads");
                AssetPipeline.ProcessUploads();
            }

            // Reclaim pool slots for textures that became unreachable this frame.
            GraphicsBackend.ProcessTextureDeletions();
            MaterialSystem.ProcessDeletions(GraphicsBackend.GetDevice().GetGlobalFrameNumber());
        }

        void EndFrame() const
        {
            GraphicsBackend.GarbageCollectTransfers();
        }
    };

    struct SimulationLaneCoordinator
    {
        Runtime::Engine& Engine;
        Runtime::RenderOrchestrator& RenderOrchestrator;

        template <typename ExecuteGraphFn>
        void Run(double& accumulator,
                 const double fixedDt,
                 const int maxSubstepsPerFrame,
                 ExecuteGraphFn&& executeGraph) const
        {
            PROFILE_SCOPE("FixedStep");

            int substeps = 0;
            while (accumulator >= fixedDt && substeps < maxSubstepsPerFrame)
            {
                {
                    PROFILE_SCOPE("OnFixedUpdate");
                    Engine.OnFixedUpdate(static_cast<float>(fixedDt));
                }

                // Allow client/engine simulation systems to run at fixed dt.
                // NOTE: This currently reuses the same FrameGraph instance.
                // If you register passes here, ensure pass names are unique per tick.
                {
                    PROFILE_SCOPE("FixedStepFrameGraph");
                    auto& fixedGraph = RenderOrchestrator.GetFrameGraph();
                    fixedGraph.Reset();

                    const float dtF = static_cast<float>(fixedDt);
                    Engine.OnRegisterFixedSystems(fixedGraph, dtF);

                    executeGraph(fixedGraph);
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
    };

    struct RenderLaneCoordinator
    {
        Runtime::Engine& Engine;
        Runtime::SceneManager& SceneManager;
        Runtime::RenderOrchestrator& RenderOrchestrator;
        Runtime::GraphicsBackend& GraphicsBackend;
        Core::FeatureRegistry& FeatureRegistry;

        template <typename ExecuteGraphFn>
        void Run(const double frameTime, ExecuteGraphFn&& executeGraph) const
        {
            {
                PROFILE_SCOPE("OnUpdate");
                // Variable-dt update for responsive input/camera.
                Engine.OnUpdate(static_cast<float>(frameTime));
            }

            // --- FrameGraph: register, compile, and execute all variable-dt systems ---
            {
                PROFILE_SCOPE("FrameGraph");
                auto& frameGraph = RenderOrchestrator.GetFrameGraph();
                frameGraph.Reset();

                auto& registry = SceneManager.GetRegistry();
                const float frameDt = static_cast<float>(frameTime);

                // Client/gameplay systems first: they produce dirty state.
                Engine.OnRegisterSystems(frameGraph, frameDt);

                // Core engine systems consume dirty state in pipeline order.
                // Registration is grouped into typed bundles so Engine::Run()
                // only orchestrates bundle boundaries rather than individual
                // systems while preserving the same feature-gated pass order.
                Runtime::CoreFrameGraphRegistrationContext coreBundleContext{
                    .Graph = frameGraph,
                    .Registry = registry,
                    .Features = FeatureRegistry,
                };
                Runtime::CoreFrameGraphSystemBundle{}.Register(coreBundleContext);

                auto* gpuScene = RenderOrchestrator.GetGPUScenePtr();
                if (gpuScene)
                {
                    Runtime::GpuFrameGraphRegistrationContext gpuBundleContext{
                        .Core = coreBundleContext,
                        .GpuScene = *gpuScene,
                        .AssetManager = Engine.GetAssetManager(),
                        .MaterialSystem = RenderOrchestrator.GetMaterialSystem(),
                        .GeometryStorage = RenderOrchestrator.GetGeometryStorage(),
                        .Device = Engine.GetDeviceShared(),
                        .TransferManager = GraphicsBackend.GetTransferManager(),
                        .Dispatcher = SceneManager.GetScene().GetDispatcher(),
                        .DefaultTextureId = GraphicsBackend.GetDefaultTextureIndex(),
                    };
                    Runtime::GpuFrameGraphSystemBundle{}.Register(gpuBundleContext);
                }

                executeGraph(frameGraph);
            }

            Runtime::PointCloudKMeans::PumpCompletions(Engine);

            // Drain deferred events enqueued during this frame's system updates.
            // All dispatcher sinks run synchronously on the main thread here,
            // after ECS systems and before rendering (see CLAUDE.md Event Communication Policy).
            SceneManager.GetScene().GetDispatcher().update();

            {
                PROFILE_SCOPE("OnRender");
                Engine.OnRender();
            }
        }
    };
}

namespace Runtime
{
    Engine::Engine(const EngineConfig& config)
        : m_EngineConfig(config)
    {
        // Configure benchmark runner if benchmark mode is enabled.
        if (config.BenchmarkMode)
        {
            Core::Benchmark::BenchmarkConfig benchCfg{};
            benchCfg.FrameCount = config.BenchmarkFrames;
            benchCfg.WarmupFrames = config.BenchmarkWarmupFrames;
            benchCfg.OutputPath = config.BenchmarkOutputPath;
            m_BenchmarkRunner.Configure(benchCfg);
            Core::Log::Info("Benchmark mode enabled: {} frames + {} warmup -> {}",
                            config.BenchmarkFrames, config.BenchmarkWarmupFrames, config.BenchmarkOutputPath);
        }
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
                             m_GraphicsBackend->GetDevice(),
                             m_GraphicsBackend->GetSwapchain(),
                             m_GraphicsBackend->GetContext().GetInstance(),
                             m_GraphicsBackend->GetDevice().GetGraphicsQueue());

        // 6. RenderOrchestrator (MaterialSystem, GeometryStorage, Pipelines, RenderSystem, GPUScene, FrameGraph)
        m_RenderOrchestrator = std::make_unique<RenderOrchestrator>(
            m_GraphicsBackend->GetDeviceShared(),
            m_GraphicsBackend->GetSwapchain(),
            m_GraphicsBackend->GetRenderer(),
            m_GraphicsBackend->GetBindlessSystem(),
            m_GraphicsBackend->GetDescriptorPool(),
            m_GraphicsBackend->GetDescriptorLayout(),
            m_GraphicsBackend->GetTextureSystem(),
            GetAssetManager(),
            &m_FeatureRegistry,
            config.FrameArenaSize);

        // 7. Connect EnTT on_destroy hook for immediate GPU slot reclaim (via SceneManager).
        //    Also provide the geometry pool so SpawnModel can inspect topology.
        m_SceneManager->SetGeometryStorage(&m_RenderOrchestrator->GetGeometryStorage());
        if (m_RenderOrchestrator->GetGPUScenePtr())
        {
            m_SceneManager->ConnectGpuHooks(m_RenderOrchestrator->GetGPUScene()
#ifdef INTRINSIC_HAS_CUDA
                                            , GetCudaDevice()
#endif
            );
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
        matSys.ProcessDeletions(m_GraphicsBackend->GetDevice().GetGlobalFrameNumber());

        m_SceneManager->Clear();
        GetAssetManager().Clear();

        m_AssetPipeline->ClearLoadedMaterials();

        // RenderOrchestrator destructor handles: GPUScene, RenderSystem, PipelineLibrary,
        // MaterialSystem, GeometryStorage, frame state.
        m_RenderOrchestrator.reset();

        // GUI-backed textures owned by render passes must be released before the ImGui backend
        // and descriptor pool are destroyed, but after RenderOrchestrator/RenderSystem shutdown.
        Interface::GUI::Shutdown();

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
                    GetDeviceShared(), m_GraphicsBackend->GetTransferManager(),
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

                    entt::entity root = SpawnModel(modelHandle, defaultMaterialHandle, glm::vec3(0.0f), glm::vec3(0.01f));

                    // Record the asset source path on the root entity for scene serialization.
                    if (root != entt::null)
                    {
                        GetScene().GetRegistry().emplace_or_replace<ECS::Components::AssetSourceRef::Component>(
                            root, path);
                    }

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
        reg("SurfacePass",           Cat::RenderFeature, "Main surface PBR rendering pass");
        reg("PickingPass",           Cat::RenderFeature, "Entity ID picking for mouse selection");
        reg("SelectionOutlinePass",  Cat::RenderFeature, "Selection outline overlay for selected/hovered entities");
        reg("LinePass",              Cat::RenderFeature, "Unified BDA line rendering (retained wireframe/graph edges + transient DebugDraw)");
        reg("PointPass",             Cat::RenderFeature, "Unified BDA point rendering (retained points/nodes/vertices + transient DebugDraw)");
        reg("PostProcessPass",       Cat::RenderFeature, "Bloom + HDR tone mapping (ACES/Reinhard/Uncharted2) + optional FXAA");
        reg("DebugViewPass",         Cat::RenderFeature, "Render target debug visualization");
        reg("ImGuiPass",             Cat::RenderFeature, "ImGui UI overlay");

        // --- Lighting Path ---
        // DeferredLighting is disabled by default; enable via FeatureRegistry UI.
        {
            Core::FeatureInfo info{};
            info.Name = "DeferredLighting";
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = Cat::RenderFeature;
            info.Description = "Deferred lighting path (G-buffer + fullscreen composition)";
            info.Enabled = false;
            m_FeatureRegistry.Register(std::move(info), noopFactory, noopDestroy);
        }

        // --- ECS Systems ---
        reg("TransformUpdate",                Cat::System, "Propagates local transforms to world matrices");
        reg("MeshRendererLifecycle",           Cat::System, "Allocates/deallocates GPU slots for mesh renderers");
        reg("PrimitiveBVHSync",                Cat::System, "Builds entity-attached primitive BVHs for local-space picking and future broadphase");
        reg("GraphGeometrySync",              Cat::System, "Uploads graph geometry to GPU and allocates GPUScene slots");
        reg("PointCloudGeometrySync",         Cat::System, "Uploads point clouds to GPU and allocates GPUScene slots");
        reg("MeshViewLifecycle",              Cat::System, "Creates GPU edge/vertex views from mesh via ReuseVertexBuffersFrom");
        reg("GPUSceneSync",                   Cat::System, "Synchronizes CPU entity data to GPU scene buffers");
        reg("PropertySetDirtySync",           Cat::System, "Syncs PropertySet dirty domains to GPU buffers (per-domain incremental)");

        Core::Log::Info("FeatureRegistry: Registered {} core features", m_FeatureRegistry.Count());
    }

    void Engine::Run()
    {
        Core::Log::Info("Engine::Run starting...");
        OnStart();

        // Bootstrap the renderer through the same path as a real framebuffer resize.
        // In practice this is what makes the selection outline appear today: swapchain
        // recreation, render-graph pool trim, presentation depth invalidation, and
        // per-pipeline resize callbacks. Some WSI state only settles after the first
        // event poll, so prime the flag here and let the first frame consume it once.
        m_FramebufferResized = true;

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
            FrameGraphTimingTotals frameGraphTimings{};
            const FrameGraphExecutor executeGraph{
                .AssetManager = GetAssetManager(),
                .Timings = frameGraphTimings,
            };
            const StreamingLaneCoordinator streamingLane{
                .AssetPipeline = *m_AssetPipeline,
                .GraphicsBackend = *m_GraphicsBackend,
                .MaterialSystem = m_RenderOrchestrator->GetMaterialSystem(),
            };
            const SimulationLaneCoordinator simulationLane{
                .Engine = *this,
                .RenderOrchestrator = *m_RenderOrchestrator,
            };
            const RenderLaneCoordinator renderLane{
                .Engine = *this,
                .SceneManager = *m_SceneManager,
                .RenderOrchestrator = *m_RenderOrchestrator,
                .GraphicsBackend = *m_GraphicsBackend,
                .FeatureRegistry = m_FeatureRegistry,
            };

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

            {
                const int fbWidth = m_Window->GetFramebufferWidth();
                const int fbHeight = m_Window->GetFramebufferHeight();
                const VkExtent2D swapExtent = m_GraphicsBackend->GetSwapchain().GetExtent();
                const bool framebufferExtentMismatch =
                    fbWidth > 0 && fbHeight > 0 &&
                    (swapExtent.width != static_cast<uint32_t>(fbWidth) ||
                     swapExtent.height != static_cast<uint32_t>(fbHeight));

                if (framebufferExtentMismatch)
                    m_FramebufferResized = true;

                // Monitor moves can change framebuffer extent/content scale without a reliable
                // resize callback on every platform. Apply resize before per-frame update/render
                // so picking, EntityId, and post-process overlays all use the current extent.
                if (m_FramebufferResized && fbWidth > 0 && fbHeight > 0)
                {
                    Core::Log::Info(
                        "Engine resize trigger: framebuffer={}x{} swapchainBefore={}x{} resizedFlag={} mismatch={}",
                        fbWidth,
                        fbHeight,
                        swapExtent.width,
                        swapExtent.height,
                        m_FramebufferResized,
                        framebufferExtentMismatch);
                    m_GraphicsBackend->OnResize();
                    m_RenderOrchestrator->OnResize();
                    m_FramebufferResized = false;
                }
            }

            streamingLane.BeginFrame();

            simulationLane.Run(accumulator, fixedDt, maxSubstepsPerFrame,
                               [&](Core::FrameGraph& graph) { executeGraph.Execute(graph); });

            // Resize is synchronized immediately after Window::OnUpdate() so render/update
            // always see the current framebuffer extent on monitor moves and DPI changes.
            renderLane.Run(frameTime,
                           [&](Core::FrameGraph& graph) { executeGraph.Execute(graph); });

            streamingLane.EndFrame();

            Core::Telemetry::TelemetrySystem::Get().SetTaskSchedulerStats(Core::Tasks::Scheduler::GetStats());
            Core::Telemetry::TelemetrySystem::Get().SetFrameGraphTimings(
                frameGraphTimings.CompileNsTotal,
                frameGraphTimings.ExecuteNsTotal,
                frameGraphTimings.CriticalPathNsTotal);

            // End frame telemetry
            Core::Telemetry::TelemetrySystem::Get().EndFrame();

            // Benchmark mode: record frame and exit when complete.
            if (m_EngineConfig.BenchmarkMode)
            {
                m_BenchmarkRunner.RecordFrame(Core::Telemetry::TelemetrySystem::Get());
                if (m_BenchmarkRunner.IsComplete())
                {
                    m_BenchmarkRunner.Finalize();
                    m_Running = false;
                }
            }
        }

        Core::Tasks::Scheduler::WaitForAll();
        m_GraphicsBackend->WaitIdle();

        // Final flush: destroy any RHI resources that were deferred via VulkanDevice::SafeDestroy().
        m_GraphicsBackend->FlushDeletionQueues();
    }
}
