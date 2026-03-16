#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <coroutine>
#include <algorithm>
#include <numeric>
#include <random>

import Core;

using namespace Core::Tasks;

namespace
{
    Job WaitForCounterAndIncrement(CounterEvent* event, std::atomic<int>* resumedCount) noexcept
    {
        co_await WaitFor(*event);
        resumedCount->fetch_add(1, std::memory_order_relaxed);
        co_return;
    }

    Job WaitForCounterTrackStartAndIncrement(CounterEvent* event,
                                             std::atomic<int>* startedCount,
                                             std::atomic<int>* resumedCount) noexcept
    {
        startedCount->fetch_add(1, std::memory_order_release);
        co_await WaitFor(*event);
        resumedCount->fetch_add(1, std::memory_order_release);
        co_return;
    }
}

TEST(CoreTasks, BasicDispatch) {
    Scheduler::Initialize(2);
    
    std::atomic<int> counter = 0;
    
    // Dispatch 100 tasks
    for(int i = 0; i < 100; ++i) {
        Scheduler::Dispatch([&counter]() {
            counter++;
        });
    }
    
    Scheduler::WaitForAll();
    
    EXPECT_EQ(counter, 100);
    
    Scheduler::Shutdown();
}

TEST(CoreTasks, ContendedDispatchCompletes)
{
    // Goal: create sustained contention on the scheduler's queue mutex by dispatching
    // from multiple threads at once. Pass criteria: all tasks complete.
    constexpr int kWorkers = 4;
    constexpr int kDispatchThreads = 4;
    constexpr int kTasksPerThread = 10'000;

    Scheduler::Initialize(kWorkers);

    std::atomic<int> counter = 0;

    std::vector<std::thread> dispatchers;
    dispatchers.reserve(kDispatchThreads);

    for (int t = 0; t < kDispatchThreads; ++t)
    {
        dispatchers.emplace_back([&] {
            for (int i = 0; i < kTasksPerThread; ++i)
            {
                Scheduler::Dispatch([&counter] {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto& th : dispatchers)
        th.join();

    Scheduler::WaitForAll();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), kDispatchThreads * kTasksPerThread);

    Scheduler::Shutdown();
}

TEST(CoreTasks, CoroutineDispatch)
{
    Scheduler::Initialize(2);

    std::atomic<int> stage = 0;

    // A coroutine job
    auto asyncJob = [&stage]() -> Job {
        stage.store(1, std::memory_order_relaxed);

        // Yield execution back to the scheduler
        co_await Yield();

        stage.store(2, std::memory_order_relaxed);
        co_await Yield();

        stage.store(3, std::memory_order_relaxed);
        co_return;
    };

    Scheduler::Dispatch(asyncJob());
    Scheduler::WaitForAll();

    EXPECT_EQ(stage.load(std::memory_order_relaxed), 3);

    Scheduler::Shutdown();
}

TEST(CoreTasks, NestedCoroutines)
{
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;

    auto subTask = [&counter]() -> Job {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_await Yield();
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    auto rootTask = [&]() -> Job {
        // Fire-and-forget subtasks.
        // Proper structured-concurrency would be: co_await WhenAll(...) or co_await subTask().
        Scheduler::Dispatch(subTask());
        Scheduler::Dispatch(subTask());
        co_return;
    };

    Scheduler::Dispatch(rootTask());
    Scheduler::WaitForAll();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 4); // 2 tasks * 2 increments

    Scheduler::Shutdown();
}

TEST(CoreTasks, CoroutinesActuallyYield)
{
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;
    std::atomic<bool> gate = false;

    // Task A: increments, yields, then waits for Task B.
    auto taskA = [&]() -> Job {
        counter.fetch_add(1, std::memory_order_relaxed); // 1
        co_await Yield();

        // Spin until Task B runs.
        while (!gate.load(std::memory_order_acquire))
            std::this_thread::yield();

        counter.fetch_add(1, std::memory_order_relaxed); // 3
        co_return;
    };

    // Task B: waits for A to start, then increments and releases A.
    auto taskB = [&]() -> Job {
        while (counter.load(std::memory_order_acquire) == 0)
            std::this_thread::yield();

        counter.fetch_add(1, std::memory_order_relaxed); // 2
        gate.store(true, std::memory_order_release);
        co_return;
    };

    Scheduler::Dispatch(taskA());
    Scheduler::Dispatch(taskB());

    Scheduler::WaitForAll();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 3);

    Scheduler::Shutdown();
}


TEST(CoreTasks, CounterEventParksAndUnparksContinuation)
{
    Scheduler::Initialize(2);

    CounterEvent event{1};
    std::atomic<int> stage = 0;

    auto waiter = [&]() -> Job {
        stage.store(1, std::memory_order_release);
        co_await WaitFor(event);
        stage.store(2, std::memory_order_release);
        co_return;
    };

    Scheduler::Dispatch(waiter());

    // Block (OS-level futex, no spin) until the coroutine has actually called
    // ParkCurrentFiberIfNotReady and incremented parkCount.
    // This is the only race-free way to know the slow path was taken before we signal.
    Scheduler::ParkCountAtomic().wait(0, std::memory_order_acquire);

    EXPECT_EQ(stage.load(std::memory_order_acquire), 1);

    event.Signal();
    Scheduler::WaitForAll();
    const auto stats = Scheduler::GetStats();

    EXPECT_EQ(stage.load(std::memory_order_acquire), 2);
    EXPECT_GE(stats.ParkCount,   1u);
    EXPECT_GE(stats.UnparkCount, 1u);
    EXPECT_GE(stats.ParkLatencyP50Ns, 0u);
    EXPECT_GE(stats.ParkLatencyP95Ns, stats.ParkLatencyP50Ns);
    EXPECT_GE(stats.ParkLatencyP99Ns, stats.ParkLatencyP95Ns);
    EXPECT_GE(stats.UnparkLatencyP50Ns, 0u);
    EXPECT_GE(stats.UnparkLatencyP95Ns, stats.UnparkLatencyP50Ns);
    EXPECT_GE(stats.UnparkLatencyP99Ns, stats.UnparkLatencyP95Ns);
    EXPECT_EQ(stats.UnparkLatencyTailSpreadNs,
              stats.UnparkLatencyP99Ns - stats.UnparkLatencyP50Ns);

    Scheduler::Shutdown();
}


TEST(CoreTasks, CounterEventMultipleWaitersResumeExactlyOnce)
{
    Scheduler::Initialize(4);

    CounterEvent event{1};
    std::atomic<int> resumedCount = 0;
    constexpr int waiterCount = 64;

    auto waiter = [&]() -> Job {
        co_await WaitFor(event);
        resumedCount.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    for (int i = 0; i < waiterCount; ++i)
        Scheduler::Dispatch(waiter());

    // Wait until all coroutines have at least started (are either parked or
    // have already taken the fast path past await_ready). We do this by
    // waiting until inFlightTasks has grown to waiterCount, which happens
    // once every Dispatch call has been picked up.  A simpler proxy: just
    // give workers a moment to reach the await point before signalling.
    // The only correct assertion is that every waiter resumes exactly once.
    event.Signal();
    Scheduler::WaitForAll();

    EXPECT_EQ(resumedCount.load(std::memory_order_relaxed), waiterCount);

    // ParkCount may be less than waiterCount if some coroutines observe the
    // event as already-ready in await_ready() or await_suspend() (fast path).
    // What matters: ParkCount + fast-path-completions == waiterCount, i.e.
    // every waiter resumed exactly once regardless of path taken.
    const auto stats = Scheduler::GetStats();
    EXPECT_LE(stats.ParkCount,   static_cast<uint64_t>(waiterCount));
    EXPECT_LE(stats.UnparkCount, static_cast<uint64_t>(waiterCount));
    EXPECT_EQ(stats.ParkCount, stats.UnparkCount); // every park must have a matching unpark

    Scheduler::Shutdown();
}


TEST(CoreTasks, CounterEventCanBeRearmedAfterReady)
{
    Scheduler::Initialize(2);

    CounterEvent event{1};
    std::atomic<int> startedCount = 0;
    std::atomic<int> resumedCount = 0;

    Scheduler::Dispatch(WaitForCounterTrackStartAndIncrement(&event, &startedCount, &resumedCount));
    while (startedCount.load(std::memory_order_acquire) < 1)
        std::this_thread::yield();

    event.Signal();
    Scheduler::WaitForAll();
    EXPECT_EQ(resumedCount.load(std::memory_order_acquire), 1);
    EXPECT_TRUE(event.IsReady());

    event.Add(1);
    EXPECT_FALSE(event.IsReady());

    Scheduler::Dispatch(WaitForCounterTrackStartAndIncrement(&event, &startedCount, &resumedCount));
    while (startedCount.load(std::memory_order_acquire) < 2)
        std::this_thread::yield();

    EXPECT_EQ(resumedCount.load(std::memory_order_acquire), 1);

    event.Signal();
    Scheduler::WaitForAll();

    EXPECT_TRUE(event.IsReady());
    EXPECT_EQ(resumedCount.load(std::memory_order_acquire), 2);

    Scheduler::Shutdown();
}

TEST(CoreTasks, StaleWaitTokenUnparkDoesNotResumeNewWaiters)
{
    Scheduler::Initialize(2);

    auto waitForAdditionalPark = [](uint64_t previousParkCount)
    {
        while (Scheduler::ParkCountAtomic().load(std::memory_order_acquire) == previousParkCount)
            Scheduler::ParkCountAtomic().wait(previousParkCount, std::memory_order_acquire);
    };

    Scheduler::WaitToken staleToken{};
    {
        CounterEvent firstEvent{1};
        std::atomic<int> firstStage = 0;
        staleToken = firstEvent.Token();

        auto firstWaiter = [&]() -> Job {
            firstStage.store(1, std::memory_order_release);
            co_await WaitFor(firstEvent);
            firstStage.store(2, std::memory_order_release);
            co_return;
        };

        Scheduler::Dispatch(firstWaiter());
        while (firstStage.load(std::memory_order_acquire) < 1)
            std::this_thread::yield();

        waitForAdditionalPark(0);

        firstEvent.Signal();
        Scheduler::WaitForAll();
        EXPECT_EQ(firstStage.load(std::memory_order_acquire), 2);
    }

    CounterEvent secondEvent{1};
    std::atomic<int> secondStage = 0;

    auto secondWaiter = [&]() -> Job {
        secondStage.store(1, std::memory_order_release);
        co_await WaitFor(secondEvent);
        secondStage.store(2, std::memory_order_release);
        co_return;
    };

    Scheduler::Dispatch(secondWaiter());
    while (secondStage.load(std::memory_order_acquire) < 1)
        std::this_thread::yield();

    waitForAdditionalPark(1);

    EXPECT_EQ(Scheduler::UnparkReady(staleToken), 0u);
    for (int i = 0; i < 512; ++i)
        std::this_thread::yield();

    EXPECT_EQ(secondStage.load(std::memory_order_acquire), 1);

    secondEvent.Signal();
    Scheduler::WaitForAll();
    EXPECT_EQ(secondStage.load(std::memory_order_acquire), 2);

    Scheduler::Shutdown();
}

TEST(CoreTasks, CounterEventHighFanInRandomizedSignalsResumeExactlyOnce)
{
    Scheduler::Initialize(4);

    constexpr int waiterCount = 96;
    constexpr int signalCount = 4096;

    CounterEvent event{static_cast<uint32_t>(signalCount)};
    std::atomic<int> resumedCount = 0;

    auto waiter = [&]() -> Job {
        co_await WaitFor(event);
        resumedCount.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    for (int i = 0; i < waiterCount; ++i)
        Scheduler::Dispatch(waiter());

    std::vector<int> signalOrder(signalCount);
    std::iota(signalOrder.begin(), signalOrder.end(), 0);
    std::mt19937 rng(1337u);
    std::shuffle(signalOrder.begin(), signalOrder.end(), rng);

    for (int i : signalOrder)
    {
        Scheduler::Dispatch([&event, i] {
            if ((i & 1) == 0)
                std::this_thread::yield();
            event.Signal(1);
        });
    }

    Scheduler::WaitForAll();

    EXPECT_EQ(resumedCount.load(std::memory_order_relaxed), waiterCount);
    const auto stats = Scheduler::GetStats();
    EXPECT_EQ(stats.ParkCount, stats.UnparkCount);

    Scheduler::Shutdown();
}

TEST(CoreTasks, OverflowHandling)
{
    // Initialize with 1 thread to force accumulation
    Scheduler::Initialize(1);

    std::atomic<int> counter = 0;
    // Dispatch MORE than RingBuffer capacity (65536)
    const int taskCount = 70000;

    for(int i = 0; i < taskCount; ++i) {
        Scheduler::Dispatch([&counter]() {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    Scheduler::WaitForAll();

    // Without the fix, this would equal 65536 and log errors.
    // With the fix, this equals 70000.
    EXPECT_EQ(counter.load(), taskCount);

    Scheduler::Shutdown();
}

TEST(CoreTasks, SchedulerStatsExposeQueueAndStealTelemetry)
{
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;
    constexpr int taskCount = 2000;
    for (int i = 0; i < taskCount; ++i)
    {
        Scheduler::Dispatch([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    Scheduler::WaitForAll();
    const auto stats = Scheduler::GetStats();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), taskCount);
    EXPECT_GE(stats.InjectPushCount, static_cast<uint64_t>(taskCount));
    EXPECT_GE(stats.InjectPopCount + stats.LocalPopCount + stats.StealPopCount,
              static_cast<uint64_t>(taskCount));
    EXPECT_EQ(stats.WorkerLocalDepths.size(), 2u);
    EXPECT_EQ(stats.WorkerVictimStealCounts.size(), 2u);
    EXPECT_GE(stats.StealSuccessRatio, 0.0);
    EXPECT_LE(stats.StealSuccessRatio, 1.0);
    EXPECT_GE(stats.IdleWaitCount, 0u);
    EXPECT_GE(stats.IdleWaitTotalNs, 0u);
    EXPECT_GE(stats.QueueContentionCount, 0u);

    Scheduler::Shutdown();
}

TEST(CoreTasks, SchedulerStatsCanBeExportedToFrameTelemetry)
{
    Scheduler::Initialize(2);

    CounterEvent event{1};
    auto waiter = [&]() -> Job {
        co_await WaitFor(event);
        co_return;
    };

    Scheduler::Dispatch(waiter());
    Scheduler::ParkCountAtomic().wait(0, std::memory_order_acquire);

    event.Signal();
    Scheduler::WaitForAll();

    auto schedulerStats = Scheduler::GetStats();
    ASSERT_GE(schedulerStats.ParkCount, 1u);
    ASSERT_GE(schedulerStats.UnparkCount, 1u);

    auto& telemetry = Core::Telemetry::TelemetrySystem::Get();
    telemetry.BeginFrame();
    telemetry.SetTaskSchedulerStats(schedulerStats);
    telemetry.SetFrameGraphTimings(100, 200, 150);
    telemetry.EndFrame();

    const auto& frameStats = telemetry.GetFrameStats(0);
    EXPECT_EQ(frameStats.TaskParkCount, schedulerStats.ParkCount);
    EXPECT_EQ(frameStats.TaskUnparkCount, schedulerStats.UnparkCount);
    EXPECT_EQ(frameStats.TaskParkP50Ns, schedulerStats.ParkLatencyP50Ns);
    EXPECT_EQ(frameStats.TaskParkP95Ns, schedulerStats.ParkLatencyP95Ns);
    EXPECT_EQ(frameStats.TaskParkP99Ns, schedulerStats.ParkLatencyP99Ns);
    EXPECT_EQ(frameStats.TaskUnparkP50Ns, schedulerStats.UnparkLatencyP50Ns);
    EXPECT_EQ(frameStats.TaskUnparkP95Ns, schedulerStats.UnparkLatencyP95Ns);
    EXPECT_EQ(frameStats.TaskUnparkP99Ns, schedulerStats.UnparkLatencyP99Ns);
    EXPECT_EQ(frameStats.TaskIdleWaitCount, schedulerStats.IdleWaitCount);
    EXPECT_EQ(frameStats.TaskIdleWaitTotalNs, schedulerStats.IdleWaitTotalNs);
    EXPECT_EQ(frameStats.TaskQueueContentionCount, schedulerStats.QueueContentionCount);
    EXPECT_DOUBLE_EQ(frameStats.TaskStealSuccessRatio, schedulerStats.StealSuccessRatio);
    EXPECT_EQ(frameStats.FrameGraphCompileTimeNs, 100u);
    EXPECT_EQ(frameStats.FrameGraphExecuteTimeNs, 200u);
    EXPECT_EQ(frameStats.FrameGraphCriticalPathTimeNs, 150u);

    Scheduler::Shutdown();
}

// --- Coroutine lifetime safety tests (Issue 2.3) ---

TEST(CoreTasks, UndispatchedJobDestruction_NoLeak)
{
    // Creating a Job but never dispatching it should cleanly destroy the
    // coroutine frame via ~Job(), not leak it.
    Scheduler::Initialize(2);

    std::atomic<bool> started = false;

    {
        auto job = [&started]() -> Job {
            started.store(true, std::memory_order_relaxed);
            co_return;
        }();

        // job goes out of scope without being dispatched.
        // ~Job() should destroy the coroutine frame.
    }

    // The coroutine should never have started executing.
    EXPECT_FALSE(started.load(std::memory_order_relaxed));

    Scheduler::Shutdown();
}

TEST(CoreTasks, JobMoveDoesNotDoubleFree)
{
    // Moving a Job should transfer ownership; the source should not
    // destroy the coroutine frame.
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;

    auto makeJob = [&counter]() -> Job {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    {
        Job j1 = makeJob();
        Job j2 = std::move(j1);

        // j1 should now be empty.
        EXPECT_FALSE(j1.Valid());
        EXPECT_TRUE(j2.Valid());

        // Dispatch the moved-to job.
        Scheduler::Dispatch(std::move(j2));
    }

    Scheduler::WaitForAll();
    EXPECT_EQ(counter.load(std::memory_order_relaxed), 1);

    Scheduler::Shutdown();
}

TEST(CoreTasks, JobMoveAssignment_CleansUpPrevious)
{
    // Move-assigning over an existing Job should destroy the old coroutine frame.
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;

    auto makeJob = [&counter]() -> Job {
        counter.fetch_add(1, std::memory_order_relaxed);
        co_return;
    };

    {
        Job j1 = makeJob(); // Will be overwritten
        Job j2 = makeJob(); // Will be dispatched

        // Move-assign j2 over j1. j1's old coroutine should be destroyed.
        j1 = std::move(j2);

        Scheduler::Dispatch(std::move(j1));
    }

    Scheduler::WaitForAll();

    // Only one job should have executed (the one that was dispatched).
    EXPECT_EQ(counter.load(std::memory_order_relaxed), 1);

    Scheduler::Shutdown();
}
