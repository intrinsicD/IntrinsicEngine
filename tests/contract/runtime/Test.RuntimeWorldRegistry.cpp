#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.Tasks;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    using namespace std::chrono_literals;

    struct DestroyProbeResult
    {
        int Value{0};
    };

    struct DestroyProbeCompleted
    {
        int Value{0};
    };

    class SchedulerScope final
    {
    public:
        explicit SchedulerScope(const unsigned workers = 2)
        {
            if (Extrinsic::Core::Tasks::Scheduler::IsInitialized())
                Extrinsic::Core::Tasks::Scheduler::Shutdown();
            Extrinsic::Core::Tasks::Scheduler::Initialize(workers);
        }

        ~SchedulerScope()
        {
            Extrinsic::Core::Tasks::Scheduler::WaitForAll();
            Extrinsic::Core::Tasks::Scheduler::Shutdown();
        }

        SchedulerScope(const SchedulerScope&) = delete;
        SchedulerScope& operator=(const SchedulerScope&) = delete;
    };

    template <typename Predicate>
    [[nodiscard]] bool WaitUntil(Predicate&& predicate,
                                 const std::chrono::milliseconds timeout = 2s)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::sleep_for(1ms);
        }
        return true;
    }

    [[nodiscard]] CoreConfig::EngineConfig NullWindowHeadlessConfig()
    {
        CoreConfig::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        return config;
    }

    class EngineWorldProbeApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            InitActive = engine.ActiveWorld();
            InitScene = &engine.GetScene();
            InitRegistryScene = engine.Worlds().Get(InitActive);
            CreatedWorld = engine.Worlds().CreateWorld("secondary");
            RequestResult = engine.Worlds().RequestSetActiveWorld(CreatedWorld);
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++VariableTicks;
            if (VariableTicks == 1u)
            {
                FirstVariableActive = engine.ActiveWorld();
                FirstVariableScene = &engine.GetScene();
                return;
            }

            if (VariableTicks == 2u)
            {
                SecondVariableActive = engine.ActiveWorld();
                SecondVariableScene = &engine.GetScene();
                return;
            }

            ThirdVariableActive = engine.ActiveWorld();
            LastExtractionWorld = engine.GetLastRenderExtractionStats().World;
            engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::WorldHandle InitActive{};
        Runtime::WorldHandle CreatedWorld{};
        Runtime::WorldHandle FirstVariableActive{};
        Runtime::WorldHandle SecondVariableActive{};
        Runtime::WorldHandle ThirdVariableActive{};
        Runtime::WorldHandle LastExtractionWorld{};
        Extrinsic::Core::Result RequestResult{};
        Extrinsic::ECS::Scene::Registry* InitScene{};
        Extrinsic::ECS::Scene::Registry* InitRegistryScene{};
        Extrinsic::ECS::Scene::Registry* FirstVariableScene{};
        Extrinsic::ECS::Scene::Registry* SecondVariableScene{};
        std::uint32_t VariableTicks{0};
    };
}

TEST(RuntimeWorldRegistry, CreateWorldBootsDefaultHandleAndDistinctRegistries)
{
    Runtime::WorldRegistry worlds;

    const Runtime::WorldHandle first = worlds.CreateWorld("main");
    const Runtime::WorldHandle second = worlds.CreateWorld("preview");

    EXPECT_EQ(first, Runtime::DefaultWorldHandle);
    EXPECT_TRUE(second.IsValid());
    EXPECT_NE(first, second);
    EXPECT_EQ(worlds.ActiveWorld(), first);
    ASSERT_NE(worlds.Get(first), nullptr);
    ASSERT_NE(worlds.Get(second), nullptr);
    EXPECT_NE(worlds.Get(first), worlds.Get(second));
    EXPECT_EQ(worlds.LiveWorldCount(), 2u);
    EXPECT_EQ(worlds.DebugName(second), "preview");
}

TEST(RuntimeWorldRegistry, ActiveWorldChangeIsDeferredToMaintenanceAndNextPump)
{
    Runtime::WorldRegistry worlds;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;

    const Runtime::WorldHandle first = worlds.CreateWorld("main");
    const Runtime::WorldHandle second = worlds.CreateWorld("secondary");

    Runtime::ActiveWorldChanged observed{};
    int eventHits = 0;
    (void)events.Subscribe<Runtime::ActiveWorldChanged>(
        [&](const Runtime::ActiveWorldChanged& event)
        {
            observed = event;
            ++eventHits;
        });

    ASSERT_TRUE(worlds.RequestSetActiveWorld(second).has_value());
    EXPECT_EQ(worlds.ActiveWorld(), first);
    EXPECT_EQ(eventHits, 0);

    const Runtime::WorldRegistryMaintenanceStats stats =
        worlds.ApplyMaintenance(events, jobs);
    EXPECT_EQ(stats.AppliedActiveWorldChanges, 1u);
    EXPECT_EQ(worlds.ActiveWorld(), second);
    EXPECT_EQ(eventHits, 0);

    EXPECT_EQ(events.Pump(), 1u);
    EXPECT_EQ(eventHits, 1);
    EXPECT_EQ(observed.Previous, first);
    EXPECT_EQ(observed.Current, second);
}

TEST(RuntimeWorldRegistry, DestroyAnnouncesCancelsJobsAndTearsDownNextMaintenance)
{
    SchedulerScope scheduler{2};
    Runtime::WorldRegistry worlds;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;

    const Runtime::WorldHandle active = worlds.CreateWorld("main");
    const Runtime::WorldHandle doomed = worlds.CreateWorld("doomed");
    ASSERT_EQ(worlds.ActiveWorld(), active);

    std::atomic<bool> started{false};
    std::atomic<bool> observedCancellation{false};
    int completionHits = 0;
    int willDestroyHits = 0;
    Runtime::WorldWillBeDestroyed willDestroy{};

    (void)events.Subscribe<Runtime::WorldWillBeDestroyed>(
        [&](const Runtime::WorldWillBeDestroyed& event)
        {
            willDestroy = event;
            ++willDestroyHits;
        });
    (void)events.Subscribe<DestroyProbeCompleted>(
        [&](const DestroyProbeCompleted&) { ++completionHits; });

    const Runtime::JobToken token = jobs.Submit(
        Runtime::MakeCpuJobDesc<DestroyProbeResult>(
            "destroy scoped job",
            doomed,
            [&](const Runtime::JobCancellation& cancellation)
            {
                started.store(true, std::memory_order_release);
                while (!cancellation.IsCancelled())
                    std::this_thread::sleep_for(1ms);
                observedCancellation.store(true, std::memory_order_release);
                return DestroyProbeResult{.Value = 7};
            },
            [](const DestroyProbeResult& result)
            { return DestroyProbeCompleted{.Value = result.Value}; }));
    ASSERT_TRUE(token.IsValid());
    ASSERT_TRUE(WaitUntil([&] { return started.load(std::memory_order_acquire); }));

    ASSERT_TRUE(worlds.RequestDestroyWorld(doomed).has_value());
    const Runtime::WorldRegistryMaintenanceStats announce =
        worlds.ApplyMaintenance(events, jobs);
    EXPECT_EQ(announce.DestroyAnnouncements, 1u);
    EXPECT_EQ(announce.CancelledJobs, 1u);
    EXPECT_TRUE(worlds.Contains(doomed));
    EXPECT_EQ(willDestroyHits, 0);

    EXPECT_EQ(events.Pump(), 1u);
    EXPECT_EQ(willDestroyHits, 1);
    EXPECT_EQ(willDestroy.World, doomed);
    EXPECT_TRUE(worlds.Contains(doomed));

    Extrinsic::Core::Tasks::Scheduler::WaitForAll();
    EXPECT_TRUE(observedCancellation.load(std::memory_order_acquire));
    EXPECT_EQ(jobs.GetState(token), Runtime::JobState::Cancelled);
    EXPECT_EQ(jobs.DrainCompletions(events), 0u);
    EXPECT_EQ(events.Pump(), 0u);
    EXPECT_EQ(completionHits, 0);

    const Runtime::WorldRegistryMaintenanceStats teardown =
        worlds.ApplyMaintenance(events, jobs);
    EXPECT_EQ(teardown.DestroyedWorlds, 1u);
    EXPECT_FALSE(worlds.Contains(doomed));
    EXPECT_EQ(worlds.Get(doomed), nullptr);
    EXPECT_EQ(worlds.LiveWorldCount(), 1u);
    EXPECT_EQ(worlds.ActiveWorld(), active);
}

TEST(RuntimeWorldRegistry, DestroyActiveWorldIsRejected)
{
    Runtime::WorldRegistry worlds;
    const Runtime::WorldHandle active = worlds.CreateWorld("main");

    const Extrinsic::Core::Result result = worlds.RequestDestroyWorld(active);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::ResourceBusy);
    EXPECT_TRUE(worlds.Contains(active));
}

TEST(RuntimeWorldRegistry, ActivatingDestroyPendingWorldIsRejected)
{
    // BUG-075: a world with a pending destroy must not be activatable.
    Runtime::WorldRegistry worlds;
    const Runtime::WorldHandle active = worlds.CreateWorld("main");
    const Runtime::WorldHandle doomed = worlds.CreateWorld("doomed");
    ASSERT_EQ(worlds.ActiveWorld(), active);

    ASSERT_TRUE(worlds.RequestDestroyWorld(doomed).has_value());

    const Extrinsic::Core::Result result = worlds.RequestSetActiveWorld(doomed);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::ResourceBusy);
}

TEST(RuntimeWorldRegistry, PendingActivationDroppedWhenTargetDestroyRequested)
{
    // BUG-075: a destroy requested after a set-active was queued must drop the
    // stale activation at maintenance — the destroy-pending world must never
    // become active (and thus never active-and-destroy-announced).
    Runtime::WorldRegistry worlds;
    Runtime::KernelEventBus events;
    Runtime::JobService jobs;

    const Runtime::WorldHandle active = worlds.CreateWorld("main");
    const Runtime::WorldHandle doomed = worlds.CreateWorld("secondary");
    ASSERT_EQ(worlds.ActiveWorld(), active);

    ASSERT_TRUE(worlds.RequestSetActiveWorld(doomed).has_value());
    ASSERT_TRUE(worlds.RequestDestroyWorld(doomed).has_value());

    (void)worlds.ApplyMaintenance(events, jobs);

    EXPECT_EQ(worlds.ActiveWorld(), active);
}

TEST(RuntimeWorldRegistry, EngineBootsFrameZeroWorldAndAppliesSwitchAtMaintenance)
{
    auto app = std::make_unique<EngineWorldProbeApplication>();
    EngineWorldProbeApplication* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.Initialize();

    ASSERT_TRUE(appPtr->InitActive.IsValid());
    EXPECT_EQ(appPtr->InitActive, Runtime::DefaultWorldHandle);
    EXPECT_NE(appPtr->InitScene, nullptr);
    EXPECT_EQ(appPtr->InitRegistryScene, appPtr->InitScene);
    ASSERT_TRUE(appPtr->RequestResult.has_value());

    engine.Run();

    EXPECT_EQ(appPtr->VariableTicks, 3u);
    EXPECT_EQ(appPtr->FirstVariableActive, Runtime::DefaultWorldHandle);
    EXPECT_EQ(appPtr->FirstVariableScene, appPtr->InitScene);
    EXPECT_EQ(appPtr->SecondVariableActive, appPtr->CreatedWorld);
    EXPECT_EQ(appPtr->SecondVariableScene,
              engine.Worlds().Get(appPtr->CreatedWorld));
    EXPECT_EQ(appPtr->ThirdVariableActive, appPtr->CreatedWorld);
    EXPECT_EQ(appPtr->LastExtractionWorld, appPtr->CreatedWorld);

    engine.Shutdown();
}
