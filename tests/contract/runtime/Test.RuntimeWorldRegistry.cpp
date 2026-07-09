#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Tasks;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldRegistry;

using Extrinsic::Runtime::ActiveWorldChanged;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::EventBus;
using Extrinsic::Runtime::JobCancellation;
using Extrinsic::Runtime::JobService;
using Extrinsic::Runtime::JobTarget;
using Extrinsic::Runtime::MakeJobDesc;
using Extrinsic::Runtime::WorldHandle;
using Extrinsic::Runtime::WorldRegistry;
using Extrinsic::Runtime::WorldWillBeDestroyed;

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

    template <typename TFuture>
    void ExpectReady(TFuture& future)
    {
        EXPECT_EQ(future.wait_for(2s), std::future_status::ready);
    }

    struct DestroyJobResult
    {
        bool SawCancellation{false};
    };

    struct DestroyJobCompleted
    {
    };

    class BootWorldApplication final : public Extrinsic::Runtime::IApplication
    {
    public:
        void OnInitialize(Engine& engine) override
        {
            const WorldHandle active = engine.ActiveWorld();
            SawValidWorld = active.IsValid() &&
                            engine.Worlds().Contains(active) &&
                            engine.Worlds().Get(active) == &engine.GetScene();
            const auto entity = engine.GetScene().Create();
            SceneWritable = engine.GetScene().IsValid(entity);
        }

        void OnSimTick(Engine&, double) override {}
        void OnVariableTick(Engine&, double, double) override {}
        void OnShutdown(Engine&) override {}

        bool SawValidWorld{false};
        bool SceneWritable{false};
    };

    [[nodiscard]] Extrinsic::Core::Config::EngineConfig MinimalEngineConfig()
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        return config;
    }
}

TEST(RuntimeWorldRegistry, CreateWorldMakesFirstWorldActive)
{
    WorldRegistry worlds;

    const WorldHandle main = worlds.CreateWorld("main");

    ASSERT_TRUE(main.IsValid());
    EXPECT_EQ(worlds.ActiveWorld(), main);
    ASSERT_NE(worlds.Get(main), nullptr);
    EXPECT_TRUE(worlds.Contains(main));
    EXPECT_EQ(worlds.WorldCount(), 1u);

    const auto entity = worlds.Get(main)->Create();
    EXPECT_TRUE(worlds.Get(main)->IsValid(entity));
}

TEST(RuntimeWorldRegistry, ActiveWorldSwapDefersUntilMaintenanceAndPump)
{
    WorldRegistry worlds;
    EventBus events;
    JobService jobs;

    const WorldHandle first = worlds.CreateWorld("first");
    const WorldHandle second = worlds.CreateWorld("second");
    ASSERT_EQ(worlds.ActiveWorld(), first);

    bool observed = false;
    ActiveWorldChanged observedEvent{};
    const auto subscription =
        events.Subscribe<ActiveWorldChanged>(
            [&](const ActiveWorldChanged& event)
            {
                observed = true;
                observedEvent = event;
            });
    ASSERT_TRUE(subscription.IsValid());

    ASSERT_TRUE(worlds.RequestSetActiveWorld(second));
    EXPECT_EQ(worlds.ActiveWorld(), first);

    const auto stats = worlds.ApplyDeferredOperations(events, jobs);
    EXPECT_EQ(stats.ActiveWorldChanges, 1u);
    EXPECT_EQ(worlds.ActiveWorld(), second);
    EXPECT_FALSE(observed);
    EXPECT_EQ(events.PendingCount(), 1u);

    events.Pump();

    EXPECT_TRUE(observed);
    EXPECT_EQ(observedEvent.Previous, first);
    EXPECT_EQ(observedEvent.Current, second);
    EXPECT_EQ(observedEvent.DebugName, "second");
}

TEST(RuntimeWorldRegistry, DestroyAnnouncesCancelsJobsThenTearsDownOnNextMaintenance)
{
    SchedulerScope scheduler{2};
    WorldRegistry worlds;
    JobService jobs;
    EventBus events;

    const WorldHandle main = worlds.CreateWorld("main");
    const WorldHandle doomed = worlds.CreateWorld("doomed");
    ASSERT_NE(main, doomed);

    std::promise<void> workerStarted;
    auto workerStartedFuture = workerStarted.get_future();
    std::atomic_bool sawCancellation{false};

    int completed = 0;
    const auto completionSubscription =
        events.Subscribe<DestroyJobCompleted>(
            [&](const DestroyJobCompleted&)
            {
                ++completed;
            });
    ASSERT_TRUE(completionSubscription.IsValid());

    const auto token = jobs.Submit(
        MakeJobDesc<DestroyJobResult>(
            "world-scoped-destroy-probe",
            JobTarget::CpuPool,
            doomed,
            [&workerStarted, &sawCancellation](
                const JobCancellation& cancellation) -> DestroyJobResult
            {
                workerStarted.set_value();
                while (!cancellation.IsCancellationRequested())
                {
                    std::this_thread::yield();
                }
                sawCancellation.store(true, std::memory_order_release);
                return DestroyJobResult{.SawCancellation = true};
            },
            [](EventBus& bus, DestroyJobResult&& result)
            {
                if (result.SawCancellation)
                {
                    bus.Publish(DestroyJobCompleted{});
                }
            }));
    ASSERT_TRUE(token.IsValid());
    ExpectReady(workerStartedFuture);

    int announced = 0;
    WorldWillBeDestroyed observedDestroy{};
    const auto destroySubscription =
        events.Subscribe<WorldWillBeDestroyed>(
            [&](const WorldWillBeDestroyed& event)
            {
                ++announced;
                observedDestroy = event;
            });
    ASSERT_TRUE(destroySubscription.IsValid());

    ASSERT_TRUE(worlds.RequestDestroyWorld(doomed));
    const auto announceStats = worlds.ApplyDeferredOperations(events, jobs);

    EXPECT_EQ(announceStats.DestroyAnnouncedWorlds, 1u);
    EXPECT_EQ(announceStats.DestroyedWorlds, 0u);
    EXPECT_EQ(announceStats.CancelledJobs, 1u);
    EXPECT_TRUE(worlds.IsDestroyAnnounced(doomed));
    EXPECT_NE(worlds.Get(doomed), nullptr);

    events.Pump();
    EXPECT_EQ(announced, 1);
    EXPECT_EQ(observedDestroy.World, doomed);
    EXPECT_EQ(observedDestroy.DebugName, "doomed");

    Extrinsic::Core::Tasks::Scheduler::WaitForAll();
    jobs.DrainCompletions(events);
    events.Pump();

    EXPECT_TRUE(sawCancellation.load(std::memory_order_acquire));
    EXPECT_EQ(completed, 0);
    EXPECT_TRUE(jobs.IsComplete(token));

    const auto teardownStats = worlds.ApplyDeferredOperations(events, jobs);
    EXPECT_EQ(teardownStats.DestroyedWorlds, 1u);
    EXPECT_EQ(worlds.Get(doomed), nullptr);
    EXPECT_FALSE(worlds.Contains(doomed));
    EXPECT_EQ(worlds.WorldCount(), 1u);
}

TEST(RuntimeWorldRegistry, EngineBootCreatesValidActiveWorldBeforeApplicationInitialize)
{
    auto app = std::make_unique<BootWorldApplication>();
    BootWorldApplication* observed = app.get();

    Engine engine(MinimalEngineConfig(), std::move(app));
    engine.Initialize();

    EXPECT_TRUE(observed->SawValidWorld);
    EXPECT_TRUE(observed->SceneWritable);
    EXPECT_TRUE(engine.ActiveWorld().IsValid());
    EXPECT_TRUE(engine.Worlds().Contains(engine.ActiveWorld()));

    engine.Shutdown();
}
