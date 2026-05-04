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

namespace
{
    enum class EventKind
    {
        BindPipeline,
        BindIndexBuffer,
        PushConstants,
        DrawIndexedIndirectCount,
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
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
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

    void ExpectSurfaceBucketDraw(const RecordingCommandContext& cmd,
                                 const Graphics::GpuWorld& world,
                                 const Graphics::CullingSystem& culling,
                                 const RHI::PipelineHandle pipeline,
                                 const std::uint32_t frameIndex)
    {
        ASSERT_EQ(cmd.Events.size(), 4u);
        EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
        EXPECT_EQ(cmd.Events[1].Kind, EventKind::BindIndexBuffer);
        EXPECT_EQ(cmd.Events[2].Kind, EventKind::PushConstants);
        EXPECT_EQ(cmd.Events[3].Kind, EventKind::DrawIndexedIndirectCount);

        EXPECT_EQ(cmd.LastPipeline, pipeline);
        EXPECT_EQ(cmd.LastIndexBuffer, world.GetManagedIndexBuffer());

        ASSERT_EQ(cmd.LastPushConstants.size(), sizeof(RHI::GpuScenePushConstants));
        RHI::GpuScenePushConstants pc{};
        std::memcpy(&pc, cmd.LastPushConstants.data(), sizeof(pc));
        EXPECT_EQ(pc.SceneTableBDA, world.GetSceneTableBDA());
        EXPECT_EQ(pc.FrameIndex, frameIndex);
        EXPECT_EQ(pc.DrawBucket, static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SurfaceOpaque));

        const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
        EXPECT_EQ(cmd.LastIndirectArgs, bucket.IndexedArgsBuffer);
        EXPECT_EQ(cmd.LastIndirectCount, bucket.CountBuffer);
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
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    Graphics::DepthPrepassPass pass;
    RecordingCommandContext noPipelineCmd;
    RHI::CameraUBO camera{};
    pass.Execute(noPipelineCmd, camera, world, culling, 3u);
    EXPECT_TRUE(noPipelineCmd.Events.empty());

    const RHI::PipelineHandle pipeline{101u, 1u};
    pass.SetPipeline(pipeline);

    RecordingCommandContext cmd;
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
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    Graphics::ForwardSystem forward;
    Graphics::ForwardSurfacePass pass{forward};
    const RHI::PipelineHandle pipeline{102u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    RecordingCommandContext uninitializedCmd;
    pass.Execute(uninitializedCmd, camera, world, culling, 4u);
    EXPECT_TRUE(uninitializedCmd.Events.empty());

    forward.Initialize();
    RecordingCommandContext cmd;
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
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    Graphics::DeferredSystem deferred;
    Graphics::DeferredGBufferPass pass{deferred};
    const RHI::PipelineHandle pipeline{103u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    RecordingCommandContext uninitializedCmd;
    pass.Execute(uninitializedCmd, camera, world, culling, 5u);
    EXPECT_TRUE(uninitializedCmd.Events.empty());

    deferred.Initialize();
    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 5u);
    ExpectSurfaceBucketDraw(cmd, world, culling, pipeline, 5u);

    deferred.Shutdown();
    culling.Shutdown();
    world.Shutdown();
}

