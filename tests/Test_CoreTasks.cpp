#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
import Core.Tasks;
import Core.Logging;

using namespace Core::Tasks;

TEST(CoreTasks, BasicDispatch)
{
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;

    for (int i = 0; i < 100; ++i)
    {
        Scheduler::Dispatch([&counter]()
        {
            counter++;
        });
    }

    Scheduler::WaitForAll();

    EXPECT_EQ(counter, 100);

    Scheduler::Shutdown();
}

TEST(CoreTasks, NestedDispatch)
{
    Scheduler::Initialize(3);

    std::atomic<int> counter = 0;

    Scheduler::Dispatch([&counter]()
    {
        Scheduler::Dispatch([&counter]()
        {
            counter.fetch_add(1, std::memory_order_relaxed);
        });

        counter.fetch_add(1, std::memory_order_relaxed);
    });

    Scheduler::WaitForAll();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 2);

    Scheduler::Shutdown();
}

TEST(CoreTasks, ContendedDispatch)
{
    Scheduler::Initialize(4);

    std::atomic<int> counter = 0;
    std::vector<std::thread> producers;

    for (int t = 0; t < 4; ++t)
    {
        producers.emplace_back([&counter]()
        {
            for (int i = 0; i < 50; ++i)
            {
                Scheduler::Dispatch([&counter]()
                {
                    counter.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    for (auto& thread : producers)
    {
        thread.join();
    }

    Scheduler::WaitForAll();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 200);

    Scheduler::Shutdown();
}

TEST(CoreTasks, GracefulShutdownWithOutstandingWork)
{
    Scheduler::Initialize(2);

    std::atomic<int> counter = 0;

    for (int i = 0; i < 20; ++i)
    {
        Scheduler::Dispatch([&counter]()
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    Scheduler::Shutdown();

    EXPECT_EQ(counter.load(std::memory_order_relaxed), 20);
}
