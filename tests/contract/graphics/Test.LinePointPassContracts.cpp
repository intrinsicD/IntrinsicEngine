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

namespace
{
    enum class EventKind
    {
        BindPipeline,
        BindIndexBuffer,
        PushConstants,
        DrawIndexedIndirectCount,
        DrawIndirectCount,
    };

    struct Event
    {
        EventKind Kind{};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events;
        RHI::PipelineHandle LastPipeline{};
        RHI::BufferHandle LastIndexBuffer{};
        RHI::BufferHandle LastIndirectArgs{};
        RHI::BufferHandle LastIndirectCount{};
        std::uint32_t LastMaxDrawCount = 0;
        std::vector<std::byte> LastPushConstants{};

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle pipeline) override
        {
            Events.push_back({.Kind = EventKind::BindPipeline});
            LastPipeline = pipeline;
        }
        void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t, RHI::IndexType) override
        {
            Events.push_back({.Kind = EventKind::BindIndexBuffer});
            LastIndexBuffer = buffer;
        }
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::PushConstants});
            LastPushConstants.resize(size);
            if (size > 0u && data != nullptr)
            {
                std::memcpy(LastPushConstants.data(), data, size);
            }
        }
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle argBuffer, std::uint64_t,
                                      RHI::BufferHandle countBuffer, std::uint64_t,
                                      std::uint32_t maxDrawCount) override
        {
            Events.push_back({.Kind = EventKind::DrawIndexedIndirectCount});
            LastIndirectArgs = argBuffer;
            LastIndirectCount = countBuffer;
            LastMaxDrawCount = maxDrawCount;
        }
        void DrawIndirectCount(RHI::BufferHandle argBuffer, std::uint64_t,
                               RHI::BufferHandle countBuffer, std::uint64_t,
                               std::uint32_t maxDrawCount) override
        {
            Events.push_back({.Kind = EventKind::DrawIndirectCount});
            LastIndirectArgs = argBuffer;
            LastIndirectCount = countBuffer;
            LastMaxDrawCount = maxDrawCount;
        }
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

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

    void ExpectScenePushConstants(const RecordingCommandContext& cmd,
                                  const Graphics::GpuWorld& world,
                                  const RHI::GpuDrawBucketKind bucket,
                                  const std::uint32_t frameIndex)
    {
        ASSERT_EQ(cmd.LastPushConstants.size(), sizeof(RHI::GpuScenePushConstants));
        RHI::GpuScenePushConstants pc{};
        std::memcpy(&pc, cmd.LastPushConstants.data(), sizeof(pc));
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
    RecordingCommandContext invalidBucketCmd;
    forward.Initialize();
    pass.Execute(invalidBucketCmd, camera, world, uninitializedCulling, 11u);
    EXPECT_TRUE(invalidBucketCmd.Events.empty());

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 11u);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::DrawIndirectCount);
    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_FALSE(cmd.LastIndexBuffer.IsValid());

    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::LineQuads);
    EXPECT_FALSE(bucket.Indexed);
    EXPECT_EQ(cmd.LastIndirectArgs, bucket.NonIndexedArgsBuffer);
    EXPECT_EQ(cmd.LastIndirectCount, bucket.CountBuffer);
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
    RecordingCommandContext invalidBucketCmd;
    forward.Initialize();
    pass.Execute(invalidBucketCmd, camera, world, uninitializedCulling, 12u);
    EXPECT_TRUE(invalidBucketCmd.Events.empty());

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 12u);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::DrawIndirectCount);
    EXPECT_EQ(cmd.LastPipeline, pipeline);

    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::Points);
    EXPECT_FALSE(bucket.Indexed);
    EXPECT_EQ(cmd.LastIndirectArgs, bucket.NonIndexedArgsBuffer);
    EXPECT_EQ(cmd.LastIndirectCount, bucket.CountBuffer);
    EXPECT_EQ(cmd.LastMaxDrawCount, bucket.Capacity);
    ExpectScenePushConstants(cmd, world, RHI::GpuDrawBucketKind::Points, 12u);

    culling.Shutdown();
    forward.Shutdown();
    world.Shutdown();
}
