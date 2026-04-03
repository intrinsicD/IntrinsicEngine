module;
#include <algorithm>
#include <cmath>
#include <type_traits>
#include <variant>
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
import Graphics.Components;
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
import Runtime.SystemBundles;
import Core.SystemFeatureCatalog;

using namespace Core::Hash;

namespace Runtime
{
    namespace
    {
        template <typename T>
        void PrewarmStorage(entt::registry& registry)
        {
            (void)registry.storage<T>();
        }
    }

    bool EngineConfigValidationResult::HasErrors() const
    {
        for (const EngineConfigIssue& issue : Issues)
        {
            if (issue.Severity == EngineConfigIssueSeverity::Error)
            {
                return true;
            }
        }
        return false;
    }

    EngineConfigValidationResult ValidateEngineConfig(const EngineConfig& config)
    {
        EngineConfigValidationResult result{};
        result.Sanitized = config;

        if (result.Sanitized.AppName.empty())
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "AppName",
                .Message = "Application name is empty.",
                .Remediation = "Set EngineConfig::AppName to a non-empty value; using fallback 'Intrinsic App'.",
            });
            result.Sanitized.AppName = "Intrinsic App";
        }

        if (result.Sanitized.Width <= 0 || result.Sanitized.Height <= 0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Error,
                .Field = "WindowSize",
                .Message = "Window dimensions must be positive.",
                .Remediation = "Use --width/--height values greater than zero (or set EngineConfig::Width/Height accordingly).",
            });
        }

        constexpr size_t MinFrameArenaSizeBytes = 64u * 1024u;
        if (result.Sanitized.FrameArenaSize < MinFrameArenaSizeBytes)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "FrameArenaSize",
                .Message = "Frame arena size is below 64 KiB and may underflow frame allocations.",
                .Remediation = "Increase EngineConfig::FrameArenaSize; using 1 MiB fallback.",
            });
            result.Sanitized.FrameArenaSize = 1024u * 1024u;
        }

        if (result.Sanitized.FrameContextCount == 0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Error,
                .Field = "FrameContextCount",
                .Message = "FrameContextCount must be at least 1.",
                .Remediation = "Use 2 for default double-buffered frame contexts or 3 for high-throughput modes.",
            });
        }

        if (!std::isfinite(result.Sanitized.FixedStepHz) || result.Sanitized.FixedStepHz <= 0.0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "FixedStepHz",
                .Message = "Fixed-step rate is invalid.",
                .Remediation = "Use a positive finite value; defaulting to 60 Hz.",
            });
            result.Sanitized.FixedStepHz = DefaultFixedStepHz;
        }
        if (!std::isfinite(result.Sanitized.MaxFrameDeltaSeconds) || result.Sanitized.MaxFrameDeltaSeconds <= 0.0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "MaxFrameDeltaSeconds",
                .Message = "Max frame delta is invalid.",
                .Remediation = "Use a positive finite value; defaulting to 0.25 s.",
            });
            result.Sanitized.MaxFrameDeltaSeconds = DefaultMaxFrameDeltaSeconds;
        }
        if (result.Sanitized.MaxSubstepsPerFrame <= 0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "MaxSubstepsPerFrame",
                .Message = "Max substeps must be positive.",
                .Remediation = "Use a value like 4, 8, or 16; defaulting to 8.",
            });
            result.Sanitized.MaxSubstepsPerFrame = DefaultMaxSubstepsPerFrame;
        }

        if (result.Sanitized.BenchmarkMode)
        {
            if (result.Sanitized.BenchmarkFrames == 0)
            {
                result.Issues.push_back({
                    .Severity = EngineConfigIssueSeverity::Error,
                    .Field = "BenchmarkFrames",
                    .Message = "Benchmark mode is enabled but frame count is zero.",
                    .Remediation = "Set --benchmark <frames> with frames > 0.",
                });
            }
            if (result.Sanitized.BenchmarkOutputPath.empty())
            {
                result.Issues.push_back({
                    .Severity = EngineConfigIssueSeverity::Error,
                    .Field = "BenchmarkOutputPath",
                    .Message = "Benchmark mode is enabled but output path is empty.",
                    .Remediation = "Set --out <file> (for example --out benchmark.json).",
                });
            }
        }

        if (!std::isfinite(result.Sanitized.FramePacing.ActiveFps) || result.Sanitized.FramePacing.ActiveFps < 0.0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "FramePacing.ActiveFps",
                .Message = "Active FPS cap is invalid.",
                .Remediation = "Use 0 (VSync-only) or a positive finite value; defaulting to 60.",
            });
            result.Sanitized.FramePacing.ActiveFps = 60.0;
        }
        if (!std::isfinite(result.Sanitized.FramePacing.IdleFps) || result.Sanitized.FramePacing.IdleFps < 0.0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "FramePacing.IdleFps",
                .Message = "Idle FPS cap is invalid.",
                .Remediation = "Use 0 to disable idle cap or a positive finite value; defaulting to 15.",
            });
            result.Sanitized.FramePacing.IdleFps = 15.0;
        }
        if (!std::isfinite(result.Sanitized.FramePacing.IdleTimeoutSeconds) || result.Sanitized.FramePacing.IdleTimeoutSeconds < 0.0)
        {
            result.Issues.push_back({
                .Severity = EngineConfigIssueSeverity::Warning,
                .Field = "FramePacing.IdleTimeoutSeconds",
                .Message = "Idle timeout is invalid.",
                .Remediation = "Use a non-negative finite timeout in seconds; defaulting to 2.0.",
            });
            result.Sanitized.FramePacing.IdleTimeoutSeconds = 2.0;
        }

        return result;
    }

    Engine::Engine(const EngineConfig& config)
        : m_EngineConfig(config)
    {
        const EngineConfigValidationResult validation = ValidateEngineConfig(config);
        m_EngineConfig = validation.Sanitized;
        for (const EngineConfigIssue& issue : validation.Issues)
        {
            const char* severity = issue.Severity == EngineConfigIssueSeverity::Error ? "ERROR" : "WARN";
            if (issue.Severity == EngineConfigIssueSeverity::Error)
            {
                Core::Log::Error("Engine config {} [{}]: {} | Fix: {}", severity, issue.Field, issue.Message, issue.Remediation);
            }
            else
            {
                Core::Log::Warn("Engine config {} [{}]: {} | Fix: {}", severity, issue.Field, issue.Message, issue.Remediation);
            }
        }
        if (validation.HasErrors())
        {
            Core::Log::Error("Engine startup aborted: invalid EngineConfig. Fix the configuration and restart.");
            std::exit(-1);
        }

        // Configure benchmark runner if benchmark mode is enabled.
        if (m_EngineConfig.BenchmarkMode)
        {
            Core::Benchmark::BenchmarkConfig benchCfg{};
            benchCfg.FrameCount = m_EngineConfig.BenchmarkFrames;
            benchCfg.WarmupFrames = m_EngineConfig.BenchmarkWarmupFrames;
            benchCfg.OutputPath = m_EngineConfig.BenchmarkOutputPath;
            m_BenchmarkRunner.Configure(benchCfg);
            Core::Log::Info("Benchmark mode enabled: {} frames + {} warmup -> {}",
                            m_EngineConfig.BenchmarkFrames, m_EngineConfig.BenchmarkWarmupFrames, m_EngineConfig.BenchmarkOutputPath);
        }
        Core::Tasks::Scheduler::Initialize();
        Core::Filesystem::FileWatcher::Initialize();

        Core::Log::Info("Initializing Engine...");

        // 1. SceneManager (ECS scene, entity lifecycle, GPU-reclaim hooks)
        m_SceneManager = std::make_unique<SceneManager>();

        // Prewarm component storages on the main thread before any frame-graph
        // worker can touch the registry. EnTT creates storages lazily, and the
        // first concurrent insert/view on a new component type can race on the
        // internal dense-map reallocation path.
        {
            auto& registry = m_SceneManager->GetRegistry();
            PrewarmStorage<ECS::Components::Transform::Component>(registry);
            PrewarmStorage<ECS::Components::Transform::WorldMatrix>(registry);
            PrewarmStorage<ECS::Components::Transform::IsDirtyTag>(registry);
            PrewarmStorage<ECS::Components::Transform::WorldUpdatedTag>(registry);
            PrewarmStorage<ECS::Components::Hierarchy::Component>(registry);
            PrewarmStorage<ECS::Components::NameTag::Component>(registry);
            PrewarmStorage<ECS::DirtyTag::VertexPositions>(registry);
            PrewarmStorage<ECS::DirtyTag::VertexAttributes>(registry);
            PrewarmStorage<ECS::DirtyTag::EdgeTopology>(registry);
            PrewarmStorage<ECS::DirtyTag::EdgeAttributes>(registry);
            PrewarmStorage<ECS::DirtyTag::FaceTopology>(registry);
            PrewarmStorage<ECS::DirtyTag::FaceAttributes>(registry);
            PrewarmStorage<ECS::MeshCollider::Component>(registry);
            PrewarmStorage<ECS::PrimitiveBVH::Data>(registry);
            PrewarmStorage<ECS::PointKDTree::Data>(registry);
            PrewarmStorage<ECS::Graph::Data>(registry);
            PrewarmStorage<ECS::PointCloud::Data>(registry);
            PrewarmStorage<ECS::Mesh::Data>(registry);
            PrewarmStorage<ECS::Surface::Component>(registry);
            PrewarmStorage<ECS::Line::Component>(registry);
            PrewarmStorage<ECS::Point::Component>(registry);
            PrewarmStorage<ECS::MeshEdgeView::Component>(registry);
            PrewarmStorage<ECS::MeshVertexView::Component>(registry);
            PrewarmStorage<ECS::Components::Selection::PickID>(registry);
        }

        // 2. Window
        Core::Windowing::WindowProps props{m_EngineConfig.AppName, m_EngineConfig.Width, m_EngineConfig.Height};
        m_Window = std::make_unique<Core::Windowing::Window>(props);

        if (!m_Window->IsValid())
        {
            Core::Log::Error("FATAL: Window initialization failed ({}x{}, app='{}').",
                            m_EngineConfig.Width,
                            m_EngineConfig.Height,
                            m_EngineConfig.AppName);
            Core::Log::Error("Fix: verify desktop/windowing environment availability and use positive --width/--height values.");
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
                    if (m_GraphicsBackend)
                        m_GraphicsBackend->GetRenderer().RequestShutdown();
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
        GraphicsBackendConfig gfxConfig{m_EngineConfig.AppName, enableValidation};
        m_GraphicsBackend = std::make_unique<GraphicsBackend>(*m_Window, gfxConfig);

        // 4. AssetPipeline (AssetManager, pending transfers, main-thread queue)
        m_AssetPipeline = std::make_unique<AssetPipeline>(m_GraphicsBackend->GetTransferManager());

        // 5. ImGui
        Interface::GUI::Init(*m_Window,
                             m_GraphicsBackend->GetDevice(),
                             m_GraphicsBackend->GetSwapchain(),
                             m_GraphicsBackend->GetContext().GetInstance(),
                             m_GraphicsBackend->GetDevice().GetGraphicsQueue());

        // 6. RenderOrchestrator (MaterialRegistry, GeometryStorage, Pipelines, RenderDriver, GPUScene, FrameGraph)
        m_RenderOrchestrator = std::make_unique<RenderOrchestrator>(
            m_GraphicsBackend->GetDeviceShared(),
            m_GraphicsBackend->GetSwapchain(),
            m_GraphicsBackend->GetRenderer(),
            m_GraphicsBackend->GetBindlessSystem(),
            m_GraphicsBackend->GetDescriptorPool(),
            m_GraphicsBackend->GetDescriptorLayout(),
            m_GraphicsBackend->GetTextureManager(),
            m_AssetPipeline->GetAssetManager(),
            &m_FeatureRegistry,
            m_EngineConfig.FrameArenaSize,
            m_EngineConfig.FrameContextCount);

        // 7. Connect EnTT on_destroy hook for immediate GPU slot reclaim (via SceneManager).
        //    Also provide the geometry pool so SpawnModel can inspect topology.
        m_SceneManager->SetGeometryStorage(&m_RenderOrchestrator->GetGeometryStorage());
        if (m_RenderOrchestrator->GetGPUScenePtr())
        {
            m_SceneManager->ConnectGpuHooks(m_RenderOrchestrator->GetGPUScene()
#ifdef INTRINSIC_HAS_CUDA
                                            , m_GraphicsBackend->GetCudaDevice()
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
            m_GraphicsBackend->GetDeviceShared(),
            m_GraphicsBackend->GetTransferManager(),
            m_RenderOrchestrator->GetGeometryStorage(),
            m_RenderOrchestrator->GetMaterialRegistry(),
            *m_AssetPipeline,
            *m_SceneManager,
            m_IORegistry,
            *m_IOBackend,
            m_GraphicsBackend->GetDefaultTextureIndex());

        Core::Log::Info("Engine: Constructor complete.");
    }

    Engine::~Engine()
    {
        if (m_GraphicsBackend)
            m_GraphicsBackend->GetRenderer().RequestShutdown();

        if (m_GraphicsBackend)
            m_GraphicsBackend->WaitIdle();

        // Disconnect entity destruction hooks before tearing down GPU systems.
        if (m_SceneManager)
            m_SceneManager->DisconnectGpuHooks();

        // Order matters!
        Core::Tasks::Scheduler::Shutdown();
        Core::Filesystem::FileWatcher::Shutdown();

        // Process material deletions before RenderOrchestrator destroys MaterialRegistry.
        if (m_RenderOrchestrator && m_GraphicsBackend)
        {
            auto& matSys = m_RenderOrchestrator->GetMaterialRegistry();
            matSys.ProcessDeletions(m_GraphicsBackend->GetDevice().GetGlobalFrameNumber());
        }

        if (m_SceneManager)
            m_SceneManager->Clear();
        if (m_AssetPipeline)
            m_AssetPipeline->GetAssetManager().Clear();

        if (m_AssetPipeline)
            m_AssetPipeline->ClearLoadedMaterials();

        // AssetIngestService borrows runtime subsystems; release it before tearing them down.
        m_AssetIngestService.reset();

        // RenderOrchestrator destructor handles: GPUScene, RenderDriver, PipelineLibrary,
        // MaterialRegistry, GeometryStorage, frame state.
        m_RenderOrchestrator.reset();

        // GUI-backed textures owned by render passes must be released before the ImGui backend
        // and descriptor pool are destroyed, but after RenderOrchestrator/RenderDriver shutdown.
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
        return m_SceneManager->SpawnModel(m_AssetPipeline->GetAssetManager(), modelHandle, materialHandle, position, scale);
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
        registerDescriptor(Graphics::FeatureCatalog::ShadowPass);
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
        registerDescriptor(Runtime::SystemFeatureCatalog::PrimitiveBVHBuild);
        registerDescriptor(Runtime::SystemFeatureCatalog::GraphLifecycle);
        registerDescriptor(Runtime::SystemFeatureCatalog::PointCloudLifecycle);
        registerDescriptor(Runtime::SystemFeatureCatalog::MeshViewLifecycle);
        registerDescriptor(Runtime::SystemFeatureCatalog::GPUSceneSync);
        registerDescriptor(Runtime::SystemFeatureCatalog::PropertySetDirtySync);
        for (const auto& preset : Runtime::SystemFeatureCatalog::GpuMemoryWarnThresholdPresets)
        {
            registerDescriptor(preset);
        }
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
        const VkExtent2D startupSwapchainExtent = m_GraphicsBackend->GetSwapchain().GetExtent();
        const bool startupFramebufferValid = startupFbWidth > 0 && startupFbHeight > 0;
        const bool startupExtentMismatch =
            startupFramebufferValid &&
            (startupSwapchainExtent.width != static_cast<uint32_t>(startupFbWidth) ||
             startupSwapchainExtent.height != static_cast<uint32_t>(startupFbHeight));

        if (startupExtentMismatch)
        {
            Core::Log::Info("Engine startup resize: framebuffer={}x{} swapchain={}x{}",
                            startupFbWidth,
                            startupFbHeight,
                            startupSwapchainExtent.width,
                            startupSwapchainExtent.height);
            m_GraphicsBackend->OnResize();
            m_RenderOrchestrator->OnResize();
        }
        else
        {
            Core::Log::Info("Engine startup resize: skipped (framebuffer={}x{}, swapchain={}x{}, validFb={})",
                            startupFbWidth,
                            startupFbHeight,
                            startupSwapchainExtent.width,
                            startupSwapchainExtent.height,
                            startupFramebufferValid);
        }

        // Bootstrap the normal resize path on first frame only when startup did
        // not already perform a concrete extent reconciliation. This avoids
        // redundant back-to-back swapchain recreation during startup.
        m_FramebufferResized = !startupExtentMismatch;

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
        ActivityTracker activityTracker{};
        activityTracker.Config = m_EngineConfig.FramePacing;
        // If FramePacing is disabled but MaxActiveFps is set, honour the legacy
        // field so existing callers keep working.
        if (!m_EngineConfig.FramePacing.Enabled && m_EngineConfig.MaxActiveFps > 0.0)
        {
            activityTracker.Config.Enabled = true;
            activityTracker.Config.ActiveFps = m_EngineConfig.MaxActiveFps;
            activityTracker.Config.IdleFps = 0.0; // no idle throttle, just cap
        }
        activityTracker.Reset();
        RuntimePlatformFrameHost platformFrameHost{*m_Window, m_FramebufferResized};
        PlatformFrameCoordinator platformFrame{
            .Host = platformFrameHost,
            .Clock = frameClock,
            .Activity = activityTracker.Config.Enabled ? &activityTracker : nullptr,
        };
        RuntimeResizeSyncHost resizeSyncHost{
            *m_GraphicsBackend,
            *m_RenderOrchestrator,
        };
        const ResizeSyncCoordinator resizeSync{.Host = resizeSyncHost};
        RuntimeStreamingLaneHost streamingLaneHost{
            m_AssetIngestService.get(),
            *m_AssetPipeline,
        };
        const StreamingLaneCoordinator streamingLane{.Host = streamingLaneHost};
        RuntimeMaintenanceLaneHost maintenanceLaneHost{
            *m_SceneManager,
            *m_RenderOrchestrator,
            *m_GraphicsBackend,
            m_FeatureRegistry,
        };
        const MaintenanceLaneCoordinator maintenanceLane{.Host = maintenanceLaneHost};
        RuntimeRenderLaneHost renderLaneHost{
            *m_SceneManager,
            *m_RenderOrchestrator,
            *m_GraphicsBackend,
            m_FeatureRegistry,
            m_AssetPipeline->GetAssetManager(),
        };
        const RenderLaneCoordinator renderLane{.Host = renderLaneHost};
        FrameLoopMode activeFrameLoopMode = ResolveFrameLoopMode(m_FeatureRegistry);
        Core::Log::Info("Engine::Run frame-loop mode: {}", ToString(activeFrameLoopMode));
        if (activityTracker.Config.Enabled)
        {
            Core::Log::Info("Engine frame pacing: active={:.0f} fps (0=VSync), idle={:.0f} fps, timeout={:.1f}s",
                            activityTracker.Config.ActiveFps,
                            activityTracker.Config.IdleFps,
                            activityTracker.Config.IdleTimeoutSeconds);
        }

        while (m_Running)
        {
            // Begin frame telemetry
            Core::Telemetry::TelemetrySystem::Get().BeginFrame();
            FrameGraphTimingTotals frameGraphTimings{};
            const FrameGraphExecutor executeGraph{
                .AssetManager = m_AssetPipeline->GetAssetManager(),
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
                    if (platformResult.ThreadViolation)
                    {
                        Core::Log::Error("Engine::Run aborting due to platform frame thread violation.");
                        m_Running = false;
                    }
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
                    const ResizeSyncResult resizeResult = resizeSync.Sync(platformResult);
                    const FramebufferExtent swapExtent = resizeResult.SwapchainExtentBefore;
                    const bool resizeRequested = resizeResult.ResizeRequested;
                    const bool framebufferExtentMismatch = resizeResult.FramebufferExtentMismatch;

                    // Monitor moves can change framebuffer extent/content scale without a reliable
                    // resize callback on every platform. Apply resize before per-frame update/render
                    // so picking, EntityId, and post-process overlays all use the current extent.
                    if (resizeResult.ResizeApplied)
                    {
                        Core::Log::Info(
                            "Engine resize trigger: framebuffer={}x{} swapchainBefore={}x{} resizeRequested={} mismatch={}",
                            fbWidth,
                            fbHeight,
                            swapExtent.Width,
                            swapExtent.Height,
                            resizeRequested,
                            framebufferExtentMismatch);
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
                (void)RunFramePhasesForMode(
                    frameLoopMode,
                    frameTime,
                    accumulator,
                    frameLoopPolicy,
                    streamingLane,
                    maintenanceLane,
                    renderLane,
                    m_RenderOrchestrator->GetFrameGraph(),
                    {
                        .OnFixedUpdate = [&](float fixedDeltaTime) { OnFixedUpdate(fixedDeltaTime); },
                        .RegisterFixedSystems = [&](Core::FrameGraph& graph, float fixedDeltaTime)
                        {
                            // Fixed-step lane is the authoritative deterministic update path.
                            // Register caller-owned simulation/gameplay systems first, then
                            // run the engine's core ECS deterministic systems (transforms,
                            // property dirty propagation, CPU-side primitive BVH sync).
                            OnRegisterFixedSystems(graph, fixedDeltaTime);
                            CoreFrameGraphRegistrationContext fixedContext{
                                .Graph = graph,
                                .Registry = m_SceneManager->GetRegistry(),
                                .Features = m_FeatureRegistry,
                            };
                            CoreFrameGraphSystemBundle{}.Register(fixedContext);
                        },
                        .CommitFixedTick = [&]() { m_SceneManager->CommitFixedTick(); },
                        .ExecuteFixedGraph = [&](Core::FrameGraph& graph) { executeGraph.Execute(graph); },
                        .Render =
                            {
                                // Resize is synchronized immediately after Window::OnUpdate() so render/update
                                // always see the current framebuffer extent on monitor moves and DPI changes.
                                .OnUpdate = [&](float dt) { OnUpdate(dt); },
                                .RegisterVariableSystems = [&](Core::FrameGraph& graph, float dt)
                                { OnRegisterSystems(graph, dt); },
                                .BeforeDispatch = [&]() { Runtime::PointCloudKMeans::PumpCompletions(*this); },
                                .OnRender = [&](double alpha) { OnRender(alpha); },
                            },
                        .ExecuteVariableGraph = [&](Core::FrameGraph& graph) { executeGraph.Execute(graph); },
                        .Timings = &frameGraphTimings,
                    });

                // End frame telemetry (simulation, task, and frame-graph stats are now
                // captured by the maintenance lane via CaptureFrameTelemetry).
                Core::Telemetry::TelemetrySystem::Get().EndFrame();

                // Benchmark capture: supports both CLI benchmark mode and
                // interactive editor-triggered runs.
                if (m_BenchmarkRunner.IsRunning())
                {
                    m_BenchmarkRunner.RecordFrame(Core::Telemetry::TelemetrySystem::Get());
                    if (m_BenchmarkRunner.IsComplete())
                    {
                        m_BenchmarkRunner.Finalize();
                        if (m_EngineConfig.BenchmarkMode)
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
