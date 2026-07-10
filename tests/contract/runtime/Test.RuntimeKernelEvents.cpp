#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.KernelEvents;

namespace Runtime = Extrinsic::Runtime;
namespace CoreConfig = Extrinsic::Core::Config;

namespace
{
    struct RootEvent
    {
        int Value{0};
    };

    struct CascadeEvent
    {
        int Value{0};
    };

    struct StartEventPumpProbe
    {
    };

    [[nodiscard]] CoreConfig::EngineConfig NullWindowHeadlessConfig()
    {
        CoreConfig::EngineConfig config{};
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled         = false;
        config.Window.Backend         = CoreConfig::WindowBackend::Null;
        return config;
    }

    class EnginePumpProbeApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            RootSubscription = engine.Events().Subscribe<RootEvent>(
                [this, &engine](const RootEvent& event)
                {
                    RootHits += event.Value;
                    engine.Events().Publish(CascadeEvent{7});
                });
            CascadeSubscription = engine.Events().Subscribe<CascadeEvent>(
                [this](const CascadeEvent& event)
                { CascadeHits += event.Value; });

            Runtime::KernelEventBus* events = &engine.Events();
            engine.Commands().RegisterHandler<StartEventPumpProbe>(
                [events](Runtime::CommandContext&,
                         const StartEventPumpProbe&) -> Runtime::CommandOutcome
                {
                    events->Publish(RootEvent{5});
                    return Runtime::CommandOutcome::Ok();
                });
            engine.Commands().Enqueue(StartEventPumpProbe{});
        }

        void OnSimTick(Runtime::Engine&, double) override {}

        void OnVariableTick(Runtime::Engine& engine, double, double) override
        {
            ++VariableTicks;
            ObservedBeforeVariableTick = RootHits == 5 && CascadeHits == 7;
            EventStats = engine.Events().Stats();
            engine.RequestExit();
        }

        void OnShutdown(Runtime::Engine&) override {}

        Runtime::KernelEventSubscription RootSubscription{};
        Runtime::KernelEventSubscription CascadeSubscription{};
        Runtime::KernelEventBusStats EventStats{};
        int RootHits{0};
        int CascadeHits{0};
        std::uint32_t VariableTicks{0};
        bool ObservedBeforeVariableTick{false};
    };
}

TEST(RuntimeKernelEvents, PublishQueuesUntilPump)
{
    Runtime::KernelEventBus bus;

    int observed = 0;
    const Runtime::KernelEventSubscription sub =
        bus.Subscribe<RootEvent>(
            [&](const RootEvent& event) { observed += event.Value; });
    ASSERT_TRUE(sub.IsValid());

    bus.Publish(RootEvent{3});
    EXPECT_EQ(observed, 0);
    EXPECT_EQ(bus.Stats().PublishedEvents, 1u);

    EXPECT_EQ(bus.Pump(), 1u);
    EXPECT_EQ(observed, 3);
    EXPECT_EQ(bus.Stats().Pumps, 1u);
    EXPECT_EQ(bus.Stats().LastPumpEvents, 1u);
    EXPECT_EQ(bus.Stats().LastPumpDeliveredEvents, 1u);
    EXPECT_EQ(bus.Stats().LastPumpListenerInvocations, 1u);
}

TEST(RuntimeKernelEvents, ListenerPublishDuringPumpDefersToNextPump)
{
    Runtime::KernelEventBus bus;

    int rootHits = 0;
    int cascadeHits = 0;
    (void)bus.Subscribe<RootEvent>(
        [&](const RootEvent&)
        {
            ++rootHits;
            bus.Publish(CascadeEvent{});
        });
    (void)bus.Subscribe<CascadeEvent>(
        [&](const CascadeEvent&) { ++cascadeHits; });

    bus.Publish(RootEvent{});
    EXPECT_EQ(bus.Pump(), 1u);
    EXPECT_EQ(rootHits, 1);
    EXPECT_EQ(cascadeHits, 0);

    EXPECT_EQ(bus.Pump(), 1u);
    EXPECT_EQ(cascadeHits, 1);
}

TEST(RuntimeKernelEvents, CrossThreadPublishIsDeliveredAtNextPump)
{
    Runtime::KernelEventBus bus;

    std::atomic<int> observed{0};
    (void)bus.Subscribe<RootEvent>(
        [&](const RootEvent& event) { observed += event.Value; });

    constexpr int kThreads = 4;
    constexpr int kPerThread = 200;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        workers.emplace_back(
            [&bus]
            {
                for (int j = 0; j < kPerThread; ++j)
                    bus.Publish(RootEvent{1});
            });
    }
    for (std::thread& worker : workers)
        worker.join();

    EXPECT_EQ(observed.load(), 0);
    EXPECT_EQ(bus.Pump(), static_cast<std::uint64_t>(kThreads * kPerThread));
    EXPECT_EQ(observed.load(), kThreads * kPerThread);
    EXPECT_EQ(bus.Stats().LastPumpEvents,
              static_cast<std::uint64_t>(kThreads * kPerThread));
}

TEST(RuntimeKernelEvents, UnsubscribeStopsDelivery)
{
    Runtime::KernelEventBus bus;

    int observed = 0;
    const Runtime::KernelEventSubscription sub =
        bus.Subscribe<RootEvent>(
            [&](const RootEvent&) { ++observed; });
    ASSERT_TRUE(sub.IsValid());

    bus.Unsubscribe(sub);
    bus.Publish(RootEvent{});
    EXPECT_EQ(bus.Pump(), 0u);
    EXPECT_EQ(observed, 0);
}

TEST(RuntimeKernelEvents, UnsubscribeDuringPumpIsSafe)
{
    Runtime::KernelEventBus bus;

    int observed = 0;
    Runtime::KernelEventSubscription sub{};
    sub = bus.Subscribe<RootEvent>(
        [&](const RootEvent&)
        {
            ++observed;
            bus.Unsubscribe(sub);
        });
    ASSERT_TRUE(sub.IsValid());

    bus.Publish(RootEvent{});
    bus.Publish(RootEvent{});
    EXPECT_EQ(bus.Pump(), 1u);
    EXPECT_EQ(observed, 1);
    EXPECT_EQ(bus.Pump(), 0u);
}

TEST(RuntimeKernelEvents, EnginePumpsPostDrainAndPostSimulationOnNullBackend)
{
    auto app = std::make_unique<EnginePumpProbeApplication>();
    EnginePumpProbeApplication* appPtr = app.get();

    Runtime::Engine engine(NullWindowHeadlessConfig(), std::move(app));
    engine.Initialize();

    ASSERT_FALSE(engine.GetWindow().ShouldClose())
        << "explicit Null window backend must keep Engine::Run() drivable on headless hosts";

    engine.Run();

    EXPECT_EQ(appPtr->VariableTicks, 1u);
    EXPECT_TRUE(appPtr->ObservedBeforeVariableTick);
    EXPECT_EQ(appPtr->RootHits, 5);
    EXPECT_EQ(appPtr->CascadeHits, 7);
    EXPECT_EQ(appPtr->EventStats.Pumps, 2u);
    EXPECT_EQ(appPtr->EventStats.PublishedEvents, 2u);
    EXPECT_EQ(appPtr->EventStats.DeliveredEvents, 2u);

    engine.Shutdown();
}
