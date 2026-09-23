#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Selection.Id;
import Extrinsic.Graphics.Pass.Selection.Outline;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;
using Tests::MockCommandContext;

namespace
{
    using EventKind = MockCommandContext::EventKind;

    Graphics::GpuWorld::InitDesc TinyWorldDesc()
    {
        Graphics::GpuWorld::InitDesc init{};
        init.MaxInstances = 4;
        init.MaxGeometryRecords = 4;
        init.MaxLights = 1;
        init.VertexBufferBytes = 4096;
        init.IndexBufferBytes = 4096;
        return init;
    }

    void ExpectScenePushConstants(const MockCommandContext& cmd,
                                  const Graphics::GpuWorld& world,
                                  const RHI::GpuDrawBucketKind bucket,
                                  const std::uint32_t frameIndex)
    {
        ASSERT_EQ(cmd.PushConstantPayloads.size(), 1u);
        ASSERT_EQ(cmd.PushConstantPayloads.back().size(), sizeof(RHI::GpuScenePushConstants));
        RHI::GpuScenePushConstants pc{};
        std::memcpy(&pc, cmd.PushConstantPayloads.back().data(), sizeof(pc));
        EXPECT_EQ(pc.SceneTableBDA, world.GetSceneTableBDA());
        EXPECT_EQ(pc.FrameIndex, frameIndex);
        EXPECT_EQ(pc.DrawBucket, static_cast<std::uint32_t>(bucket));
    }
}

TEST(GraphicsSelectionPassContracts, EntityAndFaceIdPassesDrawSurfaceBucket)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));
    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);

    Graphics::SelectionSystem selection;
    Graphics::EntityIdPass entityPass{selection};
    entityPass.SetPipeline(RHI::PipelineHandle{701u, 1u});
    MockCommandContext skipped;
    RHI::CameraUBO camera{};
    entityPass.Execute(skipped, camera, world, culling, 3u);
    EXPECT_TRUE(skipped.Events.empty());

    selection.Initialize();
    MockCommandContext entityCmd;
    entityPass.Execute(entityCmd, camera, world, culling, 3u);
    ASSERT_EQ(entityCmd.Events.size(), 4u);
    EXPECT_EQ(entityCmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(entityCmd.Events[1], EventKind::BindIndexBuffer);
    EXPECT_EQ(entityCmd.Events[2], EventKind::PushConstants);
    EXPECT_EQ(entityCmd.Events[3], EventKind::DrawIndexedIndirectCount);
    EXPECT_EQ(entityCmd.LastBoundPipeline, (RHI::PipelineHandle{701u, 1u}));
    EXPECT_EQ(entityCmd.LastIndexBuffer, world.GetManagedIndexBuffer());
    EXPECT_EQ(entityCmd.LastDrawIndexedIndirectCount.ArgumentBuffer, bucket.IndexedArgsBuffer);
    EXPECT_EQ(entityCmd.LastDrawIndexedIndirectCount.CountBuffer, bucket.CountBuffer);
    EXPECT_EQ(entityCmd.LastMaxDrawCount, bucket.Capacity);
    ExpectScenePushConstants(entityCmd, world, RHI::GpuDrawBucketKind::SurfaceOpaque, 3u);

    Graphics::FaceIdPass facePass{selection};
    facePass.SetPipeline(RHI::PipelineHandle{702u, 1u});
    MockCommandContext faceCmd;
    facePass.Execute(faceCmd, camera, world, culling, 4u);
    ASSERT_EQ(faceCmd.Events.size(), 4u);
    EXPECT_EQ(faceCmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(faceCmd.Events[1], EventKind::BindIndexBuffer);
    EXPECT_EQ(faceCmd.Events[2], EventKind::PushConstants);
    EXPECT_EQ(faceCmd.Events[3], EventKind::DrawIndexedIndirectCount);
    EXPECT_EQ(faceCmd.LastBoundPipeline, (RHI::PipelineHandle{702u, 1u}));
    EXPECT_EQ(faceCmd.LastDrawIndexedIndirectCount.ArgumentBuffer, bucket.IndexedArgsBuffer);
    ExpectScenePushConstants(faceCmd, world, RHI::GpuDrawBucketKind::SurfaceOpaque, 4u);

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsSelectionPassContracts, EdgeAndPointIdPassesDrawLineAndPointBuckets)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));
    Graphics::SelectionSystem selection;
    selection.Initialize();
    RHI::CameraUBO camera{};

    Graphics::EdgeIdPass edgePass{selection};
    edgePass.SetPipeline(RHI::PipelineHandle{703u, 1u});
    MockCommandContext edgeCmd;
    edgePass.Execute(edgeCmd, camera, world, culling, 5u);
    const auto& lineBucket = culling.GetBucket(RHI::GpuDrawBucketKind::Lines);
    ASSERT_EQ(edgeCmd.Events.size(), 4u);
    EXPECT_EQ(edgeCmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(edgeCmd.Events[1], EventKind::BindIndexBuffer);
    EXPECT_EQ(edgeCmd.Events[2], EventKind::PushConstants);
    EXPECT_EQ(edgeCmd.Events[3], EventKind::DrawIndexedIndirectCount);
    EXPECT_EQ(edgeCmd.LastDrawIndexedIndirectCount.ArgumentBuffer, lineBucket.IndexedArgsBuffer);
    ExpectScenePushConstants(edgeCmd, world, RHI::GpuDrawBucketKind::Lines, 5u);

    Graphics::PointIdPass pointPass{selection};
    pointPass.SetPipeline(RHI::PipelineHandle{704u, 1u});
    MockCommandContext pointCmd;
    pointPass.Execute(pointCmd, camera, world, culling, 6u);
    const auto& pointBucket = culling.GetBucket(RHI::GpuDrawBucketKind::SelectionPoints);
    ASSERT_EQ(pointCmd.Events.size(), 3u);
    EXPECT_EQ(pointCmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(pointCmd.Events[1], EventKind::PushConstants);
    EXPECT_EQ(pointCmd.Events[2], EventKind::DrawIndirectCount);
    EXPECT_EQ(pointCmd.LastDrawIndirectCount.ArgumentBuffer, pointBucket.NonIndexedArgsBuffer);
    ExpectScenePushConstants(pointCmd, world, RHI::GpuDrawBucketKind::SelectionPoints, 6u);

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsSelectionPassContracts, SelectionOutlinePassDrawsFullscreenTriangle)
{
    Graphics::SelectionSystem selection;
    Graphics::SelectionOutlinePass pass{selection};
    pass.SetPipeline(RHI::PipelineHandle{705u, 1u});
    RHI::CameraUBO camera{};

    MockCommandContext skipped;
    pass.Execute(skipped, camera, 7u);
    EXPECT_TRUE(skipped.Events.empty());

    selection.Initialize();
    MockCommandContext cmd;
    pass.Execute(cmd, camera, 7u);
    // GRAPHICS-074 Slice C — the pass body is now
    // `BindPipeline → PushConstants → Draw(3,1,0,0)`. The push-constant
    // step writes a 144-byte zero-initialised `SelectionOutlinePushConstants`
    // instance so the `selection_outline.frag` shader sees defined values
    // rather than stale push-constant memory left by an earlier draw —
    // without this, `OutlineWidth` could be arbitrary bytes and the
    // fragment shader's neighbour-sampling loop would run for an unbounded
    // number of iterations.
    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1], EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2], EventKind::Draw);
    EXPECT_EQ(cmd.LastBoundPipeline, (RHI::PipelineHandle{705u, 1u}));
    EXPECT_EQ(cmd.LastDraw.VertexCount, 3u);
    ASSERT_EQ(cmd.PushConstantPayloads.size(), 1u);
    ASSERT_EQ(cmd.PushConstantPayloads.back().size(), 144u);
    for (const std::byte b : cmd.PushConstantPayloads.back())
    {
        EXPECT_EQ(b, std::byte{0});
    }
}
