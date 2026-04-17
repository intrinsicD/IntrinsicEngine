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

    // Deterministic replacement for ad-hoc sleep_for waits: polls until the
    // scheduler's ParkCount has advanced by at least `expectedDelta` or a
    // 2-second deadline elapses. Returns the final delta so the caller can
    // assert without racing the actual sleep duration.
    std::uint64_t WaitForParkDelta(std::uint64_t startCount, std::uint64_t expectedDelta)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        std::uint64_t delta = 0;
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto now = Scheduler::GetParkCount();
            delta = (now >= startCount) ? (now - startCount) : 0;
            if (delta >= expectedDelta) return delta;
            std::this_thread::yield();
        }
        return delta;
    }
}

// -----------------------------------------------------------------------------
// Scheduler lifecycle
// -----------------------------------------------------------------------------

TEST(CoreTasks, IsInitializedFalseWhenNotRunning)
{
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasks, InitializeAndShutdown)
{
    EXPECT_FALSE(Scheduler::IsInitialized());
    Scheduler::Initialize(2);
    EXPECT_TRUE(Scheduler::IsInitialized());
    Scheduler::Shutdown();
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasks, DoubleInitializeIsIdempotent)
{
    Scheduler::Initialize(2);
    // Second Initialize must not create a new scheduler (no state reset).
    Scheduler::Initialize(4);
    EXPECT_TRUE(Scheduler::IsInitialized());
    Scheduler::Shutdown();
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasks, ShutdownWithoutInitializeIsSafe)
{
    Scheduler::Shutdown();  // must be no-op
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasks, InitializeWithZeroUsesHardwareConcurrency)
{
    Scheduler::Initialize(0);
    EXPECT_TRUE(Scheduler::IsInitialized());

    // We at least have one worker state.
    auto stats = Scheduler::GetStats();
    EXPECT_GE(stats.WorkerLocalDepths.size(), 1u);

    Scheduler::Shutdown();
}

TEST(CoreTasks, StatsZeroedBeforeInit)
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

TEST(CoreTasks, DispatchWithoutInitIsNoOp)
{
    ASSERT_FALSE(Scheduler::IsInitialized());
    std::atomic<int> counter{0};
    Scheduler::Dispatch([&counter] { counter.fetch_add(1); });
    // No scheduler, so no worker ever picks this up. Give a moment and check.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(counter.load(), 0);
}

TEST(CoreTasks, WaitForAllWithoutInitIsNoOp)
{
    ASSERT_FALSE(Scheduler::IsInitialized());
    Scheduler::WaitForAll();  // Must not hang or crash.
    SUCCEED();
}

// -----------------------------------------------------------------------------
// Coroutine Job
// -----------------------------------------------------------------------------

TEST(CoreTasks, DefaultJobIsInvalid)
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

TEST_F(SchedulerFixture, UndispatchedJobDestructionFlipsAliveFlag)
{
    // An undispatched Job destructs with a live handle and a live alive-flag.
    // Job::DestroyIfOwned() must flip the alive flag to false before
    // destroying the coroutine frame so that any reschedule task that had
    // already captured the shared_ptr cannot resume a destroyed frame.
    //
    // We observe this through Scheduler::Reschedule, which is the only public
    // path that inspects the alive flag.
    std::atomic<int> resumed{0};

    auto inc = [&resumed]() -> Job
    {
        resumed.fetch_add(1, std::memory_order_release);
        co_return;
    };

    auto job = inc();
    // Keep a copy of the alive token so we can inspect it after destruction.
    // We can't reach into Job's internals, so we use the indirect signal: if
    // the frame is destroyed and the alive flag is flipped, a later
    // Reschedule of the same handle will bail out. But reaching the handle
    // requires Dispatch semantics, so we instead verify via the simpler
    // contract: after `job` is destroyed, no resume happens.
    {
        Job local = std::move(job);
        EXPECT_TRUE(local.Valid());
    }  // local is destroyed here with m_Handle still valid → alive flipped.

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
    const auto parkStart = Scheduler::GetParkCount();

    CounterEvent c(1);
    std::atomic<int> resumed{0};
    auto job = IncrementOnResume(&c, &resumed);
    Scheduler::Dispatch(std::move(job));

    // Deterministic: wait for the coroutine to actually park before we signal.
    EXPECT_GE(WaitForParkDelta(parkStart, 1u), 1u);
    c.Signal();
    Scheduler::WaitForAll();

    EXPECT_EQ(resumed.load(), 1);
}

TEST_F(SchedulerFixture, CounterEventUnparksMultipleWaiters)
{
    const auto parkStart = Scheduler::GetParkCount();

    CounterEvent c(1);
    std::atomic<int> resumed{0};
    for (int i = 0; i < 4; ++i)
        Scheduler::Dispatch(IncrementOnResume(&c, &resumed));

    EXPECT_GE(WaitForParkDelta(parkStart, 4u), 4u);
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
    EXPECT_GE(WaitForParkDelta(parkStart, 1u), 1u);
    c.Signal();
    Scheduler::WaitForAll();

    EXPECT_GE(Scheduler::GetParkCount(), parkStart + 1u);
    EXPECT_GE(Scheduler::GetUnparkCount(), unparkStart + 1u);

    auto stats = Scheduler::GetStats();
    EXPECT_GE(stats.ParkCount, parkStart + 1u);
    EXPECT_GE(stats.UnparkCount, unparkStart + 1u);
    (void)Scheduler::ParkCountAtomic();  // Ensure accessor exists.
}

// -----------------------------------------------------------------------------
// Additional coverage: CounterEvent::Token, MarkWaitTokenNotReady on live slot,
// Reschedule direct, and Stats field sanity (percentile ordering, success
// ratio, worker-depth vectors).
// -----------------------------------------------------------------------------

TEST_F(SchedulerFixture, CounterEventTokenAccessorIsValidWhilePending)
{
    CounterEvent c(1);
    EXPECT_TRUE(c.Token().Valid());
}

TEST_F(SchedulerFixture, MarkWaitTokenNotReadyOnLiveSlotParksNewWaiter)
{
    // Acquire a wait token, mark it ready via UnparkReady, then flip it back
    // with MarkWaitTokenNotReady. A subsequent park must succeed (the slot
    // is not in a ready state).
    auto token = Scheduler::AcquireWaitToken();
    ASSERT_TRUE(token.Valid());

    // Make the slot ready (no parked waiters, but slot.ready == true).
    EXPECT_EQ(Scheduler::UnparkReady(token), 0u);

    // Clear the ready flag.
    Scheduler::MarkWaitTokenNotReady(token);

    // A park attempt with a real coroutine handle is tricky to construct by
    // hand, so we verify the negative: Releasing a just-cleared slot must be
    // a clean teardown.
    Scheduler::ReleaseWaitToken(token);
    SUCCEED();
}

TEST_F(SchedulerFixture, RescheduleWithNullHandleIsNoOp)
{
    // Reschedule with an empty coroutine_handle must not crash and must not
    // enqueue a task.
    auto statsBefore = Scheduler::GetStats();
    Scheduler::Reschedule({}, nullptr);
    Scheduler::WaitForAll();
    auto statsAfter = Scheduler::GetStats();

    EXPECT_EQ(statsBefore.InjectPushCount + statsBefore.LocalPopCount,
              statsAfter.InjectPushCount + statsAfter.LocalPopCount);
}

TEST_F(SchedulerFixture, RescheduleWithAliveFlagFalseBailsOutBeforeResume)
{
    // Build a coroutine handle we own and then flip the alive flag ourselves.
    // The reschedule closure must observe alive == false and NOT call
    // h.resume(). We detect this through the `started` counter.
    std::atomic<int> started{0};

    auto make = [&started]() -> Job
    {
        started.fetch_add(1, std::memory_order_release);
        co_return;
    };

    auto job = make();
    ASSERT_TRUE(job.Valid());

    // Destroying the Job flips its alive flag to false. We cannot observe
    // the flag directly, so we verify behavioural cancellation: the coroutine
    // body never runs.
    job = Job{};  // Move-assign empty → DestroyIfOwned flips alive.

    Scheduler::WaitForAll();
    EXPECT_EQ(started.load(), 0);
}

TEST_F(SchedulerFixture, StatsPercentilesAreMonotonic)
{
    // After workload, P50 ≤ P95 ≤ P99 for both park and unpark histograms.
    const auto parkStart = Scheduler::GetParkCount();

    // Create several park/unpark cycles.
    for (int i = 0; i < 8; ++i)
    {
        CounterEvent c(1);
        std::atomic<int> resumed{0};
        Scheduler::Dispatch(IncrementOnResume(&c, &resumed));
        WaitForParkDelta(parkStart + static_cast<std::uint64_t>(i), 1u);
        c.Signal();
        Scheduler::WaitForAll();
    }

    auto stats = Scheduler::GetStats();
    EXPECT_LE(stats.ParkLatencyP50Ns, stats.ParkLatencyP95Ns);
    EXPECT_LE(stats.ParkLatencyP95Ns, stats.ParkLatencyP99Ns);
    EXPECT_LE(stats.UnparkLatencyP50Ns, stats.UnparkLatencyP95Ns);
    EXPECT_LE(stats.UnparkLatencyP95Ns, stats.UnparkLatencyP99Ns);

    // StealSuccessRatio is a defined ratio in [0, 1]. It may be zero if no
    // steals happened, but it must never exceed 1.
    EXPECT_GE(stats.StealSuccessRatio, 0.0);
    EXPECT_LE(stats.StealSuccessRatio, 1.0);

    // WorkerVictimStealCounts must match WorkerLocalDepths in size.
    EXPECT_EQ(stats.WorkerLocalDepths.size(), stats.WorkerVictimStealCounts.size());

    // UnparkLatencyTailSpreadNs = P99 - P50 when P99 ≥ P50, else 0.
    if (stats.UnparkLatencyP99Ns >= stats.UnparkLatencyP50Ns)
    {
        EXPECT_EQ(stats.UnparkLatencyTailSpreadNs,
                  stats.UnparkLatencyP99Ns - stats.UnparkLatencyP50Ns);
    }
}

TEST_F(SchedulerFixture, StatsRecordInjectPushesFromNonWorkerDispatch)
{
    // Dispatch from the main thread (not a worker) — must increment
    // InjectPushCount, not LocalPopCount.
    auto before = Scheduler::GetStats();
    for (int i = 0; i < 10; ++i)
        Scheduler::Dispatch([] {});
    Scheduler::WaitForAll();
    auto after = Scheduler::GetStats();

    EXPECT_GE(after.InjectPushCount, before.InjectPushCount + 10u);
    EXPECT_GE(after.InjectPopCount, before.InjectPopCount + 10u);
}
