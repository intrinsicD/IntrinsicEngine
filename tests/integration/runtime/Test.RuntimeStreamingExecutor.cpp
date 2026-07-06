#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <expected>
#include <span>
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

TEST(RuntimeStreamingExecutor, CompletedSlotsAreRecycledAcrossSequentialSubmitCycles)
{
    StreamingExecutor executor{};

    StreamingTaskHandle previous{};
    for (std::uint32_t cycle = 0u; cycle < 64u; ++cycle)
    {
        const auto handle = executor.Submit(StreamingTaskDesc{
            .Name = "ReusableSlot",
            .Execute = []()
            {
                return StreamingResult{};
            },
        });

        ASSERT_TRUE(handle.IsValid());
        if (previous.IsValid())
        {
            EXPECT_EQ(handle.Index, previous.Index);
            EXPECT_GT(handle.Generation, previous.Generation);
            EXPECT_EQ(executor.GetState(previous), StreamingTaskState::Cancelled);
        }

        StreamingExecutorDiagnostics submitted = executor.GetDiagnostics();
        EXPECT_EQ(submitted.SlotCount, 1u);
        EXPECT_EQ(submitted.ActiveSlotCount, 1u);
        EXPECT_EQ(submitted.FreeSlotCount, 0u);
        EXPECT_EQ(submitted.ReadyTaskCount, 1u);

        executor.PumpBackground(1);
        executor.DrainCompletions();

        EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
        StreamingExecutorDiagnostics completed = executor.GetDiagnostics();
        EXPECT_EQ(completed.SlotCount, 1u);
        EXPECT_EQ(completed.ActiveSlotCount, 0u);
        EXPECT_EQ(completed.FreeSlotCount, 1u);
        EXPECT_EQ(completed.ReadyTaskCount, 0u);

        previous = handle;
    }
}

TEST(RuntimeStreamingExecutor, RecycledDependentSlotDoesNotReceiveStaleParentRelease)
{
    StreamingExecutor executor{};

    std::vector<int> order{};
    const auto parent = executor.Submit(StreamingTaskDesc{
        .Name = "Parent",
        .Priority = Extrinsic::Core::Dag::TaskPriority::High,
        .Execute = [&order]()
        {
            order.push_back(1);
            return StreamingResult{};
        },
    });
    const auto blocker = executor.Submit(StreamingTaskDesc{
        .Name = "Blocker",
        .Priority = Extrinsic::Core::Dag::TaskPriority::Low,
        .Execute = [&order]()
        {
            order.push_back(9);
            return StreamingResult{};
        },
    });
    const auto cancelled = executor.Submit(StreamingTaskDesc{
        .Name = "CancelledDependent",
        .DependsOn = {parent},
        .Execute = [&order]()
        {
            order.push_back(-1);
            return StreamingResult{};
        },
    });

    executor.Cancel(cancelled);
    ASSERT_EQ(executor.GetState(cancelled), StreamingTaskState::Cancelled);

    const auto replacement = executor.Submit(StreamingTaskDesc{
        .Name = "ReplacementDependent",
        .Priority = Extrinsic::Core::Dag::TaskPriority::Normal,
        .DependsOn = {blocker},
        .Execute = [&order]()
        {
            order.push_back(2);
            return StreamingResult{};
        },
    });

    ASSERT_TRUE(replacement.IsValid());
    EXPECT_EQ(replacement.Index, cancelled.Index);
    EXPECT_GT(replacement.Generation, cancelled.Generation);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(executor.GetState(replacement), StreamingTaskState::Pending);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[1], 9);
    EXPECT_EQ(executor.GetState(replacement), StreamingTaskState::Pending);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[2], 2);
    EXPECT_EQ(executor.GetState(replacement), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, BatchStateSnapshotMatchesSingleHandleQueries)
{
    StreamingExecutor executor{};

    const auto pending = executor.Submit(StreamingTaskDesc{
        .Name = "Pending",
        .Execute = []()
        {
            return StreamingResult{};
        },
    });
    const auto cancelled = executor.Submit(StreamingTaskDesc{
        .Name = "Cancelled",
        .Execute = []()
        {
            return StreamingResult{};
        },
    });
    executor.Cancel(cancelled);

    const std::vector<StreamingTaskHandle> handles{
        pending,
        cancelled,
        StreamingTaskHandle{},
    };
    const std::vector<StreamingTaskState> states =
        executor.GetStates(std::span<const StreamingTaskHandle>(
            handles.data(),
            handles.size()));

    ASSERT_EQ(states.size(), handles.size());
    EXPECT_EQ(states[0], executor.GetState(pending));
    EXPECT_EQ(states[1], StreamingTaskState::Cancelled);
    EXPECT_EQ(states[2], StreamingTaskState::Cancelled);
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

TEST(RuntimeStreamingExecutor, ApplyMainThreadResultsHonorsCountBudget)
{
    StreamingExecutor executor{};
    std::vector<std::uint64_t> applied{};
    std::vector<StreamingTaskHandle> handles{};

    for (std::uint64_t token = 1u; token <= 3u; ++token)
    {
        handles.push_back(executor.Submit(StreamingTaskDesc{
            .Name = "BudgetedApply",
            .Execute = [token]()
            {
                return StreamingResult{StreamingCpuPayloadReady{.PayloadToken = token}};
            },
            .ApplyOnMainThread = [&applied](StreamingResult&& result)
            {
                if (result.has_value() &&
                    std::holds_alternative<StreamingCpuPayloadReady>(*result))
                {
                    applied.push_back(
                        std::get<StreamingCpuPayloadReady>(*result).PayloadToken);
                }
            },
        }));
    }

    executor.PumpBackground(3u);
    executor.DrainCompletions();
    ASSERT_EQ(handles.size(), 3u);
    EXPECT_EQ(executor.GetState(handles[0]), StreamingTaskState::WaitingForMainThreadApply);
    EXPECT_EQ(executor.GetState(handles[1]), StreamingTaskState::WaitingForMainThreadApply);
    EXPECT_EQ(executor.GetState(handles[2]), StreamingTaskState::WaitingForMainThreadApply);

    EXPECT_EQ(executor.ApplyMainThreadResults(1u), 1u);
    ASSERT_EQ(applied.size(), 1u);
    EXPECT_EQ(applied[0], 1u);
    EXPECT_EQ(executor.GetState(handles[0]), StreamingTaskState::Complete);
    EXPECT_EQ(executor.GetState(handles[1]), StreamingTaskState::WaitingForMainThreadApply);
    EXPECT_EQ(executor.GetState(handles[2]), StreamingTaskState::WaitingForMainThreadApply);

    EXPECT_EQ(executor.ApplyMainThreadResults(0u), 0u);
    EXPECT_EQ(applied.size(), 1u);
    EXPECT_EQ(executor.GetState(handles[1]), StreamingTaskState::WaitingForMainThreadApply);
    EXPECT_EQ(executor.GetState(handles[2]), StreamingTaskState::WaitingForMainThreadApply);

    EXPECT_EQ(executor.ApplyMainThreadResults(8u), 2u);
    ASSERT_EQ(applied.size(), 3u);
    EXPECT_EQ(applied[1], 2u);
    EXPECT_EQ(applied[2], 3u);
    EXPECT_EQ(executor.GetState(handles[1]), StreamingTaskState::Complete);
    EXPECT_EQ(executor.GetState(handles[2]), StreamingTaskState::Complete);
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

TEST(RuntimeStreamingExecutor, CpuPayloadResultEnqueuesMainThreadApplyCallback)
{
    StreamingExecutor executor{};

    std::atomic<bool> cpuApplied = false;
    const auto callerId = std::this_thread::get_id();
    std::thread::id applyThread{};

    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "CpuPayloadApply",
        .Execute = []()
        {
            return StreamingResult{StreamingCpuPayloadReady{.PayloadToken = 5150}};
        },
        .ApplyOnMainThread = [&cpuApplied, &applyThread](StreamingResult&& result)
        {
            if (result.has_value() && std::holds_alternative<StreamingCpuPayloadReady>(*result))
            {
                cpuApplied.store(true, std::memory_order_release);
                applyThread = std::this_thread::get_id();
            }
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::WaitingForMainThreadApply);

    executor.ApplyMainThreadResults();
    EXPECT_TRUE(cpuApplied.load(std::memory_order_acquire));
    EXPECT_EQ(applyThread, callerId);
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
}

TEST(RuntimeStreamingExecutor, ReadbackRequestParksUntilExplicitResume)
{
    StreamingExecutor executor{};

    std::atomic<bool> readbackApplied = false;
    const auto handle = executor.Submit(StreamingTaskDesc{
        .Name = "ReadbackRequest",
        .Execute = []()
        {
            return StreamingResult{StreamingReadbackRequest{
                .PayloadToken = 91,
                .ByteSize = 512,
            }};
        },
        .ApplyOnMainThread = [&readbackApplied](StreamingResult&& result)
        {
            if (result.has_value() && std::holds_alternative<StreamingReadbackRequest>(*result))
            {
                readbackApplied.store(true, std::memory_order_release);
            }
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::WaitingForReadback);

    executor.ApplyMainThreadResults();
    EXPECT_FALSE(readbackApplied.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::WaitingForReadback);

    EXPECT_TRUE(executor.ResumeReadback(handle));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::WaitingForMainThreadApply);

    executor.ApplyMainThreadResults();
    EXPECT_TRUE(readbackApplied.load(std::memory_order_acquire));
    EXPECT_EQ(executor.GetState(handle), StreamingTaskState::Complete);
    EXPECT_FALSE(executor.ResumeReadback(handle));
}

TEST(RuntimeStreamingExecutor, ReadbackDependencyReleasesOnlyAfterApplyCompletes)
{
    StreamingExecutor executor{};

    std::vector<int> order{};
    const auto readback = executor.Submit(StreamingTaskDesc{
        .Name = "ReadbackDependencyRoot",
        .Execute = []()
        {
            return StreamingResult{StreamingReadbackRequest{
                .PayloadToken = 92,
                .ByteSize = 128,
            }};
        },
        .ApplyOnMainThread = [&order](StreamingResult&&)
        {
            order.push_back(1);
        },
    });

    const auto dependent = executor.Submit(StreamingTaskDesc{
        .Name = "ReadbackDependent",
        .DependsOn = {readback},
        .Execute = [&order]()
        {
            order.push_back(2);
            return StreamingResult{};
        },
    });

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_EQ(executor.GetState(readback), StreamingTaskState::WaitingForReadback);
    EXPECT_EQ(executor.GetState(dependent), StreamingTaskState::Pending);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    EXPECT_EQ(order.size(), 0u);
    EXPECT_EQ(executor.GetState(dependent), StreamingTaskState::Pending);

    EXPECT_TRUE(executor.ResumeReadback(readback));
    executor.ApplyMainThreadResults();
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1);

    executor.PumpBackground(1);
    executor.DrainCompletions();
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(executor.GetState(readback), StreamingTaskState::Complete);
    EXPECT_EQ(executor.GetState(dependent), StreamingTaskState::Complete);
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
