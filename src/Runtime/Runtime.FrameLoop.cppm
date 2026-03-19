module;
#include <cstdint>

export module Runtime.FrameLoop;

import Core.Assets;
import Core.FeatureRegistry;
import Core.FrameGraph;
import Core.InplaceFunction;
import Graphics;
import Runtime.AssetPipeline;
import Runtime.GraphicsBackend;
import Runtime.RenderOrchestrator;
import Runtime.SceneManager;

export namespace Runtime
{
    struct FrameLoopPolicy
    {
        double FixedDt = 1.0 / 60.0;
        double MaxFrameDelta = 0.25;
        int MaxSubstepsPerFrame = 8;
    };

    struct FrameTimeStep
    {
        double FrameTime = 0.0;
        bool Clamped = false;
    };

    [[nodiscard]] FrameTimeStep ComputeFrameTime(double rawFrameTime,
                                                 const FrameLoopPolicy& policy = {});

    struct FixedStepAdvanceResult
    {
        int ExecutedSubsteps = 0;
        bool AccumulatorClamped = false;
    };

    using ExecuteGraphFn = Core::InplaceFunction<void(Core::FrameGraph&), 96>;
    using FixedUpdateFn = Core::InplaceFunction<void(float), 64>;
    using RegisterFixedSystemsFn = Core::InplaceFunction<void(Core::FrameGraph&, float), 96>;
    using VariableUpdateFn = Core::InplaceFunction<void(float), 64>;
    using RegisterVariableSystemsFn = Core::InplaceFunction<void(Core::FrameGraph&, float), 96>;
    using RenderHookFn = Core::InplaceFunction<void(), 64>;

    [[nodiscard]] FixedStepAdvanceResult RunFixedSteps(double& accumulator,
                                                       const FrameLoopPolicy& policy,
                                                       FixedUpdateFn&& onFixedUpdate,
                                                       RegisterFixedSystemsFn&& registerFixedSystems,
                                                       Core::FrameGraph& fixedGraph,
                                                       ExecuteGraphFn&& executeGraph);

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

        void Execute(this const FrameGraphExecutor&, Core::FrameGraph& graph);
    };

    struct StreamingLaneCoordinator
    {
        AssetPipeline& Assets;
        GraphicsBackend& Graphics;
        Graphics::MaterialSystem& Materials;

        void BeginFrame(this const StreamingLaneCoordinator&);
        void EndFrame(this const StreamingLaneCoordinator&);
    };

    struct RenderLaneCallbacks
    {
        VariableUpdateFn OnUpdate;
        RegisterVariableSystemsFn RegisterVariableSystems;
        RenderHookFn OnRender;
    };

    struct RenderLaneCoordinator
    {
        SceneManager& Scene;
        RenderOrchestrator& Renderer;
        GraphicsBackend& Graphics;
        Core::FeatureRegistry& Features;
        Core::Assets::AssetManager& Assets;

        void Run(this const RenderLaneCoordinator&,
                 double frameTime,
                 RenderLaneCallbacks&& callbacks,
                 ExecuteGraphFn&& executeGraph);
    };
}
