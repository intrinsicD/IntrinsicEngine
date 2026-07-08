#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.CommandBus;

// ARCH-007 — contract coverage for the kernel command bus (ADR-0024 D5/D13).
// Headless: a bus plus a standalone scene registry; no Engine, no platform.

using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::CommandBus;
using Extrinsic::Runtime::CommandContext;
using Extrinsic::Runtime::CommandCorrelationId;
using Extrinsic::Runtime::CommandEnvelope;
using Extrinsic::Runtime::CommandHistoryRecord;
using Extrinsic::Runtime::CommandOutcome;
using Extrinsic::Runtime::CommandStatus;

namespace
{
    struct AppendValue
    {
        int Value{0};
    };

    struct UnhandledCommand
    {
    };

    struct FailingCommand
    {
    };

    struct FollowUp
    {
    };

    struct SetValue
    {
        int Old{0};
        int New{0};
    };
}

TEST(RuntimeCommandBus, DrainExecutesInEnqueueOrderOnDrainingThread)
{
    Registry   scene;
    CommandBus bus;

    std::vector<int>      seen;
    const std::thread::id mainThread = std::this_thread::get_id();
    bool                  threadOk   = true;

    bus.RegisterHandler<AppendValue>(
        [&](CommandContext&, const AppendValue& cmd) -> CommandOutcome
        {
            seen.push_back(cmd.Value);
            threadOk = threadOk && (std::this_thread::get_id() == mainThread);
            return CommandOutcome::Ok();
        });

    const CommandCorrelationId first  = bus.Enqueue(AppendValue{1});
    const CommandCorrelationId second = bus.Enqueue(AppendValue{2});
    bus.Enqueue(AppendValue{3});

    EXPECT_TRUE(first.IsValid());
    EXPECT_TRUE(second.IsValid());
    EXPECT_NE(first, second);

    bus.Drain(scene);

    EXPECT_EQ(seen, (std::vector<int>{1, 2, 3}));
    EXPECT_TRUE(threadOk);
    EXPECT_EQ(bus.Stats().Executed, 3u);
    EXPECT_EQ(bus.Stats().LastDrainCount, 3u);
}

TEST(RuntimeCommandBus, CrossThreadEnqueueIsSafeAndComplete)
{
    Registry   scene;
    CommandBus bus;

    std::atomic<int> total{0};
    bus.RegisterHandler<AppendValue>(
        [&](CommandContext&, const AppendValue& cmd) -> CommandOutcome
        {
            total += cmd.Value;
            return CommandOutcome::Ok();
        });

    constexpr int kThreads          = 4;
    constexpr int kPerThread        = 250;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i)
    {
        workers.emplace_back(
            [&bus]
            {
                for (int j = 0; j < kPerThread; ++j)
                {
                    bus.Enqueue(AppendValue{1});
                }
            });
    }
    for (std::thread& worker : workers)
    {
        worker.join();
    }

    bus.Drain(scene);

    EXPECT_EQ(total.load(), kThreads * kPerThread);
    EXPECT_EQ(bus.Stats().Executed,
              static_cast<std::uint64_t>(kThreads * kPerThread));
}

TEST(RuntimeCommandBus, PayloadIsCopiedAtEnqueueTime)
{
    Registry   scene;
    CommandBus bus;

    int observed = 0;
    bus.RegisterHandler<AppendValue>(
        [&](CommandContext&, const AppendValue& cmd) -> CommandOutcome
        {
            observed = cmd.Value;
            return CommandOutcome::Ok();
        });

    AppendValue source{42};
    bus.Enqueue(source);
    source.Value = 99;  // must not affect the already-enqueued payload

    bus.Drain(scene);

    EXPECT_EQ(observed, 42);
}

TEST(RuntimeCommandBus, MissingHandlerFailsClosedWithoutCrashing)
{
    Registry   scene;
    CommandBus bus;

    bus.Enqueue(UnhandledCommand{});
    bus.Drain(scene);

    EXPECT_EQ(bus.Stats().MissingHandler, 1u);
    EXPECT_EQ(bus.Stats().Executed, 0u);
}

TEST(RuntimeCommandBus, HandlerFailureIsCountedAndDrainContinues)
{
    Registry   scene;
    CommandBus bus;

    int executed = 0;
    bus.RegisterHandler<FailingCommand>(
        [](CommandContext&, const FailingCommand&) -> CommandOutcome
        { return CommandOutcome::Fail("intentional test failure"); });
    bus.RegisterHandler<AppendValue>(
        [&](CommandContext&, const AppendValue&) -> CommandOutcome
        {
            ++executed;
            return CommandOutcome::Ok();
        });

    bus.Enqueue(FailingCommand{});
    bus.Enqueue(AppendValue{1});
    bus.Drain(scene);

    EXPECT_EQ(bus.Stats().Failed, 1u);
    EXPECT_EQ(executed, 1);
}

TEST(RuntimeCommandBus, ThrowingHandlerIsCountedAsFailureAndDrainContinues)
{
    Registry   scene;
    CommandBus bus;

    int executed = 0;
    bus.RegisterHandler<FailingCommand>(
        [](CommandContext&, const FailingCommand&) -> CommandOutcome
        { throw std::runtime_error("intentional test throw"); });
    bus.RegisterHandler<AppendValue>(
        [&](CommandContext&, const AppendValue&) -> CommandOutcome
        {
            ++executed;
            return CommandOutcome::Ok();
        });

    bus.Enqueue(FailingCommand{});
    bus.Enqueue(AppendValue{1});
    bus.Drain(scene);

    EXPECT_EQ(bus.Stats().Failed, 1u);
    EXPECT_EQ(executed, 1);
}

TEST(RuntimeCommandBus, HandlerEnqueuedFollowUpDefersToNextDrain)
{
    Registry   scene;
    CommandBus bus;

    int followUps = 0;
    bus.RegisterHandler<AppendValue>(
        [&](CommandContext& ctx, const AppendValue&) -> CommandOutcome
        {
            ctx.Commands.Enqueue(FollowUp{});
            return CommandOutcome::Ok();
        });
    bus.RegisterHandler<FollowUp>(
        [&](CommandContext&, const FollowUp&) -> CommandOutcome
        {
            ++followUps;
            return CommandOutcome::Ok();
        });

    bus.Enqueue(AppendValue{1});
    bus.Drain(scene);
    EXPECT_EQ(followUps, 0);  // deferred, not same-drain (ADR-0024 D5)

    bus.Drain(scene);
    EXPECT_EQ(followUps, 1);
}

TEST(RuntimeCommandBus, HistoryHookReceivesReEnqueueableInverse)
{
    Registry   scene;
    CommandBus bus;

    int current = 0;
    bus.RegisterHandler<SetValue>(
        [&](CommandContext& ctx, const SetValue& cmd) -> CommandOutcome
        {
            current = cmd.New;
            ctx.Commands.RecordInverse(
                CommandEnvelope::Make(SetValue{cmd.New, cmd.Old}));
            return CommandOutcome::Ok();
        });

    CommandEnvelope inverse;
    int             hookCalls = 0;
    bus.SetHistoryHook(
        [&](const CommandHistoryRecord& record)
        {
            ++hookCalls;
            inverse = record.Inverse;
            EXPECT_TRUE(record.Correlation.IsValid());
        });

    bus.Enqueue(SetValue{0, 7});
    bus.Drain(scene);

    EXPECT_EQ(current, 7);
    EXPECT_EQ(hookCalls, 1);
    ASSERT_TRUE(inverse.IsValid());

    // Undo = re-enqueue the recorded inverse envelope.
    bus.Enqueue(inverse);
    bus.Drain(scene);

    EXPECT_EQ(current, 0);
    EXPECT_EQ(hookCalls, 2);  // the undo records its own inverse (redo)
}

TEST(RuntimeCommandBus, ThrowingHistoryHookDoesNotWedgeTheBus)
{
    Registry   scene;
    CommandBus bus;

    int executed = 0;
    bus.RegisterHandler<SetValue>(
        [&](CommandContext& ctx, const SetValue& cmd) -> CommandOutcome
        {
            ++executed;
            ctx.Commands.RecordInverse(
                CommandEnvelope::Make(SetValue{cmd.New, cmd.Old}));
            return CommandOutcome::Ok();
        });
    bus.SetHistoryHook([](const CommandHistoryRecord&)
                       { throw std::runtime_error("intentional hook throw"); });

    bus.Enqueue(SetValue{0, 1});
    bus.Enqueue(SetValue{1, 2});
    bus.Drain(scene);

    // The hook throw is isolated per record: both commands executed and
    // the command itself still counts as a success.
    EXPECT_EQ(executed, 2);
    EXPECT_EQ(bus.Stats().Executed, 2u);
    EXPECT_EQ(bus.Stats().Failed, 0u);

    // Regression (PR #1010 review): the bus must not stay wedged as
    // "reentrant" after an unwinding hook — later drains still run.
    bus.Enqueue(SetValue{2, 3});
    bus.Drain(scene);
    EXPECT_EQ(executed, 3);
}

TEST(RuntimeCommandBus, DiscardPendingDropsQueuedCommandsWithoutExecuting)
{
    Registry   scene;
    CommandBus bus;

    int executed = 0;
    bus.RegisterHandler<AppendValue>(
        [&](CommandContext&, const AppendValue&) -> CommandOutcome
        {
            ++executed;
            return CommandOutcome::Ok();
        });

    // Regression (PR #1010 review): commands enqueued after the final
    // frame's drain must not replay into a freshly initialized scene on
    // the Engine Shutdown() + Initialize() reuse path.
    bus.Enqueue(AppendValue{1});
    bus.Enqueue(AppendValue{2});
    EXPECT_EQ(bus.DiscardPending(), 2u);
    EXPECT_EQ(bus.Stats().Discarded, 2u);

    bus.Drain(scene);
    EXPECT_EQ(executed, 0);
    EXPECT_EQ(bus.Stats().Executed, 0u);
    EXPECT_EQ(bus.DiscardPending(), 0u);
}

TEST(RuntimeCommandBus, FailedCommandDoesNotNotifyHistoryHook)
{
    Registry   scene;
    CommandBus bus;

    bus.RegisterHandler<FailingCommand>(
        [](CommandContext& ctx, const FailingCommand&) -> CommandOutcome
        {
            ctx.Commands.RecordInverse(CommandEnvelope::Make(AppendValue{1}));
            return CommandOutcome::Fail("intentional test failure");
        });

    int hookCalls = 0;
    bus.SetHistoryHook([&](const CommandHistoryRecord&) { ++hookCalls; });

    bus.Enqueue(FailingCommand{});
    bus.Drain(scene);

    EXPECT_EQ(hookCalls, 0);
    EXPECT_EQ(bus.Stats().Failed, 1u);
}
