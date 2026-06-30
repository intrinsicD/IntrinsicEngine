#include <algorithm>
#include <cstdint>

#include <gtest/gtest.h>

import Extrinsic.Runtime.ProgressivePoissonGpuBackend;
import Extrinsic.Graphics.ComputeParallelPrimitives;
import Extrinsic.RHI.Descriptors;

#include "MockRHI.hpp"

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    namespace Tests = Extrinsic::Tests;

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
