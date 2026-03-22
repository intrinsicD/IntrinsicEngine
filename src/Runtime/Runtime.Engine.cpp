module;
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
import Graphics.FeatureCatalog;
import Graphics.IORegistry;
import ECS;
import Interface;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.AssetIngestService;
import Runtime.SceneManager;
import Runtime.RenderOrchestrator;
import Runtime.FrameLoop;
import Runtime.PointCloudKMeans;
import Runtime.SystemFeatureCatalog;

using namespace Core::Hash;

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
                        if (m_AssetIngestService)
                            m_AssetIngestService->EnqueueDropImport(path);
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

        // 10. Asset ingest orchestration (drag-drop + sync re-import).
        m_AssetIngestService = std::make_unique<AssetIngestService>(
            GetDeviceShared(),
            m_GraphicsBackend->GetTransferManager(),
            m_RenderOrchestrator->GetGeometryStorage(),
            m_RenderOrchestrator->GetMaterialSystem(),
            *m_AssetPipeline,
            *m_SceneManager,
            m_IORegistry,
            *m_IOBackend,
            m_GraphicsBackend->GetDefaultTextureIndex());

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

        // AssetIngestService borrows runtime subsystems; release it before tearing them down.
        m_AssetIngestService.reset();

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

    entt::entity Engine::SpawnModel(Core::Assets::AssetHandle modelHandle,
                                    Core::Assets::AssetHandle materialHandle,
                                    glm::vec3 position,
                                    glm::vec3 scale)
    {
        return m_SceneManager->SpawnModel(GetAssetManager(), modelHandle, materialHandle, position, scale);
    }

    void Engine::RegisterCoreFeatures()
    {
        // All features are registered as catalog entries for discovery and
        // runtime enable/disable.  Factories are no-ops: render passes are
        // owned by DefaultPipeline, and ECS systems are stateless free
        // functions — neither needs FeatureRegistry to create instances.
        auto noopFactory = []() -> void* { return nullptr; };
        auto noopDestroy = [](void*) {};

        const auto registerDescriptor = [&](const Core::FeatureDescriptor& descriptor)
        {
            m_FeatureRegistry.Register(descriptor, noopFactory, noopDestroy);
        };

        registerDescriptor(Graphics::FeatureCatalog::SurfacePass);
        registerDescriptor(Graphics::FeatureCatalog::PickingPass);
        registerDescriptor(Graphics::FeatureCatalog::SelectionOutlinePass);
        registerDescriptor(Graphics::FeatureCatalog::LinePass);
        registerDescriptor(Graphics::FeatureCatalog::PointPass);
        registerDescriptor(Graphics::FeatureCatalog::PostProcessPass);
        registerDescriptor(Graphics::FeatureCatalog::DebugViewPass);
        registerDescriptor(Graphics::FeatureCatalog::HtexPatchPreviewPass);
        registerDescriptor(Graphics::FeatureCatalog::ImGuiPass);
        registerDescriptor(Graphics::FeatureCatalog::DeferredLighting);

        registerDescriptor(Runtime::SystemFeatureCatalog::TransformUpdate);
        registerDescriptor(Runtime::SystemFeatureCatalog::MeshRendererLifecycle);
        registerDescriptor(Runtime::SystemFeatureCatalog::PrimitiveBVHSync);
        registerDescriptor(Runtime::SystemFeatureCatalog::GraphGeometrySync);
        registerDescriptor(Runtime::SystemFeatureCatalog::PointCloudGeometrySync);
        registerDescriptor(Runtime::SystemFeatureCatalog::MeshViewLifecycle);
        registerDescriptor(Runtime::SystemFeatureCatalog::GPUSceneSync);
        registerDescriptor(Runtime::SystemFeatureCatalog::PropertySetDirtySync);
        registerDescriptor(Runtime::FrameLoopFeatureCatalog::StagedPhases);
        registerDescriptor(Runtime::FrameLoopFeatureCatalog::LegacyCompatibility);

        Core::Log::Info("FeatureRegistry: Registered {} core features", m_FeatureRegistry.Count());
    }

    void Engine::Run()
    {
        Core::Log::Info("Engine::Run starting...");
        OnStart();

        const int startupFbWidth = m_Window->GetFramebufferWidth();
        const int startupFbHeight = m_Window->GetFramebufferHeight();
        const bool startupResizePerformed = startupFbWidth > 0 && startupFbHeight > 0;
        if (startupResizePerformed)
        {
            Core::Log::Info("Engine startup resize: framebuffer={}x{}", startupFbWidth, startupFbHeight);
            m_GraphicsBackend->OnResize();
            m_RenderOrchestrator->OnResize();
        }

        // Bootstrap the renderer through the same path as a real framebuffer resize.
        // The flag stays armed even if we already performed the startup resize so the
        // first pumped frame still exercises the normal resize branch after WSI settles.
        m_FramebufferResized = true;

        double accumulator = 0.0;
        const FrameLoopPolicy frameLoopPolicy = MakeFrameLoopPolicy(
            m_EngineConfig.FixedStepHz,
            m_EngineConfig.MaxFrameDeltaSeconds,
            m_EngineConfig.MaxSubstepsPerFrame);
        Core::Log::Info("Engine fixed-step policy: {:.3f} Hz, maxFrameDelta={:.3f}s, maxSubsteps={}",
                        frameLoopPolicy.FixedDt > 0.0 ? 1.0 / frameLoopPolicy.FixedDt : 0.0,
                        frameLoopPolicy.MaxFrameDelta,
                        frameLoopPolicy.MaxSubstepsPerFrame);
        FrameClock frameClock{};
        RuntimePlatformFrameHost platformFrameHost{*m_Window, m_FramebufferResized};
        PlatformFrameCoordinator platformFrame{
            .Host = platformFrameHost,
            .Clock = frameClock,
        };
        RuntimeStreamingLaneHost streamingLaneHost{
            m_AssetIngestService.get(),
            *m_AssetPipeline,
            *m_GraphicsBackend,
            m_RenderOrchestrator->GetMaterialSystem(),
        };
        const StreamingLaneCoordinator streamingLane{.Host = streamingLaneHost};
        RuntimeRenderLaneHost renderLaneHost{
            *m_SceneManager,
            *m_RenderOrchestrator,
            *m_GraphicsBackend,
            m_FeatureRegistry,
            GetAssetManager(),
        };
        const RenderLaneCoordinator renderLane{.Host = renderLaneHost};
        FrameLoopMode activeFrameLoopMode = ResolveFrameLoopMode(m_FeatureRegistry);
        Core::Log::Info("Engine::Run frame-loop mode: {}", ToString(activeFrameLoopMode));

        while (m_Running)
        {
            // Begin frame telemetry
            Core::Telemetry::TelemetrySystem::Get().BeginFrame();
            FrameGraphTimingTotals frameGraphTimings{};
            const FrameGraphExecutor executeGraph{
                .AssetManager = GetAssetManager(),
                .Timings = frameGraphTimings,
            };

            {
                PROFILE_SCOPE("PlatformStage");
                const PlatformFrameResult platformResult = platformFrame.BeginFrame(frameLoopPolicy);
                if (platformResult.ShouldQuit)
                {
                    m_Running = false;
                    Core::Telemetry::TelemetrySystem::Get().EndFrame();
                    continue;
                }

                if (!platformResult.ContinueFrame)
                {
                    Core::Telemetry::TelemetrySystem::Get().EndFrame();
                    continue;
                }

                accumulator += platformResult.FrameStep.FrameTime;

                {
                    PROFILE_SCOPE("FrameArena::Reset");
                    m_RenderOrchestrator->ResetFrameState();
                }

                {
                    const int fbWidth = platformResult.FramebufferWidth;
                    const int fbHeight = platformResult.FramebufferHeight;
                    const VkExtent2D swapExtent = m_GraphicsBackend->GetSwapchain().GetExtent();
                    const bool framebufferExtentMismatch =
                        fbWidth > 0 && fbHeight > 0 &&
                        (swapExtent.width != static_cast<uint32_t>(fbWidth) ||
                         swapExtent.height != static_cast<uint32_t>(fbHeight));

                    const bool resizeRequested =
                        platformResult.ResizeRequested || framebufferExtentMismatch;

                    // Monitor moves can change framebuffer extent/content scale without a reliable
                    // resize callback on every platform. Apply resize before per-frame update/render
                    // so picking, EntityId, and post-process overlays all use the current extent.
                    if (resizeRequested && fbWidth > 0 && fbHeight > 0)
                    {
                        Core::Log::Info(
                            "Engine resize trigger: framebuffer={}x{} swapchainBefore={}x{} resizeRequested={} mismatch={}",
                            fbWidth,
                            fbHeight,
                            swapExtent.width,
                            swapExtent.height,
                            resizeRequested,
                            framebufferExtentMismatch);
                        m_GraphicsBackend->OnResize();
                        m_RenderOrchestrator->OnResize();
                    }
                }

                const FrameLoopMode frameLoopMode = ResolveFrameLoopMode(m_FeatureRegistry);
                if (frameLoopMode != activeFrameLoopMode)
                {
                    Core::Log::Warn("Engine::Run frame-loop mode switch: {} -> {}",
                                    ToString(activeFrameLoopMode),
                                    ToString(frameLoopMode));
                    activeFrameLoopMode = frameLoopMode;
                }

                const double frameTime = platformResult.FrameStep.FrameTime;
                const FramePhaseRunResult framePhaseResult = RunFramePhasesForMode(
                    frameLoopMode,
                    frameTime,
                    accumulator,
                    frameLoopPolicy,
                    streamingLane,
                    renderLane,
                    m_RenderOrchestrator->GetFrameGraph(),
                    {
                        .OnFixedUpdate = [&](float fixedDeltaTime) { OnFixedUpdate(fixedDeltaTime); },
                        .RegisterFixedSystems = [&](Core::FrameGraph& graph, float fixedDeltaTime)
                        { OnRegisterFixedSystems(graph, fixedDeltaTime); },
                        .ExecuteFixedGraph = [&](Core::FrameGraph& graph) { executeGraph.Execute(graph); },
                        .Render =
                            {
                                // Resize is synchronized immediately after Window::OnUpdate() so render/update
                                // always see the current framebuffer extent on monitor moves and DPI changes.
                                .OnUpdate = [&](float dt) { OnUpdate(dt); },
                                .RegisterVariableSystems = [&](Core::FrameGraph& graph, float dt)
                                { OnRegisterSystems(graph, dt); },
                                .BeforeDispatch = [&]() { Runtime::PointCloudKMeans::PumpCompletions(*this); },
                                .OnRender = [&]() { OnRender(); },
                            },
                        .ExecuteVariableGraph = [&](Core::FrameGraph& graph) { executeGraph.Execute(graph); },
                    });

                Core::Telemetry::TelemetrySystem::Get().SetSimulationStats(
                    static_cast<uint32_t>(framePhaseResult.FixedStep.ExecutedSubsteps),
                    framePhaseResult.FixedStep.AccumulatorClamped ? 1u : 0u,
                    framePhaseResult.FixedStep.CpuTimeNs);
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
        }

        Core::Tasks::Scheduler::WaitForAll();
        m_GraphicsBackend->WaitIdle();

        // Final flush: destroy any RHI resources that were deferred via VulkanDevice::SafeDestroy().
        m_GraphicsBackend->FlushDeletionQueues();

    }
}
