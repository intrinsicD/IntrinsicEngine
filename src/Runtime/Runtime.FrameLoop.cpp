module;
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>
#include <utility>

#include "Core.Profiling.Macros.hpp"

module Runtime.FrameLoop;

import Core.Logging;
import Runtime.SystemBundles;

namespace Runtime
{
    IPlatformFrameHost::~IPlatformFrameHost() = default;
    RuntimePlatformFrameHost::~RuntimePlatformFrameHost() = default;
    IStreamingLaneHost::~IStreamingLaneHost() = default;
    RuntimeStreamingLaneHost::~RuntimeStreamingLaneHost() = default;
    IRenderLaneHost::~IRenderLaneHost() = default;
    RuntimeRenderLaneHost::~RuntimeRenderLaneHost() = default;

    namespace
    {
        [[nodiscard]] FramePhaseRunResult RunFramePhasesStaged(double frameTime,
                                                               double& accumulator,
                                                               const FrameLoopPolicy& policy,
                                                               const StreamingLaneCoordinator& streamingLane,
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
                                           fixedGraph,
                                           std::move(callbacks.ExecuteFixedGraph)),
                .Mode = FrameLoopMode::StagedPhases,
            };

            renderLane.Run(frameTime,
                           std::move(callbacks.Render),
                           std::move(callbacks.ExecuteVariableGraph));

            streamingLane.EndFrame();
            return result;
        }

        [[nodiscard]] FramePhaseRunResult RunFramePhasesLegacyCompatibility(double frameTime,
                                                                            double& accumulator,
                                                                            const FrameLoopPolicy& policy,
                                                                            const StreamingLaneCoordinator& streamingLane,
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
                                                                   fixedGraph,
                                                                   std::move(callbacks.ExecuteFixedGraph));

            renderLane.Run(frameTime,
                           std::move(callbacks.Render),
                           std::move(callbacks.ExecuteVariableGraph));

            streamingLane.EndFrame();
            return FramePhaseRunResult{
                .FixedStep = fixedStep,
                .Mode = FrameLoopMode::LegacyCompatibility,
            };
        }
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

    FrameTimeStep ComputeFrameTime(double rawFrameTime, const FrameLoopPolicy& policy)
    {
        const double sanitized = std::clamp(rawFrameTime, 0.0, policy.MaxFrameDelta);
        return FrameTimeStep{
            .FrameTime = sanitized,
            .Clamped = sanitized != rawFrameTime,
        };
    }

    namespace
    {
        [[nodiscard]] double SampleFrameClockSeconds()
        {
            using Seconds = std::chrono::duration<double>;
            return std::chrono::duration_cast<Seconds>(
                       std::chrono::high_resolution_clock::now().time_since_epoch())
                .count();
        }
    }

    void FrameClock::Reset()
    {
        m_LastSampleSeconds = SampleFrameClockSeconds();
        m_HasLastSample = false;
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

    bool RuntimePlatformFrameHost::ConsumeResizeRequest()
    {
        return std::exchange(m_ResizeRequested, false);
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
        if (self.Host.ShouldQuit())
        {
            self.Clock.Reset();
            return PlatformFrameResult{
                .ContinueFrame = false,
                .ShouldQuit = true,
                .ResizeRequested = self.Host.ConsumeResizeRequest(),
                .FramebufferWidth = self.Host.GetFramebufferWidth(),
                .FramebufferHeight = self.Host.GetFramebufferHeight(),
            };
        }

        if (!self.Host.IsMinimized())
        {
            return PlatformFrameResult{
                .ContinueFrame = true,
                .Minimized = false,
                .ResizeRequested = self.Host.ConsumeResizeRequest(),
                .FramebufferWidth = self.Host.GetFramebufferWidth(),
                .FramebufferHeight = self.Host.GetFramebufferHeight(),
                .FrameStep = self.Clock.Advance(policy),
            };
        }

        self.Host.WaitForEventsOrTimeout(self.MinimizedWaitSeconds);
        self.Clock.Reset();
        return PlatformFrameResult{
            .ContinueFrame = false,
            .Minimized = true,
            .ResizeRequested = self.Host.ConsumeResizeRequest(),
            .FramebufferWidth = self.Host.GetFramebufferWidth(),
            .FramebufferHeight = self.Host.GetFramebufferHeight(),
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

    void RuntimeStreamingLaneHost::ProcessTextureDeletions()
    {
        m_Graphics.ProcessTextureDeletions();
    }

    void RuntimeStreamingLaneHost::ProcessMaterialDeletions()
    {
        m_Materials.ProcessDeletions(m_Graphics.GetDevice().GetGlobalFrameNumber());
    }

    void RuntimeStreamingLaneHost::GarbageCollectTransfers()
    {
        m_Graphics.GarbageCollectTransfers();
    }

    void StreamingLaneCoordinator::BeginFrame(this const StreamingLaneCoordinator& self)
    {
        self.Host.ProcessAssetIngest();
        self.Host.ProcessMainThreadQueue();

        {
            PROFILE_SCOPE("ProcessUploads");
            self.Host.ProcessUploads();
        }

        self.Host.ProcessTextureDeletions();
        self.Host.ProcessMaterialDeletions();
    }

    void StreamingLaneCoordinator::EndFrame(this const StreamingLaneCoordinator& self)
    {
        self.Host.GarbageCollectTransfers();
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
                .MaterialSystem = m_Renderer.GetMaterialSystem(),
                .GeometryStorage = m_Renderer.GetGeometryStorage(),
                .Device = m_Graphics.GetDeviceShared(),
                .TransferManager = m_Graphics.GetTransferManager(),
                .Dispatcher = m_Scene.GetScene().GetDispatcher(),
                .DefaultTextureId = m_Graphics.GetDefaultTextureIndex(),
            };
            VariableFrameGraphSystemBundle{}.Register(coreBundleContext, &gpuBundleContext);
            return;
        }

        VariableFrameGraphSystemBundle{}.Register(coreBundleContext);
    }

    void RuntimeRenderLaneHost::DispatchDeferredEvents()
    {
        m_Scene.GetScene().GetDispatcher().update();
    }

    void RenderLaneCoordinator::Run(this const RenderLaneCoordinator& self,
                                    double frameTime,
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

        {
            PROFILE_SCOPE("OnRender");
            callbacks.OnRender();
        }
    }

    FramePhaseRunResult RunFramePhases(double frameTime,
                                       double& accumulator,
                                       const FrameLoopPolicy& policy,
                                       const StreamingLaneCoordinator& streamingLane,
                                       const RenderLaneCoordinator& renderLane,
                                       Core::FrameGraph& fixedGraph,
                                       FramePhaseCallbacks&& callbacks)
    {
        return RunFramePhasesForMode(FrameLoopMode::StagedPhases,
                                     frameTime,
                                     accumulator,
                                     policy,
                                     streamingLane,
                                     renderLane,
                                     fixedGraph,
                                     std::move(callbacks));
    }

    FramePhaseRunResult RunFramePhasesForMode(FrameLoopMode mode,
                                              double frameTime,
                                              double& accumulator,
                                              const FrameLoopPolicy& policy,
                                              const StreamingLaneCoordinator& streamingLane,
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
                                                     renderLane,
                                                     fixedGraph,
                                                     std::move(callbacks));
        case FrameLoopMode::StagedPhases:
            return RunFramePhasesStaged(frameTime,
                                        accumulator,
                                        policy,
                                        streamingLane,
                                        renderLane,
                                        fixedGraph,
                                        std::move(callbacks));
        }

        return RunFramePhasesStaged(frameTime,
                                    accumulator,
                                    policy,
                                    streamingLane,
                                    renderLane,
                                    fixedGraph,
                                    std::move(callbacks));
    }
}
