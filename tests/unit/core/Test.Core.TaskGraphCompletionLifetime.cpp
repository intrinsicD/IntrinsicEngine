#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.Core.Dag.TaskGraph;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;

using namespace Extrinsic::Core;
using namespace Extrinsic::Core::Dag;

namespace
{
    class SchedulerFixture
    {
    public:
        explicit SchedulerFixture(const unsigned workerCount)
        {
            Tasks::Scheduler::Initialize(workerCount);
        }

        ~SchedulerFixture()
        {
            Tasks::Scheduler::WaitForAll();
            Tasks::Scheduler::Shutdown();
        }

        SchedulerFixture(const SchedulerFixture&) = delete;
        SchedulerFixture& operator=(const SchedulerFixture&) = delete;
    };

    void Publish(std::atomic<bool>& flag)
    {
        flag.store(true, std::memory_order_release);
        flag.notify_all();
    }

    void WaitUntilPublished(std::atomic<bool>& flag)
    {
        while (!flag.load(std::memory_order_acquire))
            flag.wait(false, std::memory_order_acquire);
    }

    void WaitUntilAtLeast(std::atomic<std::uint32_t>& value, const std::uint32_t target)
    {
        auto observed = value.load(std::memory_order_acquire);
        while (observed < target)
        {
            value.wait(observed, std::memory_order_acquire);
            observed = value.load(std::memory_order_acquire);
        }
    }
}

TEST(CoreTaskGraphCompletionLifetime, SubmittedGraphAllowsUnrelatedWorkBeforeDependencyOrderedWait)
{
    SchedulerFixture scheduler{3};

    std::atomic<bool> rootStarted{false};
    std::atomic<bool> releaseRoot{false};
    std::atomic<std::uint32_t> unrelatedWork{0u};
    std::mutex orderMutex;
    std::vector<std::uint32_t> order{};

    TaskGraph graph;
    graph.AddPass("Root",
        [&]()
        {
            {
                std::scoped_lock lock(orderMutex);
                order.push_back(0u);
            }
            Publish(rootStarted);
            WaitUntilPublished(releaseRoot);
        });
    graph.AddPass("Dependent",
        [](TaskGraphBuilder& builder) { builder.DependsOn(0u); },
        [&]()
        {
            std::scoped_lock lock(orderMutex);
            order.push_back(1u);
        });

    ASSERT_TRUE(graph.Compile().has_value());
    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());

    WaitUntilPublished(rootStarted);
    EXPECT_FALSE(submitted->IsReady());
    unrelatedWork.fetch_add(1u, std::memory_order_release);
    EXPECT_EQ(unrelatedWork.load(std::memory_order_acquire), 1u);

    Publish(releaseRoot);
    ASSERT_TRUE(submitted->Wait().has_value());
    EXPECT_TRUE(submitted->IsReady());

    std::scoped_lock lock(orderMutex);
    EXPECT_EQ(order, (std::vector<std::uint32_t>{0u, 1u}));
}

TEST(CoreTaskGraphCompletionLifetime, MainThreadPassesRunOnlyWhenOwnerPumps)
{
    SchedulerFixture scheduler{2};
    const auto ownerThread = std::this_thread::get_id();
    std::thread::id callbackThread{};
    std::atomic<bool> ran{false};

    TaskGraphPassOptions options{};
    options.MainThreadOnly = true;
    options.AllowParallel = false;

    TaskGraph graph;
    graph.AddPass("OwnerOnly", options,
        [](TaskGraphBuilder&) {},
        [&]()
        {
            callbackThread = std::this_thread::get_id();
            ran.store(true, std::memory_order_release);
        });

    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    EXPECT_FALSE(ran.load(std::memory_order_acquire));
    EXPECT_FALSE(submitted->IsReady());

    auto pumped = submitted->PumpMainThreadPasses();
    ASSERT_TRUE(pumped.has_value());
    EXPECT_EQ(*pumped, 1u);
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
    EXPECT_EQ(callbackThread, ownerThread);
    EXPECT_TRUE(submitted->IsReady());
    EXPECT_TRUE(submitted->Wait().has_value());
}

TEST(CoreTaskGraphCompletionLifetime, PumpAndWaitRejectNonOwnerThread)
{
    TaskGraphPassOptions options{};
    options.MainThreadOnly = true;
    options.AllowParallel = false;

    TaskGraph graph;
    graph.AddPass("OwnerOnly", options, [](TaskGraphBuilder&) {}, []() {});
    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());

    ErrorCode pumpError = ErrorCode::Success;
    ErrorCode waitError = ErrorCode::Success;
    std::thread nonOwner([completion = *submitted, &pumpError, &waitError]() mutable
    {
        const auto pumped = completion.PumpMainThreadPasses();
        if (!pumped.has_value())
            pumpError = pumped.error();
        const auto waited = completion.Wait();
        if (!waited.has_value())
            waitError = waited.error();
    });
    nonOwner.join();

    EXPECT_EQ(pumpError, ErrorCode::ThreadViolation);
    EXPECT_EQ(waitError, ErrorCode::ThreadViolation);
    ASSERT_TRUE(submitted->PumpMainThreadPasses().has_value());
    EXPECT_TRUE(submitted->Wait().has_value());
}

TEST(CoreTaskGraphCompletionLifetime, ResetFailsClosedWhileSubmissionIsLive)
{
    SchedulerFixture scheduler{2};
    std::atomic<bool> started{false};
    std::atomic<bool> release{false};

    TaskGraph graph;
    graph.AddPass("Blocked", [&]()
    {
        Publish(started);
        WaitUntilPublished(release);
    });

    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    WaitUntilPublished(started);

    const auto resetWhileLive = graph.Reset();
    EXPECT_FALSE(resetWhileLive.has_value());
    if (!resetWhileLive.has_value())
        EXPECT_EQ(resetWhileLive.error(), ErrorCode::InvalidState);
    EXPECT_EQ(graph.PassCount(), 1u);

    const auto secondSubmit = graph.Submit();
    EXPECT_FALSE(secondSubmit.has_value());
    if (!secondSubmit.has_value())
        EXPECT_EQ(secondSubmit.error(), ErrorCode::InvalidState);

    std::atomic<bool> lateSetupRan{false};
    std::atomic<bool> latePassRan{false};
    graph.AddPass("RejectedWhileLive",
                  [&](TaskGraphBuilder&)
                  {
                      lateSetupRan.store(true, std::memory_order_release);
                  },
                  [&]() { latePassRan.store(true, std::memory_order_release); });
    EXPECT_EQ(graph.PassCount(), 1u);

    Publish(release);
    ASSERT_TRUE(submitted->Wait().has_value());
    EXPECT_FALSE(lateSetupRan.load(std::memory_order_acquire));
    EXPECT_FALSE(latePassRan.load(std::memory_order_acquire));
    EXPECT_TRUE(graph.Reset().has_value());
    EXPECT_EQ(graph.PassCount(), 0u);
}

TEST(CoreTaskGraphCompletionLifetime, WorkerCompletionWakesOwnerForMainThreadSuccessor)
{
    SchedulerFixture scheduler{2};
    constexpr std::uint32_t kEpochs = 200u;
    std::atomic<std::uint32_t> ownerPasses{0u};

    for (std::uint32_t epoch = 0u; epoch < kEpochs; ++epoch)
    {
        std::atomic<bool> rootStarted{false};
        std::atomic<bool> beginWait{false};
        std::atomic<bool> releaseRoot{false};

        TaskGraph graph;
        graph.AddPass("WorkerRoot",
            [&]()
            {
                Publish(rootStarted);
                WaitUntilPublished(releaseRoot);
            });

        TaskGraphPassOptions ownerOptions{};
        ownerOptions.MainThreadOnly = true;
        ownerOptions.AllowParallel = false;
        graph.AddPass("OwnerSuccessor", ownerOptions,
            [](TaskGraphBuilder& builder) { builder.DependsOn(0u); },
            [&]() { ownerPasses.fetch_add(1u, std::memory_order_acq_rel); });

        auto submitted = graph.Submit();
        ASSERT_TRUE(submitted.has_value());
        WaitUntilPublished(rootStarted);

        std::thread releaser([&]()
        {
            WaitUntilPublished(beginWait);
            Publish(releaseRoot);
        });
        Publish(beginWait);
        ASSERT_TRUE(submitted->Wait().has_value());
        releaser.join();
    }

    EXPECT_EQ(ownerPasses.load(std::memory_order_acquire), kEpochs);
}

TEST(CoreTaskGraphCompletionLifetime, CompletionKeepsCallbacksAliveAfterGraphDestruction)
{
    TaskGraphCompletion completion;
    std::atomic<bool> ran{false};

    {
        TaskGraphPassOptions options{};
        options.MainThreadOnly = true;
        options.AllowParallel = false;

        TaskGraph graph;
        graph.AddPass("OutliveGraph", options,
            [](TaskGraphBuilder&) {},
            [&]()
            {
                ran.store(true, std::memory_order_release);
            });
        auto submitted = graph.Submit();
        ASSERT_TRUE(submitted.has_value());
        completion = std::move(*submitted);
    }

    EXPECT_FALSE(completion.IsReady());
    ASSERT_TRUE(completion.Wait().has_value());
    EXPECT_TRUE(ran.load(std::memory_order_acquire));
    EXPECT_TRUE(completion.IsReady());
}

TEST(CoreTaskGraphCompletionLifetime, DroppingLastHandleWithOwnerWorkLeavesGraphFailClosed)
{
    std::atomic<bool> ran{false};
    TaskGraphPassOptions options{};
    options.MainThreadOnly = true;
    options.AllowParallel = false;

    TaskGraph graph;
    graph.AddPass("Unpumped", options,
        [](TaskGraphBuilder&) {},
        [&]() { ran.store(true, std::memory_order_release); });

    {
        auto submitted = graph.Submit();
        ASSERT_TRUE(submitted.has_value());
        EXPECT_FALSE(submitted->IsReady());
    }

    EXPECT_FALSE(ran.load(std::memory_order_acquire));
    const auto reset = graph.Reset();
    ASSERT_FALSE(reset.has_value());
    EXPECT_EQ(reset.error(), ErrorCode::InvalidState);
}

TEST(CoreTaskGraphCompletionLifetime, WaitHelpRunsGraphWorkWhenSingleWorkerIsSaturated)
{
    SchedulerFixture scheduler{1};
    std::atomic<bool> blockerStarted{false};
    std::atomic<bool> releaseBlocker{false};
    std::thread::id callbackThread{};
    const auto waitingThread = std::this_thread::get_id();

    Tasks::Scheduler::Dispatch([&]()
    {
        Publish(blockerStarted);
        WaitUntilPublished(releaseBlocker);
    });
    WaitUntilPublished(blockerStarted);

    TaskGraph graph;
    graph.AddPass("Helped", [&]() { callbackThread = std::this_thread::get_id(); });
    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    const auto waited = submitted->Wait();
    Publish(releaseBlocker);
    Tasks::Scheduler::WaitForAll();
    ASSERT_TRUE(waited.has_value());

    EXPECT_EQ(callbackThread, waitingThread);
}

TEST(CoreTaskGraphCompletionLifetime, ExternalHelperStealsWorkerLocalWork)
{
    SchedulerFixture scheduler{1};
    std::atomic<bool> childQueued{false};
    std::atomic<bool> releaseParent{false};
    std::atomic<bool> childRan{false};

    Tasks::Scheduler::Dispatch([&]()
    {
        Tasks::Scheduler::Dispatch([&]() { Publish(childRan); });
        Publish(childQueued);
        WaitUntilPublished(releaseParent);
    });
    WaitUntilPublished(childQueued);

    EXPECT_TRUE(Tasks::Scheduler::TryRunOne());
    EXPECT_TRUE(childRan.load(std::memory_order_acquire));
    Publish(releaseParent);
    Tasks::Scheduler::WaitForAll();
}

TEST(CoreTaskGraphCompletionLifetime, WaitStealsWorkerLocalWorkNeededByGraph)
{
    SchedulerFixture scheduler{1};
    std::atomic<bool> childQueued{false};
    std::atomic<bool> childRan{false};
    std::thread::id childThread{};
    const auto waitingThread = std::this_thread::get_id();

    TaskGraph graph;
    graph.AddPass("Parent", [&]()
    {
        Tasks::Scheduler::Dispatch([&]()
        {
            childThread = std::this_thread::get_id();
            Publish(childRan);
        });
        Publish(childQueued);
        WaitUntilPublished(childRan);
    });

    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    WaitUntilPublished(childQueued);
    ASSERT_TRUE(submitted->Wait().has_value());

    EXPECT_EQ(childThread, waitingThread);
}

TEST(CoreTaskGraphCompletionLifetime, NonOwnerCanPollCopiedCompletionUntilReady)
{
    SchedulerFixture scheduler{2};
    std::atomic<bool> rootStarted{false};
    std::atomic<bool> releaseRoot{false};
    std::atomic<bool> observedNotReady{false};
    std::atomic<bool> observedReady{false};

    TaskGraph graph;
    graph.AddPass("Polled", [&]()
    {
        Publish(rootStarted);
        WaitUntilPublished(releaseRoot);
    });

    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    WaitUntilPublished(rootStarted);

    std::thread poller([completion = *submitted, &observedNotReady, &observedReady]()
    {
        while (!completion.IsReady())
        {
            Publish(observedNotReady);
            std::this_thread::yield();
        }
        Publish(observedReady);
    });

    WaitUntilPublished(observedNotReady);
    Publish(releaseRoot);
    ASSERT_TRUE(submitted->Wait().has_value());
    poller.join();

    EXPECT_TRUE(observedNotReady.load(std::memory_order_acquire));
    EXPECT_TRUE(observedReady.load(std::memory_order_acquire));
}

TEST(CoreTaskGraphCompletionLifetime, PendingWorkerSubmissionFailsClosedAfterSchedulerReplacement)
{
    SchedulerFixture scheduler{2};
    std::atomic<bool> ownerPassRan{false};

    TaskGraph graph;
    graph.AddPass("WorkerRoot", []() {});

    TaskGraphPassOptions ownerOptions{};
    ownerOptions.MainThreadOnly = true;
    ownerOptions.AllowParallel = false;
    graph.AddPass("OwnerSuccessor", ownerOptions,
        [](TaskGraphBuilder& builder) { builder.DependsOn(0u); },
        [&]() { ownerPassRan.store(true, std::memory_order_release); });

    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    Tasks::Scheduler::WaitForAll();
    EXPECT_FALSE(submitted->IsReady());

    Tasks::Scheduler::Shutdown();
    Tasks::Scheduler::Initialize(2);

    const auto pumped = submitted->PumpMainThreadPasses();
    ASSERT_FALSE(pumped.has_value());
    EXPECT_EQ(pumped.error(), ErrorCode::InvalidState);
    const auto waited = submitted->Wait();
    ASSERT_FALSE(waited.has_value());
    EXPECT_EQ(waited.error(), ErrorCode::InvalidState);
    EXPECT_FALSE(ownerPassRan.load(std::memory_order_acquire));
}

TEST(CoreTaskGraphCompletionLifetime, SubmittedIndependentPassesRunInParallel)
{
    SchedulerFixture scheduler{4};
    constexpr std::uint32_t kPasses = 12u;
    std::atomic<std::uint32_t> active{0u};
    std::atomic<std::uint32_t> executed{0u};
    std::atomic<bool> release{false};

    TaskGraph graph;
    for (std::uint32_t pass = 0u; pass < kPasses; ++pass)
    {
        graph.AddPass(std::string("Parallel") + std::to_string(pass), [&]()
        {
            active.fetch_add(1u, std::memory_order_acq_rel);
            active.notify_all();
            WaitUntilPublished(release);
            active.fetch_sub(1u, std::memory_order_acq_rel);
            executed.fetch_add(1u, std::memory_order_acq_rel);
        });
    }

    auto submitted = graph.Submit();
    ASSERT_TRUE(submitted.has_value());
    WaitUntilAtLeast(active, 2u);
    EXPECT_FALSE(submitted->IsReady());
    Publish(release);
    ASSERT_TRUE(submitted->Wait().has_value());
    EXPECT_EQ(executed.load(std::memory_order_acquire), kPasses);
}

TEST(CoreTaskGraphCompletionLifetime, SubmitKeepsCompletionStateAliveUntilWorkerClosuresRetire)
{
    SchedulerFixture scheduler{4};

    constexpr std::uint32_t kEpochs = 300u;
    constexpr std::uint32_t kPasses = 24u;
    std::atomic<std::uint32_t> executed{0u};

    for (std::uint32_t epoch = 0u; epoch < kEpochs; ++epoch)
    {
        TaskGraph graph;
        for (std::uint32_t pass = 0u; pass < kPasses; ++pass)
        {
            graph.AddPass(std::string("RacePass") + std::to_string(epoch) + "_" + std::to_string(pass),
                          [](TaskGraphBuilder&) {},
                          [&executed]()
                          {
                              executed.fetch_add(1u, std::memory_order_acq_rel);
                              std::this_thread::yield();
                          });
        }

        ASSERT_TRUE(graph.Compile().has_value()) << "Compile failed at epoch " << epoch;
        auto submitted = graph.Submit();
        ASSERT_TRUE(submitted.has_value()) << "Submit failed at epoch " << epoch;
        ASSERT_TRUE(submitted->Wait().has_value()) << "Wait failed at epoch " << epoch;
    }

    EXPECT_EQ(executed.load(std::memory_order_acquire), kEpochs * kPasses);
}
