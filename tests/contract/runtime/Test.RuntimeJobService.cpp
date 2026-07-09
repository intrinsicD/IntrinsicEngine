#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;

using Extrinsic::Runtime::EventBus;
using Extrinsic::Runtime::JobCancellation;
using Extrinsic::Runtime::JobService;
using Extrinsic::Runtime::JobTarget;
using Extrinsic::Runtime::MakeJobDesc;
using Extrinsic::Runtime::WorldHandle;

namespace
{
    using namespace std::chrono_literals;

    struct SchedulerScope
    {
        bool Owned{false};

        explicit SchedulerScope(const unsigned workerCount)
        {
            if (!Extrinsic::Core::Tasks::Scheduler::IsInitialized())
            {
                Extrinsic::Core::Tasks::Scheduler::Initialize(workerCount);
                Owned = true;
            }
        }

        ~SchedulerScope()
        {
            if (Owned)
            {
                Extrinsic::Core::Tasks::Scheduler::WaitForAll();
                Extrinsic::Core::Tasks::Scheduler::Shutdown();
            }
        }
    };

    struct JobResult
    {
        int Value{0};
        bool WorkerWasNotMain{false};
    };

    struct JobCompleted
    {
        int Value{0};
        bool WorkerWasNotMain{false};
        bool PublishedOnMain{false};
    };

    template <typename TFuture>
    void ExpectReady(TFuture& future)
    {
        EXPECT_EQ(future.wait_for(2s), std::future_status::ready);
    }
}

TEST(RuntimeJobService, CompletionPublishesAtPumpBOnMainThread)
{
    SchedulerScope scheduler{2};
    JobService jobs;
    EventBus events;

    const std::thread::id mainThread = std::this_thread::get_id();
    std::promise<void> workerStarted;
    auto workerStartedFuture = workerStarted.get_future();

    bool observed = false;
    JobCompleted observedEvent{};
    const auto subscription =
        events.Subscribe<JobCompleted>(
            [&](const JobCompleted& event)
            {
                observed = true;
                observedEvent = event;
                EXPECT_EQ(std::this_thread::get_id(), mainThread);
            });
    ASSERT_TRUE(subscription.IsValid());

    const auto token = jobs.Submit(
        MakeJobDesc<JobResult>(
            "complete-on-pump-b",
            JobTarget::CpuPool,
            WorldHandle{1u, 1u},
            [&workerStarted, mainThread](
                const JobCancellation&) -> JobResult
            {
                workerStarted.set_value();
                return JobResult{
                    .Value = 7,
                    .WorkerWasNotMain = std::this_thread::get_id() != mainThread};
            },
            [mainThread](EventBus& bus, JobResult&& result)
            {
                bus.Publish(JobCompleted{
                    .Value = result.Value,
                    .WorkerWasNotMain = result.WorkerWasNotMain,
                    .PublishedOnMain = std::this_thread::get_id() == mainThread});
            }));

    ASSERT_TRUE(token.IsValid());
    ExpectReady(workerStartedFuture);
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    EXPECT_EQ(events.PendingCount(), 0u);
    jobs.DrainCompletions(events);

    EXPECT_EQ(jobs.Stats().PublishedCompletions, 1u);
    EXPECT_EQ(jobs.Stats().LastDrainPublished, 1u);
    EXPECT_EQ(events.PendingCount(), 1u);
    EXPECT_FALSE(observed);

    events.Pump();

    EXPECT_TRUE(observed);
    EXPECT_EQ(observedEvent.Value, 7);
    EXPECT_TRUE(observedEvent.WorkerWasNotMain);
    EXPECT_TRUE(observedEvent.PublishedOnMain);
    EXPECT_TRUE(jobs.IsComplete(token));
    EXPECT_EQ(jobs.ReapCompleted(), 1u);
    EXPECT_EQ(jobs.Stats().Reaped, 1u);
}

TEST(RuntimeJobService, CancelBeforeStartSkipsWorkAndDropsCompletion)
{
    SchedulerScope scheduler{1};
    JobService jobs;
    EventBus events;

    std::promise<void> blockerStarted;
    std::promise<void> releaseBlocker;
    auto blockerStartedFuture = blockerStarted.get_future();
    auto releaseFuture = releaseBlocker.get_future().share();

    Extrinsic::Core::Tasks::Scheduler::Dispatch(
        [&blockerStarted, releaseFuture]
        {
            blockerStarted.set_value();
            releaseFuture.wait();
        });
    ExpectReady(blockerStartedFuture);

    std::atomic_bool ran{false};
    int observed = 0;
    const auto subscription =
        events.Subscribe<JobCompleted>(
            [&](const JobCompleted&)
            {
                ++observed;
            });
    ASSERT_TRUE(subscription.IsValid());

    const auto token = jobs.Submit(
        MakeJobDesc<JobResult>(
            "cancel-before-start",
            JobTarget::CpuPool,
            WorldHandle{1u, 1u},
            [&ran](const JobCancellation&) -> JobResult
            {
                ran.store(true, std::memory_order_release);
                return JobResult{.Value = 1, .WorkerWasNotMain = true};
            },
            [](EventBus& bus, JobResult&& result)
            {
                bus.Publish(JobCompleted{.Value = result.Value});
            }));

    ASSERT_TRUE(token.IsValid());
    jobs.Cancel(token);
    releaseBlocker.set_value();
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    jobs.DrainCompletions(events);
    events.Pump();

    EXPECT_FALSE(ran.load(std::memory_order_acquire));
    EXPECT_EQ(observed, 0);
    EXPECT_TRUE(jobs.IsComplete(token));
    EXPECT_EQ(jobs.Stats().CancelRequested, 1u);
    EXPECT_EQ(jobs.Stats().DroppedCancelled, 1u);
}

TEST(RuntimeJobService, CancelMidFlightIsObservedAndDroppedAtGate)
{
    SchedulerScope scheduler{2};
    JobService jobs;
    EventBus events;

    std::promise<void> workerStarted;
    auto workerStartedFuture = workerStarted.get_future();
    std::atomic_bool sawCancellation{false};
    int observed = 0;
    const auto subscription =
        events.Subscribe<JobCompleted>(
            [&](const JobCompleted&)
            {
                ++observed;
            });
    ASSERT_TRUE(subscription.IsValid());

    const auto token = jobs.Submit(
        MakeJobDesc<JobResult>(
            "cancel-mid-flight",
            JobTarget::CpuPool,
            WorldHandle{1u, 1u},
            [&workerStarted, &sawCancellation](
                const JobCancellation& cancellation) -> JobResult
            {
                workerStarted.set_value();
                while (!cancellation.IsCancellationRequested())
                {
                    std::this_thread::yield();
                }
                sawCancellation.store(true, std::memory_order_release);
                return JobResult{.Value = 2, .WorkerWasNotMain = true};
            },
            [](EventBus& bus, JobResult&& result)
            {
                bus.Publish(JobCompleted{.Value = result.Value});
            }));

    ASSERT_TRUE(token.IsValid());
    ExpectReady(workerStartedFuture);
    jobs.Cancel(token);
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    jobs.DrainCompletions(events);
    events.Pump();

    EXPECT_TRUE(sawCancellation.load(std::memory_order_acquire));
    EXPECT_EQ(observed, 0);
    EXPECT_TRUE(jobs.IsComplete(token));
    EXPECT_EQ(jobs.Stats().DroppedCancelled, 1u);
}

TEST(RuntimeJobService, FinishedJobCancelledBeforeGatePublishesNothing)
{
    SchedulerScope scheduler{2};
    JobService jobs;
    EventBus events;

    std::promise<void> workerFinished;
    auto workerFinishedFuture = workerFinished.get_future();
    int observed = 0;
    const auto subscription =
        events.Subscribe<JobCompleted>(
            [&](const JobCompleted&)
            {
                ++observed;
            });
    ASSERT_TRUE(subscription.IsValid());

    const auto token = jobs.Submit(
        MakeJobDesc<JobResult>(
            "cancel-after-finish-before-gate",
            JobTarget::CpuPool,
            WorldHandle{1u, 1u},
            [&workerFinished](const JobCancellation&) -> JobResult
            {
                workerFinished.set_value();
                return JobResult{.Value = 3, .WorkerWasNotMain = true};
            },
            [](EventBus& bus, JobResult&& result)
            {
                bus.Publish(JobCompleted{.Value = result.Value});
            }));

    ASSERT_TRUE(token.IsValid());
    ExpectReady(workerFinishedFuture);
    Extrinsic::Core::Tasks::Scheduler::WaitForAll();

    jobs.Cancel(token);
    jobs.DrainCompletions(events);
    events.Pump();

    EXPECT_EQ(observed, 0);
    EXPECT_TRUE(jobs.IsComplete(token));
    EXPECT_EQ(jobs.Stats().DroppedCancelled, 1u);
}

TEST(RuntimeJobService, CancelAllForWorldCancelsOnlyThatScope)
{
    SchedulerScope scheduler{2};
    JobService jobs;
    EventBus events;

    const WorldHandle worldA{1u, 1u};
    const WorldHandle worldB{2u, 1u};
    std::vector<int> observed;
    const auto subscription =
        events.Subscribe<JobCompleted>(
            [&](const JobCompleted& event)
            {
                observed.push_back(event.Value);
            });
    ASSERT_TRUE(subscription.IsValid());

    const auto submitScoped = [&](WorldHandle world, int value)
    {
        return jobs.Submit(
            MakeJobDesc<JobResult>(
                "scoped",
                JobTarget::CpuPool,
                world,
                [value](const JobCancellation&) -> JobResult
                {
                    return JobResult{.Value = value, .WorkerWasNotMain = true};
                },
                [](EventBus& bus, JobResult&& result)
                {
                    bus.Publish(JobCompleted{.Value = result.Value});
                }));
    };

    const auto tokenA = submitScoped(worldA, 10);
    const auto tokenB = submitScoped(worldB, 20);
    ASSERT_TRUE(tokenA.IsValid());
    ASSERT_TRUE(tokenB.IsValid());

    Extrinsic::Core::Tasks::Scheduler::WaitForAll();
    EXPECT_EQ(jobs.CancelAllForWorld(worldA), 1u);

    jobs.DrainCompletions(events);
    events.Pump();

    ASSERT_EQ(observed.size(), 1u);
    EXPECT_EQ(observed[0], 20);
    EXPECT_TRUE(jobs.IsComplete(tokenA));
    EXPECT_TRUE(jobs.IsComplete(tokenB));
    EXPECT_EQ(jobs.Stats().DroppedCancelled, 1u);
    EXPECT_EQ(jobs.Stats().PublishedCompletions, 1u);
}
