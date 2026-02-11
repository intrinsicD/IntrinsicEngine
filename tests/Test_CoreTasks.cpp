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
