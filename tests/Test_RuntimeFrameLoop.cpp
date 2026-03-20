#include <gtest/gtest.h>

#include <string>
#include <vector>

import Core;
import Runtime.FrameLoop;

namespace
{
    constexpr double kEpsilon = 1e-9;

    class FakeStreamingLaneHost final : public Runtime::IStreamingLaneHost
    {
    public:
        void ProcessMainThreadQueue() override { Calls.emplace_back("queue"); }
        void ProcessUploads() override { Calls.emplace_back("uploads"); }
        void ProcessTextureDeletions() override { Calls.emplace_back("textures"); }
        void ProcessMaterialDeletions() override { Calls.emplace_back("materials"); }
        void GarbageCollectTransfers() override { Calls.emplace_back("gc"); }

        std::vector<std::string> Calls;
    };

    class FakeRenderLaneHost final : public Runtime::IRenderLaneHost
    {
    public:
        explicit FakeRenderLaneHost(Core::FrameGraph& graph, std::vector<std::string>* trace = nullptr)
            : Graph(graph)
            , Trace(trace)
        {
        }

        [[nodiscard]] Core::FrameGraph& GetFrameGraph() override
        {
            Calls.emplace_back("get_graph");
            if (Trace)
                Trace->emplace_back("host:get_graph");
            return Graph;
        }

        void RegisterEngineSystems(Core::FrameGraph& graph) override
        {
            EXPECT_EQ(&graph, &Graph);
            Calls.emplace_back("register_engine");
            if (Trace)
                Trace->emplace_back("host:register_engine");
        }

        void DispatchDeferredEvents() override
        {
            Calls.emplace_back("dispatch");
            if (Trace)
                Trace->emplace_back("host:dispatch");
        }

        Core::FrameGraph& Graph;
        std::vector<std::string>* Trace = nullptr;
        std::vector<std::string> Calls;
    };
}

TEST(RuntimeFrameLoop, ComputeFrameTime_ClampsLargeSpikes)
{
    const Runtime::FrameLoopPolicy policy{
        .FixedDt = 1.0 / 60.0,
        .MaxFrameDelta = 0.25,
        .MaxSubstepsPerFrame = 8,
    };

    const Runtime::FrameTimeStep step = Runtime::ComputeFrameTime(1.5, policy);
    EXPECT_TRUE(step.Clamped);
    EXPECT_NEAR(step.FrameTime, 0.25, kEpsilon);
}

TEST(RuntimeFrameLoop, ComputeFrameTime_RejectsNegativeDelta)
{
    const Runtime::FrameTimeStep step = Runtime::ComputeFrameTime(-0.5);
    EXPECT_TRUE(step.Clamped);
    EXPECT_NEAR(step.FrameTime, 0.0, kEpsilon);
}

TEST(RuntimeFrameLoop, RunFixedSteps_AdvancesUntilAccumulatorIsBelowFixedDt)
{
    Runtime::FrameLoopPolicy policy{
        .FixedDt = 0.1,
        .MaxFrameDelta = 0.25,
        .MaxSubstepsPerFrame = 8,
    };

    double accumulator = 0.35;
    std::vector<float> fixedUpdates;
    int graphExecutions = 0;

    Core::Memory::ScopeStack scope(64 * 1024);
    Core::FrameGraph graph(scope);

    const Runtime::FixedStepAdvanceResult result = Runtime::RunFixedSteps(
        accumulator,
        policy,
        [&](float dt) { fixedUpdates.push_back(dt); },
        [&](Core::FrameGraph& registeredGraph, float dt)
        {
            EXPECT_EQ(&registeredGraph, &graph);
            EXPECT_NEAR(dt, 0.1f, 1e-6f);
            ++graphExecutions;
        },
        graph,
        [&](Core::FrameGraph& executedGraph)
        {
            EXPECT_EQ(&executedGraph, &graph);
        });

    EXPECT_EQ(result.ExecutedSubsteps, 3);
    EXPECT_FALSE(result.AccumulatorClamped);
    EXPECT_EQ(graphExecutions, 3);
    ASSERT_EQ(fixedUpdates.size(), 3u);
    EXPECT_NEAR(accumulator, 0.05, kEpsilon);
}

TEST(RuntimeFrameLoop, RunFixedSteps_ClampsAccumulatorAfterMaxSubsteps)
{
    Runtime::FrameLoopPolicy policy{
        .FixedDt = 0.1,
        .MaxFrameDelta = 0.25,
        .MaxSubstepsPerFrame = 2,
    };

    double accumulator = 0.45;

    Core::Memory::ScopeStack scope(64 * 1024);
    Core::FrameGraph graph(scope);

    const Runtime::FixedStepAdvanceResult result = Runtime::RunFixedSteps(
        accumulator,
        policy,
        [](float) {},
        [](Core::FrameGraph&, float) {},
        graph,
        [](Core::FrameGraph&) {});

    EXPECT_EQ(result.ExecutedSubsteps, 2);
    EXPECT_TRUE(result.AccumulatorClamped);
    EXPECT_NEAR(accumulator, 0.0, kEpsilon);
}

TEST(RuntimeFrameLoop, StreamingLaneCoordinator_OrdersMainThreadUploadAndCleanupWork)
{
    FakeStreamingLaneHost host;
    const Runtime::StreamingLaneCoordinator coordinator{.Host = host};

    coordinator.BeginFrame();
    coordinator.EndFrame();

    const std::vector<std::string> expected{
        "queue",
        "uploads",
        "textures",
        "materials",
        "gc",
    };
    EXPECT_EQ(host.Calls, expected);
}

TEST(RuntimeFrameLoop, RenderLaneCoordinator_OrdersUpdateRegistrationExecutionDispatchAndRender)
{
    Core::Memory::ScopeStack scope(64 * 1024);
    Core::FrameGraph graph(scope);
    std::vector<std::string> trace;
    FakeRenderLaneHost host(graph, &trace);
    const Runtime::RenderLaneCoordinator coordinator{.Host = host};

    std::vector<std::string> calls;
    coordinator.Run(
        1.0 / 60.0,
        {
            .OnUpdate = [&](float dt)
            {
                EXPECT_NEAR(dt, 1.0f / 60.0f, 1e-6f);
                calls.emplace_back("update");
                trace.emplace_back("callback:update");
            },
            .RegisterVariableSystems = [&](Core::FrameGraph& registeredGraph, float dt)
            {
                EXPECT_EQ(&registeredGraph, &graph);
                EXPECT_NEAR(dt, 1.0f / 60.0f, 1e-6f);
                calls.emplace_back("register_client");
                trace.emplace_back("callback:register_client");
            },
            .BeforeDispatch = [&]()
            {
                calls.emplace_back("before_dispatch");
                trace.emplace_back("callback:before_dispatch");
            },
            .OnRender = [&]()
            {
                calls.emplace_back("render");
                trace.emplace_back("callback:render");
            },
        },
        [&](Core::FrameGraph& executedGraph)
        {
            EXPECT_EQ(&executedGraph, &graph);
            calls.emplace_back("execute");
            trace.emplace_back("callback:execute");
        });

    const std::vector<std::string> expectedCalls{
        "update",
        "register_client",
        "execute",
        "before_dispatch",
        "render",
    };
    EXPECT_EQ(calls, expectedCalls);

    const std::vector<std::string> expectedHostCalls{
        "get_graph",
        "register_engine",
        "dispatch",
    };
    EXPECT_EQ(host.Calls, expectedHostCalls);

    const std::vector<std::string> expectedTrace{
        "callback:update",
        "host:get_graph",
        "callback:register_client",
        "host:register_engine",
        "callback:execute",
        "callback:before_dispatch",
        "host:dispatch",
        "callback:render",
    };
    EXPECT_EQ(trace, expectedTrace);
}

TEST(RuntimeFrameLoop, RunFramePhases_PreservesStreamingFixedAndRenderLaneBaselineOrder)
{
    Runtime::FrameLoopPolicy policy{
        .FixedDt = 0.1,
        .MaxFrameDelta = 0.25,
        .MaxSubstepsPerFrame = 8,
    };

    double accumulator = 0.25;

    Core::Memory::ScopeStack scope(64 * 1024);
    Core::FrameGraph graph(scope);
    std::vector<std::string> trace;

    FakeStreamingLaneHost streamingHost;
    FakeRenderLaneHost renderHost(graph);

    const Runtime::StreamingLaneCoordinator streamingLane{.Host = streamingHost};
    const Runtime::RenderLaneCoordinator renderLane{.Host = renderHost};

    const Runtime::FramePhaseRunResult result = Runtime::RunFramePhases(
        1.0 / 60.0,
        accumulator,
        policy,
        streamingLane,
        renderLane,
        graph,
        {
            .OnFixedUpdate = [&](float dt)
            {
                EXPECT_NEAR(dt, 0.1f, 1e-6f);
                trace.emplace_back("fixed:update");
            },
            .RegisterFixedSystems = [&](Core::FrameGraph& fixedGraph, float dt)
            {
                EXPECT_EQ(&fixedGraph, &graph);
                EXPECT_NEAR(dt, 0.1f, 1e-6f);
                trace.emplace_back("fixed:register");
            },
            .ExecuteFixedGraph = [&](Core::FrameGraph& fixedGraph)
            {
                EXPECT_EQ(&fixedGraph, &graph);
                trace.emplace_back("fixed:execute");
            },
            .Render =
                {
                    .OnUpdate = [&](float dt)
                    {
                        EXPECT_NEAR(dt, 1.0f / 60.0f, 1e-6f);
                        trace.emplace_back("render:update");
                    },
                    .RegisterVariableSystems = [&](Core::FrameGraph& renderGraph, float dt)
                    {
                        EXPECT_EQ(&renderGraph, &graph);
                        EXPECT_NEAR(dt, 1.0f / 60.0f, 1e-6f);
                        trace.emplace_back("render:register_client");
                    },
                    .BeforeDispatch = [&]() { trace.emplace_back("render:before_dispatch"); },
                    .OnRender = [&]() { trace.emplace_back("render"); },
                },
            .ExecuteVariableGraph = [&](Core::FrameGraph& renderGraph)
            {
                EXPECT_EQ(&renderGraph, &graph);
                trace.emplace_back("render:execute");
            },
        });

    EXPECT_EQ(result.FixedStep.ExecutedSubsteps, 2);
    EXPECT_FALSE(result.FixedStep.AccumulatorClamped);
    EXPECT_NEAR(accumulator, 0.05, kEpsilon);

    const std::vector<std::string> expectedStreamingCalls{
        "queue",
        "uploads",
        "textures",
        "materials",
        "gc",
    };
    EXPECT_EQ(streamingHost.Calls, expectedStreamingCalls);

    const std::vector<std::string> expectedRenderHostCalls{
        "get_graph",
        "register_engine",
        "dispatch",
    };
    EXPECT_EQ(renderHost.Calls, expectedRenderHostCalls);

    const std::vector<std::string> expectedTrace{
        "fixed:update",
        "fixed:register",
        "fixed:execute",
        "fixed:update",
        "fixed:register",
        "fixed:execute",
        "render:update",
        "render:register_client",
        "render:execute",
        "render:before_dispatch",
        "render",
    };
    EXPECT_EQ(trace, expectedTrace);
}
