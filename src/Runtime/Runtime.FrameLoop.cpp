module;
#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <thread>
#include <utility>

#include "Core.Profiling.Macros.hpp"
#include "RHI.Vulkan.hpp"

module Runtime.FrameLoop;

import Core.Logging;
import Graphics.Camera;
import Graphics.RenderPipeline;
import Runtime.RenderExtraction;
import Runtime.SystemBundles;

namespace Runtime
{
    IPlatformFrameHost::~IPlatformFrameHost() = default;
    RuntimePlatformFrameHost::~RuntimePlatformFrameHost() = default;
    IResizeSyncHost::~IResizeSyncHost() = default;
    RuntimeResizeSyncHost::~RuntimeResizeSyncHost() = default;
    IStreamingLaneHost::~IStreamingLaneHost() = default;
    RuntimeStreamingLaneHost::~RuntimeStreamingLaneHost() = default;
    IMaintenanceLaneHost::~IMaintenanceLaneHost() = default;
    RuntimeMaintenanceLaneHost::~RuntimeMaintenanceLaneHost() = default;
    IRenderLaneHost::~IRenderLaneHost() = default;
    RuntimeRenderLaneHost::~RuntimeRenderLaneHost() = default;

    namespace
    {
        [[nodiscard]] FrameTelemetrySnapshot BuildTelemetrySnapshot(
            const FixedStepAdvanceResult& fixedStep,
            const FrameGraphTimingTotals* timings)
        {
            FrameTelemetrySnapshot snapshot{
                .FixedStepSubsteps = static_cast<uint32_t>(fixedStep.ExecutedSubsteps),
                .AccumulatorClamped = fixedStep.AccumulatorClamped,
                .SimulationCpuTimeNs = fixedStep.CpuTimeNs,
            };
            if (timings)
            {
                snapshot.FrameGraphCompileNs = timings->CompileNsTotal;
                snapshot.FrameGraphExecuteNs = timings->ExecuteNsTotal;
                snapshot.FrameGraphCriticalPathNs = timings->CriticalPathNsTotal;
            }
            return snapshot;
        }

        [[nodiscard]] FramePhaseRunResult RunFramePhasesStaged(double frameTime,
                                                               double& accumulator,
                                                               const FrameLoopPolicy& policy,
                                                               const StreamingLaneCoordinator& streamingLane,
                                                               const MaintenanceLaneCoordinator& maintenanceLane,
                                                               const RenderLaneCoordinator& renderLane,
                                                               Core::FrameGraph& fixedGraph,
                                                               FramePhaseCallbacks&& callbacks)
        {
            streamingLane.BeginFrame();

            FramePhaseRunResult result{
                .FixedStep = RunFixedSteps(accumulator,
                                           policy,
                                           std::move(callbacks.OnFixedUpdate),
                                           std::move(callbacks.RegisterFixedSystems),
                                           std::move(callbacks.CommitFixedTick),
                                           fixedGraph,
                                           std::move(callbacks.ExecuteFixedGraph)),
                .Mode = FrameLoopMode::StagedPhases,
            };

            const double alpha = ComputeRenderInterpolationAlpha(accumulator, policy);

            renderLane.Run(frameTime,
                           alpha,
                           std::move(callbacks.Render),
                           std::move(callbacks.ExecuteVariableGraph));

            maintenanceLane.Run(BuildTelemetrySnapshot(result.FixedStep, callbacks.Timings));
            return result;
        }

        [[nodiscard]] FramePhaseRunResult RunFramePhasesLegacyCompatibility(double frameTime,
                                                                            double& accumulator,
                                                                            const FrameLoopPolicy& policy,
                                                                            const StreamingLaneCoordinator& streamingLane,
                                                                            const MaintenanceLaneCoordinator& maintenanceLane,
                                                                            const RenderLaneCoordinator& renderLane,
                                                                            Core::FrameGraph& fixedGraph,
                                                                            FramePhaseCallbacks&& callbacks)
        {
            // Temporary rollback shim: preserve the pre-cutover frame order while the staged
            // frame-pipeline migration lands incrementally. Delete after the migration window.
            streamingLane.BeginFrame();

            const FixedStepAdvanceResult fixedStep = RunFixedSteps(accumulator,
                                                                   policy,
                                                                   std::move(callbacks.OnFixedUpdate),
                                                                   std::move(callbacks.RegisterFixedSystems),
                                                                   std::move(callbacks.CommitFixedTick),
                                                                   fixedGraph,
                                                                   std::move(callbacks.ExecuteFixedGraph));

            const double alpha = ComputeRenderInterpolationAlpha(accumulator, policy);

            renderLane.Run(frameTime,
                           alpha,
                           std::move(callbacks.Render),
                           std::move(callbacks.ExecuteVariableGraph));

            maintenanceLane.Run(BuildTelemetrySnapshot(fixedStep, callbacks.Timings));
            return FramePhaseRunResult{
                .FixedStep = fixedStep,
                .Mode = FrameLoopMode::LegacyCompatibility,
            };
        }
    }

    // Forward declaration — defined further down alongside FrameClock.
    [[nodiscard]] static double SampleFrameClockSeconds();

    // ---------------------------------------------------------------------------
    // ActivityTracker
    // ---------------------------------------------------------------------------

    void ActivityTracker::NoteActivity() &
    {
        m_HasActivity = true;
        m_LastActivityTimeSeconds = SampleFrameClockSeconds();
    }

    double ActivityTracker::EffectiveFpsCap(double currentTimeSeconds) const &
    {
        if (!Config.Enabled || Config.IdleFps <= 0.0)
            return Config.ActiveFps;

        const double idleDuration = currentTimeSeconds - m_LastActivityTimeSeconds;
        if (!m_HasActivity || idleDuration >= Config.IdleTimeoutSeconds)
            return Config.IdleFps;

        return Config.ActiveFps;
    }

    bool ActivityTracker::IsIdle(double currentTimeSeconds) const &
    {
        if (!Config.Enabled || Config.IdleFps <= 0.0)
            return false;

        const double idleDuration = currentTimeSeconds - m_LastActivityTimeSeconds;
        return !m_HasActivity || idleDuration >= Config.IdleTimeoutSeconds;
    }

    void ActivityTracker::Reset() &
    {
        m_LastActivityTimeSeconds = SampleFrameClockSeconds();
        m_HasActivity = true;
    }

    FrameLoopMode ResolveFrameLoopMode(const Core::FeatureRegistry& features)
    {
        if (features.IsEnabled(FrameLoopFeatureCatalog::LegacyCompatibility))
        {
            return FrameLoopMode::LegacyCompatibility;
        }

        return FrameLoopMode::StagedPhases;
    }

    const char* ToString(FrameLoopMode mode)
    {
        switch (mode)
        {
        case FrameLoopMode::LegacyCompatibility:
            return "LegacyCompatibility";
        case FrameLoopMode::StagedPhases:
            return "StagedPhases";
        }

        return "Unknown";
    }

    FrameLoopPolicy MakeFrameLoopPolicy(double fixedStepHz,
                                        double maxFrameDeltaSeconds,
                                        int maxSubstepsPerFrame)
    {
        const double sanitizedFixedStepHz =
            std::isfinite(fixedStepHz) && fixedStepHz > 0.0 ? fixedStepHz : DefaultFixedStepHz;
        const double sanitizedMaxFrameDelta =
            std::isfinite(maxFrameDeltaSeconds) && maxFrameDeltaSeconds > 0.0
                ? maxFrameDeltaSeconds
                : DefaultMaxFrameDeltaSeconds;
        const int sanitizedMaxSubsteps =
            maxSubstepsPerFrame > 0 ? maxSubstepsPerFrame : DefaultMaxSubstepsPerFrame;

        return FrameLoopPolicy{
            .FixedDt = 1.0 / sanitizedFixedStepHz,
            .MaxFrameDelta = sanitizedMaxFrameDelta,
            .MaxSubstepsPerFrame = sanitizedMaxSubsteps,
        };
    }

    FrameTimeStep ComputeFrameTime(double rawFrameTime, const FrameLoopPolicy& policy)
    {
        const double sanitized = std::clamp(rawFrameTime, 0.0, policy.MaxFrameDelta);
        return FrameTimeStep{
            .FrameTime = sanitized,
            .Clamped = sanitized != rawFrameTime,
        };
    }

    double ComputeRenderInterpolationAlpha(double accumulator, const FrameLoopPolicy& policy)
    {
        if (!std::isfinite(accumulator) || !std::isfinite(policy.FixedDt) || policy.FixedDt <= 0.0)
            return 0.0;

        return std::clamp(accumulator / policy.FixedDt, 0.0, 1.0);
    }

    [[nodiscard]] static double SampleFrameClockSeconds()
    {
        using Seconds = std::chrono::duration<double>;
        return std::chrono::duration_cast<Seconds>(
                   std::chrono::high_resolution_clock::now().time_since_epoch())
            .count();
    }

    void FrameClock::Reset()
    {
        m_LastSampleSeconds = SampleFrameClockSeconds();
        m_HasLastSample = false;
    }

    void FrameClock::Resample() &
    {
        m_LastSampleSeconds = SampleFrameClockSeconds();
    }

    FrameTimeStep FrameClock::Advance(const FrameLoopPolicy& policy) &
    {
        const double nowSeconds = SampleFrameClockSeconds();
        if (!m_HasLastSample)
        {
            m_LastSampleSeconds = nowSeconds;
            m_HasLastSample = true;
            return {};
        }

        const double rawFrameTime = nowSeconds - m_LastSampleSeconds;
        m_LastSampleSeconds = nowSeconds;
        return ComputeFrameTime(rawFrameTime, policy);
    }

    FixedStepAdvanceResult RunFixedSteps(double& accumulator,
                                         const FrameLoopPolicy& policy,
                                         FixedUpdateFn&& onFixedUpdate,
                                         RegisterFixedSystemsFn&& registerFixedSystems,
                                         CommitFixedTickFn&& commitFixedTick,
                                         Core::FrameGraph& fixedGraph,
                                         ExecuteGraphFn&& executeGraph)
    {
        PROFILE_SCOPE("FixedStep");

        const auto startTime = std::chrono::high_resolution_clock::now();
        FixedStepAdvanceResult result{};
        while (accumulator >= policy.FixedDt && result.ExecutedSubsteps < policy.MaxSubstepsPerFrame)
        {
            {
                PROFILE_SCOPE("OnFixedUpdate");
                onFixedUpdate(static_cast<float>(policy.FixedDt));
            }

            {
                PROFILE_SCOPE("FixedStepFrameGraph");
                fixedGraph.Reset();
                registerFixedSystems(fixedGraph, static_cast<float>(policy.FixedDt));
                executeGraph(fixedGraph);
            }

            if (commitFixedTick)
            {
                PROFILE_SCOPE("CommitFixedTick");
                commitFixedTick();
            }

            accumulator -= policy.FixedDt;
            ++result.ExecutedSubsteps;
        }

        if (result.ExecutedSubsteps == policy.MaxSubstepsPerFrame && accumulator >= policy.FixedDt)
        {
            accumulator = 0.0;
            result.AccumulatorClamped = true;
        }

        result.CpuTimeNs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - startTime)
                .count());

        return result;
    }

    void FrameGraphExecutor::Execute(this const FrameGraphExecutor& self, Core::FrameGraph& graph)
    {
        const auto compileResult = graph.Compile();
        self.Timings.CompileNsTotal += graph.GetLastCompileTimeNs();
        if (!compileResult)
        {
            return;
        }

        self.AssetManager.BeginReadPhase();
        graph.Execute();
        self.AssetManager.EndReadPhase();

        self.Timings.ExecuteNsTotal += graph.GetLastExecuteTimeNs();
        self.Timings.CriticalPathNsTotal += graph.GetLastCriticalPathTimeNs();
    }

    void RuntimePlatformFrameHost::PumpEvents()
    {
        m_Window.OnUpdate();
    }

    bool RuntimePlatformFrameHost::ShouldQuit() const
    {
        return m_Window.ShouldClose();
    }

    bool RuntimePlatformFrameHost::IsMinimized() const
    {
        return m_Window.IsMinimized();
    }

    void RuntimePlatformFrameHost::WaitForEventsOrTimeout(double timeoutSeconds)
    {
        m_Window.WaitForEventsTimeout(timeoutSeconds);
    }

    int RuntimePlatformFrameHost::GetFramebufferWidth() const
    {
        return m_Window.GetFramebufferWidth();
    }

    int RuntimePlatformFrameHost::GetFramebufferHeight() const
    {
        return m_Window.GetFramebufferHeight();
    }

    bool RuntimePlatformFrameHost::HasResizeRequest() const
    {
        return m_ResizeRequested;
    }

    bool RuntimePlatformFrameHost::ConsumeResizeRequest()
    {
        return std::exchange(m_ResizeRequested, false);
    }

    bool RuntimePlatformFrameHost::ConsumeInputActivity()
    {
        return m_Window.ConsumeInputActivity();
    }

    PlatformFrameResult PlatformFrameCoordinator::BeginFrame(this PlatformFrameCoordinator& self,
                                                             const FrameLoopPolicy& policy)
    {
        if (self.OwningThread != std::this_thread::get_id())
        {
            Core::Log::Error(
                "PlatformFrameCoordinator::BeginFrame must run on the owning main thread; refusing cross-thread event pumping.");
            self.Clock.Reset();
            return PlatformFrameResult{
                .ContinueFrame = false,
                .ThreadViolation = true,
            };
        }

        self.Host.PumpEvents();

        // Feed input activity into the idle tracker (if present).
        if (self.Activity && self.Host.ConsumeInputActivity())
            self.Activity->NoteActivity();

        const bool resizeRequested = self.Host.HasResizeRequest();
        if (self.Host.ShouldQuit())
        {
            self.Clock.Reset();
            return PlatformFrameResult{
                .ContinueFrame = false,
                .ShouldQuit = true,
                .ResizeRequested = resizeRequested,
                .FramebufferWidth = self.Host.GetFramebufferWidth(),
                .FramebufferHeight = self.Host.GetFramebufferHeight(),
            };
        }

        if (!self.Host.IsMinimized())
        {
            FrameTimeStep frameStep = self.Clock.Advance(policy);

            // Determine effective FPS cap from the activity tracker.
            const double nowSeconds = SampleFrameClockSeconds();
            const double effectiveFpsCap =
                self.Activity ? self.Activity->EffectiveFpsCap(nowSeconds) : 0.0;

            if (effectiveFpsCap > 0.0 && std::isfinite(effectiveFpsCap))
            {
                const double minFrameSeconds = 1.0 / effectiveFpsCap;
                if (frameStep.FrameTime > 0.0 && frameStep.FrameTime < minFrameSeconds)
                {
                    self.SleepForSeconds(minFrameSeconds - frameStep.FrameTime);
                    // Re-anchor the clock so the next Advance() does not count
                    // the sleep as part of the following frame's time.
                    self.Clock.Resample();
                    frameStep.FrameTime = minFrameSeconds;
                }
            }

            self.Host.ConsumeResizeRequest();
            return PlatformFrameResult{
                .ContinueFrame = true,
                .Minimized = false,
                .ResizeRequested = resizeRequested,
                .FramebufferWidth = self.Host.GetFramebufferWidth(),
                .FramebufferHeight = self.Host.GetFramebufferHeight(),
                .FrameStep = frameStep,
            };
        }

        self.Host.WaitForEventsOrTimeout(self.MinimizedWaitSeconds);
        self.Clock.Reset();
        return PlatformFrameResult{
            .ContinueFrame = false,
            .Minimized = true,
            .ResizeRequested = resizeRequested,
            .FramebufferWidth = self.Host.GetFramebufferWidth(),
            .FramebufferHeight = self.Host.GetFramebufferHeight(),
        };
    }

    FramebufferExtent RuntimeResizeSyncHost::GetSwapchainExtent() const
    {
        const VkExtent2D extent = m_Graphics.GetSwapchain().GetExtent();
        return FramebufferExtent{
            .Width = extent.width,
            .Height = extent.height,
        };
    }

    void RuntimeResizeSyncHost::ApplyResize()
    {
        m_Graphics.OnResize();
        m_Renderer.OnResize();
    }

    ResizeSyncResult ResizeSyncCoordinator::Sync(const PlatformFrameResult& platformFrame) const
    {
        if (!platformFrame.ContinueFrame || platformFrame.FramebufferWidth <= 0 ||
            platformFrame.FramebufferHeight <= 0)
        {
            return ResizeSyncResult{};
        }

        const FramebufferExtent swapExtent = Host.GetSwapchainExtent();
        const bool framebufferExtentMismatch =
            swapExtent.Width != static_cast<uint32_t>(platformFrame.FramebufferWidth) ||
            swapExtent.Height != static_cast<uint32_t>(platformFrame.FramebufferHeight);
        const bool resizeRequested = platformFrame.ResizeRequested || framebufferExtentMismatch;

        if (resizeRequested)
        {
            Host.ApplyResize();
        }

        return ResizeSyncResult{
            .SwapchainExtentBefore = swapExtent,
            .ResizeRequested = resizeRequested,
            .FramebufferExtentMismatch = framebufferExtentMismatch,
            .ResizeApplied = resizeRequested,
        };
    }

    void RuntimeStreamingLaneHost::ProcessAssetIngest()
    {
        if (m_Ingest)
            m_Ingest->PumpStreamingStateMachine();
    }

    void RuntimeStreamingLaneHost::ProcessMainThreadQueue()
    {
        m_Assets.ProcessMainThreadQueue();
    }

    void RuntimeStreamingLaneHost::ProcessUploads()
    {
        m_Assets.ProcessUploads();
    }

    void StreamingLaneCoordinator::BeginFrame(this const StreamingLaneCoordinator& self)
    {
        self.Host.ProcessAssetIngest();
        self.Host.ProcessMainThreadQueue();

        {
            PROFILE_SCOPE("ProcessUploads");
            self.Host.ProcessUploads();
        }
    }

    void RuntimeMaintenanceLaneHost::CollectGpuDeferredDestructions()
    {
        m_Maintenance.CollectGpuDeferredDestructions();
    }

    void RuntimeMaintenanceLaneHost::CaptureGpuSyncState()
    {
        m_Maintenance.CaptureGpuSyncState();
    }

    void RuntimeMaintenanceLaneHost::ProcessCompletedReadbacks()
    {
        m_Maintenance.ProcessCompletedReadbacks();
    }

    void RuntimeMaintenanceLaneHost::GarbageCollectTransfers()
    {
        m_Maintenance.GarbageCollectTransfers();
    }

    void RuntimeMaintenanceLaneHost::ProcessTextureDeletions()
    {
        m_Maintenance.ProcessTextureDeletions();
    }

    void RuntimeMaintenanceLaneHost::ProcessMaterialDeletions()
    {
        m_Maintenance.ProcessMaterialDeletions();
    }

    void RuntimeMaintenanceLaneHost::CaptureFrameTelemetry(const FrameTelemetrySnapshot& snapshot)
    {
        m_Maintenance.CaptureFrameTelemetry(snapshot);
    }

    void RuntimeMaintenanceLaneHost::BookkeepHotReloads()
    {
        m_Maintenance.BookkeepHotReloads();
    }

    void MaintenanceLaneCoordinator::Run(this const MaintenanceLaneCoordinator& self,
                                         const FrameTelemetrySnapshot& telemetry)
    {
        self.Host.CaptureGpuSyncState();
        self.Host.ProcessCompletedReadbacks();
        self.Host.CollectGpuDeferredDestructions();
        self.Host.GarbageCollectTransfers();
        self.Host.ProcessTextureDeletions();
        self.Host.ProcessMaterialDeletions();
        self.Host.CaptureFrameTelemetry(telemetry);
        self.Host.BookkeepHotReloads();
    }

    Core::FrameGraph& RuntimeRenderLaneHost::GetFrameGraph()
    {
        return m_Renderer.GetFrameGraph();
    }

    void RuntimeRenderLaneHost::RegisterEngineSystems(Core::FrameGraph& graph)
    {
        auto& registry = m_Scene.GetRegistry();

        CoreFrameGraphRegistrationContext coreBundleContext{
            .Graph = graph,
            .Registry = registry,
            .Features = m_Features,
        };

        auto* gpuScene = m_Renderer.GetGPUScenePtr();
        if (gpuScene)
        {
            GpuFrameGraphRegistrationContext gpuBundleContext{
                .Core = coreBundleContext,
                .GpuScene = *gpuScene,
                .AssetManager = m_Assets,
                .MaterialRegistry = m_Renderer.GetMaterialRegistry(),
                .GeometryStorage = m_Renderer.GetGeometryStorage(),
                .Device = m_Graphics.GetDeviceShared(),
                .TransferManager = m_Graphics.GetTransferManager(),
                .Dispatcher = m_Scene.GetScene().GetDispatcher(),
                .DefaultTextureId = m_Graphics.GetDefaultTextureIndex(),
            };
            GpuFrameGraphSystemBundle{}.Register(gpuBundleContext);
            return;
        }
    }

    void RuntimeRenderLaneHost::DispatchDeferredEvents()
    {
        m_Scene.GetScene().GetDispatcher().update();
    }

    std::optional<RenderWorld> RuntimeRenderLaneHost::ExtractRenderWorld(double alpha)
    {
        // Prepare the editor overlay (ImGui frame) before extraction so that
        // ImGui draw data is finalized and part of the immutable RenderWorld.
        const Graphics::EditorOverlayPacket overlay = m_Renderer.PrepareEditorOverlay();

        auto view = m_Scene.GetRegistry().view<Graphics::CameraComponent>();
        if (view.empty())
            return std::nullopt;
        auto it = view.begin();

        const Graphics::CameraComponent& camera = view.get<Graphics::CameraComponent>(*it);
        const auto extent = m_Graphics.GetSwapchain().GetExtent();

        const RenderFrameInput renderInput = MakeRenderFrameInput(
            camera,
            m_Scene.CreateReadonlySnapshot(),
            RenderViewport{
                .Width = extent.width,
                .Height = extent.height,
            },
            alpha);
        if (!renderInput.IsValid())
            return std::nullopt;

        RenderWorld world = m_Renderer.ExtractRenderWorld(renderInput);
        world.EditorOverlay = overlay;
        return world;
    }

    void RuntimeRenderLaneHost::ExecutePreparedFrame(RenderWorld renderWorld)
    {
        FrameContext& frame = m_Renderer.BeginFrame();
        m_Renderer.PrepareFrame(frame, std::move(renderWorld));
        m_Renderer.ExecuteFrame(frame);
        m_Renderer.EndFrame(frame);
    }

    void RenderLaneCoordinator::Run(this const RenderLaneCoordinator& self,
                                    double frameTime,
                                    double alpha,
                                    RenderLaneCallbacks&& callbacks,
                                    ExecuteGraphFn&& executeGraph)
    {
        {
            PROFILE_SCOPE("OnUpdate");
            callbacks.OnUpdate(static_cast<float>(frameTime));
        }

        {
            PROFILE_SCOPE("FrameGraph");
            auto& frameGraph = self.Host.GetFrameGraph();
            frameGraph.Reset();

            const float frameDt = static_cast<float>(frameTime);

            callbacks.RegisterVariableSystems(frameGraph, frameDt);
            self.Host.RegisterEngineSystems(frameGraph);

            executeGraph(frameGraph);
        }

        if (callbacks.BeforeDispatch)
            callbacks.BeforeDispatch();

        self.Host.DispatchDeferredEvents();
        std::optional<RenderWorld> renderWorld = self.Host.ExtractRenderWorld(alpha);

        {
            PROFILE_SCOPE("OnRender");
            callbacks.OnRender(alpha);
            if (renderWorld)
                self.Host.ExecutePreparedFrame(std::move(*renderWorld));
        }
    }

    FramePhaseRunResult RunFramePhases(double frameTime,
                                       double& accumulator,
                                       const FrameLoopPolicy& policy,
                                       const StreamingLaneCoordinator& streamingLane,
                                       const MaintenanceLaneCoordinator& maintenanceLane,
                                       const RenderLaneCoordinator& renderLane,
                                       Core::FrameGraph& fixedGraph,
                                       FramePhaseCallbacks&& callbacks)
    {
        return RunFramePhasesForMode(FrameLoopMode::StagedPhases,
                                     frameTime,
                                     accumulator,
                                     policy,
                                     streamingLane,
                                     maintenanceLane,
                                     renderLane,
                                     fixedGraph,
                                     std::move(callbacks));
    }

    FramePhaseRunResult RunFramePhasesForMode(FrameLoopMode mode,
                                              double frameTime,
                                              double& accumulator,
                                              const FrameLoopPolicy& policy,
                                              const StreamingLaneCoordinator& streamingLane,
                                              const MaintenanceLaneCoordinator& maintenanceLane,
                                              const RenderLaneCoordinator& renderLane,
                                              Core::FrameGraph& fixedGraph,
                                              FramePhaseCallbacks&& callbacks)
    {
        switch (mode)
        {
        case FrameLoopMode::LegacyCompatibility:
            return RunFramePhasesLegacyCompatibility(frameTime,
                                                     accumulator,
                                                     policy,
                                                     streamingLane,
                                                     maintenanceLane,
                                                     renderLane,
                                                     fixedGraph,
                                                     std::move(callbacks));
        case FrameLoopMode::StagedPhases:
            return RunFramePhasesStaged(frameTime,
                                        accumulator,
                                        policy,
                                        streamingLane,
                                        maintenanceLane,
                                        renderLane,
                                        fixedGraph,
                                        std::move(callbacks));
        }

        std::unreachable();
    }
}
