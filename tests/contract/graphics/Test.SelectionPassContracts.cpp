#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Selection.EdgeId;
import Extrinsic.Graphics.Pass.Selection.EntityId;
import Extrinsic.Graphics.Pass.Selection.FaceId;
import Extrinsic.Graphics.Pass.Selection.Outline;
import Extrinsic.Graphics.Pass.Selection.PointId;
import Extrinsic.Graphics.SelectionSystem;
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
        Draw,
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
        std::uint32_t LastVertexCount = 0;
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
        void Draw(std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::Draw});
            LastVertexCount = vertexCount;
        }
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

TEST(GraphicsSelectionPassContracts, EntityAndFaceIdPassesDrawSurfaceBucket)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));
    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);

    Graphics::SelectionSystem selection;
    Graphics::EntityIdPass entityPass{selection};
    entityPass.SetPipeline(RHI::PipelineHandle{701u, 1u});
    RecordingCommandContext skipped;
    RHI::CameraUBO camera{};
    entityPass.Execute(skipped, camera, world, culling, 3u);
    EXPECT_TRUE(skipped.Events.empty());

    selection.Initialize();
    RecordingCommandContext entityCmd;
    entityPass.Execute(entityCmd, camera, world, culling, 3u);
    ASSERT_EQ(entityCmd.Events.size(), 4u);
    EXPECT_EQ(entityCmd.LastPipeline, (RHI::PipelineHandle{701u, 1u}));
    EXPECT_EQ(entityCmd.LastIndexBuffer, world.GetManagedIndexBuffer());
    EXPECT_EQ(entityCmd.LastIndirectArgs, bucket.IndexedArgsBuffer);
    EXPECT_EQ(entityCmd.LastIndirectCount, bucket.CountBuffer);
    EXPECT_EQ(entityCmd.LastMaxDrawCount, bucket.Capacity);
    ExpectScenePushConstants(entityCmd, world, RHI::GpuDrawBucketKind::SurfaceOpaque, 3u);

    Graphics::FaceIdPass facePass{selection};
    facePass.SetPipeline(RHI::PipelineHandle{702u, 1u});
    RecordingCommandContext faceCmd;
    facePass.Execute(faceCmd, camera, world, culling, 4u);
    ASSERT_EQ(faceCmd.Events.size(), 4u);
    EXPECT_EQ(faceCmd.LastPipeline, (RHI::PipelineHandle{702u, 1u}));
    EXPECT_EQ(faceCmd.LastIndirectArgs, bucket.IndexedArgsBuffer);
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
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));
    Graphics::SelectionSystem selection;
    selection.Initialize();
    RHI::CameraUBO camera{};

    Graphics::EdgeIdPass edgePass{selection};
    edgePass.SetPipeline(RHI::PipelineHandle{703u, 1u});
    RecordingCommandContext edgeCmd;
    edgePass.Execute(edgeCmd, camera, world, culling, 5u);
    const auto& lineBucket = culling.GetBucket(RHI::GpuDrawBucketKind::Lines);
    ASSERT_EQ(edgeCmd.Events.size(), 4u);
    EXPECT_EQ(edgeCmd.Events[3].Kind, EventKind::DrawIndexedIndirectCount);
    EXPECT_EQ(edgeCmd.LastIndirectArgs, lineBucket.IndexedArgsBuffer);
    ExpectScenePushConstants(edgeCmd, world, RHI::GpuDrawBucketKind::Lines, 5u);

    Graphics::PointIdPass pointPass{selection};
    pointPass.SetPipeline(RHI::PipelineHandle{704u, 1u});
    RecordingCommandContext pointCmd;
    pointPass.Execute(pointCmd, camera, world, culling, 6u);
    const auto& pointBucket = culling.GetBucket(RHI::GpuDrawBucketKind::Points);
    ASSERT_EQ(pointCmd.Events.size(), 3u);
    EXPECT_EQ(pointCmd.Events[2].Kind, EventKind::DrawIndirectCount);
    EXPECT_EQ(pointCmd.LastIndirectArgs, pointBucket.NonIndexedArgsBuffer);
    ExpectScenePushConstants(pointCmd, world, RHI::GpuDrawBucketKind::Points, 6u);

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsSelectionPassContracts, SelectionOutlinePassDrawsFullscreenTriangle)
{
    Graphics::SelectionSystem selection;
    Graphics::SelectionOutlinePass pass{selection};
    pass.SetPipeline(RHI::PipelineHandle{705u, 1u});
    RHI::CameraUBO camera{};

    RecordingCommandContext skipped;
    pass.Execute(skipped, camera, 7u);
    EXPECT_TRUE(skipped.Events.empty());

    selection.Initialize();
    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, 7u);
    ASSERT_EQ(cmd.Events.size(), 2u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::Draw);
    EXPECT_EQ(cmd.LastPipeline, (RHI::PipelineHandle{705u, 1u}));
    EXPECT_EQ(cmd.LastVertexCount, 3u);
}

