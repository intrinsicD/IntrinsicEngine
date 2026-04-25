#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Core.Dag.Scheduler;

using namespace Extrinsic::Runtime;

TEST(RuntimeStreamingExecutor, SubmitStaysPendingUntilPumped)
{
    StreamingExecutor executor{};

    std::atomic<int> runCount = 0;
    auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "A",
        .Execute = [&runCount]()
        {
            runCount.fetch_add(1, std::memory_order_relaxed);
            return StreamingResult{};
        },
    });

    EXPECT_TRUE(handle.IsValid());
    EXPECT_EQ(runCount.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Pending);

    executor.PumpBackground(1);
    executor.DrainCompletions();

    EXPECT_EQ(runCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, DependencyChainSpansPumps)
{
    StreamingExecutor executor{};

    std::vector<int> order{};
    const auto first = executor.Submit(StreamingTaskDesc{
        .Name = "First",
        .Execute = [&order]()
        {
            order.push_back(1);
            return StreamingResult{};
        },
    });

    const auto second = executor.Submit(StreamingTaskDesc{
        .Name = "Second",
        .DependsOn = {first},
        .Execute = [&order]()
        {
            order.push_back(2);
            return StreamingResult{};
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_THAT(order, ::testing::ElementsAre(1));
    EXPECT_EQ(executor.GetState(second), StreamingTaskState::Pending);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_THAT(order, ::testing::ElementsAre(1, 2));
    EXPECT_EQ(executor.GetState(second), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, HigherPriorityTaskLaunchesFirst)
{
    StreamingExecutor executor{};

    std::vector<int> order{};
    const auto low = executor.Submit(StreamingTaskDesc{
        .Name = "Low",
        .Priority = Extrinsic::Core::Dag::TaskPriority::Low,
        .Execute = [&order]()
        {
            order.push_back(1);
            return StreamingResult{};
        },
    });

    const auto high = executor.Submit(StreamingTaskDesc{
        .Name = "High",
        .Priority = Extrinsic::Core::Dag::TaskPriority::High,
        .Execute = [&order]()
        {
            order.push_back(2);
            return StreamingResult{};
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_THAT(order, ::testing::ElementsAre(2));

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_THAT(order, ::testing::ElementsAre(2, 1));

    EXPECT_EQ(executor.GetState(low), StreamingTaskState::Complete);
    EXPECT_EQ(executor.GetState(high), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, CancelPendingTaskPreventsExecution)
{
    StreamingExecutor executor{};

    std::atomic<int> runCount = 0;
    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "CancelMe",
        .Execute = [&runCount]()
        {
            runCount.fetch_add(1, std::memory_order_relaxed);
            return StreamingResult{};
        },
    });

    executor.Cancel(handle);
    executor.PumpBackground(1);
    executor.DrainCompletions();

    EXPECT_EQ(runCount.load(std::memory_order_relaxed), 0);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Cancelled);
}

TEST(RuntimeStreamingExecutor, ApplyRunsOnCallerThread)
{
    StreamingExecutor executor{};

    const auto callerId = std::this_thread::get_id();
    std::thread::id applyThread{};

    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "Apply",
        .Execute = []() { return StreamingResult{}; },
        .ApplyOnMainThread = [&applyThread](StreamingResult&&)
        {
            applyThread = std::this_thread::get_id();
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    EXPECT_EQ(applyThread, callerId);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}
