// GRAPHICS-154 / ADR 0030: canonical residency slots upload a property once per CPU revision and
// share it; a new revision replaces the slot (the old buffer is released once nobody holds it).
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <span>

import Extrinsic.Graphics.GpuPropertyResidency;

#include "MockRHI.hpp"

namespace
{
    using namespace Extrinsic::Graphics;

    GpuPropertyKey Key(std::uint64_t owner) { return {.Scope = 1, .Owner = owner, .Domain = 2, .ValueKind = 3, .Name = "v:position"}; }

    auto Fill(std::byte value)
    {
        return [value](std::span<std::byte> out) { for (auto& b : out) b = value; };
    }
}

TEST(GpuPropertyResidency, UploadsOncePerRevisionAndSharesTheSlot)
{
    Extrinsic::Tests::MockDevice device;
    GpuPropertyResidency residency(device);
    int fills = 0;
    const auto counting = [&](std::span<std::byte> out) { ++fills; for (auto& b : out) b = std::byte{7}; };

    const auto first = residency.AcquireInput(Key(5), 10u, 12u, 4u, counting);
    ASSERT_TRUE(first && first->Valid());
    EXPECT_EQ(first->Bytes, 48u);
    EXPECT_NE(first->Address, 0u);
    const auto second = residency.AcquireInput(Key(5), 10u, 12u, 4u, counting);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->Buffer, first->Buffer) << "same revision: one shared slot";
    EXPECT_EQ(fills, 1) << "a hit does not read the CPU property again";
    EXPECT_EQ(device.CreateBufferCount, 1);
    ASSERT_EQ(device.BufferWrites.size(), 1u);
    EXPECT_EQ(device.BufferWrites.front().Data.size(), 48u);
    EXPECT_EQ(device.BufferWrites.front().Data.front(), std::byte{7});

    const auto stats = residency.Stats();
    EXPECT_EQ(stats.Uploads, 1u);
    EXPECT_EQ(stats.UploadBytes, 48u);
    EXPECT_EQ(stats.Hits, 1u);
    EXPECT_EQ(stats.ResidentBytes, 48u);
    EXPECT_EQ(stats.Slots, 1u);

    // Another owner is another slot.
    ASSERT_TRUE(residency.AcquireInput(Key(6), 10u, 12u, 4u, Fill(std::byte{1})));
    EXPECT_EQ(residency.Stats().Slots, 2u);
}

TEST(GpuPropertyResidency, ANewRevisionReplacesTheSlotAndReleasesItOnceUnheld)
{
    Extrinsic::Tests::MockDevice device;
    GpuPropertyResidency residency(device);
    auto held = residency.AcquireInput(Key(5), 1u, 4u, 8u, Fill(std::byte{1}));
    ASSERT_TRUE(held);
    auto newer = residency.AcquireInput(Key(5), 2u, 4u, 8u, Fill(std::byte{2}));
    ASSERT_TRUE(newer);
    EXPECT_NE(newer->Buffer, held->Buffer);
    EXPECT_EQ(device.DestroyBufferCount, 0) << "the old slot is still held";
    EXPECT_EQ(residency.Stats().ResidentBytes, 64u);

    held.reset();
    residency.Prune({});   // releases the retired slot; the current one is still held
    EXPECT_EQ(device.DestroyBufferCount, 1);
    EXPECT_EQ(residency.Stats().ResidentBytes, 32u);
    EXPECT_EQ(residency.Stats().Slots, 1u);
    newer.reset();
    residency.Prune({});   // with no wanted keys, an unheld slot goes too
    EXPECT_EQ(device.DestroyBufferCount, 2);
    EXPECT_EQ(residency.Stats().ResidentBytes, 0u);
    EXPECT_EQ(residency.Stats().Slots, 0u);
}

TEST(GpuPropertyResidency, PruneKeepsHeldAndWantedSlotsAndRefusesUnusableRequests)
{
    Extrinsic::Tests::MockDevice device;
    GpuPropertyResidency residency(device);
    const auto held = residency.AcquireInput(Key(1), 1u, 4u, 2u, Fill(std::byte{1}));
    ASSERT_TRUE(residency.AcquireInput(Key(2), 1u, 4u, 2u, Fill(std::byte{1})));
    ASSERT_TRUE(residency.AcquireInput(Key(3), 1u, 4u, 2u, Fill(std::byte{1})));
    residency.Prune([](const GpuPropertyKey& key) { return key.Owner == 2; });
    EXPECT_EQ(residency.Stats().Slots, 2u) << "slot 1 is held, slot 2 wanted, slot 3 released";
    EXPECT_EQ(device.DestroyBufferCount, 1);

    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, 0u, 2u, Fill(std::byte{1})));
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, 4u, 0u, Fill(std::byte{1})));
    device.Operational = false;
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, 4u, 2u, Fill(std::byte{1})));
    device.Operational = true;
    device.FailNextBufferCreate = true;
    EXPECT_FALSE(residency.AcquireInput(Key(9), 1u, 4u, 2u, Fill(std::byte{1})));
}
