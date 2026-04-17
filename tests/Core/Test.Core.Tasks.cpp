#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <memory>
#include <thread>
#include <vector>

import Extrinsic.Core.Tasks;
import Extrinsic.Core.Tasks.CounterEvent;

using Extrinsic::Core::Tasks::Scheduler;
using Extrinsic::Core::Tasks::Job;
using Extrinsic::Core::Tasks::CounterEvent;
using Extrinsic::Core::Tasks::WaitFor;
using Extrinsic::Core::Tasks::Yield;

namespace
{
    class SchedulerFixture : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            Scheduler::Initialize(2);
        }

        void TearDown() override
        {
            Scheduler::WaitForAll();
            Scheduler::Shutdown();
        }
    };

    Job IncrementOnResume(CounterEvent* event, std::atomic<int>* resumed)
    {
        co_await WaitFor(*event);
        resumed->fetch_add(1, std::memory_order_release);
        co_return;
    }

    Job YieldThenIncrement(std::atomic<int>* counter)
    {
        counter->fetch_add(1, std::memory_order_release);
        co_await Yield();
        counter->fetch_add(10, std::memory_order_release);
        co_return;
    }
}

// -----------------------------------------------------------------------------
// Scheduler lifecycle
// -----------------------------------------------------------------------------

TEST(CoreTasksLifecycle, IsInitializedFalseWhenNotRunning)
{
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasksLifecycle, InitializeAndShutdown)
{
    EXPECT_FALSE(Scheduler::IsInitialized());
    Scheduler::Initialize(2);
    EXPECT_TRUE(Scheduler::IsInitialized());
    Scheduler::Shutdown();
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasksLifecycle, DoubleInitializeIsIdempotent)
{
    Scheduler::Initialize(2);
    // Second Initialize must not create a new scheduler (no state reset).
    Scheduler::Initialize(4);
    EXPECT_TRUE(Scheduler::IsInitialized());
    Scheduler::Shutdown();
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasksLifecycle, ShutdownWithoutInitializeIsSafe)
{
    Scheduler::Shutdown();  // must be no-op
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasksLifecycle, InitializeWithZeroUsesHardwareConcurrency)
{
    Scheduler::Initialize(0);
    EXPECT_TRUE(Scheduler::IsInitialized());

    // We at least have one worker state.
    auto stats = Scheduler::GetStats();
    EXPECT_GE(stats.WorkerLocalDepths.size(), 1u);

    Scheduler::Shutdown();
}

TEST(CoreTasksLifecycle, StatsZeroedBeforeInit)
{
    auto stats = Scheduler::GetStats();
    EXPECT_EQ(stats.InFlightTasks, 0u);
    EXPECT_EQ(stats.QueuedTasks, 0u);
    EXPECT_EQ(stats.ActiveTasks, 0u);
    EXPECT_EQ(stats.ParkCount, 0u);
    EXPECT_EQ(stats.UnparkCount, 0u);
    EXPECT_EQ(Scheduler::GetParkCount(), 0u);
    EXPECT_EQ(Scheduler::GetUnparkCount(), 0u);
}

// -----------------------------------------------------------------------------
// Dispatch of plain lambdas
// -----------------------------------------------------------------------------

TEST_F(SchedulerFixture, DispatchExecutesLambda)
{
    std::atomic<int> counter{0};
    for (int i = 0; i < 200; ++i)
    {
        Scheduler::Dispatch([&counter] { counter.fetch_add(1, std::memory_order_release); });
    }
    Scheduler::WaitForAll();
    EXPECT_EQ(counter.load(), 200);
}

TEST_F(SchedulerFixture, DispatchUpdatesStatsCounters)
{
    std::atomic<int> counter{0};
    for (int i = 0; i < 50; ++i)
        Scheduler::Dispatch([&counter] { counter.fetch_add(1); });
    Scheduler::WaitForAll();

    auto stats = Scheduler::GetStats();
    EXPECT_EQ(stats.InFlightTasks, 0u);
    // 50 tasks definitely caused 50 executions.
    EXPECT_EQ(counter.load(), 50);
    // Either injected or placed on a local worker queue. Total pushes must
    // equal the number of dispatched tasks.
    EXPECT_EQ(stats.InjectPopCount + stats.LocalPopCount + stats.StealPopCount,
              static_cast<std::uint64_t>(50));
}

TEST_F(SchedulerFixture, DispatchNestedFromWorkerRunsAsLocal)
{
    std::atomic<int> counter{0};
    for (int i = 0; i < 16; ++i)
    {
        Scheduler::Dispatch([&counter]
        {
            Scheduler::Dispatch([&counter] { counter.fetch_add(1); });
            counter.fetch_add(1);
        });
    }
    Scheduler::WaitForAll();
    EXPECT_EQ(counter.load(), 32);
}

TEST_F(SchedulerFixture, ContendedDispatchFromMultipleThreadsCompletes)
{
    constexpr int kWorkers = 4;
    constexpr int kPerWorker = 200;
    std::atomic<int> counter{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kWorkers; ++t)
    {
        threads.emplace_back([&counter]
        {
            for (int i = 0; i < kPerWorker; ++i)
                Scheduler::Dispatch([&counter] { counter.fetch_add(1); });
        });
    }
    for (auto& th : threads) th.join();

    Scheduler::WaitForAll();
    EXPECT_EQ(counter.load(), kWorkers * kPerWorker);
}

// -----------------------------------------------------------------------------
// Dispatch without an initialized scheduler is a no-op.
// -----------------------------------------------------------------------------

TEST(CoreTasksLifecycle, DispatchWithoutInitIsNoOp)
{
    ASSERT_FALSE(Scheduler::IsInitialized());
    std::atomic<int> counter{0};
    Scheduler::Dispatch([&counter] { counter.fetch_add(1); });
    // No scheduler, so no worker ever picks this up. Give a moment and check.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(counter.load(), 0);
}

TEST(CoreTasksLifecycle, WaitForAllWithoutInitIsNoOp)
{
    ASSERT_FALSE(Scheduler::IsInitialized());
    Scheduler::WaitForAll();  // Must not hang or crash.
    SUCCEED();
}

// -----------------------------------------------------------------------------
// Coroutine Job
// -----------------------------------------------------------------------------

TEST(CoreTasksJob, DefaultJobIsInvalid)
{
    Job j;
    EXPECT_FALSE(j.Valid());
}

TEST_F(SchedulerFixture, DispatchJobRunsToCompletion)
{
    auto builder = []() -> Job
    {
        co_return;
    };

    for (int i = 0; i < 50; ++i)
    {
        auto job = builder();
        EXPECT_TRUE(job.Valid());
        Scheduler::Dispatch(std::move(job));
    }
    Scheduler::WaitForAll();
}

TEST_F(SchedulerFixture, JobMoveConstructorTransfersOwnership)
{
    auto builder = []() -> Job { co_return; };
    auto job = builder();
    EXPECT_TRUE(job.Valid());
    Job moved = std::move(job);
    EXPECT_TRUE(moved.Valid());
    EXPECT_FALSE(job.Valid());
    Scheduler::Dispatch(std::move(moved));
    Scheduler::WaitForAll();
}

TEST_F(SchedulerFixture, JobMoveAssignmentTransfersOwnership)
{
    auto builder = []() -> Job { co_return; };
    auto a = builder();
    auto b = builder();
    a = std::move(b);
    EXPECT_TRUE(a.Valid());
    EXPECT_FALSE(b.Valid());
    Scheduler::Dispatch(std::move(a));
    Scheduler::WaitForAll();
}

TEST_F(SchedulerFixture, JobDestructorCancelsUndispatchedCoroutine)
{
    // Create a coroutine, let it suspend at initial_suspend, then drop it
    // without dispatching. The cancellation token should be flipped so that
    // any latent reschedule would bail out.
    std::atomic<int> resumed{0};
    {
        CounterEvent counter(1);
        auto job = IncrementOnResume(&counter, &resumed);
        // Drop job without dispatching (initial_suspend is std::suspend_always).
        counter.Signal();
        // Even though the counter signalled, the coroutine frame was never
        // resumed because it was never dispatched.
    }
    // Wait for any scheduled reschedule to run (in case it somehow slipped).
    Scheduler::WaitForAll();
    EXPECT_EQ(resumed.load(), 0);
}

TEST_F(SchedulerFixture, DispatchInvalidJobIsNoOp)
{
    Job empty;
    ASSERT_FALSE(empty.Valid());
    Scheduler::Dispatch(std::move(empty));
    Scheduler::WaitForAll();
    SUCCEED();
}

// -----------------------------------------------------------------------------
// CounterEvent + WaitFor
// -----------------------------------------------------------------------------

TEST_F(SchedulerFixture, CounterEventReadyByDefault)
{
    CounterEvent c;
    EXPECT_TRUE(c.IsReady());
}

TEST_F(SchedulerFixture, CounterEventAddAndSignal)
{
    CounterEvent c;
    c.Add(3);
    EXPECT_FALSE(c.IsReady());
    c.Signal();
    EXPECT_FALSE(c.IsReady());
    c.Signal();
    EXPECT_FALSE(c.IsReady());
    c.Signal();
    EXPECT_TRUE(c.IsReady());
}

TEST_F(SchedulerFixture, CounterEventAddZeroIsNoop)
{
    CounterEvent c;
    c.Add(0);
    EXPECT_TRUE(c.IsReady());
}

TEST_F(SchedulerFixture, CounterEventSignalZeroIsNoop)
{
    CounterEvent c(2);
    c.Signal(0);
    EXPECT_FALSE(c.IsReady());
}

TEST_F(SchedulerFixture, CounterEventSignalWithLargerValueClampsToZero)
{
    CounterEvent c(2);
    c.Signal(100);  // Over-signal clamps to zero.
    EXPECT_TRUE(c.IsReady());
}

TEST_F(SchedulerFixture, CounterEventUnparksWaitingCoroutine)
{
    CounterEvent c(1);
    std::atomic<int> resumed{0};
    auto job = IncrementOnResume(&c, &resumed);
    Scheduler::Dispatch(std::move(job));

    // Let coroutine start and park.
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    c.Signal();
    Scheduler::WaitForAll();

    EXPECT_EQ(resumed.load(), 1);
}

TEST_F(SchedulerFixture, CounterEventUnparksMultipleWaiters)
{
    CounterEvent c(1);
    std::atomic<int> resumed{0};
    for (int i = 0; i < 4; ++i)
        Scheduler::Dispatch(IncrementOnResume(&c, &resumed));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    c.Signal();
    Scheduler::WaitForAll();

    EXPECT_EQ(resumed.load(), 4);
}

// -----------------------------------------------------------------------------
// Yield
// -----------------------------------------------------------------------------

TEST_F(SchedulerFixture, YieldResumesOnScheduler)
{
    std::atomic<int> counter{0};
    for (int i = 0; i < 10; ++i)
        Scheduler::Dispatch(YieldThenIncrement(&counter));
    Scheduler::WaitForAll();
    // Each job adds 1 before yield + 10 after. 10 * 11 = 110.
    EXPECT_EQ(counter.load(), 110);
}

// -----------------------------------------------------------------------------
// WaitToken lifecycle
// -----------------------------------------------------------------------------

TEST_F(SchedulerFixture, AcquireAndReleaseWaitToken)
{
    auto token = Scheduler::AcquireWaitToken();
    EXPECT_TRUE(token.Valid());
    Scheduler::ReleaseWaitToken(token);
    // Releasing again with the same (now stale) token should be safe.
    Scheduler::ReleaseWaitToken(token);
    SUCCEED();
}

TEST_F(SchedulerFixture, DefaultWaitTokenIsInvalid)
{
    Scheduler::WaitToken empty{};
    EXPECT_FALSE(empty.Valid());
    // Invalid token operations must be no-ops.
    Scheduler::ReleaseWaitToken(empty);
    Scheduler::MarkWaitTokenNotReady(empty);
    EXPECT_EQ(Scheduler::UnparkReady(empty), 0u);
}

TEST_F(SchedulerFixture, UnparkReadyWithNoParkedReturnsZero)
{
    auto token = Scheduler::AcquireWaitToken();
    EXPECT_EQ(Scheduler::UnparkReady(token), 0u);
    Scheduler::ReleaseWaitToken(token);
}

TEST_F(SchedulerFixture, StatsExposesParkAndUnparkCounts)
{
    const auto parkStart = Scheduler::GetParkCount();
    const auto unparkStart = Scheduler::GetUnparkCount();

    CounterEvent c(1);
    std::atomic<int> resumed{0};
    Scheduler::Dispatch(IncrementOnResume(&c, &resumed));
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    c.Signal();
    Scheduler::WaitForAll();

    EXPECT_GE(Scheduler::GetParkCount(), parkStart + 1u);
    EXPECT_GE(Scheduler::GetUnparkCount(), unparkStart + 1u);

    auto stats = Scheduler::GetStats();
    EXPECT_GE(stats.ParkCount, parkStart + 1u);
    EXPECT_GE(stats.UnparkCount, unparkStart + 1u);
    (void)Scheduler::ParkCountAtomic();  // Ensure accessor exists.
}
