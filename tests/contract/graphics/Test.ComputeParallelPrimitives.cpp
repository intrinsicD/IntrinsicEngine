#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "MockRHI.hpp"

import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace
{
    using namespace Extrinsic;

    [[nodiscard]] RHI::BufferHandle ValidBuffer(const std::uint32_t index) noexcept
    {
        return RHI::BufferHandle{index, 1u};
    }

    [[nodiscard]] RHI::PipelineHandle ValidPipeline(const std::uint32_t index) noexcept
    {
        return RHI::PipelineHandle{index, 1u};
    }

    [[nodiscard]] Graphics::ParallelPrimitivePipelineSet ValidPipelineSet() noexcept
    {
        return Graphics::ParallelPrimitivePipelineSet{
            .PrefixScan = ValidPipeline(10u),
            .AddBlockOffsets = ValidPipeline(11u),
            .CompactByFlags = ValidPipeline(12u),
            .SegmentedFloatReduce = ValidPipeline(14u),
        };
    }

    template <typename T>
    [[nodiscard]] T ReadPushPayload(const std::vector<std::byte>& payload)
    {
        T value{};
        EXPECT_EQ(payload.size(), sizeof(T));
        if (payload.size() == sizeof(T))
        {
            std::memcpy(&value, payload.data(), sizeof(T));
        }
        return value;
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

TEST(ComputeParallelPrimitives, SegmentedFloatReductionCpuComputesSumsCountsAndMeans)
{
    const std::array<std::uint32_t, 6> keys{{0u, 2u, 1u, 2u, 0u, 2u}};
    const std::array<float, 6> values{{1.0f, 3.0f, 5.0f, -1.0f, 2.0f, 4.0f}};
    std::array<float, 4> sums{};
    std::array<std::uint32_t, 4> counts{};
    std::array<float, 4> means{};

    const auto result = Graphics::ReduceFloatBySegmentCpu(
        keys, values, 4u, sums, counts, means);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Diagnostics.ElementCount, keys.size());
    EXPECT_EQ(result.Diagnostics.OutputCount, 4u);
    EXPECT_EQ(result.Diagnostics.Total, keys.size());
    EXPECT_FLOAT_EQ(sums[0], 3.0f);
    EXPECT_FLOAT_EQ(sums[1], 5.0f);
    EXPECT_FLOAT_EQ(sums[2], 6.0f);
    EXPECT_FLOAT_EQ(sums[3], 0.0f);
    EXPECT_EQ((std::array<std::uint32_t, 4>{{2u, 1u, 3u, 0u}}), counts);
    EXPECT_FLOAT_EQ(means[0], 1.5f);
    EXPECT_FLOAT_EQ(means[1], 5.0f);
    EXPECT_FLOAT_EQ(means[2], 2.0f);
    EXPECT_FLOAT_EQ(
        means[3],
        Graphics::kParallelSegmentedFloatReductionMeanForEmptySegment);
}

TEST(ComputeParallelPrimitives, SegmentedFloatReductionCpuHandlesEmptyInputs)
{
    std::array<float, 3> sums{{99.0f, 99.0f, 99.0f}};
    std::array<std::uint32_t, 3> counts{{9u, 9u, 9u}};
    std::array<float, 3> means{{99.0f, 99.0f, 99.0f}};

    const auto result = Graphics::ReduceFloatBySegmentCpu(
        std::span<const std::uint32_t>{},
        std::span<const float>{},
        3u,
        sums,
        counts,
        means);

    EXPECT_TRUE(result.Succeeded());
    EXPECT_EQ((std::array<float, 3>{{0.0f, 0.0f, 0.0f}}), sums);
    EXPECT_EQ((std::array<std::uint32_t, 3>{{0u, 0u, 0u}}), counts);
    EXPECT_EQ((std::array<float, 3>{{
                  Graphics::kParallelSegmentedFloatReductionMeanForEmptySegment,
                  Graphics::kParallelSegmentedFloatReductionMeanForEmptySegment,
                  Graphics::kParallelSegmentedFloatReductionMeanForEmptySegment}}),
              means);
}

TEST(ComputeParallelPrimitives, SegmentedFloatReductionCpuFailsClosedForBadShapes)
{
    const std::array<std::uint32_t, 3> keys{{0u, 1u, 2u}};
    const std::array<float, 2> shortValues{{1.0f, 2.0f}};
    std::array<float, 3> sums{};
    std::array<std::uint32_t, 3> counts{};
    std::array<float, 3> means{};

    const auto mismatch = Graphics::ReduceFloatBySegmentCpu(
        keys, shortValues, 3u, sums, counts, means);
    EXPECT_EQ(mismatch.Status, Graphics::ParallelPrimitiveStatus::SizeMismatch);

    std::array<float, 2> smallSums{};
    const std::array<float, 3> values{{1.0f, 2.0f, 3.0f}};
    const auto small = Graphics::ReduceFloatBySegmentCpu(
        keys, values, 3u, smallSums, counts, means);
    EXPECT_EQ(small.Status, Graphics::ParallelPrimitiveStatus::OutputTooSmall);
    EXPECT_EQ(small.Diagnostics.OutputCount, 2u);

    const auto zeroSegments = Graphics::ReduceFloatBySegmentCpu(
        keys, values, 0u, sums, counts, means);
    EXPECT_EQ(zeroSegments.Status, Graphics::ParallelPrimitiveStatus::InvalidInput);

    const std::array<std::uint32_t, 3> badKey{{0u, 3u, 1u}};
    const auto invalidKey = Graphics::ReduceFloatBySegmentCpu(
        badKey, values, 3u, sums, counts, means);
    EXPECT_EQ(invalidKey.Status, Graphics::ParallelPrimitiveStatus::InvalidInput);
    EXPECT_EQ(invalidKey.Diagnostics.FirstFailureIndex, 1u);
}

TEST(ComputeParallelPrimitives, DispatchPlansHandleEmptyAndInvalidInputs)
{
    const auto empty = Graphics::ComputePrefixScanDispatchPlan(
        0u, Graphics::PrefixScanMode::Exclusive);
    EXPECT_TRUE(empty.IsValid());
    EXPECT_TRUE(empty.Dispatches.empty());
    EXPECT_TRUE(empty.Barriers.empty());
    EXPECT_EQ(empty.ScratchBytes, 0u);

    const auto invalid = Graphics::ComputePrefixScanDispatchPlan(
        16u, Graphics::PrefixScanMode::Exclusive, 0u);
    EXPECT_EQ(invalid.Status, Graphics::ParallelPrimitiveStatus::InvalidInput);
    EXPECT_FALSE(invalid.IsValid());
}

TEST(ComputeParallelPrimitives, SingleBlockPrefixScanPlanNeedsNoScratch)
{
    const auto plan = Graphics::ComputePrefixScanDispatchPlan(
        128u, Graphics::PrefixScanMode::Inclusive);

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.Kind, Graphics::ParallelPrimitiveKind::PrefixScan);
    EXPECT_EQ(plan.GroupSize, Graphics::kParallelPrimitiveGroupSize);
    EXPECT_EQ(plan.ScratchBytes, 0u);
    EXPECT_TRUE(plan.ScratchLevels.empty());
    ASSERT_EQ(plan.Dispatches.size(), 1u);

    const auto& dispatch = plan.Dispatches[0];
    EXPECT_EQ(dispatch.Kind, Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(dispatch.Mode, Graphics::PrefixScanMode::Inclusive);
    EXPECT_EQ(dispatch.ElementCount, 128u);
    EXPECT_EQ(dispatch.GroupCountX, 1u);
    EXPECT_EQ(dispatch.InputRole, Graphics::ParallelPrimitiveBufferRole::Input);
    EXPECT_EQ(dispatch.OutputRole, Graphics::ParallelPrimitiveBufferRole::Output);
    EXPECT_EQ(dispatch.BlockSumsRole, Graphics::ParallelPrimitiveBufferRole::None);

    ASSERT_EQ(plan.Barriers.size(), 1u);
    EXPECT_EQ(plan.Barriers[0].AfterDispatchIndex, 0u);
    EXPECT_EQ(plan.Barriers[0].Buffer, Graphics::ParallelPrimitiveBufferRole::Output);
    EXPECT_EQ(plan.Barriers[0].Before, RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(plan.Barriers[0].After, RHI::MemoryAccess::ShaderRead);

    const RHI::BufferDesc scratch =
        Graphics::BuildParallelPrimitiveScratchBufferDesc(plan);
    EXPECT_EQ(scratch.SizeBytes, 0u);
    EXPECT_TRUE(RHI::HasUsage(scratch.Usage, RHI::BufferUsage::Storage));
}

TEST(ComputeParallelPrimitives, MultiBlockPrefixScanPlanPinsRecursiveScratch)
{
    const auto plan = Graphics::ComputePrefixScanDispatchPlan(
        1024u, Graphics::PrefixScanMode::Exclusive);

    ASSERT_TRUE(plan.IsValid());
    ASSERT_EQ(plan.ScratchLevels.size(), 1u);
    EXPECT_EQ(plan.ScratchLevels[0].LevelIndex, 0u);
    EXPECT_EQ(plan.ScratchLevels[0].ElementCount, 4u);
    EXPECT_EQ(plan.ScratchLevels[0].BlockCount, 1u);
    EXPECT_EQ(plan.ScratchLevels[0].OffsetBytes, 0u);
    EXPECT_EQ(plan.ScratchLevels[0].SizeBytes, 16u);
    EXPECT_EQ(plan.ScratchBytes, 16u);

    ASSERT_EQ(plan.Dispatches.size(), 3u);
    EXPECT_EQ(plan.Dispatches[0].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(plan.Dispatches[0].GroupCountX, 4u);
    EXPECT_EQ(plan.Dispatches[0].BlockSumsRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[0].BlockSumsOffsetBytes, 0u);

    EXPECT_EQ(plan.Dispatches[1].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(plan.Dispatches[1].LevelIndex, 1u);
    EXPECT_EQ(plan.Dispatches[1].InputRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[1].OutputRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[1].InputOffsetBytes, 0u);
    EXPECT_EQ(plan.Dispatches[1].OutputOffsetBytes, 0u);

    EXPECT_EQ(plan.Dispatches[2].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixAddBlockOffsets);
    EXPECT_EQ(plan.Dispatches[2].OutputRole,
              Graphics::ParallelPrimitiveBufferRole::Output);
    EXPECT_EQ(plan.Dispatches[2].OffsetsRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[2].OffsetsOffsetBytes, 0u);

    ASSERT_EQ(plan.Barriers.size(), 3u);
    EXPECT_EQ(plan.Barriers[0].AfterDispatchIndex, 0u);
    EXPECT_EQ(plan.Barriers[0].Buffer, Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Barriers[0].Before, RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(plan.Barriers[0].After,
              RHI::MemoryAccess::ShaderRead | RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(plan.Barriers[1].AfterDispatchIndex, 1u);
    EXPECT_EQ(plan.Barriers[1].Buffer, Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Barriers[2].AfterDispatchIndex, 2u);
    EXPECT_EQ(plan.Barriers[2].Buffer, Graphics::ParallelPrimitiveBufferRole::Output);
}

TEST(ComputeParallelPrimitives, LargePrefixScanPlanAddsOffsetsFromTopDown)
{
    const auto plan = Graphics::ComputePrefixScanDispatchPlan(
        100000u, Graphics::PrefixScanMode::Exclusive);

    ASSERT_TRUE(plan.IsValid());
    ASSERT_EQ(plan.ScratchLevels.size(), 2u);
    EXPECT_EQ(plan.ScratchLevels[0].ElementCount, 391u);
    EXPECT_EQ(plan.ScratchLevels[1].ElementCount, 2u);
    EXPECT_EQ(plan.ScratchLevels[1].OffsetBytes, 391u * sizeof(std::uint32_t));

    ASSERT_EQ(plan.Dispatches.size(), 5u);
    EXPECT_EQ(plan.Dispatches[0].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(plan.Dispatches[1].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(plan.Dispatches[1].BlockSumsRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[1].BlockSumsOffsetBytes,
              plan.ScratchLevels[1].OffsetBytes);
    EXPECT_EQ(plan.Dispatches[2].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(plan.Dispatches[2].BlockSumsRole,
              Graphics::ParallelPrimitiveBufferRole::None);
    EXPECT_EQ(plan.Dispatches[2].BlockSumsOffsetBytes,
              Graphics::kParallelPrimitiveInvalidOffset);
    EXPECT_EQ(plan.Dispatches[3].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixAddBlockOffsets);
    EXPECT_EQ(plan.Dispatches[3].OutputOffsetBytes, plan.ScratchLevels[0].OffsetBytes);
    EXPECT_EQ(plan.Dispatches[3].OffsetsOffsetBytes, plan.ScratchLevels[1].OffsetBytes);
    EXPECT_EQ(plan.Dispatches[4].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixAddBlockOffsets);
    EXPECT_EQ(plan.Dispatches[4].OutputRole,
              Graphics::ParallelPrimitiveBufferRole::Output);
    EXPECT_EQ(plan.Dispatches[4].OffsetsOffsetBytes, plan.ScratchLevels[0].OffsetBytes);
}

TEST(ComputeParallelPrimitives, StreamCompactionPlanPinsOffsetsAndScatter)
{
    const auto plan = Graphics::ComputeStreamCompactionDispatchPlan(1024u);

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.Kind, Graphics::ParallelPrimitiveKind::StreamCompaction);
    EXPECT_EQ(plan.PrefixOffsetsOffsetBytes, 0u);
    EXPECT_EQ(plan.PrefixOffsetsSizeBytes, 4096u);
    ASSERT_EQ(plan.ScratchLevels.size(), 1u);
    EXPECT_EQ(plan.ScratchLevels[0].OffsetBytes, 4096u);
    EXPECT_EQ(plan.ScratchBytes, 4112u);

    ASSERT_EQ(plan.Dispatches.size(), 4u);
    EXPECT_EQ(plan.Dispatches[0].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixBlockScan);
    EXPECT_EQ(plan.Dispatches[0].InputRole,
              Graphics::ParallelPrimitiveBufferRole::Flags);
    EXPECT_EQ(plan.Dispatches[0].OutputRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[0].OutputOffsetBytes, 0u);
    EXPECT_EQ(plan.Dispatches[0].BlockSumsOffsetBytes, 4096u);

    EXPECT_EQ(plan.Dispatches[2].Kind,
              Graphics::ParallelPrimitivePassKind::PrefixAddBlockOffsets);
    EXPECT_EQ(plan.Dispatches[2].OutputRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[2].OutputOffsetBytes, 0u);
    EXPECT_EQ(plan.Dispatches[2].OffsetsOffsetBytes, 4096u);

    EXPECT_EQ(plan.Dispatches[3].Kind,
              Graphics::ParallelPrimitivePassKind::StreamCompactScatter);
    EXPECT_EQ(plan.Dispatches[3].InputRole,
              Graphics::ParallelPrimitiveBufferRole::Keys);
    EXPECT_EQ(plan.Dispatches[3].OutputRole,
              Graphics::ParallelPrimitiveBufferRole::OutputKeys);
    EXPECT_EQ(plan.Dispatches[3].OffsetsRole,
              Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Dispatches[3].CountRole,
              Graphics::ParallelPrimitiveBufferRole::OutputCount);

    ASSERT_EQ(plan.Barriers.size(), 5u);
    EXPECT_EQ(plan.Barriers[0].Buffer, Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Barriers[1].Buffer, Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Barriers[2].Buffer, Graphics::ParallelPrimitiveBufferRole::Scratch);
    EXPECT_EQ(plan.Barriers[3].Buffer, Graphics::ParallelPrimitiveBufferRole::OutputKeys);
    EXPECT_EQ(plan.Barriers[4].Buffer, Graphics::ParallelPrimitiveBufferRole::OutputCount);
}

TEST(ComputeParallelPrimitives, SegmentedFloatReductionPlanPinsOneDispatchPerSegment)
{
    const auto plan = Graphics::ComputeSegmentedFloatReductionDispatchPlan(777u, 4u);

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.Kind, Graphics::ParallelPrimitiveKind::SegmentedFloatReduction);
    EXPECT_EQ(plan.ElementCount, 777u);
    EXPECT_EQ(plan.SegmentCount, 4u);
    EXPECT_EQ(plan.GroupSize, Graphics::kParallelPrimitiveGroupSize);
    EXPECT_EQ(plan.ScratchBytes, 0u);
    EXPECT_TRUE(plan.ScratchLevels.empty());

    ASSERT_EQ(plan.Dispatches.size(), 1u);
    const auto& dispatch = plan.Dispatches[0];
    EXPECT_EQ(dispatch.Kind,
              Graphics::ParallelPrimitivePassKind::SegmentedFloatReduce);
    EXPECT_EQ(dispatch.ElementCount, 777u);
    EXPECT_EQ(dispatch.SegmentCount, 4u);
    EXPECT_EQ(dispatch.GroupCountX, 4u);
    EXPECT_EQ(dispatch.GroupCountY, 1u);
    EXPECT_EQ(dispatch.GroupCountZ, 1u);
    EXPECT_EQ(dispatch.InputRole, Graphics::ParallelPrimitiveBufferRole::Keys);
    EXPECT_EQ(dispatch.ValuesRole, Graphics::ParallelPrimitiveBufferRole::Values);
    EXPECT_EQ(dispatch.SumsRole,
              Graphics::ParallelPrimitiveBufferRole::SegmentSums);
    EXPECT_EQ(dispatch.CountsRole,
              Graphics::ParallelPrimitiveBufferRole::SegmentCounts);
    EXPECT_EQ(dispatch.MeansRole,
              Graphics::ParallelPrimitiveBufferRole::SegmentMeans);

    ASSERT_EQ(plan.Barriers.size(), 3u);
    EXPECT_EQ(plan.Barriers[0].Buffer,
              Graphics::ParallelPrimitiveBufferRole::SegmentSums);
    EXPECT_EQ(plan.Barriers[1].Buffer,
              Graphics::ParallelPrimitiveBufferRole::SegmentCounts);
    EXPECT_EQ(plan.Barriers[2].Buffer,
              Graphics::ParallelPrimitiveBufferRole::SegmentMeans);

    const auto empty = Graphics::ComputeSegmentedFloatReductionDispatchPlan(0u, 3u);
    ASSERT_TRUE(empty.IsValid());
    ASSERT_EQ(empty.Dispatches.size(), 1u);
    EXPECT_EQ(empty.Dispatches[0].InputRole,
              Graphics::ParallelPrimitiveBufferRole::None);
    EXPECT_EQ(empty.Dispatches[0].ValuesRole,
              Graphics::ParallelPrimitiveBufferRole::None);
    EXPECT_EQ(empty.Dispatches[0].GroupCountX, 3u);

    const auto zeroSegments =
        Graphics::ComputeSegmentedFloatReductionDispatchPlan(16u, 0u);
    EXPECT_EQ(zeroSegments.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);

    const auto zeroGroup =
        Graphics::ComputeSegmentedFloatReductionDispatchPlan(16u, 2u, 0u);
    EXPECT_EQ(zeroGroup.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);
}

TEST(ComputeParallelPrimitives, BuildsComputePipelineDescriptors)
{
    const RHI::PipelineDesc prefix = Graphics::BuildParallelPrefixScanPipelineDesc();
    EXPECT_TRUE(prefix.ComputeShaderPath.ends_with(
        "shaders/parallel_prefix_scan.comp.spv"));
    EXPECT_EQ(prefix.PushConstantSize,
              static_cast<std::uint32_t>(
                  sizeof(Graphics::ParallelPrefixScanPushConstants)));

    const RHI::PipelineDesc add = Graphics::BuildParallelScanAddOffsetsPipelineDesc();
    EXPECT_TRUE(add.ComputeShaderPath.ends_with(
        "shaders/parallel_scan_add_offsets.comp.spv"));
    EXPECT_EQ(add.PushConstantSize,
              static_cast<std::uint32_t>(
                  sizeof(Graphics::ParallelScanAddOffsetsPushConstants)));

    const RHI::PipelineDesc compact =
        Graphics::BuildParallelCompactByFlagsPipelineDesc();
    EXPECT_TRUE(compact.ComputeShaderPath.ends_with(
        "shaders/parallel_compact_by_flags.comp.spv"));
    EXPECT_EQ(compact.PushConstantSize,
              static_cast<std::uint32_t>(
                  sizeof(Graphics::ParallelCompactByFlagsPushConstants)));

    const RHI::PipelineDesc countToDispatch =
        Graphics::BuildParallelCountToDispatchArgsPipelineDesc();
    EXPECT_TRUE(countToDispatch.ComputeShaderPath.ends_with(
        "shaders/parallel_count_to_dispatch_args.comp.spv"));
    EXPECT_EQ(countToDispatch.PushConstantSize,
              static_cast<std::uint32_t>(
                  sizeof(Graphics::ParallelCountToDispatchArgsPushConstants)));

    const RHI::PipelineDesc segmented =
        Graphics::BuildParallelSegmentedFloatReducePipelineDesc();
    EXPECT_TRUE(segmented.ComputeShaderPath.ends_with(
        "shaders/parallel_segmented_float_reduce.comp.spv"));
    EXPECT_EQ(segmented.PushConstantSize,
              static_cast<std::uint32_t>(
                  sizeof(Graphics::ParallelSegmentedFloatReducePushConstants)));
}

TEST(ComputeParallelPrimitives, BuildsCountPublicationBufferDescriptors)
{
    const RHI::BufferDesc readback =
        Graphics::BuildParallelCompactionCountReadbackBufferDesc();
    EXPECT_EQ(readback.SizeBytes, sizeof(std::uint32_t));
    EXPECT_TRUE(readback.HostVisible);
    EXPECT_TRUE(RHI::HasUsage(readback.Usage, RHI::BufferUsage::TransferDst));
    EXPECT_TRUE(RHI::HasUsage(readback.Usage, RHI::BufferUsage::TransferSrc));

    const RHI::BufferDesc dispatchArgs =
        Graphics::BuildParallelDispatchIndirectArgsBufferDesc();
    EXPECT_EQ(dispatchArgs.SizeBytes, sizeof(Graphics::ParallelDispatchIndirectArgs));
    EXPECT_FALSE(dispatchArgs.HostVisible);
    EXPECT_TRUE(RHI::HasUsage(dispatchArgs.Usage, RHI::BufferUsage::Storage));
    EXPECT_TRUE(RHI::HasUsage(dispatchArgs.Usage, RHI::BufferUsage::Indirect));
    EXPECT_TRUE(RHI::HasUsage(dispatchArgs.Usage, RHI::BufferUsage::TransferSrc));
    EXPECT_TRUE(RHI::HasUsage(dispatchArgs.Usage, RHI::BufferUsage::TransferDst));
}

TEST(ComputeParallelPrimitives, RecordsSingleBlockPrefixScanCommands)
{
    Tests::MockDevice device{};
    RHI::BufferManager buffers{device};
    const RHI::BufferHandle input = ValidBuffer(1u);
    const RHI::BufferHandle output = ValidBuffer(2u);

    const auto result = Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
        .Device = &device,
        .CommandContext = &device.CommandContext,
        .Buffers = &buffers,
        .Pipelines = ValidPipelineSet(),
        .Input = input,
        .Output = output,
        .ElementCount = 128u,
        .Mode = Graphics::PrefixScanMode::Inclusive,
    });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_FALSE(result.Scratch.IsValid());
    EXPECT_EQ(device.CreateBufferCount, 0);
    ASSERT_EQ(device.CommandContext.BoundPipelines.size(), 1u);
    EXPECT_EQ(device.CommandContext.BoundPipelines[0], ValidPipeline(10u));
    ASSERT_EQ(device.CommandContext.DispatchRecords.size(), 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, 1u);
    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 1u);

    const auto pc = ReadPushPayload<Graphics::ParallelPrefixScanPushConstants>(
        device.CommandContext.PushConstantPayloads[0]);
    EXPECT_EQ(pc.InputBDA, device.GetBufferDeviceAddress(input));
    EXPECT_EQ(pc.OutputBDA, device.GetBufferDeviceAddress(output));
    EXPECT_EQ(pc.BlockSumsBDA, 0u);
    EXPECT_EQ(pc.ElementCount, 128u);
    EXPECT_EQ(pc.Mode, 1u);

    ASSERT_EQ(device.CommandContext.BufferBarrierCalls.size(), 1u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Buffer, output);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Before,
              RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].After,
              RHI::MemoryAccess::ShaderRead);
}

TEST(ComputeParallelPrimitives, RecordsMultiBlockPrefixScanWithAllocatedScratch)
{
    Tests::MockDevice device{};
    RHI::BufferManager buffers{device};
    const RHI::BufferHandle input = ValidBuffer(1u);
    const RHI::BufferHandle output = ValidBuffer(2u);

    const auto result = Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
        .Device = &device,
        .CommandContext = &device.CommandContext,
        .Buffers = &buffers,
        .Pipelines = ValidPipelineSet(),
        .Input = input,
        .Output = output,
        .ElementCount = 1024u,
        .Mode = Graphics::PrefixScanMode::Exclusive,
    });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_TRUE(result.Scratch.IsValid());
    EXPECT_TRUE(result.ScratchLease.IsValid());
    EXPECT_EQ(device.CreateBufferCount, 1);
    ASSERT_EQ(result.Plan.Dispatches.size(), 3u);
    ASSERT_EQ(device.CommandContext.BoundPipelines.size(), 3u);
    EXPECT_EQ(device.CommandContext.BoundPipelines[0], ValidPipeline(10u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[1], ValidPipeline(10u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[2], ValidPipeline(11u));
    ASSERT_EQ(device.CommandContext.DispatchRecords.size(), 3u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, 4u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[1].X, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[2].X, 4u);

    const auto firstPc = ReadPushPayload<Graphics::ParallelPrefixScanPushConstants>(
        device.CommandContext.PushConstantPayloads[0]);
    EXPECT_EQ(firstPc.BlockSumsBDA, device.GetBufferDeviceAddress(result.Scratch));
    const auto addPc = ReadPushPayload<Graphics::ParallelScanAddOffsetsPushConstants>(
        device.CommandContext.PushConstantPayloads[2]);
    EXPECT_EQ(addPc.OutputBDA, device.GetBufferDeviceAddress(output));
    EXPECT_EQ(addPc.OffsetsBDA, device.GetBufferDeviceAddress(result.Scratch));

    ASSERT_EQ(device.CommandContext.BufferBarrierCalls.size(), 3u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Buffer, result.Scratch);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[2].Buffer, output);
}

TEST(ComputeParallelPrimitives, RecordsStreamCompactionWithScanAndScatter)
{
    Tests::MockDevice device{};
    RHI::BufferManager buffers{device};
    const RHI::BufferHandle keys = ValidBuffer(1u);
    const RHI::BufferHandle flags = ValidBuffer(2u);
    const RHI::BufferHandle outputKeys = ValidBuffer(3u);
    const RHI::BufferHandle outputCount = ValidBuffer(4u);

    const auto result =
        Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
            .Device = &device,
            .CommandContext = &device.CommandContext,
            .Buffers = &buffers,
            .Pipelines = ValidPipelineSet(),
            .Keys = keys,
            .Flags = flags,
            .OutputKeys = outputKeys,
            .OutputCount = outputCount,
            .ElementCount = 1024u,
        });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_TRUE(result.Scratch.IsValid());
    EXPECT_EQ(device.CreateBufferCount, 1);
    ASSERT_EQ(device.CommandContext.BoundPipelines.size(), 4u);
    EXPECT_EQ(device.CommandContext.BoundPipelines[0], ValidPipeline(10u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[1], ValidPipeline(10u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[2], ValidPipeline(11u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[3], ValidPipeline(12u));
    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 4u);

    const auto scanPc = ReadPushPayload<Graphics::ParallelPrefixScanPushConstants>(
        device.CommandContext.PushConstantPayloads[0]);
    EXPECT_EQ(scanPc.InputBDA, device.GetBufferDeviceAddress(flags));
    EXPECT_EQ(scanPc.OutputBDA, device.GetBufferDeviceAddress(result.Scratch));
    EXPECT_EQ(scanPc.BlockSumsBDA,
              device.GetBufferDeviceAddress(result.Scratch) + 4096u);
    EXPECT_EQ(scanPc.Mode, Graphics::kParallelPrefixScanModeNormalizeInputBit);

    const auto scatterPc =
        ReadPushPayload<Graphics::ParallelCompactByFlagsPushConstants>(
            device.CommandContext.PushConstantPayloads[3]);
    EXPECT_EQ(scatterPc.KeysBDA, device.GetBufferDeviceAddress(keys));
    EXPECT_EQ(scatterPc.FlagsBDA, device.GetBufferDeviceAddress(flags));
    EXPECT_EQ(scatterPc.OffsetsBDA, device.GetBufferDeviceAddress(result.Scratch));
    EXPECT_EQ(scatterPc.OutputKeysBDA, device.GetBufferDeviceAddress(outputKeys));
    EXPECT_EQ(scatterPc.OutputCountBDA, device.GetBufferDeviceAddress(outputCount));

    ASSERT_EQ(device.CommandContext.BufferBarrierCalls.size(), 5u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[3].Buffer, outputKeys);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[4].Buffer, outputCount);
}

TEST(ComputeParallelPrimitives, RecordsSegmentedFloatReductionCommands)
{
    Tests::MockDevice device{};
    RHI::BufferManager buffers{device};
    const RHI::BufferHandle keys = ValidBuffer(1u);
    const RHI::BufferHandle values = ValidBuffer(2u);
    const RHI::BufferHandle segmentSums = ValidBuffer(3u);
    const RHI::BufferHandle segmentCounts = ValidBuffer(4u);
    const RHI::BufferHandle segmentMeans = ValidBuffer(5u);

    const auto result = Graphics::RecordGpuSegmentedFloatReduction(
        Graphics::GpuSegmentedFloatReductionRecordDesc{
            .Device = &device,
            .CommandContext = &device.CommandContext,
            .Buffers = &buffers,
            .Pipelines = ValidPipelineSet(),
            .Keys = keys,
            .Values = values,
            .SegmentSums = segmentSums,
            .SegmentCounts = segmentCounts,
            .SegmentMeans = segmentMeans,
            .ElementCount = 777u,
            .SegmentCount = 4u,
        });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_FALSE(result.Scratch.IsValid());
    EXPECT_EQ(device.CreateBufferCount, 0);
    ASSERT_EQ(result.Plan.Dispatches.size(), 1u);
    EXPECT_EQ(result.Plan.Kind,
              Graphics::ParallelPrimitiveKind::SegmentedFloatReduction);
    EXPECT_EQ(result.Plan.SegmentCount, 4u);

    ASSERT_EQ(device.CommandContext.BoundPipelines.size(), 1u);
    EXPECT_EQ(device.CommandContext.BoundPipelines[0], ValidPipeline(14u));
    ASSERT_EQ(device.CommandContext.DispatchRecords.size(), 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, 4u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Y, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Z, 1u);

    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 1u);
    const auto pc =
        ReadPushPayload<Graphics::ParallelSegmentedFloatReducePushConstants>(
            device.CommandContext.PushConstantPayloads[0]);
    EXPECT_EQ(pc.KeysBDA, device.GetBufferDeviceAddress(keys));
    EXPECT_EQ(pc.ValuesBDA, device.GetBufferDeviceAddress(values));
    EXPECT_EQ(pc.SegmentSumsBDA, device.GetBufferDeviceAddress(segmentSums));
    EXPECT_EQ(pc.SegmentCountsBDA, device.GetBufferDeviceAddress(segmentCounts));
    EXPECT_EQ(pc.SegmentMeansBDA, device.GetBufferDeviceAddress(segmentMeans));
    EXPECT_EQ(pc.ElementCount, 777u);
    EXPECT_EQ(pc.SegmentCount, 4u);

    ASSERT_EQ(device.CommandContext.BufferBarrierCalls.size(), 3u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Buffer, segmentSums);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Before,
              RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].After,
              RHI::MemoryAccess::ShaderRead);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[1].Buffer, segmentCounts);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[2].Buffer, segmentMeans);
}

TEST(ComputeParallelPrimitives, PublishesCompactionCountForReadbackAndIndirectDispatch)
{
    Tests::MockDevice device{};
    const RHI::BufferHandle outputCount = ValidBuffer(4u);
    const RHI::BufferHandle readbackCount = ValidBuffer(5u);
    const RHI::BufferHandle dispatchArgs = ValidBuffer(6u);
    const RHI::PipelineHandle countPipeline = ValidPipeline(13u);

    const auto result =
        Graphics::RecordCompactionCountPublication(
            Graphics::GpuCompactionCountPublicationDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .CountToDispatchArgsPipeline = countPipeline,
                .OutputCount = outputCount,
                .ReadbackCount = readbackCount,
                .DispatchArgs = dispatchArgs,
                .OutputCountOffsetBytes = 16u,
                .ReadbackCountOffsetBytes = 4u,
                .DispatchArgsOffsetBytes = 12u,
                .DispatchGroupSize = 64u,
            });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.RecordedReadbackCopy);
    EXPECT_TRUE(result.RecordedDispatchArgs);
    EXPECT_FALSE(result.CpuFallbackRecommended);

    ASSERT_EQ(device.CommandContext.CopyBufferRecords.size(), 1u);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].Src, outputCount);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].Dst, readbackCount);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].SrcOffset, 16u);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].DstOffset, 4u);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].Size, sizeof(std::uint32_t));

    ASSERT_EQ(device.CommandContext.BoundPipelines.size(), 1u);
    EXPECT_EQ(device.CommandContext.BoundPipelines[0], countPipeline);
    ASSERT_EQ(device.CommandContext.DispatchRecords.size(), 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Y, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].Z, 1u);

    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 1u);
    const auto pc =
        ReadPushPayload<Graphics::ParallelCountToDispatchArgsPushConstants>(
            device.CommandContext.PushConstantPayloads[0]);
    EXPECT_EQ(pc.CountBDA, device.GetBufferDeviceAddress(outputCount) + 16u);
    EXPECT_EQ(pc.DispatchArgsBDA, device.GetBufferDeviceAddress(dispatchArgs) + 12u);
    EXPECT_EQ(pc.GroupSize, 64u);

    ASSERT_EQ(device.CommandContext.BufferBarrierCalls.size(), 4u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Buffer, outputCount);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Before,
              RHI::MemoryAccess::ShaderRead);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].After,
              RHI::MemoryAccess::TransferRead);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[1].Buffer, readbackCount);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[1].Before,
              RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[1].After,
              RHI::MemoryAccess::HostRead);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[2].Buffer, outputCount);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[2].Before,
              RHI::MemoryAccess::TransferRead);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[2].After,
              RHI::MemoryAccess::ShaderRead);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[3].Buffer, dispatchArgs);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[3].Before,
              RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[3].After,
              RHI::MemoryAccess::IndirectRead);
}

TEST(ComputeParallelPrimitives, GpuRecordReportsDeviceUnavailableForNullDevice)
{
    Tests::MockDevice device{};
    device.Operational = false;

    const auto scan = Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
        .Device = &device,
        .Input = ValidBuffer(1u),
        .Output = ValidBuffer(2u),
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
            .ElementCount = 32u,
        });
    EXPECT_EQ(compact.Status, Graphics::ParallelPrimitiveStatus::DeviceUnavailable);
    EXPECT_FALSE(compact.Recorded);
    EXPECT_TRUE(compact.CpuFallbackRecommended);

    const auto segmented = Graphics::RecordGpuSegmentedFloatReduction(
        Graphics::GpuSegmentedFloatReductionRecordDesc{
            .Device = &device,
            .Keys = ValidBuffer(1u),
            .Values = ValidBuffer(2u),
            .SegmentSums = ValidBuffer(3u),
            .SegmentCounts = ValidBuffer(4u),
            .SegmentMeans = ValidBuffer(5u),
            .ElementCount = 32u,
            .SegmentCount = 3u,
        });
    EXPECT_EQ(segmented.Status,
              Graphics::ParallelPrimitiveStatus::DeviceUnavailable);
    EXPECT_FALSE(segmented.Recorded);
    EXPECT_TRUE(segmented.CpuFallbackRecommended);

    const auto countPublication =
        Graphics::RecordCompactionCountPublication(
            Graphics::GpuCompactionCountPublicationDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .OutputCount = ValidBuffer(4u),
                .ReadbackCount = ValidBuffer(5u),
            });
    EXPECT_EQ(countPublication.Status,
              Graphics::ParallelPrimitiveStatus::DeviceUnavailable);
    EXPECT_TRUE(countPublication.CpuFallbackRecommended);
}

TEST(ComputeParallelPrimitives, GpuRecordValidatesRecorderInputsAndResources)
{
    Tests::MockDevice device{};
    device.Operational = true;
    RHI::BufferManager buffers{device};

    const auto missingContext = Graphics::RecordGpuPrefixScan(
        Graphics::GpuPrefixScanRecordDesc{
            .Device = &device,
            .Buffers = &buffers,
            .Pipelines = ValidPipelineSet(),
            .Input = ValidBuffer(1u),
            .Output = ValidBuffer(2u),
            .ElementCount = 4u,
        });
    EXPECT_EQ(missingContext.Status, Graphics::ParallelPrimitiveStatus::InvalidInput);

    const auto invalid = Graphics::RecordGpuPrefixScan(Graphics::GpuPrefixScanRecordDesc{
        .Device = &device,
        .CommandContext = &device.CommandContext,
        .Buffers = &buffers,
        .Pipelines = ValidPipelineSet(),
        .Input = {},
        .Output = ValidBuffer(2u),
        .ElementCount = 4u,
    });
    EXPECT_EQ(invalid.Status, Graphics::ParallelPrimitiveStatus::InvalidGpuResource);
    EXPECT_FALSE(invalid.CpuFallbackRecommended);

    const auto invalidScratch =
        Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
            .Device = &device,
            .CommandContext = &device.CommandContext,
            .Pipelines = ValidPipelineSet(),
            .Keys = ValidBuffer(1u),
            .Flags = ValidBuffer(2u),
            .OutputKeys = ValidBuffer(3u),
            .OutputCount = ValidBuffer(4u),
            .ElementCount = 4u,
        });
    EXPECT_EQ(invalidScratch.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);
    EXPECT_FALSE(invalidScratch.CpuFallbackRecommended);

    const auto missingPipeline =
        Graphics::RecordGpuStreamCompaction(Graphics::GpuStreamCompactionRecordDesc{
            .Device = &device,
            .CommandContext = &device.CommandContext,
            .Buffers = &buffers,
            .Pipelines = Graphics::ParallelPrimitivePipelineSet{
                .PrefixScan = ValidPipeline(10u),
                .AddBlockOffsets = ValidPipeline(11u),
            },
            .Keys = ValidBuffer(1u),
            .Flags = ValidBuffer(2u),
            .OutputKeys = ValidBuffer(3u),
            .OutputCount = ValidBuffer(4u),
            .ElementCount = 4u,
        });
    EXPECT_EQ(missingPipeline.Status,
              Graphics::ParallelPrimitiveStatus::InvalidGpuResource);
    EXPECT_FALSE(missingPipeline.Recorded);
    EXPECT_FALSE(missingPipeline.CpuFallbackRecommended);

    const auto segmentedMissingContext =
        Graphics::RecordGpuSegmentedFloatReduction(
            Graphics::GpuSegmentedFloatReductionRecordDesc{
                .Device = &device,
                .Buffers = &buffers,
                .Pipelines = ValidPipelineSet(),
                .Keys = ValidBuffer(1u),
                .Values = ValidBuffer(2u),
                .SegmentSums = ValidBuffer(3u),
                .SegmentCounts = ValidBuffer(4u),
                .SegmentMeans = ValidBuffer(5u),
                .ElementCount = 4u,
                .SegmentCount = 2u,
            });
    EXPECT_EQ(segmentedMissingContext.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);

    const auto segmentedInvalidOutput =
        Graphics::RecordGpuSegmentedFloatReduction(
            Graphics::GpuSegmentedFloatReductionRecordDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Buffers = &buffers,
                .Pipelines = ValidPipelineSet(),
                .Keys = ValidBuffer(1u),
                .Values = ValidBuffer(2u),
                .SegmentSums = ValidBuffer(3u),
                .SegmentCounts = {},
                .SegmentMeans = ValidBuffer(5u),
                .ElementCount = 4u,
                .SegmentCount = 2u,
            });
    EXPECT_EQ(segmentedInvalidOutput.Status,
              Graphics::ParallelPrimitiveStatus::InvalidGpuResource);

    const auto segmentedInvalidInput =
        Graphics::RecordGpuSegmentedFloatReduction(
            Graphics::GpuSegmentedFloatReductionRecordDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Buffers = &buffers,
                .Pipelines = ValidPipelineSet(),
                .Keys = {},
                .Values = ValidBuffer(2u),
                .SegmentSums = ValidBuffer(3u),
                .SegmentCounts = ValidBuffer(4u),
                .SegmentMeans = ValidBuffer(5u),
                .ElementCount = 4u,
                .SegmentCount = 2u,
            });
    EXPECT_EQ(segmentedInvalidInput.Status,
              Graphics::ParallelPrimitiveStatus::InvalidGpuResource);

    const auto segmentedMissingPipeline =
        Graphics::RecordGpuSegmentedFloatReduction(
            Graphics::GpuSegmentedFloatReductionRecordDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Buffers = &buffers,
                .Pipelines = Graphics::ParallelPrimitivePipelineSet{
                    .PrefixScan = ValidPipeline(10u),
                    .AddBlockOffsets = ValidPipeline(11u),
                    .CompactByFlags = ValidPipeline(12u),
                },
                .Keys = ValidBuffer(1u),
                .Values = ValidBuffer(2u),
                .SegmentSums = ValidBuffer(3u),
                .SegmentCounts = ValidBuffer(4u),
                .SegmentMeans = ValidBuffer(5u),
                .ElementCount = 4u,
                .SegmentCount = 2u,
            });
    EXPECT_EQ(segmentedMissingPipeline.Status,
              Graphics::ParallelPrimitiveStatus::InvalidGpuResource);
    EXPECT_FALSE(segmentedMissingPipeline.Recorded);
    EXPECT_FALSE(segmentedMissingPipeline.CpuFallbackRecommended);

    const auto missingCountTarget =
        Graphics::RecordCompactionCountPublication(
            Graphics::GpuCompactionCountPublicationDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .OutputCount = ValidBuffer(4u),
            });
    EXPECT_EQ(missingCountTarget.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);

    const auto missingCountPipeline =
        Graphics::RecordCompactionCountPublication(
            Graphics::GpuCompactionCountPublicationDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .OutputCount = ValidBuffer(4u),
                .ReadbackCount = ValidBuffer(5u),
                .DispatchArgs = ValidBuffer(6u),
                .DispatchGroupSize = 64u,
            });
    EXPECT_EQ(missingCountPipeline.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);
    EXPECT_FALSE(missingCountPipeline.RecordedReadbackCopy)
        << "Validation must reject bad indirect publication before recording copy commands.";

    const auto zeroDispatchGroupSize =
        Graphics::RecordCompactionCountPublication(
            Graphics::GpuCompactionCountPublicationDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .CountToDispatchArgsPipeline = ValidPipeline(13u),
                .OutputCount = ValidBuffer(4u),
                .DispatchArgs = ValidBuffer(6u),
                .DispatchGroupSize = 0u,
            });
    EXPECT_EQ(zeroDispatchGroupSize.Status,
              Graphics::ParallelPrimitiveStatus::InvalidInput);
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
    EXPECT_STREQ(
        Graphics::DebugNameForParallelPrimitivePassKind(
            Graphics::ParallelPrimitivePassKind::StreamCompactScatter),
        "StreamCompactScatter");
    EXPECT_STREQ(
        Graphics::DebugNameForParallelPrimitivePassKind(
            Graphics::ParallelPrimitivePassKind::SegmentedFloatReduce),
        "SegmentedFloatReduce");
    EXPECT_STREQ(
        Graphics::DebugNameForParallelPrimitiveBufferRole(
            Graphics::ParallelPrimitiveBufferRole::OutputCount),
        "OutputCount");
    EXPECT_STREQ(
        Graphics::DebugNameForParallelPrimitiveBufferRole(
            Graphics::ParallelPrimitiveBufferRole::SegmentMeans),
        "SegmentMeans");
}
