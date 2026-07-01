#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;

#include "MockRHI.hpp"

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Tests = Extrinsic::Tests;

    [[nodiscard]] RHI::BufferHandle ValidBuffer(
        const std::uint32_t index) noexcept
    {
        return RHI::BufferHandle{index, 1u};
    }

    [[nodiscard]] RHI::PipelineHandle ValidPipeline(
        const std::uint32_t index) noexcept
    {
        return RHI::PipelineHandle{index, 1u};
    }

    [[nodiscard]] Graphics::ParallelPrimitivePipelineSet
    ValidCompactionPipelines() noexcept
    {
        return Graphics::ParallelPrimitivePipelineSet{
            .PrefixScan = ValidPipeline(60u),
            .AddBlockOffsets = ValidPipeline(61u),
            .CompactByFlags = ValidPipeline(62u),
        };
    }

    [[nodiscard]] Runtime::ProgressivePoissonGpuPipelineSet
    ValidProgressivePoissonPipelines() noexcept
    {
        return Runtime::ProgressivePoissonGpuPipelineSet{
            .BuildCells = ValidPipeline(50u),
            .AcceptPhase = ValidPipeline(51u),
            .Compaction = ValidCompactionPipelines(),
        };
    }

    [[nodiscard]] Runtime::ProgressivePoissonGpuResourceSet
    ValidProgressivePoissonResources() noexcept
    {
        return Runtime::ProgressivePoissonGpuResourceSet{
            .State = ValidBuffer(1u),
            .PositionX = ValidBuffer(2u),
            .PositionY = ValidBuffer(3u),
            .PositionZ = ValidBuffer(4u),
            .RemainingKeys = ValidBuffer(5u),
            .NextRemainingKeys = ValidBuffer(6u),
            .AcceptedKeys = ValidBuffer(7u),
            .CellKeys = ValidBuffer(8u),
            .CellPhases = ValidBuffer(9u),
            .AcceptFlags = ValidBuffer(10u),
            .CarryFlags = ValidBuffer(11u),
            .HashKeys = ValidBuffer(12u),
            .HashValues = ValidBuffer(13u),
            .LevelOffsets = ValidBuffer(14u),
            .SplatRadii = ValidBuffer(15u),
            .OutputCount = ValidBuffer(16u),
            .CompactionScratch = ValidBuffer(17u),
        };
    }

    template <typename T>
    [[nodiscard]] T ReadPayload(const std::vector<std::byte>& payload)
    {
        T value{};
        EXPECT_EQ(payload.size(), sizeof(T));
        if (payload.size() == sizeof(T))
        {
            std::memcpy(&value, payload.data(), sizeof(T));
        }
        return value;
    }

    template <typename T>
    [[nodiscard]] std::vector<T> ReadVectorPayload(
        const std::vector<std::byte>& payload)
    {
        std::vector<T> values(payload.size() / sizeof(T));
        EXPECT_EQ(payload.size(), values.size() * sizeof(T));
        if (!values.empty())
        {
            std::memcpy(values.data(), payload.data(), payload.size());
        }
        return values;
    }

    [[nodiscard]] const Tests::MockDevice::BufferWriteRecord* FindWrite(
        const Tests::MockDevice& device,
        const RHI::BufferHandle handle)
    {
        const auto it = std::find_if(
            device.BufferWrites.begin(),
            device.BufferWrites.end(),
            [handle](const Tests::MockDevice::BufferWriteRecord& write)
            {
                return write.Handle == handle;
            });
        return it == device.BufferWrites.end() ? nullptr : &*it;
    }

    [[nodiscard]] const Runtime::ProgressivePoissonGpuBufferSpan* FindSpan(
        const Runtime::ProgressivePoissonGpuBufferLayout& layout,
        const Runtime::ProgressivePoissonGpuBufferRole role) noexcept
    {
        const auto it = std::find_if(
            layout.Spans.begin(),
            layout.Spans.end(),
            [role](const Runtime::ProgressivePoissonGpuBufferSpan& span)
            {
                return span.Role == role;
            });
        return it == layout.Spans.end() ? nullptr : &*it;
    }
}

TEST(ProgressivePoissonGpuBackend, PlansPerLevelBuildAcceptAndCompaction)
{
    Runtime::ProgressivePoissonGpuPlanDesc desc{};
    desc.InputCount = 100u;
    desc.GroupSize = 64u;
    desc.Config.Dimension = 2u;
    desc.Config.GridWidth = 8u;
    desc.Config.MaxLevels = 3u;
    desc.Config.HashLoadFactor = 0.5f;

    const Runtime::ProgressivePoissonGpuDispatchPlan plan =
        Runtime::ComputeProgressivePoissonGpuDispatchPlan(desc);

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.InputCount, 100u);
    EXPECT_EQ(plan.Dimension, 2u);
    EXPECT_EQ(plan.PhaseCount, 4u);
    ASSERT_EQ(plan.Levels.size(), 3u);
    EXPECT_EQ(plan.Dispatches.size(), 3u * (1u + 4u + 2u));

    const Runtime::ProgressivePoissonGpuLevelPlan& level0 = plan.Levels.front();
    EXPECT_EQ(level0.LevelIndex, 0u);
    EXPECT_EQ(level0.PhaseCount, 4u);
    EXPECT_EQ(level0.HashTableCapacity, 200u);
    EXPECT_EQ(level0.BuildCells.Kind,
              Runtime::ProgressivePoissonGpuPassKind::BuildLevelCells);
    EXPECT_EQ(level0.BuildCells.GroupCountX, 4u);
    ASSERT_EQ(level0.AcceptPhases.size(), 4u);
    EXPECT_EQ(level0.AcceptPhases[3].Kind,
              Runtime::ProgressivePoissonGpuPassKind::AcceptPhase);
    EXPECT_EQ(level0.AcceptPhases[3].PhaseIndex, 3u);

    EXPECT_EQ(level0.AcceptedCompaction.Kind,
              Graphics::ParallelPrimitiveKind::StreamCompaction);
    EXPECT_TRUE(level0.AcceptedCompaction.IsValid());
    EXPECT_EQ(level0.AcceptedCompaction.ElementCount, 100u);
    EXPECT_EQ(level0.RemainingCompaction.Kind,
              Graphics::ParallelPrimitiveKind::StreamCompaction);
    EXPECT_TRUE(level0.RemainingCompaction.ScratchBytes > 0u);

    EXPECT_EQ(plan.Layout.HashTableCapacity, 200u);
    EXPECT_EQ(plan.Layout.StateBytes,
              sizeof(Runtime::ProgressivePoissonGpuStateBufferRecord));
    EXPECT_EQ(plan.Layout.CompactionScratchBytes,
              level0.AcceptedCompaction.ScratchBytes);
    ASSERT_NE(FindSpan(plan.Layout,
                       Runtime::ProgressivePoissonGpuBufferRole::HashKeys),
              nullptr);
    ASSERT_NE(FindSpan(plan.Layout,
                       Runtime::ProgressivePoissonGpuBufferRole::CarryFlags),
              nullptr);
    EXPECT_EQ(FindSpan(plan.Layout,
                       Runtime::ProgressivePoissonGpuBufferRole::CarryFlags)
                  ->SizeBytes,
              100u * sizeof(std::uint32_t));
    EXPECT_EQ(FindSpan(plan.Layout,
                       Runtime::ProgressivePoissonGpuBufferRole::HashKeys)
                  ->SizeBytes,
              200u * sizeof(std::uint64_t));
    ASSERT_NE(FindSpan(plan.Layout,
                       Runtime::ProgressivePoissonGpuBufferRole::LevelOffsets),
              nullptr);
    EXPECT_EQ(FindSpan(plan.Layout,
                       Runtime::ProgressivePoissonGpuBufferRole::LevelOffsets)
                  ->SizeBytes,
              4u * sizeof(std::uint32_t));
}

TEST(ProgressivePoissonGpuBackend, PlansThreeDimensionalEightPhaseDispatch)
{
    Runtime::ProgressivePoissonGpuPlanDesc desc{};
    desc.InputCount = 17u;
    desc.Config.Dimension = 3u;
    desc.Config.GridWidth = 4u;
    desc.Config.MaxLevels = 2u;
    desc.Config.HashLoadFactor = 0.25f;

    const Runtime::ProgressivePoissonGpuDispatchPlan plan =
        Runtime::ComputeProgressivePoissonGpuDispatchPlan(desc);

    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.PhaseCount, 8u);
    ASSERT_EQ(plan.Levels.size(), 2u);
    EXPECT_EQ(plan.Levels[1].AcceptPhases.size(), 8u);
    EXPECT_EQ(plan.Layout.HashTableCapacity, 68u);
}

TEST(ProgressivePoissonGpuBackend, InvalidPlanInputsFailClosed)
{
    Runtime::ProgressivePoissonGpuPlanDesc badDimension{};
    badDimension.InputCount = 8u;
    badDimension.Config.Dimension = 4u;
    EXPECT_EQ(Runtime::ComputeProgressivePoissonGpuDispatchPlan(badDimension).Status,
              Runtime::ProgressivePoissonGpuStatus::InvalidInput);

    Runtime::ProgressivePoissonGpuPlanDesc badLoad{};
    badLoad.InputCount = 8u;
    badLoad.Config.HashLoadFactor = 0.0f;
    EXPECT_EQ(Runtime::ComputeProgressivePoissonGpuDispatchPlan(badLoad).Status,
              Runtime::ProgressivePoissonGpuStatus::InvalidInput);

    Runtime::ProgressivePoissonGpuPlanDesc zeroGroup{};
    zeroGroup.InputCount = 8u;
    zeroGroup.GroupSize = 0u;
    EXPECT_EQ(Runtime::ComputeProgressivePoissonGpuDispatchPlan(zeroGroup).Status,
              Runtime::ProgressivePoissonGpuStatus::InvalidInput);
}

TEST(ProgressivePoissonGpuBackend, BuildsBufferAndPipelineDescriptors)
{
    Runtime::ProgressivePoissonGpuPlanDesc desc{};
    desc.InputCount = 12u;
    const Runtime::ProgressivePoissonGpuDispatchPlan plan =
        Runtime::ComputeProgressivePoissonGpuDispatchPlan(desc);
    ASSERT_TRUE(plan.IsValid());

    const RHI::BufferDesc state =
        Runtime::BuildProgressivePoissonGpuStateBufferDesc();
    EXPECT_EQ(state.SizeBytes,
              sizeof(Runtime::ProgressivePoissonGpuStateBufferRecord));
    EXPECT_TRUE(RHI::HasUsage(state.Usage, RHI::BufferUsage::Storage));
    EXPECT_TRUE(RHI::HasUsage(state.Usage, RHI::BufferUsage::TransferDst));

    const RHI::BufferDesc work =
        Runtime::BuildProgressivePoissonGpuWorkBufferDesc(plan.Layout);
    EXPECT_EQ(work.SizeBytes, plan.Layout.WorkBufferBytes);
    EXPECT_TRUE(RHI::HasUsage(work.Usage, RHI::BufferUsage::Storage));
    EXPECT_TRUE(RHI::HasUsage(work.Usage, RHI::BufferUsage::TransferSrc));

    const RHI::BufferDesc readback =
        Runtime::BuildProgressivePoissonGpuReadbackBufferDesc(
            64u,
            "ProgressivePoissonGpu.Test.Readback");
    EXPECT_EQ(readback.SizeBytes, 64u);
    EXPECT_TRUE(readback.HostVisible);
    EXPECT_TRUE(RHI::HasUsage(readback.Usage, RHI::BufferUsage::TransferDst));
    EXPECT_TRUE(RHI::HasUsage(readback.Usage, RHI::BufferUsage::TransferSrc));

    const RHI::PipelineDesc build =
        Runtime::BuildProgressivePoissonBuildCellsPipelineDesc();
    EXPECT_TRUE(build.ComputeShaderPath.ends_with(
        "shaders/progressive_poisson_build_cells.comp.spv"));
    EXPECT_EQ(build.PushConstantSize,
              sizeof(Runtime::ProgressivePoissonGpuPassPushConstants));

    const RHI::PipelineDesc accept =
        Runtime::BuildProgressivePoissonAcceptPhasePipelineDesc();
    EXPECT_TRUE(accept.ComputeShaderPath.ends_with(
        "shaders/progressive_poisson_accept_phase.comp.spv"));
    EXPECT_EQ(accept.PushConstantSize,
              sizeof(Runtime::ProgressivePoissonGpuPassPushConstants));
}

TEST(ProgressivePoissonGpuBackend,
     BuildsStateRecordFromRoleBuffers)
{
    Tests::MockDevice device{};
    const Runtime::ProgressivePoissonGpuResourceSet resources =
        ValidProgressivePoissonResources();

    const Runtime::ProgressivePoissonGpuStateBufferRecord record =
        Runtime::BuildProgressivePoissonGpuStateRecord(device, resources);

    EXPECT_EQ(record.PositionXBDA,
              device.GetBufferDeviceAddress(resources.PositionX));
    EXPECT_EQ(record.PositionYBDA,
              device.GetBufferDeviceAddress(resources.PositionY));
    EXPECT_EQ(record.PositionZBDA,
              device.GetBufferDeviceAddress(resources.PositionZ));
    EXPECT_EQ(record.AcceptFlagsBDA,
              device.GetBufferDeviceAddress(resources.AcceptFlags));
    EXPECT_EQ(record.CarryFlagsBDA,
              device.GetBufferDeviceAddress(resources.CarryFlags));
    EXPECT_EQ(record.CompactionScratchBDA,
              device.GetBufferDeviceAddress(resources.CompactionScratch));
}

TEST(ProgressivePoissonGpuBackend,
     RecordsMethodDispatchesAndDelegatesCompaction)
{
    Tests::MockDevice device{};
    RHI::BufferManager buffers{device};

    Runtime::ProgressivePoissonGpuPlanDesc planDesc{};
    planDesc.InputCount = 128u;
    planDesc.Config.Dimension = 2u;
    planDesc.Config.GridWidth = 8u;
    planDesc.Config.MaxLevels = 1u;
    planDesc.Config.HashLoadFactor = 0.5f;
    planDesc.Config.RadiusAlpha = 0.5f;
    planDesc.Config.RandomizeGridOrigin = false;

    const Runtime::ProgressivePoissonGpuResourceSet resources =
        ValidProgressivePoissonResources();
    const Runtime::ProgressivePoissonGpuRecordResult result =
        Runtime::RecordProgressivePoissonGpuPasses(
            Runtime::ProgressivePoissonGpuRecordDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Buffers = &buffers,
                .Pipelines = ValidProgressivePoissonPipelines(),
                .Resources = resources,
                .Plan = planDesc,
            });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_FALSE(result.CpuFallbackRecommended);
    EXPECT_TRUE(result.StateRecordUploaded);
    EXPECT_EQ(result.MethodDispatchCount, 5u);
    EXPECT_EQ(result.AcceptedCompactionCount, 1u);
    EXPECT_EQ(result.RemainingCompactionCount, 1u);
    EXPECT_EQ(device.CreateBufferCount, 0);
    ASSERT_EQ(device.BufferWrites.size(), 1u);
    EXPECT_EQ(device.BufferWrites[0].Handle, resources.State);
    EXPECT_EQ(device.BufferWrites[0].Offset, 0u);
    EXPECT_EQ(device.BufferWrites[0].Data.size(),
              sizeof(Runtime::ProgressivePoissonGpuStateBufferRecord));

    ASSERT_EQ(device.CommandContext.BoundPipelines.size(), 9u);
    EXPECT_EQ(device.CommandContext.BoundPipelines[0], ValidPipeline(50u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[1], ValidPipeline(51u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[4], ValidPipeline(51u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[5], ValidPipeline(60u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[6], ValidPipeline(62u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[7], ValidPipeline(60u));
    EXPECT_EQ(device.CommandContext.BoundPipelines[8], ValidPipeline(62u));
    ASSERT_EQ(device.CommandContext.DispatchRecords.size(), 9u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[0].X, 1u);
    EXPECT_EQ(device.CommandContext.DispatchRecords[1].X, 1u);

    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 9u);
    const auto buildPc =
        ReadPayload<Runtime::ProgressivePoissonGpuPassPushConstants>(
            device.CommandContext.PushConstantPayloads[0]);
    EXPECT_EQ(buildPc.StateBDA,
              device.GetBufferDeviceAddress(resources.State));
    EXPECT_EQ(buildPc.InputCount, 128u);
    EXPECT_EQ(buildPc.RemainingCount, 128u);
    EXPECT_EQ(buildPc.HashTableCapacity, 256u);
    EXPECT_EQ(buildPc.Dimension, 2u);
    EXPECT_EQ(buildPc.GridWidth, 8u);
    EXPECT_EQ(buildPc.PhaseCount, 4u);
    EXPECT_FLOAT_EQ(buildPc.InvCellSize, 8.0f);
    EXPECT_FLOAT_EQ(buildPc.RadiusSquared, 0.00390625f);

    const auto lastAcceptPc =
        ReadPayload<Runtime::ProgressivePoissonGpuPassPushConstants>(
            device.CommandContext.PushConstantPayloads[4]);
    EXPECT_EQ(lastAcceptPc.PhaseIndex, 3u);

    const auto acceptedScatter =
        ReadPayload<Graphics::ParallelCompactByFlagsPushConstants>(
            device.CommandContext.PushConstantPayloads[6]);
    EXPECT_EQ(acceptedScatter.KeysBDA,
              device.GetBufferDeviceAddress(resources.RemainingKeys));
    EXPECT_EQ(acceptedScatter.FlagsBDA,
              device.GetBufferDeviceAddress(resources.AcceptFlags));
    EXPECT_EQ(acceptedScatter.OutputKeysBDA,
              device.GetBufferDeviceAddress(resources.AcceptedKeys));
    EXPECT_EQ(acceptedScatter.OutputCountBDA,
              device.GetBufferDeviceAddress(resources.OutputCount));

    const auto remainingScatter =
        ReadPayload<Graphics::ParallelCompactByFlagsPushConstants>(
            device.CommandContext.PushConstantPayloads[8]);
    EXPECT_EQ(remainingScatter.KeysBDA,
              device.GetBufferDeviceAddress(resources.RemainingKeys));
    EXPECT_EQ(remainingScatter.FlagsBDA,
              device.GetBufferDeviceAddress(resources.CarryFlags));
    EXPECT_EQ(remainingScatter.OutputKeysBDA,
              device.GetBufferDeviceAddress(resources.NextRemainingKeys));
    EXPECT_EQ(remainingScatter.OutputCountBDA,
              device.GetBufferDeviceAddress(resources.OutputCount));

    ASSERT_GE(device.CommandContext.BufferBarrierCalls.size(), 12u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[0].Buffer,
              resources.CellKeys);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[4].Buffer,
              resources.AcceptFlags);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls[5].Buffer,
              resources.CarryFlags);
}

TEST(ProgressivePoissonGpuBackend,
     ExecutionAllocatesUploadsRecordsAndCopiesReadbacks)
{
    Tests::MockDevice device{};
    RHI::BufferManager buffers{device};

    Runtime::ProgressivePoissonGpuPlanDesc planDesc{};
    planDesc.InputCount = 3u;
    planDesc.GroupSize = 64u;
    planDesc.Config.Dimension = 3u;
    planDesc.Config.GridWidth = 4u;
    planDesc.Config.MaxLevels = 1u;
    planDesc.Config.HashLoadFactor = 0.5f;
    planDesc.Config.RandomizeGridOrigin = false;

    const std::array<glm::vec3, 3> points{{
        glm::vec3{1.0f, 2.0f, 3.0f},
        glm::vec3{4.0f, 5.0f, 6.0f},
        glm::vec3{7.0f, 8.0f, 9.0f},
    }};

    const Runtime::ProgressivePoissonGpuExecutionResult result =
        Runtime::RecordProgressivePoissonGpuExecution(
            Runtime::ProgressivePoissonGpuExecutionDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Buffers = &buffers,
                .Pipelines = ValidProgressivePoissonPipelines(),
                .Positions = points,
                .Plan = planDesc,
            });

    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Recorded);
    EXPECT_TRUE(result.CpuFallbackRecommended);
    EXPECT_TRUE(result.UploadedInputs);
    EXPECT_TRUE(result.ReadbackCopiesRecorded);
    EXPECT_TRUE(result.Record.Recorded);
    EXPECT_TRUE(result.Resources.HasReadbackTargets());
    EXPECT_EQ(result.UploadWriteCount, 5u);
    EXPECT_EQ(result.ReadbackCopyCount, 3u);
    EXPECT_EQ(result.Resources.Leases.size(), 20u);
    EXPECT_EQ(device.CreateBufferCount, 20);
    EXPECT_EQ(device.DestroyBufferCount, 0);

    const Tests::MockDevice::BufferWriteRecord* xWrite =
        FindWrite(device, result.Resources.Resources.PositionX);
    ASSERT_NE(xWrite, nullptr);
    EXPECT_EQ((std::vector<float>{1.0f, 4.0f, 7.0f}),
              ReadVectorPayload<float>(xWrite->Data));

    const Tests::MockDevice::BufferWriteRecord* yWrite =
        FindWrite(device, result.Resources.Resources.PositionY);
    ASSERT_NE(yWrite, nullptr);
    EXPECT_EQ((std::vector<float>{2.0f, 5.0f, 8.0f}),
              ReadVectorPayload<float>(yWrite->Data));

    const Tests::MockDevice::BufferWriteRecord* zWrite =
        FindWrite(device, result.Resources.Resources.PositionZ);
    ASSERT_NE(zWrite, nullptr);
    EXPECT_EQ((std::vector<float>{3.0f, 6.0f, 9.0f}),
              ReadVectorPayload<float>(zWrite->Data));

    const Tests::MockDevice::BufferWriteRecord* keysWrite =
        FindWrite(device, result.Resources.Resources.RemainingKeys);
    ASSERT_NE(keysWrite, nullptr);
    EXPECT_EQ((std::vector<std::uint32_t>{0u, 1u, 2u}),
              ReadVectorPayload<std::uint32_t>(keysWrite->Data));

    const Tests::MockDevice::BufferWriteRecord* stateWrite =
        FindWrite(device, result.Resources.Resources.State);
    ASSERT_NE(stateWrite, nullptr);
    const Runtime::ProgressivePoissonGpuStateBufferRecord state =
        ReadPayload<Runtime::ProgressivePoissonGpuStateBufferRecord>(
            stateWrite->Data);
    EXPECT_EQ(state.PositionXBDA,
              device.GetBufferDeviceAddress(result.Resources.Resources.PositionX));
    EXPECT_EQ(state.RemainingKeysBDA,
              device.GetBufferDeviceAddress(
                  result.Resources.Resources.RemainingKeys));
    EXPECT_EQ(state.LevelOffsetsBDA,
              device.GetBufferDeviceAddress(
                  result.Resources.Resources.LevelOffsets));

    ASSERT_EQ(device.CommandContext.CopyBufferRecords.size(), 3u);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].Src,
              result.Resources.Resources.AcceptedKeys);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].Dst,
              result.Resources.OrderReadback);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[0].Size,
              3u * sizeof(std::uint32_t));
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[1].Src,
              result.Resources.Resources.LevelOffsets);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[1].Dst,
              result.Resources.LevelOffsetsReadback);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[1].Size,
              2u * sizeof(std::uint32_t));
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[2].Src,
              result.Resources.Resources.SplatRadii);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[2].Dst,
              result.Resources.SplatRadiiReadback);
    EXPECT_EQ(device.CommandContext.CopyBufferRecords[2].Size,
              3u * sizeof(float));
}

TEST(ProgressivePoissonGpuBackend,
     RecordFailsClosedForInvalidResourcesAndUnavailableDevice)
{
    Runtime::ProgressivePoissonGpuPlanDesc planDesc{};
    planDesc.InputCount = 16u;

    Tests::MockDevice unavailable{};
    unavailable.Operational = false;
    const Runtime::ProgressivePoissonGpuRecordResult unavailableResult =
        Runtime::RecordProgressivePoissonGpuPasses(
            Runtime::ProgressivePoissonGpuRecordDesc{
                .Device = &unavailable,
                .CommandContext = &unavailable.CommandContext,
                .Pipelines = ValidProgressivePoissonPipelines(),
                .Resources = ValidProgressivePoissonResources(),
                .Plan = planDesc,
            });
    EXPECT_EQ(unavailableResult.Status,
              Runtime::ProgressivePoissonGpuStatus::DeviceUnavailable);
    EXPECT_FALSE(unavailableResult.Recorded);
    EXPECT_TRUE(unavailableResult.CpuFallbackRecommended);
    EXPECT_EQ(unavailable.CommandContext.DispatchCalls, 0);

    Tests::MockDevice device{};
    Runtime::ProgressivePoissonGpuResourceSet missingCarry =
        ValidProgressivePoissonResources();
    missingCarry.CarryFlags = {};
    const Runtime::ProgressivePoissonGpuRecordResult invalidResource =
        Runtime::RecordProgressivePoissonGpuPasses(
            Runtime::ProgressivePoissonGpuRecordDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Pipelines = ValidProgressivePoissonPipelines(),
                .Resources = missingCarry,
                .Plan = planDesc,
            });
    EXPECT_EQ(invalidResource.Status,
              Runtime::ProgressivePoissonGpuStatus::InvalidGpuResource);
    EXPECT_FALSE(invalidResource.Recorded);
    EXPECT_TRUE(invalidResource.CpuFallbackRecommended);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);

    Runtime::ProgressivePoissonGpuPipelineSet missingCompaction =
        ValidProgressivePoissonPipelines();
    missingCompaction.Compaction.CompactByFlags = {};
    const Runtime::ProgressivePoissonGpuRecordResult invalidPipeline =
        Runtime::RecordProgressivePoissonGpuPasses(
            Runtime::ProgressivePoissonGpuRecordDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Pipelines = missingCompaction,
                .Resources = ValidProgressivePoissonResources(),
                .Plan = planDesc,
            });
    EXPECT_EQ(invalidPipeline.Status,
              Runtime::ProgressivePoissonGpuStatus::InvalidGpuResource);
    EXPECT_FALSE(invalidPipeline.Recorded);
    EXPECT_TRUE(invalidPipeline.CpuFallbackRecommended);
}

TEST(ProgressivePoissonGpuBackend, ResolveReportsCpuFallbackUntilExecutionLands)
{
    const Runtime::ProgressivePoissonGpuResolveResult missing =
        Runtime::ResolveProgressivePoissonGpuRequest(
            Runtime::ProgressivePoissonGpuResolveDesc{});
    EXPECT_EQ(missing.Status, Runtime::ProgressivePoissonGpuStatus::MissingDevice);
    EXPECT_TRUE(missing.CpuFallbackRecommended);
    EXPECT_FALSE(missing.GpuExecutionAvailable);

    Tests::MockDevice nonOperational;
    nonOperational.Operational = false;
    Runtime::ProgressivePoissonGpuResolveDesc request{};
    request.Device = &nonOperational;
    request.Plan.InputCount = 32u;
    const Runtime::ProgressivePoissonGpuResolveResult unavailable =
        Runtime::ResolveProgressivePoissonGpuRequest(request);
    EXPECT_EQ(unavailable.Status,
              Runtime::ProgressivePoissonGpuStatus::DeviceUnavailable);
    EXPECT_TRUE(unavailable.CpuFallbackRecommended);
    EXPECT_EQ(nonOperational.CreateBufferCount, 0);
    EXPECT_EQ(nonOperational.CreatePipelineCount, 0);

    Tests::MockDevice operational;
    operational.Operational = true;
    request.Device = &operational;
    const Runtime::ProgressivePoissonGpuResolveResult planningOnly =
        Runtime::ResolveProgressivePoissonGpuRequest(request);
    EXPECT_EQ(planningOnly.Status,
              Runtime::ProgressivePoissonGpuStatus::PlanningOnly);
    EXPECT_TRUE(planningOnly.CpuFallbackRecommended);
    EXPECT_FALSE(planningOnly.GpuExecutionAvailable);
    EXPECT_TRUE(planningOnly.Plan.IsValid());
    EXPECT_EQ(operational.CreateBufferCount, 0);
    EXPECT_EQ(operational.CreatePipelineCount, 0);
}

TEST(ProgressivePoissonGpuBackend,
     ExecutionFallsBackBeforeAllocationWhenDeviceUnavailable)
{
    Tests::MockDevice device{};
    device.Operational = false;
    RHI::BufferManager buffers{device};

    Runtime::ProgressivePoissonGpuPlanDesc planDesc{};
    planDesc.InputCount = 1u;
    const std::array<glm::vec3, 1> points{{glm::vec3{0.0f, 0.0f, 0.0f}}};

    const Runtime::ProgressivePoissonGpuExecutionResult result =
        Runtime::RecordProgressivePoissonGpuExecution(
            Runtime::ProgressivePoissonGpuExecutionDesc{
                .Device = &device,
                .CommandContext = &device.CommandContext,
                .Buffers = &buffers,
                .Pipelines = ValidProgressivePoissonPipelines(),
                .Positions = points,
                .Plan = planDesc,
            });

    EXPECT_EQ(result.Status,
              Runtime::ProgressivePoissonGpuStatus::DeviceUnavailable);
    EXPECT_FALSE(result.Recorded);
    EXPECT_TRUE(result.CpuFallbackRecommended);
    EXPECT_FALSE(result.UploadedInputs);
    EXPECT_EQ(device.CreateBufferCount, 0);
    EXPECT_EQ(device.BufferWrites.size(), 0u);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
}

TEST(ProgressivePoissonGpuBackend, DebugNamesAreStable)
{
    EXPECT_STREQ(Runtime::DebugNameForProgressivePoissonGpuStatus(
                     Runtime::ProgressivePoissonGpuStatus::PlanningOnly),
                 "PlanningOnly");
    EXPECT_STREQ(Runtime::DebugNameForProgressivePoissonGpuPassKind(
                     Runtime::ProgressivePoissonGpuPassKind::AcceptPhase),
                 "AcceptPhase");
    EXPECT_STREQ(Runtime::DebugNameForProgressivePoissonGpuBufferRole(
                     Runtime::ProgressivePoissonGpuBufferRole::HashKeys),
                 "HashKeys");
}
