// GRAPHICS-039A — CPU/null contract for clustered-light froxel grid sizing,
// logarithmic view-Z slicing, per-cell view-space AABBs, and build-pass command
// recording. Light assignment, shader consumption, and async-compute affinity are
// follow-up tasks.

#include <cmath>
#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

import Extrinsic.Graphics.LightClusters;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

#include "MockRHI.hpp"

using namespace Extrinsic;

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    [[nodiscard]] Graphics::ClusterGridProjection DefaultProjection()
    {
        return Graphics::BuildClusterGridProjectionFromVerticalFov(
            kPi * 0.5f,
            16.f / 9.f,
            0.1f,
            1000.f);
    }

    void ExpectNear(const float actual, const float expected, const float tolerance = 0.0001f)
    {
        EXPECT_NEAR(actual, expected, tolerance);
    }
}

TEST(GraphicsLightClusterGrid, DimensionFormulaScalesFromTilePixels)
{
    const Graphics::ClusterGridDesc hd = Graphics::ComputeClusterGridDesc(1280u, 720u);
    ASSERT_TRUE(hd.IsValid());
    EXPECT_EQ(hd.RenderWidth, 1280u);
    EXPECT_EQ(hd.RenderHeight, 720u);
    EXPECT_EQ(hd.ClusterTilePx, 80u);
    EXPECT_EQ(hd.TilesX, 16u);
    EXPECT_EQ(hd.TilesY, 9u);
    EXPECT_EQ(hd.SlicesZ, 24u);
    EXPECT_EQ(hd.CellCount, 16u * 9u * 24u);
    EXPECT_EQ(Graphics::ComputeClusterGridAABBBufferSizeBytes(hd),
              static_cast<std::uint64_t>(hd.CellCount) * sizeof(Graphics::ClusterGridAABB));

    const RHI::BufferDesc buffer = Graphics::BuildClusterGridAABBBufferDesc(hd);
    EXPECT_EQ(buffer.SizeBytes, Graphics::ComputeClusterGridAABBBufferSizeBytes(hd));
    EXPECT_TRUE(RHI::HasUsage(buffer.Usage, RHI::BufferUsage::Storage));
    EXPECT_TRUE(RHI::HasUsage(buffer.Usage, RHI::BufferUsage::TransferSrc));
    EXPECT_FALSE(buffer.HostVisible);

    const Graphics::ClusterGridDesc fullHd = Graphics::ComputeClusterGridDesc(1920u, 1080u);
    ASSERT_TRUE(fullHd.IsValid());
    EXPECT_EQ(fullHd.TilesX, 24u);
    EXPECT_EQ(fullHd.TilesY, 14u);
    EXPECT_EQ(fullHd.CellCount, 24u * 14u * 24u);

    EXPECT_FALSE(Graphics::ComputeClusterGridDesc(0u, 720u).IsValid());
    EXPECT_FALSE(Graphics::ComputeClusterGridDesc(1280u, 0u).IsValid());
    EXPECT_FALSE(Graphics::ComputeClusterGridDesc(1280u, 720u, 0u).IsValid());
    EXPECT_FALSE(Graphics::ComputeClusterGridDesc(1280u, 720u, 80u, 0u).IsValid());
}

TEST(GraphicsLightClusterGrid, LogViewZMappingClampsNearAndRejectsBeyondFar)
{
    const Graphics::ClusterGridProjection projection = DefaultProjection();
    ASSERT_TRUE(projection.IsValid());

    const Graphics::ClusterSliceMapping beforeNear =
        Graphics::MapViewZToClusterSlice(0.01f, projection, 24u);
    EXPECT_TRUE(beforeNear.InRange);
    EXPECT_EQ(beforeNear.Slice, 0u);

    const Graphics::ClusterSliceMapping nearSlice =
        Graphics::MapViewZToClusterSlice(0.1f, projection, 24u);
    EXPECT_TRUE(nearSlice.InRange);
    EXPECT_EQ(nearSlice.Slice, 0u);

    const Graphics::ClusterSliceMapping geometricMid =
        Graphics::MapViewZToClusterSlice(10.0f, projection, 24u);
    EXPECT_TRUE(geometricMid.InRange);
    EXPECT_EQ(geometricMid.Slice, 12u);

    const Graphics::ClusterSliceMapping farSlice =
        Graphics::MapViewZToClusterSlice(1000.0f, projection, 24u);
    EXPECT_TRUE(farSlice.InRange);
    EXPECT_EQ(farSlice.Slice, 23u);

    const Graphics::ClusterSliceMapping beyondFar =
        Graphics::MapViewZToClusterSlice(1000.001f, projection, 24u);
    EXPECT_FALSE(beyondFar.InRange);
    EXPECT_EQ(beyondFar.Slice, 23u);

    EXPECT_FALSE(Graphics::MapViewZToClusterSlice(-1.0f, projection, 24u).InRange);
    EXPECT_FALSE(Graphics::MapViewZToClusterSlice(1.0f, {}, 24u).InRange);
    EXPECT_FALSE(Graphics::MapViewZToClusterSlice(1.0f, projection, 0u).InRange);
}

TEST(GraphicsLightClusterGrid, DepthSliceBoundsUseOlssonLogarithmicSplit)
{
    const Graphics::ClusterGridDesc desc = Graphics::ComputeClusterGridDesc(1280u, 720u);
    const Graphics::ClusterGridProjection projection = DefaultProjection();

    const Graphics::ClusterDepthSlice first = Graphics::ComputeClusterDepthSlice(desc, projection, 0u);
    ASSERT_TRUE(first.Valid);
    ExpectNear(first.NearZ, 0.1f);
    const float firstExpectedFar = 0.1f * std::exp(std::log(1000.f / 0.1f) / 24.f);
    ExpectNear(first.FarZ, firstExpectedFar);

    const Graphics::ClusterDepthSlice last = Graphics::ComputeClusterDepthSlice(desc, projection, 23u);
    ASSERT_TRUE(last.Valid);
    const float lastExpectedNear = 0.1f * std::exp(std::log(1000.f / 0.1f) * 23.f / 24.f);
    ExpectNear(last.NearZ, lastExpectedNear, 0.01f);
    ExpectNear(last.FarZ, 1000.f, 0.01f);

    EXPECT_FALSE(Graphics::ComputeClusterDepthSlice(desc, projection, 24u).Valid);
}

TEST(GraphicsLightClusterGrid, PerCellAABBMatchesRightHandedViewSpaceFrustumTile)
{
    const Graphics::ClusterGridDesc desc = Graphics::ComputeClusterGridDesc(1280u, 720u);
    const Graphics::ClusterGridProjection projection = DefaultProjection();
    ASSERT_TRUE(desc.IsValid());
    ASSERT_TRUE(projection.IsValid());

    const Graphics::ClusterDepthSlice lastDepth = Graphics::ComputeClusterDepthSlice(desc, projection, 23u);
    ASSERT_TRUE(lastDepth.Valid);

    const Graphics::ClusterGridAABB bottomRight =
        Graphics::ComputeClusterCellAABB(desc, projection, 15u, 8u, 23u);

    const float ndcMinX = 0.875f;
    const float ndcMaxX = 1.0f;
    const float ndcMinY = -1.0f;
    const float ndcMaxY = -7.f / 9.f;

    ExpectNear(bottomRight.MinX, ndcMinX * lastDepth.NearZ / projection.ProjectionScaleX, 0.01f);
    ExpectNear(bottomRight.MaxX, ndcMaxX * lastDepth.FarZ / projection.ProjectionScaleX, 0.01f);
    ExpectNear(bottomRight.MinY, ndcMinY * lastDepth.FarZ / projection.ProjectionScaleY, 0.01f);
    ExpectNear(bottomRight.MaxY, ndcMaxY * lastDepth.NearZ / projection.ProjectionScaleY, 0.01f);
    ExpectNear(bottomRight.MinZ, -lastDepth.FarZ, 0.01f);
    ExpectNear(bottomRight.MaxZ, -lastDepth.NearZ, 0.01f);
    EXPECT_EQ(bottomRight.PadMin, 0.f);
    EXPECT_EQ(bottomRight.PadMax, 0.f);

    const Graphics::ClusterGridAABB invalid =
        Graphics::ComputeClusterCellAABB(desc, projection, desc.TilesX, 0u, 0u);
    EXPECT_EQ(invalid.MinX, 0.f);
    EXPECT_EQ(invalid.MaxZ, 0.f);
}

TEST(GraphicsLightClusterGrid, PartialEdgeTileAABBClampsToRenderPixelBounds)
{
    const Graphics::ClusterGridDesc desc = Graphics::ComputeClusterGridDesc(1920u, 1080u);
    const Graphics::ClusterGridProjection projection = DefaultProjection();
    ASSERT_TRUE(desc.IsValid());
    ASSERT_TRUE(projection.IsValid());
    EXPECT_EQ(desc.TilesX, 24u);
    EXPECT_EQ(desc.TilesY, 14u);

    const Graphics::ClusterDepthSlice firstDepth = Graphics::ComputeClusterDepthSlice(desc, projection, 0u);
    ASSERT_TRUE(firstDepth.Valid);

    const Graphics::ClusterGridAABB bottomRight =
        Graphics::ComputeClusterCellAABB(desc, projection, 23u, 13u, 0u);

    const float ndcMinX = (2.f * 1840.f / 1920.f) - 1.f;
    const float ndcMaxX = 1.f;
    const float ndcMinY = -1.f;
    const float ndcMaxY = 1.f - (2.f * 1040.f / 1080.f);

    ExpectNear(bottomRight.MinX, ndcMinX * firstDepth.NearZ / projection.ProjectionScaleX, 0.001f);
    ExpectNear(bottomRight.MaxX, ndcMaxX * firstDepth.FarZ / projection.ProjectionScaleX, 0.001f);
    ExpectNear(bottomRight.MinY, ndcMinY * firstDepth.FarZ / projection.ProjectionScaleY, 0.001f);
    ExpectNear(bottomRight.MaxY, ndcMaxY * firstDepth.NearZ / projection.ProjectionScaleY, 0.001f);
    ExpectNear(bottomRight.MinZ, -firstDepth.FarZ, 0.001f);
    ExpectNear(bottomRight.MaxZ, -firstDepth.NearZ, 0.001f);
}

TEST(GraphicsLightClusterGrid, CellIndexOrdersTilesInsideZSlices)
{
    const Graphics::ClusterGridDesc desc = Graphics::ComputeClusterGridDesc(1280u, 720u);
    ASSERT_TRUE(desc.IsValid());

    EXPECT_EQ(Graphics::ComputeClusterCellIndex(desc, 0u, 0u, 0u), 0u);
    EXPECT_EQ(Graphics::ComputeClusterCellIndex(desc, 1u, 0u, 0u), 1u);
    EXPECT_EQ(Graphics::ComputeClusterCellIndex(desc, 0u, 1u, 0u), desc.TilesX);
    EXPECT_EQ(Graphics::ComputeClusterCellIndex(desc, 0u, 0u, 1u), desc.TilesX * desc.TilesY);
    EXPECT_EQ(Graphics::ComputeClusterCellIndex(desc, desc.TilesX - 1u, desc.TilesY - 1u, desc.SlicesZ - 1u),
              desc.CellCount - 1u);
    EXPECT_EQ(Graphics::ComputeClusterCellIndex(desc, desc.TilesX, 0u, 0u), Graphics::kInvalidClusterCellIndex);
}

TEST(GraphicsLightClusterGrid, RecordBuildDispatchesOneLinearGridAndPublishBarrier)
{
    const Graphics::ClusterGridDesc desc = Graphics::ComputeClusterGridDesc(1280u, 720u);
    const Graphics::ClusterGridBuildDispatchPlan plan =
        Graphics::ComputeClusterGridBuildDispatchPlan(desc);
    ASSERT_TRUE(plan.IsValid());
    EXPECT_EQ(plan.GroupSize, 64u);
    EXPECT_EQ(plan.GroupCountX, 54u); // ceil(3456 cells / 64)
    EXPECT_EQ(plan.GroupCountY, 1u);
    EXPECT_EQ(plan.GroupCountZ, 1u);

    const Graphics::ClusterGridProjection projection = DefaultProjection();
    Tests::MockCommandContext cmd;
    const RHI::PipelineHandle pipeline{17u, 1u};
    const RHI::BufferHandle aabbs{18u, 1u};

    ASSERT_TRUE(Graphics::RecordClusterGridBuild(cmd, pipeline, aabbs, plan, projection));

    EXPECT_EQ(cmd.BindPipelineCalls, 1);
    EXPECT_EQ(cmd.LastBoundPipeline, pipeline);
    ASSERT_EQ(cmd.DispatchRecords.size(), 1u);
    EXPECT_EQ(cmd.DispatchRecords[0].X, plan.GroupCountX);
    EXPECT_EQ(cmd.DispatchRecords[0].Y, plan.GroupCountY);
    EXPECT_EQ(cmd.DispatchRecords[0].Z, plan.GroupCountZ);

    ASSERT_EQ(cmd.PushConstantPayloads.size(), 1u);
    ASSERT_EQ(cmd.PushConstantPayloads[0].size(), sizeof(Graphics::ClusterGridBuildPushConstants));
    Graphics::ClusterGridBuildPushConstants pc{};
    std::memcpy(&pc, cmd.PushConstantPayloads[0].data(), sizeof(pc));
    EXPECT_EQ(pc.RenderWidth, desc.RenderWidth);
    EXPECT_EQ(pc.RenderHeight, desc.RenderHeight);
    EXPECT_EQ(pc.TilesX, desc.TilesX);
    EXPECT_EQ(pc.TilesY, desc.TilesY);
    EXPECT_EQ(pc.SlicesZ, desc.SlicesZ);
    EXPECT_EQ(pc.CellCount, desc.CellCount);
    EXPECT_EQ(pc.ClusterTilePx, desc.ClusterTilePx);
    ExpectNear(pc.NearZ, projection.NearZ);
    ExpectNear(pc.FarZ, projection.FarZ);
    ExpectNear(pc.ProjectionScaleX, projection.ProjectionScaleX);
    ExpectNear(pc.ProjectionScaleY, projection.ProjectionScaleY);

    ASSERT_EQ(cmd.BufferBarrierCalls.size(), 1u);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].Buffer, aabbs);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].Before, RHI::MemoryAccess::ShaderWrite);
    EXPECT_EQ(cmd.BufferBarrierCalls[0].After, RHI::MemoryAccess::ShaderRead);
}

TEST(GraphicsLightClusterGrid, RecordRejectsInvalidInputs)
{
    const Graphics::ClusterGridDesc desc = Graphics::ComputeClusterGridDesc(1280u, 720u);
    const Graphics::ClusterGridBuildDispatchPlan plan =
        Graphics::ComputeClusterGridBuildDispatchPlan(desc);
    const Graphics::ClusterGridProjection projection = DefaultProjection();

    Tests::MockCommandContext cmd;
    EXPECT_FALSE(Graphics::RecordClusterGridBuild(cmd, RHI::PipelineHandle{}, RHI::BufferHandle{18u, 1u}, plan, projection));
    EXPECT_FALSE(Graphics::RecordClusterGridBuild(cmd, RHI::PipelineHandle{17u, 1u}, RHI::BufferHandle{}, plan, projection));
    EXPECT_FALSE(Graphics::RecordClusterGridBuild(cmd, RHI::PipelineHandle{17u, 1u}, RHI::BufferHandle{18u, 1u}, {}, projection));
    EXPECT_FALSE(Graphics::RecordClusterGridBuild(cmd, RHI::PipelineHandle{17u, 1u}, RHI::BufferHandle{18u, 1u}, plan, {}));
    EXPECT_EQ(cmd.BindPipelineCalls, 0);
    EXPECT_EQ(cmd.DispatchCalls, 0);
    EXPECT_TRUE(cmd.BufferBarrierCalls.empty());
}
