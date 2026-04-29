#include <gtest/gtest.h>
#include <atomic>

import Extrinsic.Core.Tasks;
import Extrinsic.Core.Tasks.CounterEvent;

using namespace Extrinsic::Core::Tasks;

TEST(CoreTasks, InitializeDispatchWaitAndShutdown)
{
    Scheduler::Initialize(2);
    ASSERT_TRUE(Scheduler::IsInitialized());

    std::atomic<int> counter = 0;
    for (int i = 0; i < 64; ++i)
    {
        Scheduler::Dispatch([&counter]()
        {
            counter.fetch_add(1, std::memory_order_relaxed);
        });
    }

    Scheduler::WaitForAll();
    EXPECT_EQ(counter.load(std::memory_order_relaxed), 64);

    const auto stats = Scheduler::GetStats();
    EXPECT_GE(stats.InjectPushCount, 64u);

    Scheduler::Shutdown();
    EXPECT_FALSE(Scheduler::IsInitialized());
}

TEST(CoreTasks, WaitTokenLifecycle)
{
    Scheduler::Initialize(1);

    auto token = Scheduler::AcquireWaitToken();
    EXPECT_TRUE(token.Valid());

    Scheduler::MarkWaitTokenNotReady(token);
    EXPECT_EQ(Scheduler::UnparkReady(token), 0u);

    Scheduler::ReleaseWaitToken(token);
    // Stale token should be ignored.
    EXPECT_EQ(Scheduler::UnparkReady(token), 0u);

    Scheduler::Shutdown();
}

TEST(CoreTasks, CounterEventAddAndSignal)
{
    Scheduler::Initialize(1);

    {
        CounterEvent evt{1};
        EXPECT_FALSE(evt.IsReady());

        evt.Signal();
        EXPECT_TRUE(evt.IsReady());

        evt.Add(2);
        EXPECT_FALSE(evt.IsReady());

        evt.Signal();
        EXPECT_FALSE(evt.IsReady());

        evt.Signal();
        EXPECT_TRUE(evt.IsReady());
    }

    Scheduler::Shutdown();
}
