#include <cstdint>

#include <gtest/gtest.h>

import Extrinsic.Runtime.RenderWorldPool;
import Extrinsic.Runtime.RenderExtraction;

using Extrinsic::Runtime::RenderWorldPool;
using Extrinsic::Runtime::RuntimeRenderExtractionStats;
using Extrinsic::Runtime::MirrorRenderWorldPoolDiagnostics;

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

// --- exhausted pool must not hand out a referenced slot ------------------

// Regression for PR #970 review: with all three buffers published as fronts and
// still held in flight (no retires), AcquireBack has no free slot and no
// unpublished back slot. It must fail closed (kInvalidSlot) rather than reuse a
// slot an in-flight frame still references.
TEST(RenderWorldPool, ExhaustedPoolDoesNotReuseReferencedSlot)
{
    RenderWorldPool pool(3u);

    // Publish + acquire (hold) a distinct front for frames 0, 1, 2 without ever
    // releasing, so all three slots end up referenced.
    std::uint32_t held[3] = {kInvalid, kInvalid, kInvalid};
    for (std::uint32_t frame = 0u; frame < 3u; ++frame)
    {
        const std::uint32_t back = pool.AcquireBack(frame);
        ASSERT_NE(back, kInvalid) << "frame " << frame;
        pool.PublishFront(back);
        const std::uint32_t front = pool.AcquireFront(frame);
        ASSERT_EQ(front, back);
        held[frame] = front;
    }

    // All three slots are distinct and each holds exactly one reference.
    EXPECT_NE(held[0], held[1]);
    EXPECT_NE(held[0], held[2]);
    EXPECT_NE(held[1], held[2]);
    EXPECT_EQ(pool.RefCount(held[0]), 1u);
    EXPECT_EQ(pool.RefCount(held[1]), 1u);
    EXPECT_EQ(pool.RefCount(held[2]), 1u);

    // The pool is exhausted: AcquireBack must not hand out any of the in-flight
    // slots. It fails closed and counts the skip.
    const std::uint32_t exhausted = pool.AcquireBack(3u);
    EXPECT_EQ(exhausted, kInvalid);
    EXPECT_EQ(pool.GetDiagnostics().ExtractionSkipCount, 1u);

    // No referenced slot was reused or overwritten: every held front still
    // carries exactly its one reference.
    EXPECT_EQ(pool.RefCount(held[0]), 1u);
    EXPECT_EQ(pool.RefCount(held[1]), 1u);
    EXPECT_EQ(pool.RefCount(held[2]), 1u);

    // Releasing one in-flight front lets the next AcquireBack reclaim it (the
    // pool recovers once back-pressure clears).
    pool.ReleaseFront(held[0]);
    const std::uint32_t recovered = pool.AcquireBack(4u);
    EXPECT_EQ(recovered, held[0]);
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

TEST(RenderWorldPool, PreviousFrontAcquireConsumesRenderNMinusOneWithoutStall)
{
    RenderWorldPool pool(3u);

    const std::uint32_t frame0 = pool.AcquireBack(0u);
    ASSERT_NE(frame0, kInvalid);
    pool.PublishFront(frame0);

    const std::uint32_t frame1 = pool.AcquireBack(1u);
    ASSERT_NE(frame1, kInvalid);
    ASSERT_NE(frame1, frame0);
    pool.PublishFront(frame1);

    const std::uint32_t consumed = pool.AcquirePreviousFront(1u);
    EXPECT_EQ(consumed, frame0);
    EXPECT_EQ(pool.RefCount(frame0), 1u);
    EXPECT_EQ(pool.GetDiagnostics().LastConsumedFrameAge, 1u);
    EXPECT_EQ(pool.GetDiagnostics().PipelineStallCount, 0u);
    EXPECT_EQ(pool.GetDiagnostics().ExtractionSkipCount, 0u);

    pool.ReleaseFront(consumed);
    EXPECT_EQ(pool.RefCount(frame0), 0u);
}

// --- GRAPHICS-036B: diagnostics mirror onto extraction stats -------------

TEST(RenderWorldPoolDiagnostics, MirrorIsZeroForUntouchedPool)
{
    RenderWorldPool pool;
    RuntimeRenderExtractionStats stats{};
    MirrorRenderWorldPoolDiagnostics(pool, stats);
    EXPECT_EQ(stats.RenderWorldPipelineStallCount, 0u);
    EXPECT_EQ(stats.RenderWorldExtractionSkipCount, 0u);
    EXPECT_EQ(stats.RenderWorldFrameAgeFrames, 0u);
}

TEST(RenderWorldPoolDiagnostics, MirrorCopiesStallSkipAndFrameAge)
{
    RenderWorldPool pool(2u);

    // Produce one stall (consumer reuses front with no new publish).
    const std::uint32_t a = pool.AcquireBack(0u);
    pool.PublishFront(a);
    (void)pool.AcquireFront(0u);
    (void)pool.AcquireFront(1u); // no new publish -> stall, frame age 1

    // Produce one skip (hold front, overwrite the unpublished back).
    const std::uint32_t b = pool.AcquireBack(2u);
    EXPECT_NE(b, a);
    const std::uint32_t b2 = pool.AcquireBack(3u); // front A held -> replace back
    EXPECT_EQ(b2, b);

    RuntimeRenderExtractionStats stats{};
    MirrorRenderWorldPoolDiagnostics(pool, stats);
    EXPECT_EQ(stats.RenderWorldPipelineStallCount, pool.GetDiagnostics().PipelineStallCount);
    EXPECT_EQ(stats.RenderWorldExtractionSkipCount, pool.GetDiagnostics().ExtractionSkipCount);
    EXPECT_EQ(stats.RenderWorldFrameAgeFrames, pool.GetDiagnostics().LastConsumedFrameAge);
    EXPECT_GE(stats.RenderWorldPipelineStallCount, 1u);
    EXPECT_GE(stats.RenderWorldExtractionSkipCount, 1u);
}
