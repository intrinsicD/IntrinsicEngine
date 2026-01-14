#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
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
