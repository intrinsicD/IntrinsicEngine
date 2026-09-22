#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Forward.Line;
import Extrinsic.Graphics.Pass.Forward.Point;
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

TEST(GraphicsLinePointPassContracts, LinePassSkipsInvalidStateAndDrawsLineBucket)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::ForwardSystem forward;
    Graphics::ForwardLinePass pass{forward};
    const RHI::PipelineHandle pipeline{501u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    Graphics::CullingSystem uninitializedCulling;
    MockCommandContext invalidBucketCmd;
    forward.Initialize();
    pass.Execute(invalidBucketCmd, camera, world, uninitializedCulling, 11u);
    EXPECT_TRUE(invalidBucketCmd.Events.empty());

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    MockCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 11u);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1], EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2], EventKind::DrawIndirectCount);
    EXPECT_EQ(cmd.LastBoundPipeline, pipeline);
    EXPECT_FALSE(cmd.LastIndexBuffer.IsValid());

    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::LineQuads);
    EXPECT_FALSE(bucket.Indexed);
    EXPECT_EQ(cmd.LastDrawIndirectCount.ArgumentBuffer, bucket.NonIndexedArgsBuffer);
    EXPECT_EQ(cmd.LastDrawIndirectCount.CountBuffer, bucket.CountBuffer);
    EXPECT_EQ(cmd.LastMaxDrawCount, bucket.Capacity);
    ExpectScenePushConstants(cmd, world, RHI::GpuDrawBucketKind::LineQuads, 11u);

    culling.Shutdown();
    forward.Shutdown();
    world.Shutdown();
}

TEST(GraphicsLinePointPassContracts, PointPassSkipsInvalidStateAndDrawsPointBucket)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::ForwardSystem forward;
    Graphics::ForwardPointPass pass{forward};
    const RHI::PipelineHandle pipeline{601u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    Graphics::CullingSystem uninitializedCulling;
    MockCommandContext invalidBucketCmd;
    forward.Initialize();
    pass.Execute(invalidBucketCmd, camera, world, uninitializedCulling, 12u);
    EXPECT_TRUE(invalidBucketCmd.Events.empty());

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    MockCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 12u);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0], EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1], EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2], EventKind::DrawIndirectCount);
    EXPECT_EQ(cmd.LastBoundPipeline, pipeline);

    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::Points);
    EXPECT_FALSE(bucket.Indexed);
    EXPECT_EQ(cmd.LastDrawIndirectCount.ArgumentBuffer, bucket.NonIndexedArgsBuffer);
    EXPECT_EQ(cmd.LastDrawIndirectCount.CountBuffer, bucket.CountBuffer);
    EXPECT_EQ(cmd.LastMaxDrawCount, bucket.Capacity);
    ExpectScenePushConstants(cmd, world, RHI::GpuDrawBucketKind::Points, 12u);

    culling.Shutdown();
    forward.Shutdown();
    world.Shutdown();
}
