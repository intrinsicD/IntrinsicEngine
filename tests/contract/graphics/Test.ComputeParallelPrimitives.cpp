#include <array>
#include <cstdint>
#include <limits>
#include <span>

#include <gtest/gtest.h>

#include "MockRHI.hpp"

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Handles;

namespace
{
    using namespace Extrinsic;

    [[nodiscard]] RHI::BufferHandle ValidBuffer(const std::uint32_t index) noexcept
    {
        return RHI::BufferHandle{index, 1u};
    }
}

TEST(ComputeParallelPrimitives, ExclusiveAndInclusivePrefixScanMatchReference)
{
    const std::array<std::uint32_t, 5> input{{3u, 0u, 2u, 7u, 1u}};
    std::array<std::uint32_t, 5> exclusive{};
    std::array<std::uint32_t, 5> inclusive{};

    const auto exclusiveResult = Graphics::ComputePrefixScanCpu(
        input, exclusive, Graphics::PrefixScanMode::Exclusive);
    const auto inclusiveResult = Graphics::ComputePrefixScanCpu(
        input, inclusive, Graphics::PrefixScanMode::Inclusive);

    EXPECT_TRUE(exclusiveResult.Succeeded());
    EXPECT_TRUE(inclusiveResult.Succeeded());
    EXPECT_EQ((std::array<std::uint32_t, 5>{{0u, 3u, 3u, 5u, 12u}}), exclusive);
    EXPECT_EQ((std::array<std::uint32_t, 5>{{3u, 3u, 5u, 12u, 13u}}), inclusive);
    EXPECT_EQ(exclusiveResult.Diagnostics.Total, 13u);
    EXPECT_EQ(inclusiveResult.Diagnostics.Total, 13u);
}

TEST(ComputeParallelPrimitives, EmptyPrefixScanSucceedsWithoutWriting)
{
    std::array<std::uint32_t, 1> output{{99u}};

    const auto result = Graphics::ComputePrefixScanCpu(
        std::span<const std::uint32_t>{}, output, Graphics::PrefixScanMode::Exclusive);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Diagnostics.ElementCount, 0u);
    EXPECT_EQ(result.Diagnostics.Total, 0u);
    EXPECT_EQ(output[0], 99u);
}

TEST(ComputeParallelPrimitives, PrefixScanFailsClosedForSmallOutputAndOverflow)
{
    const std::array<std::uint32_t, 3> input{{1u, 2u, 3u}};
    std::array<std::uint32_t, 2> smallOutput{};

    const auto small = Graphics::ComputePrefixScanCpu(
        input, smallOutput, Graphics::PrefixScanMode::Exclusive);
    EXPECT_EQ(small.Status, Graphics::ParallelPrimitiveStatus::OutputTooSmall);
    EXPECT_EQ(small.Diagnostics.ElementCount, input.size());
    EXPECT_EQ(small.Diagnostics.OutputCount, smallOutput.size());

    const std::array<std::uint32_t, 2> overflowing{{
        std::numeric_limits<std::uint32_t>::max(), 1u}};
    std::array<std::uint32_t, 2> output{};
    const auto overflow = Graphics::ComputePrefixScanCpu(
        overflowing, output, Graphics::PrefixScanMode::Inclusive);
    EXPECT_EQ(overflow.Status, Graphics::ParallelPrimitiveStatus::SumOverflow);
    EXPECT_EQ(overflow.Diagnostics.FirstFailureIndex, 1u);
}

TEST(ComputeParallelPrimitives, StreamCompactionKeepsStableOrder)
{
    const std::array<std::uint32_t, 6> keys{{10u, 11u, 12u, 13u, 14u, 15u}};
    const std::array<std::uint32_t, 6> flags{{0u, 1u, 2u, 0u, 1u, 0u}};
    std::array<std::uint32_t, 6> output{};

    const auto result = Graphics::CompactByFlagsCpu(keys, flags, output);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Diagnostics.ElementCount, keys.size());
    EXPECT_EQ(result.Diagnostics.OutputCount, 3u);
    EXPECT_EQ(result.Diagnostics.Total, 3u);
    EXPECT_EQ((std::array<std::uint32_t, 3>{{11u, 12u, 14u}}),
              (std::array<std::uint32_t, 3>{{output[0], output[1], output[2]}}));
}

TEST(ComputeParallelPrimitives, StreamCompactionHandlesAllKeptAndAllDropped)
{
    const std::array<std::uint32_t, 3> keys{{4u, 5u, 6u}};
    std::array<std::uint32_t, 3> output{};

    const std::array<std::uint32_t, 3> allKept{{1u, 1u, 1u}};
    const auto kept = Graphics::CompactByFlagsCpu(keys, allKept, output);
    EXPECT_TRUE(kept.Succeeded());
    EXPECT_EQ(kept.Diagnostics.OutputCount, 3u);
    EXPECT_EQ((std::array<std::uint32_t, 3>{{4u, 5u, 6u}}), output);

    output = {99u, 99u, 99u};
    const std::array<std::uint32_t, 3> allDropped{{0u, 0u, 0u}};
    const auto dropped = Graphics::CompactByFlagsCpu(keys, allDropped, output);
    EXPECT_TRUE(dropped.Succeeded());
    EXPECT_EQ(dropped.Diagnostics.OutputCount, 0u);
    EXPECT_EQ((std::array<std::uint32_t, 3>{{99u, 99u, 99u}}), output);
}

TEST(ComputeParallelPrimitives, StreamCompactionFailsClosedForBadShapes)
{
    const std::array<std::uint32_t, 3> keys{{1u, 2u, 3u}};
    const std::array<std::uint32_t, 2> mismatchedFlags{{1u, 0u}};
    std::array<std::uint32_t, 3> output{};

    const auto mismatch = Graphics::CompactByFlagsCpu(keys, mismatchedFlags, output);
    EXPECT_EQ(mismatch.Status, Graphics::ParallelPrimitiveStatus::SizeMismatch);

    const std::array<std::uint32_t, 3> flags{{1u, 1u, 0u}};
    std::array<std::uint32_t, 1> smallOutput{};
    const auto small = Graphics::CompactByFlagsCpu(keys, flags, smallOutput);
    EXPECT_EQ(small.Status, Graphics::ParallelPrimitiveStatus::OutputTooSmall);
    EXPECT_EQ(small.Diagnostics.OutputCount, 2u);
}

TEST(ComputeParallelPrimitives, GpuRecordReportsDeviceUnavailableForNullDevice)
{
    Tests::MockDevice device{};
    device.Operational = false;

    const auto scan = Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
        .Device = &device,
        .Input = ValidBuffer(1u),
        .Output = ValidBuffer(2u),
        .Scratch = ValidBuffer(3u),
        .ElementCount = 32u,
    });
    EXPECT_EQ(scan.Status, Graphics::ParallelPrimitiveStatus::DeviceUnavailable);
    EXPECT_FALSE(scan.Recorded);
    EXPECT_TRUE(scan.CpuFallbackRecommended);

    const auto compact =
        Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
            .Device = &device,
            .Keys = ValidBuffer(1u),
            .Flags = ValidBuffer(2u),
            .OutputKeys = ValidBuffer(3u),
            .OutputCount = ValidBuffer(4u),
            .Scratch = ValidBuffer(5u),
            .ElementCount = 32u,
        });
    EXPECT_EQ(compact.Status, Graphics::ParallelPrimitiveStatus::DeviceUnavailable);
    EXPECT_FALSE(compact.Recorded);
    EXPECT_TRUE(compact.CpuFallbackRecommended);
}

TEST(ComputeParallelPrimitives, GpuRecordValidatesResourcesBeforeFutureDispatch)
{
    Tests::MockDevice device{};
    device.Operational = true;

    const auto invalid = Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
        .Device = &device,
        .Input = {},
        .Output = ValidBuffer(2u),
        .Scratch = ValidBuffer(3u),
        .ElementCount = 4u,
    });
    EXPECT_EQ(invalid.Status, Graphics::ParallelPrimitiveStatus::InvalidGpuResource);
    EXPECT_FALSE(invalid.CpuFallbackRecommended);

    const auto invalidScratch =
        Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
            .Device = &device,
            .Keys = ValidBuffer(1u),
            .Flags = ValidBuffer(2u),
            .OutputKeys = ValidBuffer(3u),
            .OutputCount = ValidBuffer(4u),
            .Scratch = {},
            .ElementCount = 4u,
        });
    EXPECT_EQ(invalidScratch.Status,
              Graphics::ParallelPrimitiveStatus::InvalidGpuResource);
    EXPECT_FALSE(invalidScratch.CpuFallbackRecommended);

    const auto futureSlice =
        Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
            .Device = &device,
            .Keys = ValidBuffer(1u),
            .Flags = ValidBuffer(2u),
            .OutputKeys = ValidBuffer(3u),
            .OutputCount = ValidBuffer(4u),
            .Scratch = ValidBuffer(5u),
            .ElementCount = 4u,
        });
    EXPECT_EQ(futureSlice.Status,
              Graphics::ParallelPrimitiveStatus::UnsupportedInCurrentSlice);
    EXPECT_FALSE(futureSlice.Recorded);
    EXPECT_TRUE(futureSlice.CpuFallbackRecommended);
}

TEST(ComputeParallelPrimitives, StatusDebugNamesAreStable)
{
    EXPECT_STREQ(
        Graphics::DebugNameForParallelPrimitiveStatus(
            Graphics::ParallelPrimitiveStatus::DeviceUnavailable),
        "DeviceUnavailable");
    EXPECT_STREQ(
        Graphics::DebugNameForParallelPrimitiveStatus(
            Graphics::ParallelPrimitiveStatus::UnsupportedInCurrentSlice),
        "UnsupportedInCurrentSlice");
}
