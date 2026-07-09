#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

import Extrinsic.Runtime.CommandBus;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.KernelEvents;

// ARCH-008 - contract coverage for the queued-only kernel event bus
// (ADR-0024 D7). Headless: standalone bus, no Engine/platform.

using Extrinsic::Runtime::CommandBus;
using Extrinsic::Runtime::CommandContext;
using Extrinsic::Runtime::CommandOutcome;
using Extrinsic::Runtime::EventBus;
using Extrinsic::Runtime::EventSubscriptionHandle;

namespace
{
    struct Incremented
    {
        int Value{0};
    };

    struct Cascaded
    {
        int Value{0};
    };

    struct SelfRemoving
    {
    };

    struct PublishIncrement
    {
        int Value{0};
    };
}

TEST(RuntimeKernelEvents, PublishDefersUntilPump)
{
    EventBus bus;

    int observed = 0;
    const EventSubscriptionHandle handle =
        bus.Subscribe<Incremented>(
            [&](const Incremented& event)
            {
                observed += event.Value;
            });

    ASSERT_TRUE(handle.IsValid());
    bus.Publish(Incremented{3});
    EXPECT_EQ(observed, 0);
    EXPECT_EQ(bus.PendingCount(), 1u);

    bus.Pump();

    EXPECT_EQ(observed, 3);
    EXPECT_EQ(bus.PendingCount(), 0u);
    EXPECT_EQ(bus.Stats().PumpCount, 1u);
    EXPECT_EQ(bus.Stats().LastPumpPublished, 1u);
    EXPECT_EQ(bus.Stats().LastPumpDelivered, 1u);
}

TEST(RuntimeKernelEvents, ListenerPublishedEventWaitsForNextPump)
{
    EventBus bus;

    int cascaded = 0;
    const EventSubscriptionHandle incremented =
        bus.Subscribe<Incremented>(
            [&](const Incremented& event)
            {
                bus.Publish(Cascaded{event.Value + 1});
            });
    const EventSubscriptionHandle cascadedHandle =
        bus.Subscribe<Cascaded>(
            [&](const Cascaded& event)
            {
                cascaded += event.Value;
            });
    ASSERT_TRUE(incremented.IsValid());
    ASSERT_TRUE(cascadedHandle.IsValid());

    bus.Publish(Incremented{4});
    bus.Pump();

    EXPECT_EQ(cascaded, 0);
    EXPECT_EQ(bus.PendingCount(), 1u);
    EXPECT_EQ(bus.Stats().LastPumpDelivered, 1u);

    bus.Pump();

    EXPECT_EQ(cascaded, 5);
    EXPECT_EQ(bus.PendingCount(), 0u);
    EXPECT_EQ(bus.Stats().LastPumpDelivered, 1u);
}

TEST(RuntimeKernelEvents, CrossThreadPublishIsDeliveredAtMainPump)
{
    EventBus bus;

    std::atomic<int> total{0};
    const EventSubscriptionHandle handle =
        bus.Subscribe<Incremented>(
            [&](const Incremented& event)
            {
                total += event.Value;
            });
    ASSERT_TRUE(handle.IsValid());

    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        workers.emplace_back(
            [&bus]
            {
                for (int j = 0; j < kPerThread; ++j)
                {
                    bus.Publish(Incremented{1});
                }
            });
    }

    for (std::thread& worker : workers)
    {
        worker.join();
    }

    bus.Pump();

    EXPECT_EQ(total.load(), kThreads * kPerThread);
    EXPECT_EQ(bus.Stats().LastPumpPublished,
              static_cast<std::uint64_t>(kThreads * kPerThread));
    EXPECT_EQ(bus.Stats().LastPumpDelivered,
              static_cast<std::uint64_t>(kThreads * kPerThread));
}

TEST(RuntimeKernelEvents, UnsubscribeBeforePumpStopsDelivery)
{
    EventBus bus;

    int observed = 0;
    const EventSubscriptionHandle handle =
        bus.Subscribe<Incremented>(
            [&](const Incremented&)
            {
                ++observed;
            });

    bus.Publish(Incremented{1});
    bus.Unsubscribe(handle);
    bus.Pump();

    EXPECT_EQ(observed, 0);
    EXPECT_EQ(bus.Stats().LastPumpPublished, 1u);
    EXPECT_EQ(bus.Stats().LastPumpDelivered, 0u);
    EXPECT_EQ(bus.Stats().ActiveSubscriptions, 0u);
}

TEST(RuntimeKernelEvents, UnsubscribeDuringPumpIsSafe)
{
    EventBus bus;

    int calls = 0;
    EventSubscriptionHandle handle{};
    handle = bus.Subscribe<SelfRemoving>(
        [&](const SelfRemoving&)
        {
            ++calls;
            bus.Unsubscribe(handle);
        });

    bus.Publish(SelfRemoving{});
    bus.Publish(SelfRemoving{});
    bus.Pump();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(bus.Stats().LastPumpDelivered, 1u);
    EXPECT_EQ(bus.Stats().ActiveSubscriptions, 0u);

    bus.Publish(SelfRemoving{});
    bus.Pump();

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(bus.Stats().LastPumpDelivered, 0u);
}

TEST(RuntimeKernelEvents, CommandHandlerCanPublishQueuedEvent)
{
    Extrinsic::ECS::Scene::Registry scene;
    CommandBus bus;
    EventBus events;

    int observed = 0;
    const EventSubscriptionHandle eventHandle =
        events.Subscribe<Incremented>(
            [&](const Incremented& event)
            {
                observed += event.Value;
            });
    ASSERT_TRUE(eventHandle.IsValid());
    bus.RegisterHandler<PublishIncrement>(
        [](CommandContext& ctx, const PublishIncrement& command) -> CommandOutcome
        {
            ctx.Events.Publish(Incremented{command.Value});
            return CommandOutcome::Ok();
        });

    bus.Enqueue(PublishIncrement{7});
    bus.Drain(scene, events);
    EXPECT_EQ(observed, 0);

    events.Pump();

    EXPECT_EQ(observed, 7);
}
