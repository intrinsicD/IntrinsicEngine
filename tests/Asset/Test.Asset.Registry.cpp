#include <gtest/gtest.h>

import Extrinsic.Asset.Registry;
import Extrinsic.Core.Error;

using namespace Extrinsic::Assets;
using Extrinsic::Core::ErrorCode;

// -----------------------------------------------------------------------------
// Create / destroy / aliveness
// -----------------------------------------------------------------------------

TEST(AssetRegistry, CreateReturnsValidId)
{
    AssetRegistry r;
    auto id = r.Create(123u, 1u);
    ASSERT_TRUE(id.has_value());
    EXPECT_TRUE(id->IsValid());
    EXPECT_TRUE(r.IsAlive(*id));
}

TEST(AssetRegistry, CreateDistinctIdsForDifferentCalls)
{
    AssetRegistry r;
    auto a = r.Create(1u, 1u);
    auto b = r.Create(2u, 1u);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(a->Index, b->Index);
}

TEST(AssetRegistry, GetMetaReflectsCreateArguments)
{
    AssetRegistry r;
    auto id = r.Create(42u, 7u).value();
    auto meta = r.GetMeta(id);
    ASSERT_TRUE(meta.has_value());
    EXPECT_EQ(meta->pathHash, 42u);
    EXPECT_EQ(meta->typeId, 7u);
    EXPECT_EQ(meta->state, AssetState::Unloaded);
    EXPECT_EQ(meta->payloadSlot, 0u);
}

TEST(AssetRegistry, DestroyMakesHandleDead)
{
    AssetRegistry r;
    auto id = r.Create(1u, 1u).value();
    ASSERT_TRUE(r.Destroy(id).has_value());
    EXPECT_FALSE(r.IsAlive(id));
}

TEST(AssetRegistry, DestroyTwiceReturnsNotFound)
{
    AssetRegistry r;
    auto id = r.Create(1u, 1u).value();
    ASSERT_TRUE(r.Destroy(id).has_value());
    auto again = r.Destroy(id);
    ASSERT_FALSE(again.has_value());
    EXPECT_EQ(again.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetRegistry, StaleHandleAfterRecycleIsDead)
{
    AssetRegistry r;
    auto first = r.Create(1u, 1u).value();
    ASSERT_TRUE(r.Destroy(first).has_value());
    auto reused = r.Create(2u, 2u).value();

    // The recycled slot bumps the generation counter.
    EXPECT_EQ(reused.Index, first.Index);
    EXPECT_NE(reused.Generation, first.Generation);
    EXPECT_FALSE(r.IsAlive(first));
    EXPECT_TRUE(r.IsAlive(reused));
}

TEST(AssetRegistry, InvalidHandleNotAlive)
{
    AssetRegistry r;
    AssetId invalid{};
    EXPECT_FALSE(r.IsAlive(invalid));
    auto meta = r.GetMeta(invalid);
    EXPECT_FALSE(meta.has_value());
    EXPECT_EQ(meta.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetRegistry, OutOfRangeHandleNotAlive)
{
    AssetRegistry r;
    auto id = r.Create(1u, 1u).value();
    AssetId bogus{id.Index + 99u, 1u};
    EXPECT_FALSE(r.IsAlive(bogus));
}

// -----------------------------------------------------------------------------
// State machine
// -----------------------------------------------------------------------------

TEST(AssetRegistry_State, SetStateAcceptsMatchingExpectation)
{
    AssetRegistry r;
    auto id = r.Create(0u, 0u).value();
    EXPECT_TRUE(r.SetState(id, AssetState::Unloaded, AssetState::QueuedIO).has_value());
    auto st = r.GetState(id);
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(*st, AssetState::QueuedIO);
}

TEST(AssetRegistry_State, SetStateRejectsMismatchedExpectation)
{
    AssetRegistry r;
    auto id = r.Create(0u, 0u).value();
    auto res = r.SetState(id, AssetState::Ready, AssetState::Unloaded);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ErrorCode::InvalidState);
}

TEST(AssetRegistry_State, SetStateRejectsDeadHandle)
{
    AssetRegistry r;
    auto id = r.Create(0u, 0u).value();
    (void)r.Destroy(id);
    auto res = r.SetState(id, AssetState::Unloaded, AssetState::Ready);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetRegistry_State, SetPayloadSlotUpdatesMeta)
{
    AssetRegistry r;
    auto id = r.Create(0u, 0u).value();
    ASSERT_TRUE(r.SetPayloadSlot(id, 42u).has_value());
    auto meta = r.GetMeta(id).value();
    EXPECT_EQ(meta.payloadSlot, 42u);
}

TEST(AssetRegistry_State, SetPayloadSlotRejectsDeadHandle)
{
    AssetRegistry r;
    auto id = r.Create(0u, 0u).value();
    (void)r.Destroy(id);
    auto res = r.SetPayloadSlot(id, 1u);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), ErrorCode::ResourceNotFound);
}

TEST(AssetRegistry_State, GetStateRejectsDeadHandle)
{
    AssetRegistry r;
    auto id = r.Create(0u, 0u).value();
    (void)r.Destroy(id);
    auto st = r.GetState(id);
    ASSERT_FALSE(st.has_value());
    EXPECT_EQ(st.error(), ErrorCode::ResourceNotFound);
}

// -----------------------------------------------------------------------------
// Accounting
// -----------------------------------------------------------------------------

TEST(AssetRegistry_Accounting, LiveCountTracksCreatesAndDestroys)
{
    AssetRegistry r;
    EXPECT_EQ(r.LiveCount(), 0u);
    auto a = r.Create(1u, 1u).value();
    auto b = r.Create(2u, 2u).value();
    EXPECT_EQ(r.LiveCount(), 2u);
    (void)r.Destroy(a);
    EXPECT_EQ(r.LiveCount(), 1u);
    (void)r.Destroy(b);
    EXPECT_EQ(r.LiveCount(), 0u);
}

TEST(AssetRegistry_Accounting, CapacityGrowsMonotonically)
{
    AssetRegistry r;
    EXPECT_EQ(r.Capacity(), 0u);
    (void)r.Create(0u, 0u);
    EXPECT_GE(r.Capacity(), 1u);
    auto b = r.Create(0u, 0u).value();
    (void)r.Destroy(b);
    const auto cap = r.Capacity();
    (void)r.Create(0u, 0u); // reuses slot - no growth
    EXPECT_EQ(r.Capacity(), cap);
}
