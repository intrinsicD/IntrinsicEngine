module;
#include <algorithm>
#include <cstdint>

#include "Core.Profiling.Macros.hpp"

module Runtime.FrameLoop;

import Runtime.SystemBundles;

namespace Runtime
{
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

    void StreamingLaneCoordinator::BeginFrame(this const StreamingLaneCoordinator& self)
    {
        self.Assets.ProcessMainThreadQueue();

        {
            PROFILE_SCOPE("ProcessUploads");
            self.Assets.ProcessUploads();
        }

        self.Graphics.ProcessTextureDeletions();
        self.Materials.ProcessDeletions(self.Graphics.GetDevice().GetGlobalFrameNumber());
    }

    void StreamingLaneCoordinator::EndFrame(this const StreamingLaneCoordinator& self)
    {
        self.Graphics.GarbageCollectTransfers();
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
            auto& frameGraph = self.Renderer.GetFrameGraph();
            frameGraph.Reset();

            auto& registry = self.Scene.GetRegistry();
            const float frameDt = static_cast<float>(frameTime);

            callbacks.RegisterVariableSystems(frameGraph, frameDt);

            CoreFrameGraphRegistrationContext coreBundleContext{
                .Graph = frameGraph,
                .Registry = registry,
                .Features = self.Features,
            };
            CoreFrameGraphSystemBundle{}.Register(coreBundleContext);

            auto* gpuScene = self.Renderer.GetGPUScenePtr();
            if (gpuScene)
            {
                GpuFrameGraphRegistrationContext gpuBundleContext{
                    .Core = coreBundleContext,
                    .GpuScene = *gpuScene,
                    .AssetManager = self.Assets,
                    .MaterialSystem = self.Renderer.GetMaterialSystem(),
                    .GeometryStorage = self.Renderer.GetGeometryStorage(),
                    .Device = self.Graphics.GetDeviceShared(),
                    .TransferManager = self.Graphics.GetTransferManager(),
                    .Dispatcher = self.Scene.GetScene().GetDispatcher(),
                    .DefaultTextureId = self.Graphics.GetDefaultTextureIndex(),
                };
                GpuFrameGraphSystemBundle{}.Register(gpuBundleContext);
            }

            executeGraph(frameGraph);
        }

        self.Scene.GetScene().GetDispatcher().update();

        {
            PROFILE_SCOPE("OnRender");
            callbacks.OnRender();
        }
    }
}
