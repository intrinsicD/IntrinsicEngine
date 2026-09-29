// GRAPHICS-154/155 / ADR 0030: canonical residency slots upload a property once per CPU revision
// in its own layout and share it; output rings reuse a slot only after its completions and drop
// previews instead of blocking; canonical slots are an LRU cache with an idle timeout and a
// byte budget that never evicts rings, pending, leased or currently observed slots.
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic::Graphics;

    GpuPropertyKey Key(std::uint64_t owner) { return {.Scope = 1, .Owner = owner, .Domain = 2, .ValueKind = 3, .Name = "v:position"}; }

    constexpr GpuPropertyLayout Float3(std::uint32_t count)
    {
        return {.Scalar = GpuScalarType::Float32, .Channels = 3u, .Stride = 12u, .Count = count};
    }
    constexpr GpuPropertyLayout Scalar(GpuScalarType scalar, std::uint32_t count)
    {
        return {.Scalar = scalar, .Channels = 1u, .Count = count};
    }

    auto Fill(std::byte value)
    {
        return [value](std::span<std::byte> out) { for (auto& b : out) b = value; };
    }

    struct Clock
    {
        double Seconds{};
        std::function<double()> Fn() { return [this] { return Seconds; }; }
    };

    // Frame N's fence is waited by BeginFrame(N + FramesInFlight), after the counter already
    // reads N + FramesInFlight: a use is complete one frame later than that distance.
    void AdvanceFrames(Extrinsic::Tests::MockDevice& device, std::uint32_t frames = 0)
    {
        device.GlobalFrameNumber += frames ? frames : device.FramesInFlight + 1u;
    }

}

// The residency uploads through the transfer queue only; the mock must accept uploads.
#define MOCK_DEVICE_ACCEPTING_UPLOADS(name) \
    Extrinsic::Tests::MockDevice name;      \
    name.TransferQueue.AcceptBufferUploads = true

TEST(GpuPropertyResidency, ResolvingTwiceUploadsOnceThroughTheTransferQueue)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    int fills = 0;
    const auto counting = [&](std::span<std::byte> out) { ++fills; for (auto& b : out) b = std::byte{7}; };

    const auto first = residency.AcquireInput(Key(5), 10u, Float3(4), counting);
    ASSERT_TRUE(first && first->Valid());
    EXPECT_EQ(first->Bytes, 48u);
    EXPECT_NE(first->Address, 0u);
    EXPECT_TRUE(first->Upload.IsValid()) << "device-local slot filled through the transfer queue";
    EXPECT_EQ(first->Layout.ElementBytes(), 12u);
    EXPECT_EQ(first->Layout.Stride, 0u) << "a packed stride is normalized to 0";
    const auto second = residency.AcquireInput(Key(5), 10u, Float3(4), counting);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->Buffer, first->Buffer) << "same revision and layout: one shared slot";
    EXPECT_EQ(fills, 1) << "a hit does not read the CPU property again";
    EXPECT_EQ(device.CreateBufferCount, 1);
    ASSERT_EQ(device.TransferQueue.BufferUploads.size(), 1u);
    EXPECT_EQ(device.TransferQueue.BufferUploads.front().Buffer, first->Buffer);
    EXPECT_EQ(device.TransferQueue.BufferUploads.front().Data.size(), 48u);
    EXPECT_EQ(device.TransferQueue.BufferUploads.front().Data.front(), std::byte{7});
    EXPECT_TRUE(device.BufferWrites.empty());

    const auto stats = residency.Stats();
    EXPECT_EQ(stats.Uploads, 1u);
    EXPECT_EQ(stats.UploadBytes, 48u);
    EXPECT_EQ(stats.Hits, 1u);
    EXPECT_EQ(stats.Misses, 1u);
    EXPECT_EQ(stats.ResidentBytes, 48u);
    EXPECT_EQ(stats.Slots, 1u);

    // Another owner is another slot.
    ASSERT_TRUE(residency.AcquireInput(Key(6), 10u, Float3(4), Fill(std::byte{1})));
    EXPECT_EQ(residency.Stats().Slots, 2u);
}

TEST(GpuPropertyResidency, ARefusedUploadCachesNothingAndIsCounted)
{
    Extrinsic::Tests::MockDevice device; // AcceptBufferUploads is off: the queue refuses
    GpuPropertyResidency residency(device);
    EXPECT_FALSE(residency.AcquireInput(Key(5), 1u, Float3(2), Fill(std::byte{9})));
    EXPECT_TRUE(device.BufferWrites.empty()) << "no synchronous write whose outcome is unknown";
    EXPECT_EQ(device.CreateBufferCount, 1);
    EXPECT_EQ(device.DestroyBufferCount, 1);
    const auto stats = residency.Stats();
    EXPECT_EQ(stats.UploadRefusals, 1u);
    EXPECT_EQ(stats.Uploads, 0u);
    EXPECT_EQ(stats.Slots, 0u);
    EXPECT_EQ(stats.ResidentBytes, 0u);

    // The next attempt uploads once the queue accepts; a resident older revision survives a
    // refused newer one untouched.
    device.TransferQueue.AcceptBufferUploads = true;
    const auto first = residency.AcquireInput(Key(5), 1u, Float3(2), Fill(std::byte{9}));
    ASSERT_TRUE(first);
    device.TransferQueue.AcceptBufferUploads = false;
    EXPECT_FALSE(residency.AcquireInput(Key(5), 2u, Float3(2), Fill(std::byte{9})));
    device.TransferQueue.AcceptBufferUploads = true;
    const auto still = residency.AcquireInput(Key(5), 1u, Float3(2), Fill(std::byte{9}));
    ASSERT_TRUE(still);
    EXPECT_EQ(still->Buffer, first->Buffer);
    EXPECT_EQ(residency.Stats().Hits, 1u);
    EXPECT_EQ(residency.Stats().UploadRefusals, 2u);
}

TEST(GpuPropertyResidency, APackedStrideAndStrideZeroAreOneLayoutIdentity)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    const auto strided = residency.AcquireInput(Key(5), 1u, Float3(4), Fill(std::byte{1}));  // Stride = 12
    ASSERT_TRUE(strided);
    const auto packed = residency.AcquireInput(
        Key(5), 1u, {.Scalar = GpuScalarType::Float32, .Channels = 3u, .Stride = 0u, .Count = 4u}, Fill(std::byte{1}));
    ASSERT_TRUE(packed);
    EXPECT_EQ(packed->Buffer, strided->Buffer) << "the same bytes: one slot, one upload";
    EXPECT_EQ(strided->Layout, packed->Layout);
    EXPECT_EQ(strided->Layout.ElementBytes(), 12u);
    EXPECT_EQ(residency.Stats().Uploads, 1u);
    EXPECT_EQ(residency.Stats().Hits, 1u);
    // A genuinely wider stride stays distinct.
    ASSERT_TRUE(residency.AcquireInput(
        Key(5), 1u, {.Scalar = GpuScalarType::Float32, .Channels = 3u, .Stride = 16u, .Count = 4u}, Fill(std::byte{1})));
    EXPECT_EQ(residency.Stats().Uploads, 2u);
}

TEST(GpuPropertyResidency, AUseIsCompleteOnlyOneFramePastTheFramesInFlightDistance)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    device.FramesInFlight = 2u;
    GpuPropertyResidency residency(device);
    const auto layout = Scalar(GpuScalarType::Float32, 4);
    const auto a = residency.AcquireBack(Key(1), layout, 2u)->Buffer;  // used in frame 0
    EXPECT_TRUE(residency.Publish(Key(1)));
    ASSERT_TRUE(residency.AcquireBack(Key(1), layout, 2u));
    EXPECT_TRUE(residency.Publish(Key(1)));
    device.GlobalFrameNumber = 2u;  // == FramesInFlight: frame 0's fence is not yet waited
    EXPECT_FALSE(residency.AcquireBack(Key(1), layout, 2u));
    device.GlobalFrameNumber = 3u;
    const auto reused = residency.AcquireBack(Key(1), layout, 2u);
    ASSERT_TRUE(reused);
    EXPECT_EQ(reused->Buffer, a);
    EXPECT_TRUE(residency.Publish(Key(1)));

    // NoteUse pins the same boundary on a canonical slot's release.
    ASSERT_TRUE(residency.AcquireInput(Key(2), 1u, layout, Fill(std::byte{1})));
    const auto old = residency.Front(Key(2))->Buffer;
    residency.NoteUse(old, 5u);
    ASSERT_TRUE(residency.AcquireInput(Key(2), 2u, layout, Fill(std::byte{2})));  // retires the old slot
    device.GlobalFrameNumber = 7u;
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 0);
    device.GlobalFrameNumber = 8u;
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 1);
}

TEST(GpuPropertyResidency, ARevisionBumpUploadsOnceMoreAndReleasesTheOldSlotOnceUnheldAndComplete)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    auto held = residency.AcquireInput(Key(5), 1u, Scalar(GpuScalarType::Float32, 8), Fill(std::byte{1}));
    ASSERT_TRUE(held);
    auto newer = residency.AcquireInput(Key(5), 2u, Scalar(GpuScalarType::Float32, 8), Fill(std::byte{2}));
    ASSERT_TRUE(newer);
    EXPECT_NE(newer->Buffer, held->Buffer);
    EXPECT_EQ(residency.Stats().Uploads, 2u);
    EXPECT_EQ(device.DestroyBufferCount, 0) << "the old slot is still held";
    EXPECT_EQ(residency.Stats().ResidentBytes, 64u);

    held.reset();
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 0) << "released, but its frame is still in flight";
    AdvanceFrames(device);
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 1);
    EXPECT_EQ(residency.Stats().ResidentBytes, 32u);
    EXPECT_EQ(residency.Stats().Slots, 1u);
    EXPECT_EQ(residency.Stats().Releases, 1u);

    // The same revision again: still one slot, no upload.
    ASSERT_TRUE(residency.AcquireInput(Key(5), 2u, Scalar(GpuScalarType::Float32, 8), Fill(std::byte{2})));
    EXPECT_EQ(residency.Stats().Uploads, 2u);
}

TEST(GpuPropertyResidency, ALayoutMismatchIsAMissAndADoublePropertyStaysDouble)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    const auto asFloat = residency.AcquireInput(Key(5), 1u, Scalar(GpuScalarType::Float32, 4), Fill(std::byte{1}));
    ASSERT_TRUE(asFloat);
    EXPECT_EQ(asFloat->Bytes, 16u);
    const auto asDouble = residency.AcquireInput(Key(5), 1u, Scalar(GpuScalarType::Float64, 4), Fill(std::byte{2}));
    ASSERT_TRUE(asDouble);
    EXPECT_NE(asDouble->Buffer, asFloat->Buffer) << "another layout is never a reinterpretation";
    EXPECT_EQ(asDouble->Bytes, 32u);
    EXPECT_EQ(asDouble->Layout.Scalar, GpuScalarType::Float64);
    EXPECT_EQ(asDouble->Layout.ElementBytes(), 8u);
    EXPECT_EQ(residency.Stats().Uploads, 2u);
    EXPECT_EQ(residency.Stats().Misses, 2u);
    EXPECT_EQ(residency.Stats().Hits, 0u);
    const auto again = residency.AcquireInput(Key(5), 1u, Scalar(GpuScalarType::Float64, 4), Fill(std::byte{2}));
    ASSERT_TRUE(again);
    EXPECT_EQ(again->Buffer, asDouble->Buffer);
    EXPECT_EQ(residency.Stats().Hits, 1u);

    // A different row map or count is a different layout too.
    auto remapped = Scalar(GpuScalarType::Float64, 4);
    remapped.RowMap = 7u;
    const auto other = residency.AcquireInput(Key(5), 1u, remapped, Fill(std::byte{3}));
    ASSERT_TRUE(other);
    EXPECT_NE(other->Buffer, asDouble->Buffer);
    EXPECT_EQ(residency.Stats().Uploads, 3u);
}

TEST(GpuPropertyResidency, RefusesUnusableRequests)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, Scalar(GpuScalarType::Float32, 0), Fill(std::byte{1})));
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, {.Channels = 5u, .Count = 2u}, Fill(std::byte{1})));
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, {.Channels = 3u, .Stride = 8u, .Count = 2u}, Fill(std::byte{1})));
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, Float3(2), {}));
    device.Operational = false;
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, Float3(2), Fill(std::byte{1})));
    EXPECT_FALSE(residency.AcquireBack(Key(9), Float3(2), 2u));
    device.Operational = true;
    device.FailNextBufferCreate = true;
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, Float3(2), Fill(std::byte{1})));
    EXPECT_FALSE(residency.AcquireBack(Key(9), Float3(2), 0u));
    EXPECT_FALSE(residency.AcquireBack(Key(9), Float3(2), 4u));
    EXPECT_EQ(residency.Stats().Slots, 0u);
    EXPECT_EQ(residency.Stats().ResidentBytes, 0u);
}

TEST(GpuPropertyResidency, ARingSlotIsNotRewrittenBeforeItsCompletions)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    device.FramesInFlight = 2u;
    GpuPropertyResidency residency(device);
    const auto layout = Scalar(GpuScalarType::Float32, 16);

    // Frame 0: write A, publish; write B, publish. A is free but its frame is in flight.
    // (Handles only: a held view is a lease, which is the last case below.)
    const auto acquire = [&] {
        const auto view = residency.AcquireBack(Key(1), layout, 2u);
        return view ? view->Buffer : Extrinsic::RHI::BufferHandle{};
    };
    const auto a = acquire();
    ASSERT_TRUE(a.IsValid());
    EXPECT_TRUE(residency.Publish(Key(1)));
    const auto b = acquire();
    ASSERT_TRUE(b.IsValid());
    EXPECT_NE(b, a);
    EXPECT_TRUE(residency.Publish(Key(1)));
    EXPECT_EQ(residency.Front(Key(1))->Buffer, b);
    EXPECT_FALSE(residency.AcquireBack(Key(1), layout, 2u)) << "A's frame is still in flight";
    EXPECT_EQ(residency.Stats().RingWaits, 1u);
    EXPECT_EQ(residency.Stats().DroppedPreviews, 1u);
    EXPECT_EQ(device.CreateBufferCount, 2) << "the ring never grows past its depth";

    // Frame 2: A is reusable; a readback token on it keeps it pending again.
    AdvanceFrames(device);
    EXPECT_EQ(acquire(), a);
    EXPECT_TRUE(residency.Publish(Key(1)));
    AdvanceFrames(device);
    device.TransferQueue.ReadbacksComplete = false;
    residency.AddCompletion(b, Extrinsic::RHI::ReadbackToken{1u}, 64u);
    EXPECT_FALSE(acquire().IsValid()) << "B's readback is pending";
    device.TransferQueue.ReadbacksComplete = true;
    EXPECT_EQ(acquire(), b);

    // A transfer token behaves the same; a held lease as well.
    EXPECT_TRUE(residency.Publish(Key(1)));
    AdvanceFrames(device);
    device.TransferQueue.AlwaysComplete = false;
    residency.AddCompletion(a, Extrinsic::RHI::TransferToken{5u});
    EXPECT_FALSE(acquire().IsValid()) << "A's transfer is pending";
    device.TransferQueue.AlwaysComplete = true;
    {
        const auto leased = residency.AcquireBack(Key(1), layout, 2u);
        ASSERT_TRUE(leased);
        EXPECT_EQ(leased->Buffer, a);
        AdvanceFrames(device);
        EXPECT_FALSE(acquire().IsValid()) << "A is leased for writing";
    }
    EXPECT_EQ(acquire(), a) << "the dropped lease frees A";
    const auto stats = residency.Stats();
    EXPECT_EQ(stats.Publishes, 4u);
    EXPECT_EQ(stats.Readbacks, 1u);
    EXPECT_EQ(stats.ReadbackBytes, 64u);
    EXPECT_EQ(stats.Rings, 1u);
}

TEST(GpuPropertyResidency, AnExhaustedRingDropsThePreviewInsteadOfBlocking)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    const auto layout = Scalar(GpuScalarType::Float32, 4);
    const auto only = residency.AcquireBack(Key(1), layout, 1u)->Buffer;  // the view's lease is dropped
    ASSERT_TRUE(only.IsValid());
    EXPECT_TRUE(residency.Publish(Key(1)));
    AdvanceFrames(device, 10u);  // its completions are long done: only front status protects it
    EXPECT_FALSE(residency.AcquireBack(Key(1), layout, 1u)) << "the front is never overwritten";
    EXPECT_FALSE(residency.AcquireBack(Key(1), layout, 1u));
    EXPECT_EQ(residency.Stats().DroppedPreviews, 2u);
    EXPECT_EQ(residency.Stats().RingWaits, 0u);
    EXPECT_EQ(device.CreateBufferCount, 1);
    ASSERT_TRUE(residency.Front(Key(1)));
    EXPECT_EQ(residency.Front(Key(1))->Buffer, only);
    EXPECT_FALSE(residency.Publish(Key(1))) << "nothing acquired to publish";
    EXPECT_FALSE(residency.AcquireBack(Key(1), Scalar(GpuScalarType::Float64, 4), 1u)) << "a ring keeps its layout";
}

TEST(GpuPropertyResidency, DiscardFreesOnlyAfterPendingReadbacksComplete)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    const auto layout = Scalar(GpuScalarType::Float64, 4);
    const auto slot = residency.AcquireBack(Key(1), layout, 2u)->Buffer; // the view's lease is dropped
    ASSERT_TRUE(slot.IsValid());
    EXPECT_TRUE(residency.Publish(Key(1)));
    EXPECT_TRUE(residency.HasRing(Key(1)));
    device.TransferQueue.ReadbacksComplete = false;
    residency.AddCompletion(slot, Extrinsic::RHI::ReadbackToken{3u}, 32u);
    residency.Discard(Key(1));
    EXPECT_FALSE(residency.HasRing(Key(1)));
    EXPECT_FALSE(residency.Front(Key(1)));
    EXPECT_EQ(device.DestroyBufferCount, 0);
    AdvanceFrames(device);
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 0) << "the readback is still pending";
    EXPECT_EQ(residency.Stats().ResidentBytes, 32u);
    device.TransferQueue.ReadbacksComplete = true;
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 1);
    EXPECT_EQ(residency.Stats().ResidentBytes, 0u);
    EXPECT_EQ(residency.Stats().Rings, 0u);
}

TEST(GpuPropertyResidency, BindRevisionMakesTheFrontTheCanonicalSlot)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    const auto layout = Scalar(GpuScalarType::Float32, 8);
    const auto canonical = residency.AcquireInput(Key(1), 1u, layout, Fill(std::byte{1}))->Buffer;
    ASSERT_TRUE(canonical.IsValid());
    EXPECT_EQ(residency.Front(Key(1))->Buffer, canonical) << "no ring: the canonical slot is observed";
    EXPECT_FALSE(residency.BindRevision(Key(1), 2u)) << "nothing to bind without a front";

    const auto back = residency.AcquireBack(Key(1), layout, 2u)->Buffer;
    ASSERT_TRUE(back.IsValid());
    EXPECT_EQ(residency.Front(Key(1))->Buffer, canonical) << "an unpublished back is not observed";
    EXPECT_TRUE(residency.Publish(Key(1)));
    EXPECT_EQ(residency.Front(Key(1))->Buffer, back);
    ASSERT_TRUE(residency.AcquireBack(Key(1), layout, 2u)); // a second, unpublished slot
    EXPECT_TRUE(residency.BindRevision(Key(1), 2u));
    EXPECT_FALSE(residency.HasRing(Key(1)));
    EXPECT_EQ(residency.Stats().Slots, 1u);

    const auto bound = residency.AcquireInput(Key(1), 2u, layout, Fill(std::byte{9}));
    ASSERT_TRUE(bound);
    EXPECT_EQ(bound->Buffer, back) << "the accepted front serves the new revision";
    EXPECT_EQ(bound->Revision, 2u);
    EXPECT_EQ(residency.Stats().Uploads, 1u) << "no upload after Accept";
    EXPECT_EQ(residency.Stats().Hits, 1u);
    AdvanceFrames(device);
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 2) << "the old canonical slot and the unpublished ring slot";
    EXPECT_EQ(residency.Stats().ResidentBytes, 32u);
}

TEST(GpuPropertyResidency, IdleCanonicalSlotsAreEvictedAfterTAndReuploadOnce)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    Clock clock;
    GpuPropertyResidency residency(device, {.IdleEvictSeconds = 60.0, .Clock = clock.Fn()});
    ASSERT_TRUE(residency.AcquireInput(Key(1), 1u, Float3(4), Fill(std::byte{1})));
    AdvanceFrames(device);
    clock.Seconds = 30.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 0u);
    clock.Seconds = 61.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 1u);
    EXPECT_EQ(residency.Stats().Slots, 0u);
    EXPECT_EQ(residency.Stats().ResidentBytes, 0u);
    EXPECT_EQ(device.DestroyBufferCount, 1);

    ASSERT_TRUE(residency.AcquireInput(Key(1), 1u, Float3(4), Fill(std::byte{1})));
    ASSERT_TRUE(residency.AcquireInput(Key(1), 1u, Float3(4), Fill(std::byte{1})));
    const auto stats = residency.Stats();
    EXPECT_EQ(stats.Uploads, 2u) << "an evicted slot costs one upload on its next use";
    EXPECT_EQ(stats.Misses, 2u);
    EXPECT_EQ(stats.Hits, 1u);
    EXPECT_EQ(stats.ResidentBytes, 48u);

    // A use (hit or observation) restarts the idle time.
    AdvanceFrames(device);
    clock.Seconds = 100.0;
    residency.MarkObserved(Key(1));
    AdvanceFrames(device);
    clock.Seconds = 150.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 1u) << "observed at 100 s: not idle at 150 s";
    clock.Seconds = 161.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 2u);
}

TEST(GpuPropertyResidency, OverBudgetTheLeastRecentlyUsedSlotsGoFirstWeightedBySize)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    Clock clock;
    GpuPropertyResidency residency(device, {.IdleEvictSeconds = 0.0, .BudgetBytes = 100u, .Clock = clock.Fn()});
    ASSERT_TRUE(residency.AcquireInput(Key(1), 1u, Float3(4), Fill(std::byte{1})));  // 48 B at 0 s
    clock.Seconds = 5.0;
    ASSERT_TRUE(residency.AcquireInput(Key(2), 1u, Float3(1), Fill(std::byte{1})));  // 12 B at 5 s
    clock.Seconds = 10.0;
    ASSERT_TRUE(residency.AcquireInput(Key(3), 1u, Float3(4), Fill(std::byte{1})));  // 48 B at 10 s
    EXPECT_EQ(residency.Stats().ResidentBytes, 108u);
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 0u) << "nothing is complete yet";
    AdvanceFrames(device);
    clock.Seconds = 20.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 1u) << "one eviction brings the total under budget";
    EXPECT_EQ(residency.Stats().ResidentBytes, 60u);
    ASSERT_TRUE(residency.AcquireInput(Key(2), 1u, Float3(1), Fill(std::byte{1})));
    ASSERT_TRUE(residency.AcquireInput(Key(3), 1u, Float3(4), Fill(std::byte{1})));
    EXPECT_EQ(residency.Stats().Hits, 2u) << "the newer slots stay";
    ASSERT_TRUE(residency.AcquireInput(Key(1), 1u, Float3(4), Fill(std::byte{1})));
    EXPECT_EQ(residency.Stats().Uploads, 4u) << "the oldest large slot was evicted";

    // Size-weighted: a larger, slightly newer slot goes before a small older one.
    GpuPropertyResidency weighted(device, {.IdleEvictSeconds = 0.0, .BudgetBytes = 50u, .Clock = clock.Fn()});
    clock.Seconds = 0.0;
    ASSERT_TRUE(weighted.AcquireInput(Key(1), 1u, Float3(1), Fill(std::byte{1})));  // 12 B at 0 s
    clock.Seconds = 1.0;
    ASSERT_TRUE(weighted.AcquireInput(Key(2), 1u, Float3(4), Fill(std::byte{1})));  // 48 B at 1 s
    AdvanceFrames(device);
    clock.Seconds = 10.0;
    weighted.Tick();
    EXPECT_EQ(weighted.Stats().Evictions, 1u);
    EXPECT_EQ(weighted.Stats().ResidentBytes, 12u);
    ASSERT_TRUE(weighted.AcquireInput(Key(1), 1u, Float3(1), Fill(std::byte{1})));
    EXPECT_EQ(weighted.Stats().Hits, 1u) << "the small older slot survived";
}

TEST(GpuPropertyResidency, RingsPendingLeasedAndObservedSlotsAreNeverEvicted)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    Clock clock;
    GpuPropertyResidency residency(device, {.IdleEvictSeconds = 1.0, .BudgetBytes = 1u, .Clock = clock.Fn()});
    const auto layout = Float3(2);
    ASSERT_TRUE(residency.AcquireInput(Key(1), 1u, layout, Fill(std::byte{1})));   // with a ring
    ASSERT_TRUE(residency.AcquireBack(Key(1), layout, 2u));
    EXPECT_TRUE(residency.Publish(Key(1)));
    ASSERT_TRUE(residency.AcquireInput(Key(2), 1u, layout, Fill(std::byte{1})));   // readback pending
    device.TransferQueue.ReadbacksComplete = false;
    residency.AddCompletion(residency.Front(Key(2))->Buffer, Extrinsic::RHI::ReadbackToken{1u}, 24u);
    auto leased = residency.AcquireInput(Key(3), 1u, layout, Fill(std::byte{1}));  // leased
    ASSERT_TRUE(leased);
    ASSERT_TRUE(residency.AcquireInput(Key(4), 1u, layout, Fill(std::byte{1})));   // observed this frame
    ASSERT_TRUE(residency.AcquireInput(Key(5), 1u, layout, Fill(std::byte{1})));   // plain
    AdvanceFrames(device);
    residency.MarkObserved(Key(4));
    clock.Seconds = 10.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Evictions, 1u) << "only the plain slot";
    EXPECT_EQ(residency.Stats().Slots, 4u);
    ASSERT_TRUE(residency.AcquireInput(Key(5), 1u, layout, Fill(std::byte{1})));
    EXPECT_EQ(residency.Stats().Uploads, 6u);

    // Remove the blockers one by one.
    residency.Discard(Key(1));
    device.TransferQueue.ReadbacksComplete = true;
    leased.reset();
    AdvanceFrames(device);
    clock.Seconds = 20.0;
    residency.Tick();
    EXPECT_EQ(residency.Stats().Slots, 0u);
    EXPECT_EQ(residency.Stats().Evictions, 6u);
    EXPECT_EQ(residency.Stats().ResidentBytes, 0u);
}

TEST(GpuPropertyResidency, PruneReleasesRejectedKeysIncludingTheirRingsOnceComplete)
{
    MOCK_DEVICE_ACCEPTING_UPLOADS(device);
    GpuPropertyResidency residency(device);
    const auto layout = Float3(2);
    const auto held = residency.AcquireInput(Key(1), 1u, layout, Fill(std::byte{1}));
    ASSERT_TRUE(residency.AcquireInput(Key(2), 1u, layout, Fill(std::byte{1})));
    ASSERT_TRUE(residency.AcquireInput(Key(3), 1u, layout, Fill(std::byte{1})));
    ASSERT_TRUE(residency.AcquireBack(Key(3), layout, 1u));
    residency.Prune([](const GpuPropertyKey& key) { return key.Owner == 2; });
    EXPECT_EQ(residency.Stats().Slots, 1u) << "slot 1 is held but pruned, slot 2 wanted, slot 3 released";
    EXPECT_FALSE(residency.HasRing(Key(3)));
    EXPECT_EQ(device.DestroyBufferCount, 0) << "released slots wait for their frames in flight";
    AdvanceFrames(device);
    residency.Tick();
    EXPECT_EQ(device.DestroyBufferCount, 2) << "slot 3 and its ring slot; slot 1 is still held";
    EXPECT_EQ(residency.Stats().ResidentBytes, 48u);
    residency.Clear();
    EXPECT_EQ(device.DestroyBufferCount, 4) << "Clear frees everything, held or not";
    EXPECT_EQ(residency.Stats().ResidentBytes, 0u);
}
