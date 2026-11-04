#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
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
        std::optional<RHI::GpuCullPushConstants> LastCullPushConstants;
        std::vector<std::uint32_t> IndexedDrawLimits;

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override { Events.push_back({.Kind = EventKind::BindPipeline}); }
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::PushConstants});
            if (data != nullptr && size == sizeof(RHI::GpuCullPushConstants))
            {
                RHI::GpuCullPushConstants constants{};
                std::memcpy(&constants, data, sizeof(constants));
                LastCullPushConstants = constants;
            }
        }
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t maxDrawCount) override
        {
            IndexedDrawLimits.push_back(maxDrawCount);
        }
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override { Events.push_back({.Kind = EventKind::Dispatch}); }
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle buffer, RHI::MemoryAccess before, RHI::MemoryAccess after) override
        {
            Events.push_back({.Kind = EventKind::BufferBarrier, .Buffer = buffer, .Before = before, .After = after});
        }
        void SubmitBarriers(const RHI::BarrierBatchDesc& batch) override
        {
            for (const RHI::TextureBarrierDesc& barrier : batch.TextureBarriers)
            {
                TextureBarrier(barrier.Texture, barrier.BeforeLayout, barrier.AfterLayout);
            }
            for (const RHI::BufferBarrierDesc& barrier : batch.BufferBarriers)
            {
                BufferBarrier(barrier.Buffer, barrier.BeforeAccess, barrier.AfterAccess);
            }
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
        RHI::GpuDrawBucketKind::LineQuads,
    };

    constexpr std::size_t kBucketCount = static_cast<std::size_t>(RHI::GpuDrawBucketKind::Count);
    using Access = RHI::MemoryAccess;

    // Every bucket's per-phase args and count buffers and its diagnostics buffer, in the culling system's order.
    struct CullHandles
    {
        std::array<std::array<RHI::BufferHandle, 2>, kBucketCount> Args{};
        std::array<std::array<RHI::BufferHandle, 2>, kBucketCount> Counts{};
        std::array<RHI::BufferHandle, kBucketCount> Diagnostics{};
    };

    CullHandles HandlesOf(const Graphics::CullingSystem& culling)
    {
        CullHandles handles{};
        for (std::size_t i = 0; i < kBucketCount; ++i)
        {
            const auto kind = static_cast<RHI::GpuDrawBucketKind>(i);
            for (std::size_t phase = 0; phase < 2u; ++phase)
            {
                const auto output = culling.GetBucketPhase(
                    kind, phase == 0u ? Graphics::CullingPhase::Phase1 : Graphics::CullingPhase::Phase2);
                handles.Args[i][phase] = RHI::IsIndexedDrawBucket(kind) ? output.IndexedArgsBuffer : output.NonIndexedArgsBuffer;
                handles.Counts[i][phase] = output.CountBuffer;
            }
            handles.Diagnostics[i] = culling.GetBucket(kind).DiagnosticsBuffer;
        }
        return handles;
    }

    void ExpectBarrier(const std::vector<Event>& events, std::size_t& at, const RHI::BufferHandle buffer,
                       const Access before, const Access after)
    {
        ASSERT_LT(at, events.size());
        const Event& event = events[at];
        EXPECT_EQ(event.Kind, EventKind::BufferBarrier) << "event " << at;
        EXPECT_EQ(event.Buffer, buffer) << "event " << at;
        EXPECT_EQ(event.Before, before) << "event " << at;
        EXPECT_EQ(event.After, after) << "event " << at;
        ++at;
    }

    // ResetCounters: every count and diagnostics buffer waits for the earlier frames' indirect reads and cull
    // atomics (the frames in flight share these buffers), is cleared, and is made visible to this frame's atomics.
    void ExpectReset(const std::vector<Event>& events, std::size_t& at, const CullHandles& handles)
    {
        const auto cleared = [&](const RHI::BufferHandle buffer)
        {
            ExpectBarrier(events, at, buffer, Access::IndirectRead | Access::ShaderRead | Access::ShaderWrite,
                          Access::TransferWrite);
            ASSERT_LT(at, events.size());
            EXPECT_EQ(events[at].Kind, EventKind::FillBuffer) << "event " << at;
            EXPECT_EQ(events[at].Buffer, buffer) << "event " << at;
            ++at;
            ExpectBarrier(events, at, buffer, Access::TransferWrite, Access::ShaderRead | Access::ShaderWrite);
        };
        for (std::size_t i = 0; i < kBucketCount; ++i)
        {
            cleared(handles.Counts[i][0]);
            cleared(handles.Counts[i][1]);
            cleared(handles.Diagnostics[i]);
        }
    }

    // DispatchCull: the table's barrier, every args buffer after the earlier frames' indirect reads, the dispatch,
    // and every output made visible to its readers.
    void ExpectDispatch(const std::vector<Event>& events, std::size_t& at, const CullHandles& handles,
                        const RHI::BufferHandle table)
    {
        ExpectBarrier(events, at, table, Access::TransferWrite, Access::ShaderRead);
        for (std::size_t i = 0; i < kBucketCount; ++i)
            for (std::size_t phase = 0; phase < 2u; ++phase)
                ExpectBarrier(events, at, handles.Args[i][phase], Access::IndirectRead, Access::ShaderWrite);
        ASSERT_LE(at + 3u, events.size());
        EXPECT_EQ(events[at++].Kind, EventKind::BindPipeline);
        EXPECT_EQ(events[at++].Kind, EventKind::PushConstants);
        EXPECT_EQ(events[at++].Kind, EventKind::Dispatch);
        for (std::size_t i = 0; i < kBucketCount; ++i)
        {
            for (std::size_t phase = 0; phase < 2u; ++phase)
            {
                ExpectBarrier(events, at, handles.Args[i][phase], Access::ShaderWrite, Access::IndirectRead);
                ExpectBarrier(events, at, handles.Counts[i][phase], Access::ShaderWrite, Access::IndirectRead);
            }
            ExpectBarrier(events, at, handles.Diagnostics[i], Access::ShaderWrite, Access::ShaderRead);
        }
    }

    const MockDevice::BufferWriteRecord* FindWrite(const MockDevice& device, const std::uint64_t bda)
    {
        for (auto write = device.BufferWrites.rbegin(); write != device.BufferWrites.rend(); ++write) // the latest
            if (device.GetBufferDeviceAddress(write->Handle) == bda)
                return &*write;
        return nullptr;
    }

    std::size_t BucketTableWrites(const MockDevice& device)
    {
        std::size_t writes = 0;
        for (const auto& write : device.BufferWrites)
            writes += write.Data.size() == sizeof(RHI::GpuCullBucketTable) ? 1u : 0u;
        return writes;
    }

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

static_assert(static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::Count) == 9u);
static_assert(static_cast<std::uint32_t>(RHI::GpuCullPhase::Count) == 2u);
static_assert(sizeof(RHI::GpuCullBucketOutput) == 24u);
static_assert(sizeof(RHI::GpuCullBucketDiagnosticsCounters) == 16u);
static_assert(sizeof(RHI::GpuCullBucketPhases) == 64u);
static_assert(sizeof(RHI::GpuCullBucketTable) == 576u);
static_assert(RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionSurface));
static_assert(RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionLines));
static_assert(!RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::SelectionPoints));
static_assert(!RHI::IsIndexedDrawBucket(RHI::GpuDrawBucketKind::LineQuads));
static_assert(RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SelectionSurface));
static_assert(RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SelectionLines));
static_assert(RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SelectionPoints));
static_assert(!RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::SurfaceOpaque));
static_assert(!RHI::IsSelectionDrawBucket(RHI::GpuDrawBucketKind::LineQuads));

TEST(GraphicsCullingContracts, BucketsCoverSurfaceLinePointShadowAndSelectionDomains)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    for (const auto kind : kIndexedBuckets)
    {
        const auto& bucket = culling.GetBucket(kind);
        EXPECT_TRUE(bucket.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.DiagnosticsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_GT(bucket.Capacity, 0u) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.IndexedArgsBuffer, bucket.Phase2.IndexedArgsBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.CountBuffer, bucket.Phase2.CountBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase1).IndexedArgsBuffer,
                  bucket.IndexedArgsBuffer);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase2).IndexedArgsBuffer,
                  bucket.Phase2.IndexedArgsBuffer);
    }

    for (const auto kind : kNonIndexedBuckets)
    {
        const auto& bucket = culling.GetBucket(kind);
        EXPECT_FALSE(bucket.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.IndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.DiagnosticsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_GT(bucket.Capacity, 0u) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.Phase1.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase1.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_FALSE(bucket.Phase2.Indexed) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.NonIndexedArgsBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_TRUE(bucket.Phase2.CountBuffer.IsValid()) << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.NonIndexedArgsBuffer, bucket.Phase2.NonIndexedArgsBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_NE(bucket.Phase1.CountBuffer, bucket.Phase2.CountBuffer)
            << RHI::GpuDrawBucketName(kind);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase1).NonIndexedArgsBuffer,
                  bucket.NonIndexedArgsBuffer);
        EXPECT_EQ(culling.GetBucketPhase(kind, Graphics::CullingPhase::Phase2).NonIndexedArgsBuffer,
                  bucket.Phase2.NonIndexedArgsBuffer);
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
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));
    const std::size_t writesAtInitialize = device.BufferWrites.size();

    RecordingCommandContext cmd;
    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};

    Graphics::CullingPass pass{culling};
    pass.Execute(cmd, camera, world);

    ASSERT_TRUE(cmd.LastCullPushConstants.has_value());
    EXPECT_EQ(cmd.LastCullPushConstants->CullPhase,
              static_cast<std::uint32_t>(RHI::GpuCullPhase::Phase1));
    EXPECT_NE(cmd.LastCullPushConstants->CullingFlags &
                  RHI::GpuCullFlag_SelectionBucketOcclusionExempt,
              0u);
    EXPECT_EQ(cmd.LastCullPushConstants->CullingFlags &
                  RHI::GpuCullFlag_HZBStaleSkip,
              0u);
    const auto* table = FindWrite(device, cmd.LastCullPushConstants->CullBucketTableBDA);
    ASSERT_NE(table, nullptr) << "the table is written when the buckets are allocated";
    EXPECT_EQ(device.BufferWrites.size(), writesAtInitialize) << "a frame writes no table";

    std::size_t event = 0;
    const CullHandles handles = HandlesOf(culling);
    ExpectReset(cmd.Events, event, handles);
    ExpectDispatch(cmd.Events, event, handles, table->Handle);
    EXPECT_EQ(event, cmd.Events.size());

    ASSERT_EQ(table->Offset, 0u);
    ASSERT_EQ(table->Data.size(), sizeof(RHI::GpuCullBucketTable));
    RHI::GpuCullBucketTable content{};
    std::memcpy(&content, table->Data.data(), sizeof(content));
    EXPECT_GT(content.SurfaceOpaque.Phase1.Capacity, 0u);
    EXPECT_GT(content.SurfaceOpaque.Phase2.Capacity, 0u);
    EXPECT_GT(content.SurfaceAlphaMask.Phase1.Capacity, 0u);
    EXPECT_GT(content.Lines.Phase1.Capacity, 0u);
    EXPECT_GT(content.Points.Phase1.Capacity, 0u);
    EXPECT_GT(content.ShadowOpaque.Phase1.Capacity, 0u);
    EXPECT_GT(content.SelectionSurface.Phase1.Capacity, 0u);
    EXPECT_GT(content.SelectionLines.Phase1.Capacity, 0u);
    EXPECT_GT(content.SelectionPoints.Phase1.Capacity, 0u);
    EXPECT_EQ(content.SurfaceOpaque.Phase1.ArgsBDA, device.GetBufferDeviceAddress(handles.Args[0][0]));
    EXPECT_EQ(content.SelectionPoints.Phase2.CountBDA,
              device.GetBufferDeviceAddress(
                  handles.Counts[static_cast<std::size_t>(RHI::GpuDrawBucketKind::SelectionPoints)][1]));

    culling.Shutdown();
    world.Shutdown();
}

// ARCH-022 Q79 step 7a: the bucket table holds only buffer addresses and capacities, so it is written when the buckets
// are allocated (initialization and a capacity growth), never per frame (each write waited for the graphics queue to
// go idle); a growth frame clears the new counts as well and its barriers name the new buffers.
TEST(GraphicsCullingContracts, TheBucketTableIsWrittenOncePerAllocation)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld small;
    ASSERT_TRUE(small.Initialize(device, bufferMgr, TinyWorldDesc()));
    small.SyncFrame();
    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));
    EXPECT_EQ(BucketTableWrites(device), 1u);

    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};
    Graphics::CullingPass pass{culling};
    RecordingCommandContext first;
    pass.Execute(first, camera, small);
    pass.Execute(first, camera, small);
    EXPECT_EQ(BucketTableWrites(device), 1u) << "two frames write no table";
    ASSERT_TRUE(first.LastCullPushConstants.has_value());
    const std::uint64_t firstTable = first.LastCullPushConstants->CullBucketTableBDA;

    auto desc = TinyWorldDesc();
    desc.MaxInstances = 2048u;
    Graphics::GpuWorld large;
    ASSERT_TRUE(large.Initialize(device, bufferMgr, desc));
    large.SyncFrame();
    const CullHandles before = HandlesOf(culling);
    RecordingCommandContext grown;
    pass.Execute(grown, camera, large);
    EXPECT_EQ(BucketTableWrites(device), 2u) << "the growth writes its new table";
    ASSERT_TRUE(grown.LastCullPushConstants.has_value());
    EXPECT_NE(grown.LastCullPushConstants->CullBucketTableBDA, firstTable);
    const auto* table = FindWrite(device, grown.LastCullPushConstants->CullBucketTableBDA);
    ASSERT_NE(table, nullptr);
    RHI::GpuCullBucketTable content{};
    ASSERT_EQ(table->Data.size(), sizeof(content));
    std::memcpy(&content, table->Data.data(), sizeof(content));
    EXPECT_EQ(content.SurfaceOpaque.Phase1.Capacity, 2048u);
    const CullHandles after = HandlesOf(culling);
    EXPECT_NE(after.Args[0][0], before.Args[0][0]) << "new buckets";
    EXPECT_EQ(content.SurfaceOpaque.Phase1.ArgsBDA, device.GetBufferDeviceAddress(after.Args[0][0]));
    std::size_t event = 0;
    ExpectReset(grown.Events, event, before); // the pass's reset, before the growth
    ExpectReset(grown.Events, event, after);  // the grown buckets cleared
    ExpectDispatch(grown.Events, event, after, table->Handle);
    EXPECT_EQ(event, grown.Events.size());

    RecordingCommandContext next;
    pass.Execute(next, camera, large);
    EXPECT_EQ(BucketTableWrites(device), 2u);
    event = 0;
    ExpectReset(next.Events, event, after);
    ExpectDispatch(next.Events, event, after, table->Handle);
    EXPECT_EQ(event, next.Events.size());

    culling.Shutdown();
    large.Shutdown();
    small.Shutdown();
}

// ARCH-022 Q79 step 7a: a growth that fails partway keeps the old buckets and their table together (the table is no
// longer rewritten per frame, so a half-replaced set would stay out of step with it); a later growth still succeeds.
TEST(GraphicsCullingContracts, AFailedGrowthKeepsTheBucketsAndTheirTable)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};
    Graphics::GpuWorld small;
    ASSERT_TRUE(small.Initialize(device, bufferMgr, TinyWorldDesc()));
    small.SyncFrame();
    auto desc = TinyWorldDesc();
    desc.MaxInstances = 2048u;
    Graphics::GpuWorld large;
    ASSERT_TRUE(large.Initialize(device, bufferMgr, desc));
    large.SyncFrame();
    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};
    Graphics::CullingPass pass{culling};
    RecordingCommandContext first;
    pass.Execute(first, camera, small);
    ASSERT_TRUE(first.LastCullPushConstants.has_value());
    const std::uint64_t table = first.LastCullPushConstants->CullBucketTableBDA;
    const CullHandles before = HandlesOf(culling);

    device.FailBufferCreateCall = device.CreateBufferCount + 30; // the sixth bucket's diagnostics buffer
    RecordingCommandContext failed;
    pass.Execute(failed, camera, large);
    EXPECT_FALSE(failed.LastCullPushConstants.has_value()) << "no cull without buckets for every instance";
    const CullHandles kept = HandlesOf(culling);
    EXPECT_EQ(kept.Args, before.Args);
    EXPECT_EQ(kept.Counts, before.Counts);
    EXPECT_EQ(kept.Diagnostics, before.Diagnostics);

    device.FailBufferCreateCall = 0;
    RecordingCommandContext next;
    pass.Execute(next, camera, small);
    ASSERT_TRUE(next.LastCullPushConstants.has_value());
    EXPECT_EQ(next.LastCullPushConstants->CullBucketTableBDA, table);
    const auto* write = FindWrite(device, table);
    ASSERT_NE(write, nullptr);
    RHI::GpuCullBucketTable content{};
    ASSERT_EQ(write->Data.size(), sizeof(content));
    std::memcpy(&content, write->Data.data(), sizeof(content));
    EXPECT_EQ(content.SurfaceOpaque.Phase1.ArgsBDA, device.GetBufferDeviceAddress(before.Args[0][0]));
    const auto shadow = static_cast<std::size_t>(RHI::GpuDrawBucketKind::ShadowOpaque);
    EXPECT_EQ(content.ShadowOpaque.Phase2.CountBDA, device.GetBufferDeviceAddress(before.Counts[shadow][1]));
    std::size_t event = 0;
    ExpectReset(next.Events, event, before);
    ExpectDispatch(next.Events, event, before, write->Handle);
    EXPECT_EQ(event, next.Events.size());

    RecordingCommandContext grown;
    pass.Execute(grown, camera, large);
    ASSERT_TRUE(grown.LastCullPushConstants.has_value()) << "a later growth succeeds";
    EXPECT_NE(grown.LastCullPushConstants->CullBucketTableBDA, table);

    culling.Shutdown();
    large.Shutdown();
    small.Shutdown();
}

TEST(GraphicsCullingContracts, TwoPhasePartitionIsDeterministicAndConservative)
{
    const Graphics::CullingHZBDepthSample visibleAtEqualDepth{
        .NearestDepth = 0.5f,
        .ConservativeMaxDepth = 0.5f,
        .Valid = true,
    };
    const Graphics::CullingHZBDepthSample rejectedBehindDepth{
        .NearestDepth = 0.6f,
        .ConservativeMaxDepth = 0.5f,
        .Valid = true,
    };
    const Graphics::CullingHZBDepthSample noSample{
        .NearestDepth = 0.9f,
        .ConservativeMaxDepth = 0.0f,
        .Valid = false,
    };

    EXPECT_FALSE(Graphics::HZBRejectsNearestDepth(visibleAtEqualDepth));
    EXPECT_TRUE(Graphics::HZBRejectsNearestDepth(rejectedBehindDepth));
    EXPECT_FALSE(Graphics::HZBRejectsNearestDepth(noSample));

    const std::vector<Graphics::CullingTwoPhaseCandidate> candidates{
        {
            .Bucket = RHI::GpuDrawBucketKind::SurfaceOpaque,
            .FrustumVisible = true,
            .PreviousFrameHZB = visibleAtEqualDepth,
            .CurrentFrameHZB = rejectedBehindDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::SurfaceOpaque,
            .FrustumVisible = true,
            .PreviousFrameHZB = rejectedBehindDepth,
            .CurrentFrameHZB = visibleAtEqualDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::Lines,
            .FrustumVisible = true,
            .PreviousFrameHZB = rejectedBehindDepth,
            .CurrentFrameHZB = rejectedBehindDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::Points,
            .FrustumVisible = false,
            .PreviousFrameHZB = visibleAtEqualDepth,
            .CurrentFrameHZB = visibleAtEqualDepth,
        },
        {
            .Bucket = RHI::GpuDrawBucketKind::SelectionSurface,
            .FrustumVisible = true,
            .PreviousFrameHZB = rejectedBehindDepth,
            .CurrentFrameHZB = rejectedBehindDepth,
        },
    };

    const Graphics::CullingTwoPhasePartition first =
        Graphics::ComputeTwoPhaseCullPartition(candidates);
    const Graphics::CullingTwoPhasePartition second =
        Graphics::ComputeTwoPhaseCullPartition(candidates);

    ASSERT_EQ(first.Decisions.size(), candidates.size());
    EXPECT_EQ(first.Decisions, second.Decisions);
    EXPECT_EQ(first.Decisions[0], Graphics::CullingTwoPhaseDecision::Phase1Visible);
    EXPECT_EQ(first.Decisions[1], Graphics::CullingTwoPhaseDecision::Phase2Rescued);
    EXPECT_EQ(first.Decisions[2], Graphics::CullingTwoPhaseDecision::Phase2Rejected);
    EXPECT_EQ(first.Decisions[3], Graphics::CullingTwoPhaseDecision::FrustumRejected);
    EXPECT_EQ(first.Decisions[4], Graphics::CullingTwoPhaseDecision::Phase1Visible);

    const auto& surface = first.Buckets[static_cast<std::size_t>(RHI::GpuDrawBucketKind::SurfaceOpaque)];
    EXPECT_EQ(surface.Phase1VisibleCount, 1u);
    EXPECT_EQ(surface.Phase1RejectedCount, 1u);
    EXPECT_EQ(surface.Phase2RescuedCount, 1u);

    const auto& lines = first.Buckets[static_cast<std::size_t>(RHI::GpuDrawBucketKind::Lines)];
    EXPECT_EQ(lines.Phase1VisibleCount, 0u);
    EXPECT_EQ(lines.Phase1RejectedCount, 1u);
    EXPECT_EQ(lines.Phase2RescuedCount, 0u);

    EXPECT_EQ(first.FrustumRejectedCount, 1u);
    EXPECT_EQ(first.SelectionOcclusionExemptCount, 1u);

    const auto& selection = first.Buckets[static_cast<std::size_t>(RHI::GpuDrawBucketKind::SelectionSurface)];
    EXPECT_EQ(selection.Phase1VisibleCount, 1u);
    EXPECT_EQ(selection.Phase1RejectedCount, 0u);
    EXPECT_EQ(selection.Phase2RescuedCount, 0u);
}

TEST(GraphicsCullingContracts, CameraTransitionDecisionSkipsOnExplicitFlagAndDeltaThresholds)
{
    const Graphics::CullingCameraTransitionThresholds thresholds{
        .PositionDeltaThreshold = 5.0f,
        .DirectionDotThreshold = 0.95f,
    };

    const Graphics::CullingCameraTransitionState previous{
        .Position = {0.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
    };

    const Graphics::CullingCameraTransitionState explicitCurrent{
        .Position = {0.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
        .ExplicitCameraTransition = true,
    };
    const Graphics::CullingCameraTransitionDecision explicitDecision =
        Graphics::EvaluateCameraTransition(previous, explicitCurrent, thresholds);
    EXPECT_TRUE(explicitDecision.SkipHZBPhase1);
    EXPECT_TRUE(explicitDecision.ExplicitTrigger);
    EXPECT_FALSE(explicitDecision.PositionDeltaTrigger);
    EXPECT_FALSE(explicitDecision.DirectionDeltaTrigger);

    const Graphics::CullingCameraTransitionState movedCurrent{
        .Position = {6.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
    };
    const Graphics::CullingCameraTransitionDecision movedDecision =
        Graphics::EvaluateCameraTransition(previous, movedCurrent, thresholds);
    EXPECT_TRUE(movedDecision.SkipHZBPhase1);
    EXPECT_TRUE(movedDecision.PositionDeltaTrigger);

    const Graphics::CullingCameraTransitionState rotatedCurrent{
        .Position = {0.0f, 0.0f, 0.0f},
        .Forward = {1.0f, 0.0f, 0.0f},
        .Valid = true,
    };
    const Graphics::CullingCameraTransitionDecision rotatedDecision =
        Graphics::EvaluateCameraTransition(previous, rotatedCurrent, thresholds);
    EXPECT_TRUE(rotatedDecision.SkipHZBPhase1);
    EXPECT_TRUE(rotatedDecision.DirectionDeltaTrigger);

    const Graphics::CullingCameraTransitionState stableCurrent{
        .Position = {1.0f, 0.0f, 0.0f},
        .Forward = {0.0f, 0.0f, -1.0f},
        .Valid = true,
    };
    const Graphics::CullingCameraTransitionDecision stableDecision =
        Graphics::EvaluateCameraTransition(previous, stableCurrent, thresholds);
    EXPECT_FALSE(stableDecision.SkipHZBPhase1);
}

TEST(GraphicsCullingContracts, CullingDiagnosticsCountExplicitAndDeltaHZBStaleSkips)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/instance_cull.comp"));

    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};
    camera.CameraPosition = {0.0f, 0.0f, 0.0f, 0.0f};
    camera.CameraDirection = {0.0f, 0.0f, -1.0f, 0.0f};
    camera.CullingFlags = RHI::CameraCulling_ExplicitTransition;

    RecordingCommandContext explicitCmd;
    culling.ResetCounters(explicitCmd);
    culling.DispatchCull(explicitCmd, camera, world);
    Graphics::CullingDiagnostics diagnostics = culling.GetDiagnostics();
    EXPECT_EQ(diagnostics.HzbStaleSkipCount, 1u);
    EXPECT_TRUE(diagnostics.LastHzbStaleSkip);
    EXPECT_TRUE(diagnostics.LastExplicitCameraTransition);
    ASSERT_TRUE(explicitCmd.LastCullPushConstants.has_value());
    EXPECT_NE(explicitCmd.LastCullPushConstants->CullingFlags & RHI::GpuCullFlag_HZBStaleSkip, 0u);

    camera.CullingFlags = RHI::CameraCulling_None;
    RecordingCommandContext stableCmd;
    culling.ResetCounters(stableCmd);
    culling.DispatchCull(stableCmd, camera, world);
    diagnostics = culling.GetDiagnostics();
    EXPECT_EQ(diagnostics.HzbStaleSkipCount, 1u);
    EXPECT_FALSE(diagnostics.LastHzbStaleSkip);
    ASSERT_TRUE(stableCmd.LastCullPushConstants.has_value());
    EXPECT_EQ(stableCmd.LastCullPushConstants->CullingFlags & RHI::GpuCullFlag_HZBStaleSkip, 0u);

    camera.CameraPosition = {20.0f, 0.0f, 0.0f, 0.0f};
    RecordingCommandContext deltaCmd;
    culling.ResetCounters(deltaCmd);
    culling.DispatchCull(deltaCmd, camera, world);
    diagnostics = culling.GetDiagnostics();
    EXPECT_EQ(diagnostics.HzbStaleSkipCount, 2u);
    EXPECT_TRUE(diagnostics.LastHzbStaleSkip);
    EXPECT_TRUE(diagnostics.LastCameraPositionDeltaTransition);
    EXPECT_FALSE(diagnostics.LastExplicitCameraTransition);
    ASSERT_TRUE(deltaCmd.LastCullPushConstants.has_value());
    EXPECT_NE(deltaCmd.LastCullPushConstants->CullingFlags & RHI::GpuCullFlag_HZBStaleSkip, 0u);

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsCullingContracts, OpaqueBucketDrawLimitRemainsCallerOwned)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RecordingCommandContext commands;
    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    Graphics::GpuDrawBucket bucket{};
    bucket.Indexed = true;
    bucket.IndexedArgsBuffer = RHI::BufferHandle{1u, 1u};
    bucket.CountBuffer = RHI::BufferHandle{2u, 1u};
    bucket.Capacity = 10u;
    const RHI::PipelineHandle pipeline{1u, 1u};
    Graphics::RecordOpaqueSurfaceBucket(commands, pipeline, world, bucket, 0u, 3u);
    Graphics::RecordOpaqueSurfaceBucket(commands, pipeline, world, bucket, 0u);
    EXPECT_EQ(commands.IndexedDrawLimits, (std::vector<std::uint32_t>{3u, 10u}));
    bucket.Indexed = false;
    Graphics::RecordOpaqueSurfaceBucket(commands, pipeline, world, bucket, 0u);
    EXPECT_EQ(commands.IndexedDrawLimits.size(), 2u);
    world.Shutdown();
}
