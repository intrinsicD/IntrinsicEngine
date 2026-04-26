#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <expected>
#include <thread>
#include <variant>
#include <vector>

import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;

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
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(executor.GetState(second), StreamingTaskState::Pending);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
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
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 2);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 2);
    EXPECT_EQ(order[1], 1);

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
        .Execute = []()
        {
            return StreamingResult{StreamingGpuUploadRequest{
                .PayloadToken = 123,
                .ByteSize = 64,
            }};
        },
        .ApplyOnMainThread = [&applyThread](StreamingResult&&)
        {
            applyThread = std::this_thread::get_id();
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::WaitingForGpuUpload);
    executor.ApplyMainThreadResults();

    EXPECT_EQ(applyThread, callerId);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, CancelRunningTaskSuppressesApply)
{
    StreamingExecutor executor{};

    std::atomic<bool> started = false;
    std::atomic<bool> applyCalled = false;

    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "CancelableRunning",
        .Execute = [&started]()
        {
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return StreamingResult{};
        },
        .ApplyOnMainThread = [&applyCalled](StreamingResult&&)
        {
            applyCalled.store(true, std::memory_order_release);
        },
    });

    executor.PumpBackground(1);
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    executor.Cancel(handle);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    EXPECT_FALSE(applyCalled.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Cancelled);
}

TEST(RuntimeStreamingExecutor, ShutdownDrainsRunningWorkDeterministically)
{
    StreamingExecutor executor{};

    std::atomic<bool> ran = false;
    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "ShutdownDrain",
        .Execute = [&ran]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ran.store(true, std::memory_order_release);
            return StreamingResult{};
        },
    });

    executor.PumpBackground(1);
    executor.ShutdownAndDrain();

    EXPECT_TRUE(ran.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, UploadRequestResultEnqueuesMainThreadUploadCallback)
{
    StreamingExecutor executor{};

    std::atomic<bool> uploadApplied = false;
    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "UploadRequest",
        .Execute = []()
        {
            return StreamingResult{StreamingGpuUploadRequest{
                .PayloadToken = 42,
                .ByteSize = 4096,
            }};
        },
        .ApplyOnMainThread = [&uploadApplied](StreamingResult&& result)
        {
            if (result.has_value() && std::holds_alternative<StreamingGpuUploadRequest>(*result))
            {
                uploadApplied.store(true, std::memory_order_release);
            }
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::WaitingForGpuUpload);

    executor.ApplyMainThreadResults();
    EXPECT_TRUE(uploadApplied.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, CancelledUploadRequestSkipsUploadApplyCallback)
{
    StreamingExecutor executor{};

    std::atomic<bool> started = false;
    std::atomic<bool> uploadApplied = false;

    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "CancelledUpload",
        .Execute = [&started]()
        {
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return StreamingResult{StreamingGpuUploadRequest{
                .PayloadToken = 7,
                .ByteSize = 256,
            }};
        },
        .ApplyOnMainThread = [&uploadApplied](StreamingResult&&)
        {
            uploadApplied.store(true, std::memory_order_release);
        },
    });

    executor.PumpBackground(1);
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    executor.Cancel(handle);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    EXPECT_FALSE(uploadApplied.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Cancelled);
}

TEST(RuntimeStreamingExecutor, FailedWorkerResultSkipsUploadApplyAndMarksFailed)
{
    StreamingExecutor executor{};

    std::atomic<bool> uploadApplied = false;
    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "FailedUpload",
        .Execute = []()
        {
            return StreamingResult{std::unexpected(Extrinsic::Core::ErrorCode::InvalidArgument)};
        },
        .ApplyOnMainThread = [&uploadApplied](StreamingResult&&)
        {
            uploadApplied.store(true, std::memory_order_release);
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    EXPECT_FALSE(uploadApplied.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Failed);
}

TEST(RuntimeStreamingExecutor, CancellationGenerationMismatchSuppressesStalePublication)
{
    StreamingExecutor executor{};

    std::atomic<bool> started = false;
    std::atomic<bool> applyCalled = false;
    std::atomic<bool> dependentRan = false;

    const auto cancelled = executor.Submit(StreamingTaskDesc{
        .Name = "CancelledWithGeneration",
        .CancellationGeneration = 10,
        .Execute = [&started]()
        {
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return StreamingResult{StreamingGpuUploadRequest{
                .PayloadToken = 99,
                .ByteSize = 512,
            }};
        },
        .ApplyOnMainThread = [&applyCalled](StreamingResult&&)
        {
            applyCalled.store(true, std::memory_order_release);
        },
    });

    const auto dependent = executor.Submit(StreamingTaskDesc{
        .Name = "DependentAfterCancel",
        .DependsOn = {cancelled},
        .Execute = [&dependentRan]() -> StreamingResult
        {
            dependentRan.store(true, std::memory_order_release);
            return StreamingResult{};
        },
    });

    executor.PumpBackground(1);
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    executor.Cancel(cancelled);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    executor.PumpBackground(1);
    executor.DrainCompletions();

    EXPECT_FALSE(applyCalled.load(std::memory_order_acquire));
    EXPECT_TRUE(dependentRan.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(cancelled), StreamingTaskState::Cancelled);
    EXPECT_EQ(executor.GetState(dependent), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, ApplyMainThreadResultsPublishesCompletionExactlyOnce)
{
    StreamingExecutor executor{};

    std::atomic<int> applyCount = 0;
    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "ApplyOnce",
        .Execute = []() -> StreamingResult
        {
            return StreamingResult{StreamingGpuUploadRequest{
                .PayloadToken = 314,
                .ByteSize = 2048,
            }};
        },
        .ApplyOnMainThread = [&applyCount](StreamingResult&&)
        {
            applyCount.fetch_add(1, std::memory_order_relaxed);
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    EXPECT_EQ(applyCount.load(std::memory_order_relaxed), 1);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, LongRunningTaskDoesNotBlockPumpOrApplyTick)
{
    StreamingExecutor executor{};

    std::atomic<bool> started = false;
    std::atomic<bool> finished = false;

    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "LongRunning",
        .Execute = [&started, &finished]() -> StreamingResult
        {
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            finished.store(true, std::memory_order_release);
            return StreamingResult{};
        },
    });

    executor.PumpBackground(1);
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    const auto before = std::chrono::steady_clock::now();
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();
    const auto after = std::chrono::steady_clock::now();
    const auto tickMicros = std::chrono::duration_cast<std::chrono::microseconds>(after - before).count();

    EXPECT_LT(tickMicros, 10'000);

    while (!finished.load(std::memory_order_acquire))
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, ShutdownPreventsPostShutdownLatePublication)
{
    StreamingExecutor executor{};

    std::atomic<bool> started = false;
    std::atomic<int> applyCount = 0;

    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "ShutdownNoLateApply",
        .Execute = [&started]() -> StreamingResult
        {
            started.store(true, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return StreamingResult{StreamingGpuUploadRequest{
                .PayloadToken = 777,
                .ByteSize = 64,
            }};
        },
        .ApplyOnMainThread = [&applyCount](StreamingResult&&)
        {
            applyCount.fetch_add(1, std::memory_order_relaxed);
        },
    });

    executor.PumpBackground(1);
    while (!started.load(std::memory_order_acquire))
    {
        std::this_thread::yield();
    }

    executor.ShutdownAndDrain();
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();
    const int afterFirstApply = applyCount.load(std::memory_order_relaxed);

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    executor.DrainCompletions();
    executor.ApplyMainThreadResults();

    EXPECT_EQ(applyCount.load(std::memory_order_relaxed), afterFirstApply);
    EXPECT_TRUE(handle.IsValid());
}
