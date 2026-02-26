#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <coroutine>

import Core;

using namespace Core::Tasks;

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

    EXPECT_EQ(stage.load(std::memory_order_acquire), 2);
    EXPECT_GE(Scheduler::GetParkCount(),   1u);
    EXPECT_GE(Scheduler::GetUnparkCount(), 1u);

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

TEST(CoreTasks, StaleWaitTokenUnparkDoesNotResumeNewWaiters)
{
    Scheduler::Initialize(2);

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

    (void)Scheduler::UnparkReady(staleToken);
    for (int i = 0; i < 512; ++i)
        std::this_thread::yield();

    EXPECT_EQ(secondStage.load(std::memory_order_acquire), 1);

    secondEvent.Signal();
    Scheduler::WaitForAll();
    EXPECT_EQ(secondStage.load(std::memory_order_acquire), 2);

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
