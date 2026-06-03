#include <cstdint>

#include <gtest/gtest.h>

import Extrinsic.Runtime.RenderWorldPool;

using Extrinsic::Runtime::RenderWorldPool;

namespace
{
    constexpr std::uint32_t kInvalid = RenderWorldPool::kInvalidSlot;
}

// --- construction / clamp ------------------------------------------------

TEST(RenderWorldPool, DefaultsToTripleBuffer)
{
    RenderWorldPool pool;
    EXPECT_EQ(pool.BufferCount(), 3u);
    EXPECT_FALSE(pool.IsSynchronous());
    EXPECT_EQ(pool.FrontSlot(), kInvalid);
}

TEST(RenderWorldPool, BufferCountClampsToSupportedRange)
{
    EXPECT_EQ(RenderWorldPool(0u).BufferCount(), RenderWorldPool::kMinBuffers);
    EXPECT_EQ(RenderWorldPool(1u).BufferCount(), 1u);
    EXPECT_EQ(RenderWorldPool(2u).BufferCount(), 2u);
    EXPECT_EQ(RenderWorldPool(99u).BufferCount(), RenderWorldPool::kMaxBuffers);
}

// --- nothing-published behavior ------------------------------------------

TEST(RenderWorldPool, AcquireFrontBeforeAnyPublishReturnsInvalid)
{
    RenderWorldPool pool;
    EXPECT_EQ(pool.AcquireFront(0u), kInvalid);
    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 0u);
}

// --- deterministic rotation (GRAPHICS-036 decision 9 seam) ---------------

TEST(RenderWorldPool, DeterministicRotationSchedule)
{
    RenderWorldPool pool(3u);

    // AcquireBack(A), PublishFront(A), AcquireFront -> A
    const std::uint32_t a = pool.AcquireBack(0u);
    EXPECT_NE(a, kInvalid);
    pool.PublishFront(a);
    EXPECT_EQ(pool.FrontSlot(), a);

    const std::uint32_t consumedA = pool.AcquireFront(0u);
    EXPECT_EQ(consumedA, a);
    EXPECT_EQ(pool.RefCount(a), 1u);

    // AcquireBack(B) must pick a different slot than the held front A.
    const std::uint32_t b = pool.AcquireBack(1u);
    EXPECT_NE(b, kInvalid);
    EXPECT_NE(b, a);
    pool.PublishFront(b);

    // Release the in-flight A, then a new producer acquire can reclaim it.
    pool.ReleaseFront(a);
    EXPECT_EQ(pool.RefCount(a), 0u);

    const std::uint32_t consumedB = pool.AcquireFront(1u);
    EXPECT_EQ(consumedB, b);

    // No stalls or skips occurred on this well-behaved schedule.
    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 0u);
    EXPECT_EQ(pool.GetDiagnostics().ExtractionSkipCount, 0u);
}

// --- reclamation against a held front ------------------------------------

TEST(RenderWorldPool, HeldFrontIsNotRecycledUntilReleased)
{
    RenderWorldPool pool(3u);

    const std::uint32_t a = pool.AcquireBack(0u);
    pool.PublishFront(a);
    (void)pool.AcquireFront(0u); // refcount(a) == 1, in flight

    // Rotate forward repeatedly; A must never be handed back to the producer
    // while it is still referenced.
    for (std::uint64_t frame = 1u; frame <= 4u; ++frame)
    {
        const std::uint32_t slot = pool.AcquireBack(frame);
        EXPECT_NE(slot, a) << "frame " << frame;
        pool.PublishFront(slot);
    }

    EXPECT_EQ(pool.RefCount(a), 1u);

    // Once released, the slot becomes reclaimable on the next AcquireBack.
    pool.ReleaseFront(a);
    bool reclaimed = false;
    for (std::uint64_t frame = 5u; frame <= 8u; ++frame)
    {
        if (pool.AcquireBack(frame) == a)
        {
            reclaimed = true;
            break;
        }
        pool.PublishFront(pool.FrontSlot());
    }
    EXPECT_TRUE(reclaimed);
}

// --- refcount lifecycle for multiple consumers ---------------------------

TEST(RenderWorldPool, RefcountTracksConcurrentFrontHolders)
{
    RenderWorldPool pool(3u);
    const std::uint32_t a = pool.AcquireBack(0u);
    pool.PublishFront(a);

    (void)pool.AcquireFront(0u); // re-acquire same front twice without a new publish
    (void)pool.AcquireFront(0u);
    EXPECT_EQ(pool.RefCount(a), 2u);
    // The second acquire saw no new publish -> counted as a stall.
    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 1u);

    pool.ReleaseFront(a);
    EXPECT_EQ(pool.RefCount(a), 1u);
    pool.ReleaseFront(a);
    EXPECT_EQ(pool.RefCount(a), 0u);

    // Over-release is a safe no-op.
    pool.ReleaseFront(a);
    EXPECT_EQ(pool.RefCount(a), 0u);
}

// --- consumer-faster-than-producer (stall) -------------------------------

TEST(RenderWorldPool, ConsumerReusesFrontAndCountsStall)
{
    RenderWorldPool pool(3u);
    const std::uint32_t a = pool.AcquireBack(0u);
    pool.PublishFront(a);

    const std::uint32_t first = pool.AcquireFront(0u);
    pool.ReleaseFront(first);
    // No new publish: consumer reuses the same front and records a stall.
    const std::uint32_t second = pool.AcquireFront(1u);
    EXPECT_EQ(second, first);
    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 1u);
    pool.ReleaseFront(second);
}

// --- producer-faster-than-consumer (skip / replace) ----------------------

TEST(RenderWorldPool, ProducerReplacesUnpublishedBackAndCountsSkip)
{
    RenderWorldPool pool(2u);

    // Publish + hold a front so only one slot is free.
    const std::uint32_t a = pool.AcquireBack(0u);
    pool.PublishFront(a);
    (void)pool.AcquireFront(0u); // hold front A in flight

    // Acquire the one remaining slot as an unpublished back...
    const std::uint32_t b = pool.AcquireBack(1u);
    EXPECT_NE(b, a);
    // ...then acquire again before publishing: front A is still held, so no
    // free slot exists; the producer overwrites the unpublished back B.
    const std::uint32_t b2 = pool.AcquireBack(2u);
    EXPECT_EQ(b2, b);
    EXPECT_EQ(pool.GetDiagnostics().ExtractionSkipCount, 1u);
}

// --- synchronous (single-buffer) collapse --------------------------------

TEST(RenderWorldPool, SynchronousModeReusesSingleSlotWithoutCounters)
{
    RenderWorldPool pool(1u);
    EXPECT_TRUE(pool.IsSynchronous());

    for (std::uint64_t frame = 0u; frame < 4u; ++frame)
    {
        const std::uint32_t slot = pool.AcquireBack(frame);
        EXPECT_EQ(slot, 0u);
        pool.PublishFront(slot);
        const std::uint32_t consumed = pool.AcquireFront(frame);
        EXPECT_EQ(consumed, 0u);
        // Same-frame publish/consume -> zero frame age.
        EXPECT_EQ(pool.GetDiagnostics().LastConsumedFrameAge, 0u);
        pool.ReleaseFront(consumed);
    }

    // Synchronous reuse is by design, not back-pressure.
    EXPECT_EQ(pool.GetDiagnostics().ExtractionSkipCount, 0u);
    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 0u);
}

// --- frame-age reporting for pipelined consume ---------------------------

TEST(RenderWorldPool, FrameAgeReflectsPublishToConsumeDelta)
{
    RenderWorldPool pool(3u);
    const std::uint32_t a = pool.AcquireBack(/*frameIndex=*/5u);
    pool.PublishFront(a);
    // Consumer renders a later frame than the one the snapshot was produced for.
    (void)pool.AcquireFront(/*frameIndex=*/7u);
    EXPECT_EQ(pool.GetDiagnostics().LastConsumedFrameAge, 2u);
}
