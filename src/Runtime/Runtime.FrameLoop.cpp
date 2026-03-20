module;
#include <algorithm>
#include <cstdint>

#include "Core.Profiling.Macros.hpp"

module Runtime.FrameLoop;

import Runtime.SystemBundles;

namespace Runtime
{
    IStreamingLaneHost::~IStreamingLaneHost() = default;
    RuntimeStreamingLaneHost::~RuntimeStreamingLaneHost() = default;
    IRenderLaneHost::~IRenderLaneHost() = default;
    RuntimeRenderLaneHost::~RuntimeRenderLaneHost() = default;

    FrameTimeStep ComputeFrameTime(double rawFrameTime, const FrameLoopPolicy& policy)
    {
        const double sanitized = std::clamp(rawFrameTime, 0.0, policy.MaxFrameDelta);
        return FrameTimeStep{
            .FrameTime = sanitized,
            .Clamped = sanitized != rawFrameTime,
        };
    }

    FixedStepAdvanceResult RunFixedSteps(double& accumulator,
                                         const FrameLoopPolicy& policy,
                                         FixedUpdateFn&& onFixedUpdate,
                                         RegisterFixedSystemsFn&& registerFixedSystems,
                                         Core::FrameGraph& fixedGraph,
                                         ExecuteGraphFn&& executeGraph)
    {
        PROFILE_SCOPE("FixedStep");

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
        CoreFrameGraphSystemBundle{}.Register(coreBundleContext);

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
            GpuFrameGraphSystemBundle{}.Register(gpuBundleContext);
        }
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
        streamingLane.BeginFrame();

        FramePhaseRunResult result{
            .FixedStep = RunFixedSteps(accumulator,
                                       policy,
                                       std::move(callbacks.OnFixedUpdate),
                                       std::move(callbacks.RegisterFixedSystems),
                                       fixedGraph,
                                       std::move(callbacks.ExecuteFixedGraph)),
        };

        renderLane.Run(frameTime,
                       std::move(callbacks.Render),
                       std::move(callbacks.ExecuteVariableGraph));

        streamingLane.EndFrame();
        return result;
    }
}
