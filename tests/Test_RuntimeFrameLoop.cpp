#include <gtest/gtest.h>

#include <vector>

import Core;
import Runtime.FrameLoop;

namespace
{
    constexpr double kEpsilon = 1e-9;
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
