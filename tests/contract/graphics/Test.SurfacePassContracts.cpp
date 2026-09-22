#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.ForwardSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Deferred.GBuffers;
import Extrinsic.Graphics.Pass.DepthPrepass;
import Extrinsic.Graphics.Pass.Forward.Surface;
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

    void ExpectSurfaceBucketDraw(const MockCommandContext& cmd,
                                 const Graphics::GpuWorld& world,
                                 const Graphics::CullingSystem& culling,
                                 const RHI::PipelineHandle pipeline,
                                 const std::uint32_t frameIndex)
    {
        ASSERT_EQ(cmd.Events.size(), 4u);
        EXPECT_EQ(cmd.Events[0], EventKind::BindPipeline);
        EXPECT_EQ(cmd.Events[1], EventKind::BindIndexBuffer);
        EXPECT_EQ(cmd.Events[2], EventKind::PushConstants);
        EXPECT_EQ(cmd.Events[3], EventKind::DrawIndexedIndirectCount);

        EXPECT_EQ(cmd.LastBoundPipeline, pipeline);
        EXPECT_EQ(cmd.LastIndexBuffer, world.GetManagedIndexBuffer());

        ASSERT_EQ(cmd.PushConstantPayloads.size(), 1u);
        ASSERT_EQ(cmd.PushConstantPayloads.back().size(), sizeof(RHI::GpuScenePushConstants));
        RHI::GpuScenePushConstants pc{};
        std::memcpy(&pc, cmd.PushConstantPayloads.back().data(), sizeof(pc));
        EXPECT_EQ(pc.SceneTableBDA, world.GetSceneTableBDA());
        EXPECT_EQ(pc.FrameIndex, frameIndex);
        EXPECT_EQ(pc.DrawBucket, static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SurfaceOpaque));

        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
        EXPECT_EQ(cmd.LastDrawIndexedIndirectCount.ArgumentBuffer, bucket.IndexedArgsBuffer);
        EXPECT_EQ(cmd.LastDrawIndexedIndirectCount.CountBuffer, bucket.CountBuffer);
        EXPECT_EQ(cmd.LastMaxDrawCount, bucket.Capacity);
    }
}

static_assert(sizeof(RHI::GpuScenePushConstants) <= 128u);

TEST(GraphicsSurfacePassContracts, DepthPrepassRecordsSurfaceOpaqueIndirectDraw)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    Graphics::DepthPrepassPass pass;
    MockCommandContext noPipelineCmd;
    RHI::CameraUBO camera{};
    pass.Execute(noPipelineCmd, camera, world, culling, 3u);
    EXPECT_TRUE(noPipelineCmd.Events.empty());

    const RHI::PipelineHandle pipeline{101u, 1u};
    pass.SetPipeline(pipeline);

    MockCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 3u);
    ExpectSurfaceBucketDraw(cmd, world, culling, pipeline, 3u);

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsSurfacePassContracts, ForwardSurfaceRequiresInitializedSystemAndRecordsSurfaceOpaqueDraw)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    Graphics::ForwardSystem forward;
    Graphics::ForwardSurfacePass pass{forward};
    const RHI::PipelineHandle pipeline{102u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    MockCommandContext uninitializedCmd;
    pass.Execute(uninitializedCmd, camera, world, culling, 4u);
    EXPECT_TRUE(uninitializedCmd.Events.empty());

    forward.Initialize();
    MockCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 4u);
    ExpectSurfaceBucketDraw(cmd, world, culling, pipeline, 4u);

    forward.Shutdown();
    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsSurfacePassContracts, DeferredGBufferRequiresInitializedSystemAndRecordsSurfaceOpaqueDraw)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    Graphics::DeferredSystem deferred;
    Graphics::DeferredGBufferPass pass{deferred};
    const RHI::PipelineHandle pipeline{103u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    MockCommandContext uninitializedCmd;
    pass.Execute(uninitializedCmd, camera, world, culling, 5u);
    EXPECT_TRUE(uninitializedCmd.Events.empty());

    deferred.Initialize();
    MockCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 5u);
    ExpectSurfaceBucketDraw(cmd, world, culling, pipeline, 5u);

    deferred.Shutdown();
    culling.Shutdown();
    world.Shutdown();
}

