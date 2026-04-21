
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <vector>
 
import Extrinsic.Asset.EventBus;
import Extrinsic.Asset.Registry;
 
using namespace Extrinsic::Assets;
 
namespace
{
    constexpr AssetId MakeId(uint32_t idx, uint32_t gen = 1u)
    {
        return AssetId{idx, gen};
    }
}
 
// -----------------------------------------------------------------------------
// Subscribe / Unsubscribe
// -----------------------------------------------------------------------------
 
TEST(AssetEventBus, SubscribeReturnsNonZeroToken)
{
    AssetEventBus bus;
    auto tok = bus.Subscribe(MakeId(1u), [](AssetId, AssetEvent){});
    EXPECT_NE(tok, AssetEventBus::InvalidToken);
}
 
TEST(AssetEventBus, SubscribeRejectsEmptyCallback)
{
    AssetEventBus bus;
    auto tok = bus.Subscribe(MakeId(1u), AssetEventBus::ListenerCallback{});
    EXPECT_EQ(tok, AssetEventBus::InvalidToken);
}
 
TEST(AssetEventBus, FirstValidTokenIsNotInvalidSentinel)
{
    // Regression: m_NextToken originally started at 0, which collided with
    // the "invalid" sentinel. The first successful Subscribe must never
    // return InvalidToken.
    AssetEventBus bus;
    auto tok = bus.Subscribe(MakeId(1u), [](AssetId, AssetEvent){});
    EXPECT_NE(tok, AssetEventBus::InvalidToken);
}
 
TEST(AssetEventBus, UnsubscribeStopsDelivery)
{
    AssetEventBus bus;
    std::atomic<int> hits{0};
    auto tok = bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent){ ++hits; });
    ASSERT_NE(tok, AssetEventBus::InvalidToken);
    bus.Unsubscribe(MakeId(1u), tok);
 
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(hits.load(), 0);
}
 
TEST(AssetEventBus, UnsubscribeUnknownTokenIsNoOp)
{
    AssetEventBus bus;
    bus.Unsubscribe(MakeId(1u), 999u);
    bus.Unsubscribe(MakeId(1u), AssetEventBus::InvalidToken);
    SUCCEED();
}
 
// -----------------------------------------------------------------------------
// Publish / Flush
// -----------------------------------------------------------------------------
 
TEST(AssetEventBus, PublishQueuesUntilFlush)
{
    AssetEventBus bus;
    std::atomic<int> hits{0};
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent){ ++hits; });
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    EXPECT_EQ(hits.load(), 0);
    EXPECT_EQ(bus.PendingCount(), 1u);
 
    bus.Flush();
    EXPECT_EQ(hits.load(), 1);
    EXPECT_EQ(bus.PendingCount(), 0u);
}
 
TEST(AssetEventBus, FlushDeliversToMatchingId)
{
    AssetEventBus bus;
    std::atomic<int> a{0}, b{0};
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent){ ++a; });
    (void)bus.Subscribe(MakeId(2u), [&](AssetId, AssetEvent){ ++b; });
 
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Publish(MakeId(2u), AssetEvent::Failed);
    bus.Publish(MakeId(1u), AssetEvent::Destroyed);
    bus.Flush();
 
    EXPECT_EQ(a.load(), 2);
    EXPECT_EQ(b.load(), 1);
}
 
TEST(AssetEventBus, FlushIsIdempotent)
{
    AssetEventBus bus;
    std::atomic<int> hits{0};
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent){ ++hits; });
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    bus.Flush(); // second flush sees empty queue - no re-delivery
    EXPECT_EQ(hits.load(), 1);
}
 
TEST(AssetEventBus, FlushForwardsEventTypeUnchanged)
{
    AssetEventBus bus;
    AssetEvent received = AssetEvent::Ready;
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent e){ received = e; });
 
    bus.Publish(MakeId(1u), AssetEvent::Failed);
    bus.Flush();
    EXPECT_EQ(received, AssetEvent::Failed);
 
    bus.Publish(MakeId(1u), AssetEvent::Reloaded);
    bus.Flush();
    EXPECT_EQ(received, AssetEvent::Reloaded);
 
    bus.Publish(MakeId(1u), AssetEvent::Destroyed);
    bus.Flush();
    EXPECT_EQ(received, AssetEvent::Destroyed);
}
 
TEST(AssetEventBus, FlushWithNoSubscribersIsHarmless)
{
    AssetEventBus bus;
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(bus.PendingCount(), 0u);
}
 
// -----------------------------------------------------------------------------
// Broadcast subscribers
// -----------------------------------------------------------------------------
 
TEST(AssetEventBus, SubscribeAllReceivesAllEvents)
{
    AssetEventBus bus;
    std::atomic<int> hits{0};
    auto tok = bus.SubscribeAll([&](AssetId, AssetEvent){ ++hits; });
    ASSERT_NE(tok, AssetEventBus::InvalidToken);
 
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Publish(MakeId(2u), AssetEvent::Failed);
    bus.Flush();
    EXPECT_EQ(hits.load(), 2);
}
 
TEST(AssetEventBus, SubscribeAllRejectsEmptyCallback)
{
    AssetEventBus bus;
    auto tok = bus.SubscribeAll(AssetEventBus::ListenerCallback{});
    EXPECT_EQ(tok, AssetEventBus::InvalidToken);
}
 
TEST(AssetEventBus, UnsubscribeAllStopsDelivery)
{
    AssetEventBus bus;
    std::atomic<int> hits{0};
    auto tok = bus.SubscribeAll([&](AssetId, AssetEvent){ ++hits; });
    bus.UnsubscribeAll(tok);
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(hits.load(), 0);
}
 
TEST(AssetEventBus, PerIdAndBroadcastBothReceive)
{
    AssetEventBus bus;
    std::atomic<int> specific{0}, broadcast{0};
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent){ ++specific; });
    (void)bus.SubscribeAll([&](AssetId, AssetEvent){ ++broadcast; });
 
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Publish(MakeId(2u), AssetEvent::Ready);
    bus.Flush();
 
    EXPECT_EQ(specific.load(), 1);
    EXPECT_EQ(broadcast.load(), 2);
}
 
// -----------------------------------------------------------------------------
// Concurrency
// -----------------------------------------------------------------------------
 
TEST(AssetEventBus, PublishFromMultipleThreadsAllDelivered)
{
    AssetEventBus bus;
    std::atomic<int> hits{0};
    (void)bus.SubscribeAll([&](AssetId, AssetEvent){ ++hits; });
 
    constexpr int kThreads = 4;
    constexpr int kPerThread = 250;
    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        pool.emplace_back([&bus]
        {
            for (int i = 0; i < kPerThread; ++i)
            {
                bus.Publish(MakeId(static_cast<uint32_t>(i + 1)), AssetEvent::Ready);
            }
        });
    }
    for (auto& th : pool) th.join();
 
    bus.Flush();
    EXPECT_EQ(hits.load(), kThreads * kPerThread);
}
 
TEST(AssetEventBus, ConcurrentSubscriberTokensAreUnique)
{
    AssetEventBus bus;
    constexpr int kThreads = 4;
    constexpr int kPerThread = 100;
    std::vector<std::vector<AssetEventBus::ListenerToken>> results(kThreads);
    std::vector<std::thread> pool;
    pool.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t)
    {
        pool.emplace_back([&, t]
        {
            results[t].reserve(kPerThread);
            for (int i = 0; i < kPerThread; ++i)
            {
                results[t].push_back(bus.Subscribe(MakeId(1u), [](AssetId, AssetEvent){}));
            }
        });
    }
    for (auto& th : pool) th.join();
 
    std::vector<AssetEventBus::ListenerToken> flat;
    for (auto& v : results) flat.insert(flat.end(), v.begin(), v.end());
    std::sort(flat.begin(), flat.end());
    auto it = std::unique(flat.begin(), flat.end());
    EXPECT_EQ(it, flat.end());
}

// -----------------------------------------------------------------------------
// Re-entrancy regression tests
//
// The EventBus originally held m_Mutex across callback invocation in Flush().
// A subscriber callback that called back into Publish / Subscribe / Unsubscribe
// deadlocked on the same mutex. Commit 3355038 fixed this by copying callbacks
// and pending events into local storage under the lock, then invoking them
// without holding the lock. These tests pin that contract so future refactors
// cannot silently regress.
// -----------------------------------------------------------------------------

TEST(AssetEventBus, CallbackPublishDuringFlushQueuesForNextFlush)
{
    // A callback that Publishes a new event must NOT deadlock and must NOT
    // be delivered this Flush — the new event joins the pending queue and
    // fires on the next Flush.
    AssetEventBus bus;
    std::atomic<int> aHits{0}, bHits{0};
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent)
    {
        ++aHits;
        bus.Publish(MakeId(2u), AssetEvent::Reloaded);
    });
    (void)bus.Subscribe(MakeId(2u), [&](AssetId, AssetEvent){ ++bHits; });

    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush(); // must return in bounded time — no deadlock

    EXPECT_EQ(aHits.load(), 1);
    EXPECT_EQ(bHits.load(), 0) << "Re-publish in callback must defer to next Flush.";
    EXPECT_EQ(bus.PendingCount(), 1u);

    bus.Flush();
    EXPECT_EQ(bHits.load(), 1);
}

TEST(AssetEventBus, CallbackUnsubscribesSelfDuringFlush)
{
    // Self-unsubscribe from inside a callback is a common pattern (one-shot
    // listeners). It must not deadlock; the current delivery completes; the
    // listener is gone by the next Flush.
    AssetEventBus bus;
    std::atomic<int> hits{0};

    // Subscribe then store the token in a shared slot the callback can read.
    AssetEventBus::ListenerToken tok = AssetEventBus::InvalidToken;
    tok = bus.Subscribe(MakeId(1u), [&](AssetId id, AssetEvent)
    {
        ++hits;
        bus.Unsubscribe(id, tok);
    });
    ASSERT_NE(tok, AssetEventBus::InvalidToken);

    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(hits.load(), 1);

    // Subsequent Publish must not re-fire the unsubscribed callback.
    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(hits.load(), 1);
}

TEST(AssetEventBus, CallbackSubscribesNewListenerDuringFlush)
{
    // A callback that Subscribes a new listener must not deadlock. The new
    // listener joins the listener table; it does not see the current event
    // (already dispatched to the copied snapshot) but receives subsequent ones.
    AssetEventBus bus;
    std::atomic<int> newHits{0};
    (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent)
    {
        (void)bus.Subscribe(MakeId(1u), [&](AssetId, AssetEvent){ ++newHits; });
    });

    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(newHits.load(), 0) << "Newly subscribed listener must not see the event already being delivered.";

    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();
    EXPECT_EQ(newHits.load(), 1);
}

TEST(AssetEventBus, BroadcastCallbackPublishDuringFlushDoesNotDeadlock)
{
    // Same re-entrancy contract for broadcast (SubscribeAll) callbacks.
    AssetEventBus bus;
    std::atomic<int> broadcastHits{0}, otherHits{0};
    (void)bus.SubscribeAll([&](AssetId id, AssetEvent)
    {
        ++broadcastHits;
        if (id == MakeId(1u))
            bus.Publish(MakeId(2u), AssetEvent::Reloaded);
    });
    (void)bus.Subscribe(MakeId(2u), [&](AssetId, AssetEvent){ ++otherHits; });

    bus.Publish(MakeId(1u), AssetEvent::Ready);
    bus.Flush();

    EXPECT_EQ(broadcastHits.load(), 1);
    EXPECT_EQ(otherHits.load(), 0) << "Re-publish from broadcast callback must defer to next Flush.";
    EXPECT_EQ(bus.PendingCount(), 1u);

    bus.Flush();
    EXPECT_EQ(broadcastHits.load(), 2);
    EXPECT_EQ(otherHits.load(), 1);
}

TEST(AssetEventBus, BurstyReEntrantPublishTerminates)
{
    // Smoke test: cascaded re-entrant Publishes. If Flush ever regresses to
    // holding m_Mutex across callback invocation, this deadlocks (GTest's
    // default timeout fails the test). Bounded fan-out so Flush must
    // terminate even under the fixed code.
    AssetEventBus bus;
    std::atomic<int> hits{0};
    (void)bus.SubscribeAll([&](AssetId id, AssetEvent)
    {
        ++hits;
        if (id.Index < 8u) // bounded cascade; no infinite recursion
            bus.Publish(AssetId{id.Index + 1u, 1u}, AssetEvent::Ready);
    });

    bus.Publish(MakeId(1u), AssetEvent::Ready);
    for (int i = 0; i < 16 && bus.PendingCount() > 0u; ++i)
    {
        bus.Flush();
    }
    EXPECT_EQ(bus.PendingCount(), 0u);
    EXPECT_GE(hits.load(), 8) << "All 8 cascaded events should have fired.";
}