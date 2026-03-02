#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

import Core;

// =============================================================================
// ResourcePool — Lifecycle and generational handle tests
// =============================================================================
//
// These tests validate the CPU-side contract of Core::ResourcePool which
// underpins the GPU geometry pool (GeometryPool). Tests cover:
//   - Add/Get/Remove lifecycle with generational handles.
//   - Retirement frame semantics (deferred deletion for GPU safety).
//   - Slot reuse after retirement.
//   - Stale handle rejection via generation mismatch.
//   - Concurrent read safety (shared_mutex).

namespace
{
    struct DummyTag {};
    using DummyHandle = Core::StrongHandle<DummyTag>;

    struct DummyResource
    {
        int Value = 0;
        std::string Name;

        DummyResource() = default;
        explicit DummyResource(int v) : Value(v) {}
        DummyResource(int v, std::string n) : Value(v), Name(std::move(n)) {}
    };

    // CPU-only pool: RetirementFrames = 0 → immediate reclamation.
    using ImmediatePool = Core::ResourcePool<DummyResource, DummyHandle, 0>;

    // GPU-style pool: RetirementFrames = 3 → deferred reclamation.
    using DeferredPool = Core::ResourcePool<DummyResource, DummyHandle, 3>;
}

// ---- Basic Add / Get ----

TEST(ResourcePool, AddAndGet)
{
    ImmediatePool pool;

    auto h = pool.Add(std::make_unique<DummyResource>(42));
    ASSERT_TRUE(h.IsValid());

    auto result = pool.Get(h);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->Value, 42);
}

TEST(ResourcePool, CreateInPlace)
{
    ImmediatePool pool;

    auto h = pool.Create(7, "test");
    ASSERT_TRUE(h.IsValid());

    auto result = pool.Get(h);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value()->Value, 7);
    EXPECT_EQ(result.value()->Name, "test");
}

TEST(ResourcePool, GetUnchecked)
{
    ImmediatePool pool;

    auto h = pool.Create(99);
    ASSERT_TRUE(h.IsValid());

    DummyResource* ptr = pool.GetUnchecked(h);
    ASSERT_NE(ptr, nullptr);
    EXPECT_EQ(ptr->Value, 99);
}

TEST(ResourcePool, MultipleAdds)
{
    ImmediatePool pool;

    auto h0 = pool.Create(0);
    auto h1 = pool.Create(1);
    auto h2 = pool.Create(2);

    EXPECT_TRUE(h0.IsValid());
    EXPECT_TRUE(h1.IsValid());
    EXPECT_TRUE(h2.IsValid());

    // All handles should be distinct
    EXPECT_NE(h0.Index, h1.Index);
    EXPECT_NE(h1.Index, h2.Index);

    // All values should be retrievable
    EXPECT_EQ(pool.Get(h0).value()->Value, 0);
    EXPECT_EQ(pool.Get(h1).value()->Value, 1);
    EXPECT_EQ(pool.Get(h2).value()->Value, 2);
}

// ---- Remove ----

TEST(ResourcePool_Immediate, RemoveInvalidatesGet)
{
    ImmediatePool pool;

    auto h = pool.Create(42);
    ASSERT_TRUE(pool.Get(h).has_value());

    pool.Remove(h, 0);

    // After Remove, Get should fail immediately (IsActive = false).
    auto result = pool.Get(h);
    EXPECT_FALSE(result.has_value());

    auto* ptr = pool.GetUnchecked(h);
    EXPECT_EQ(ptr, nullptr);
}

TEST(ResourcePool_Immediate, RemoveDoesNotAffectOthers)
{
    ImmediatePool pool;

    auto h0 = pool.Create(0);
    auto h1 = pool.Create(1);
    auto h2 = pool.Create(2);

    pool.Remove(h1, 0);

    // h0 and h2 should still be accessible.
    EXPECT_TRUE(pool.Get(h0).has_value());
    EXPECT_FALSE(pool.Get(h1).has_value());
    EXPECT_TRUE(pool.Get(h2).has_value());
}

TEST(ResourcePool_Immediate, DoubleRemoveIsSafe)
{
    ImmediatePool pool;

    auto h = pool.Create(42);
    pool.Remove(h, 0);
    pool.Remove(h, 0); // Should not crash or corrupt state.

    EXPECT_FALSE(pool.Get(h).has_value());
}

// ---- Generational Handles ----

TEST(ResourcePool, StaleHandleRejected)
{
    ImmediatePool pool;

    auto h1 = pool.Create(10);
    uint32_t slotIndex = h1.Index;

    pool.Remove(h1, 0);
    pool.ProcessDeletions(1); // Reclaim the slot (RetirementFrames = 0).

    // Add a new resource — should reuse the reclaimed slot.
    auto h2 = pool.Create(20);
    EXPECT_EQ(h2.Index, slotIndex); // Reused slot.
    EXPECT_NE(h2.Generation, h1.Generation); // New generation.

    // The old handle (stale generation) must be rejected.
    EXPECT_FALSE(pool.Get(h1).has_value());
    EXPECT_EQ(pool.GetUnchecked(h1), nullptr);

    // The new handle works.
    EXPECT_TRUE(pool.Get(h2).has_value());
    EXPECT_EQ(pool.Get(h2).value()->Value, 20);
}

TEST(ResourcePool, GenerationIncrements)
{
    ImmediatePool pool;

    auto h1 = pool.Create(1);
    uint32_t gen1 = h1.Generation;

    pool.Remove(h1, 0);
    pool.ProcessDeletions(1);

    auto h2 = pool.Create(2);
    uint32_t gen2 = h2.Generation;

    EXPECT_GT(gen2, gen1);
}

// ---- Retirement Frame Semantics (GPU safety) ----

TEST(ResourcePool_Deferred, SlotNotReclaimedBeforeRetirement)
{
    DeferredPool pool; // RetirementFrames = 3

    auto h = pool.Create(42);
    uint32_t slotIndex = h.Index;

    pool.Remove(h, 10); // Removed at frame 10.

    // Process at frames 11, 12, 13 — should NOT reclaim yet.
    pool.ProcessDeletions(11);
    pool.ProcessDeletions(12);
    pool.ProcessDeletions(13);

    // Add a new resource — should NOT reuse the slot.
    auto h2 = pool.Create(99);
    EXPECT_NE(h2.Index, slotIndex);
}

TEST(ResourcePool_Deferred, SlotReclaimedAfterRetirement)
{
    DeferredPool pool; // RetirementFrames = 3

    auto h = pool.Create(42);
    uint32_t slotIndex = h.Index;

    pool.Remove(h, 10); // Removed at frame 10.

    // Process at frame 14 — 4 frames elapsed, > RetirementFrames (3).
    pool.ProcessDeletions(14);

    // Add a new resource — should reuse the reclaimed slot.
    auto h2 = pool.Create(99);
    EXPECT_EQ(h2.Index, slotIndex);
}

TEST(ResourcePool_Deferred, MultipleRemovalsWithDifferentRetirementTimes)
{
    DeferredPool pool;

    auto h0 = pool.Create(0); // index 0
    auto h1 = pool.Create(1); // index 1
    auto h2 = pool.Create(2); // index 2

    pool.Remove(h0, 10); // Removed at frame 10
    pool.Remove(h2, 12); // Removed at frame 12

    // At frame 14: h0 should be reclaimable (10 + 3 < 14), h2 should not (12 + 3 = 15).
    pool.ProcessDeletions(14);

    auto h3 = pool.Create(3);
    EXPECT_EQ(h3.Index, h0.Index); // Reuses slot 0

    auto h4 = pool.Create(4);
    EXPECT_NE(h4.Index, h2.Index); // Cannot reuse slot 2 yet

    // At frame 16: h2 should now be reclaimable.
    pool.ProcessDeletions(16);

    auto h5 = pool.Create(5);
    EXPECT_EQ(h5.Index, h2.Index); // Reuses slot 2
}

// ---- Capacity / Clear ----

TEST(ResourcePool, CapacityGrows)
{
    ImmediatePool pool;

    EXPECT_EQ(pool.Capacity(), 0u);

    pool.Create(1);
    EXPECT_EQ(pool.Capacity(), 1u);

    pool.Create(2);
    EXPECT_EQ(pool.Capacity(), 2u);

    pool.Create(3);
    EXPECT_EQ(pool.Capacity(), 3u);
}

TEST(ResourcePool, ClearResetsEverything)
{
    ImmediatePool pool;

    auto h = pool.Create(42);
    EXPECT_TRUE(pool.Get(h).has_value());

    pool.Clear();

    EXPECT_EQ(pool.Capacity(), 0u);
    EXPECT_FALSE(pool.Get(h).has_value());
}

// ---- Invalid Handle Queries ----

TEST(ResourcePool, GetWithInvalidHandle)
{
    ImmediatePool pool;

    DummyHandle invalid; // Default: INVALID_INDEX, generation 0
    auto result = pool.Get(invalid);
    EXPECT_FALSE(result.has_value());

    auto* ptr = pool.GetUnchecked(invalid);
    EXPECT_EQ(ptr, nullptr);
}

TEST(ResourcePool, GetWithOutOfRangeIndex)
{
    ImmediatePool pool;

    DummyHandle outOfRange(999, 1);
    auto result = pool.Get(outOfRange);
    EXPECT_FALSE(result.has_value());
}

// ---- Pointer Stability ----

TEST(ResourcePool, PointerStableAcrossAdds)
{
    ImmediatePool pool;

    auto h0 = pool.Create(0);
    DummyResource* ptr0 = pool.GetUnchecked(h0);
    ASSERT_NE(ptr0, nullptr);

    // Add many more resources to potentially trigger vector reallocation.
    for (int i = 1; i < 100; ++i)
        pool.Create(i);

    // The original pointer should still be valid (unique_ptr heap allocation).
    DummyResource* ptr0After = pool.GetUnchecked(h0);
    EXPECT_EQ(ptr0, ptr0After);
    EXPECT_EQ(ptr0After->Value, 0);
}

// ---- Thread Safety (basic) ----

TEST(ResourcePool, ConcurrentReads)
{
    ImmediatePool pool;

    // Pre-populate
    std::vector<DummyHandle> handles;
    for (int i = 0; i < 50; ++i)
        handles.push_back(pool.Create(i));

    // Launch readers concurrently
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
    {
        threads.emplace_back([&pool, &handles]()
        {
            for (int iter = 0; iter < 100; ++iter)
            {
                for (const auto& h : handles)
                {
                    auto result = pool.Get(h);
                    EXPECT_TRUE(result.has_value());
                }
            }
        });
    }

    for (auto& t : threads) t.join();
}
