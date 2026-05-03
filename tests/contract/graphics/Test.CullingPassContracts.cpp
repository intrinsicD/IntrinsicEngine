#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Pass.Culling;
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
        FillBuffer,
        BufferBarrier,
        BindPipeline,
        PushConstants,
        Dispatch,
    };

    struct Event
    {
        EventKind Kind{};
        RHI::BufferHandle Buffer{};
        RHI::MemoryAccess Before = RHI::MemoryAccess::None;
        RHI::MemoryAccess After = RHI::MemoryAccess::None;
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events;

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override { Events.push_back({.Kind = EventKind::BindPipeline}); }
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override { Events.push_back({.Kind = EventKind::PushConstants}); }
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override { Events.push_back({.Kind = EventKind::Dispatch}); }
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle buffer, RHI::MemoryAccess before, RHI::MemoryAccess after) override
        {
            Events.push_back({.Kind = EventKind::BufferBarrier, .Buffer = buffer, .Before = before, .After = after});
        }
        void FillBuffer(RHI::BufferHandle buffer, std::uint64_t, std::uint64_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::FillBuffer, .Buffer = buffer});
        }
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    constexpr std::array kIndexedBuckets{
        RHI::GpuDrawBucketKind::SurfaceOpaque,
        RHI::GpuDrawBucketKind::SurfaceAlphaMask,
        RHI::GpuDrawBucketKind::Lines,
        RHI::GpuDrawBucketKind::ShadowOpaque,
        RHI::GpuDrawBucketKind::SelectionSurface,
        RHI::GpuDrawBucketKind::SelectionLines,
    };

    constexpr std::array kNonIndexedBuckets{
        RHI::GpuDrawBucketKind::Points,
        RHI::GpuDrawBucketKind::SelectionPoints,
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
}

static_assert(static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::Count) == 8u);
static_assert(sizeof(RHI::GpuCullBucketTable) == 192u);
static_assert(RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionSurface));
static_assert(RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionLines));
static_assert(!RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionPoints));

TEST(GraphicsCullingContracts, BucketsCoverSurfaceLinePointShadowAndSelectionDomains)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::CullingSystem culling;
    culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp");

    for (const auto kind : kIndexedBuckets)
    {
        const auto& bucket = culling.GetBucket(kind);
        EXPECT_TRUE(bucket.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_GT(bucket.Capacity, 0u) << RHI::GpuDrawBucketName(kind);
    }

    for (const auto kind : kNonIndexedBuckets)
    {
        const auto& bucket = culling.GetBucket(kind);
        EXPECT_FALSE(bucket.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_GT(bucket.Capacity, 0u) << RHI::GpuDrawBucketName(kind);
    }

    culling.Shutdown();
}

TEST(GraphicsCullingContracts, CullingPassResetsDispatchesAndPublishesAllBucketMetadata)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp");

    RecordingCommandContext cmd;
    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};

    Graphics::CullingPass pass{culling};
    pass.Execute(cmd, camera, world);

    constexpr std::size_t bucketCount = static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count);
    const std::size_t expectedEvents = (bucketCount * 2u) + 1u + 3u + (bucketCount * 2u);
    ASSERT_EQ(cmd.Events.size(), expectedEvents);

    std::size_t event = 0;
    for (std::size_t i = 0; i < bucketCount; ++i)
    {
        EXPECT_EQ(cmd.Events[event].Kind, EventKind::FillBuffer);
        EXPECT_TRUE(cmd.Events[event].Buffer.IsValid());
        ++event;

        EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
        EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::TransferWrite);
        EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::ShaderWrite);
        ++event;
    }

    EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
    EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::ShaderRead);
    ++event;

    EXPECT_EQ(cmd.Events[event++].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[event++].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[event++].Kind, EventKind::Dispatch);

    for (std::size_t i = 0; i < bucketCount; ++i)
    {
        EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
        EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::ShaderWrite);
        EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::IndirectRead);
        ++event;

        EXPECT_EQ(cmd.Events[event].Kind, EventKind::BufferBarrier);
        EXPECT_EQ(cmd.Events[event].Before, RHI::MemoryAccess::ShaderWrite);
        EXPECT_EQ(cmd.Events[event].After, RHI::MemoryAccess::IndirectRead);
        ++event;
    }
    EXPECT_EQ(event, cmd.Events.size());

    bool sawBucketTableWrite = false;
    for (const auto& write : device.BufferWrites)
    {
        if (write.Data.size() != sizeof(RHI::GpuCullBucketTable))
        {
            continue;
        }

        RHI::GpuCullBucketTable table{};
        std::memcpy(&table, write.Data.data(), sizeof(table));
        EXPECT_GT(table.SurfaceOpaque.Capacity, 0u);
        EXPECT_GT(table.SurfaceAlphaMask.Capacity, 0u);
        EXPECT_GT(table.Lines.Capacity, 0u);
        EXPECT_GT(table.Points.Capacity, 0u);
        EXPECT_GT(table.ShadowOpaque.Capacity, 0u);
        EXPECT_GT(table.SelectionSurface.Capacity, 0u);
        EXPECT_GT(table.SelectionLines.Capacity, 0u);
        EXPECT_GT(table.SelectionPoints.Capacity, 0u);
        sawBucketTableWrite = true;
    }
    EXPECT_TRUE(sawBucketTableWrite);

    culling.Shutdown();
    world.Shutdown();
}

