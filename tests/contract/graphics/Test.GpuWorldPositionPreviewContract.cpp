// GRAPHICS-156 (ADR 0030 decision 5): a GPU method's position ring front is shown by
// GpuWorld through a copy (or a seam gather) into the geometry block at the culling head.
// The block's CPU shadow is stale from then on: compaction and rebuild replay skip the
// position range, a cleared preview leaves the shadow stale until the next position upload,
// and that upload makes the CPU bytes authoritative again.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.SceneHandles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
    namespace Graphics = Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;
    using Extrinsic::Tests::MockCommandContext;
    using Extrinsic::Tests::MockDevice;
    using Status = Graphics::GpuWorld::GeometryPositionPreviewStatus;

    struct PackedVertex
    {
        float Px, Py, Pz;
        float U, V;
    };

    constexpr std::array<PackedVertex, 3> kTriangleVerts{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        {0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.5f, 0.0f, 0.5f, 1.0f},
    }};
    constexpr std::array<std::uint32_t, 3> kTriangleIndices{{0u, 1u, 2u}};
    constexpr std::uint64_t kPositionBytes = kTriangleVerts.size() * sizeof(float) * 3u;

    [[nodiscard]] Graphics::GpuWorld::GeometryUploadDesc TriangleUpload()
    {
        Graphics::GpuWorld::GeometryUploadDesc desc{};
        desc.PackedVertexBytes = std::as_bytes(std::span<const PackedVertex>{kTriangleVerts});
        desc.SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices};
        desc.VertexCount = static_cast<std::uint32_t>(kTriangleVerts.size());
        desc.DebugName = "position-preview-triangle";
        return desc;
    }

    struct Fixture
    {
        MockDevice Device{};
        RHI::BufferManager Buffers{Device};
        Graphics::GpuWorld World{};
        MockCommandContext Commands{};
        // A ring front the residency would hand out: any valid device buffer.
        RHI::BufferHandle Front{};

        Fixture()
        {
            Graphics::GpuWorld::InitDesc init{};
            init.MaxInstances = 2u;
            init.MaxGeometryRecords = 4u;
            init.MaxLights = 1u;
            init.DeferredFreeFrames = 0u;
            init.VertexBufferBytes = 16u * 1024u;
            init.IndexBufferBytes = 16u * 1024u;
            EXPECT_TRUE(World.Initialize(Device, Buffers, init));
            Front = Device.CreateBuffer(RHI::BufferDesc{.SizeBytes = kPositionBytes});
            EXPECT_TRUE(Front.IsValid());
        }

        [[nodiscard]] Graphics::GpuWorld::GeometryPositionPreviewDesc Preview(
            const std::uint64_t stamp,
            const std::span<const std::uint32_t> gather = {},
            const std::uint64_t gatherStamp = 0u) const
        {
            return Graphics::GpuWorld::GeometryPositionPreviewDesc{
                .Source = Front,
                .SourceOffsetBytes = 0u,
                .SourceRowCount = static_cast<std::uint32_t>(kTriangleVerts.size()),
                .Stamp = stamp,
                .GatherMap = gather,
                .GatherStamp = gatherStamp,
            };
        }

        [[nodiscard]] Graphics::GpuGeometryResidencyView View(const Graphics::GpuGeometryHandle geometry)
        {
            Graphics::GpuGeometryResidencyView view{};
            EXPECT_TRUE(World.TryGetGeometryResidencyView(geometry, view));
            return view;
        }

        [[nodiscard]] std::uint64_t BlockPositionOffset(const Graphics::GpuGeometryHandle geometry)
        {
            const auto view = View(geometry);
            const std::uint64_t base =
                Device.GetBufferDeviceAddress(World.GetManagedVertexBuffer());
            return view.Record.VertexBufferBDA - base;
        }

        // Writes into the managed vertex buffer that overlap [offset, offset + bytes).
        [[nodiscard]] std::size_t VertexWritesOverlapping(const std::uint64_t offset,
                                                          const std::uint64_t bytes,
                                                          const std::size_t firstRecord = 0u) const
        {
            std::size_t count = 0u;
            for (std::size_t i = firstRecord; i < Device.BufferWrites.size(); ++i)
            {
                const auto& write = Device.BufferWrites[i];
                if (write.Handle != World.GetManagedVertexBuffer())
                    continue;
                const std::uint64_t end = write.Offset + write.Data.size();
                if (write.Offset < offset + bytes && end > offset)
                    ++count;
            }
            return count;
        }

        [[nodiscard]] bool HasBarrier(const RHI::BufferHandle buffer,
                                      const RHI::MemoryAccess before,
                                      const RHI::MemoryAccess after) const
        {
            return std::ranges::any_of(Commands.BufferBarrierCalls, [&](const auto& record) {
                return record.Buffer == buffer && record.Before == before && record.After == after;
            });
        }
    };
}

TEST(GpuWorldPositionPreviewContract, CopiesTheFrontOnceAtTheCullingHeadAndMarksTheShadowStale)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);

    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);

    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    const auto& copy = f.Commands.CopyBufferRecords.front();
    EXPECT_EQ(copy.Src, f.Front);
    EXPECT_EQ(copy.Dst, f.World.GetManagedVertexBuffer());
    EXPECT_EQ(copy.SrcOffset, 0u);
    EXPECT_EQ(copy.DstOffset, f.BlockPositionOffset(geometry));
    EXPECT_EQ(copy.Size, kPositionBytes);
    // compute-write -> transfer-read on the front, transfer-write -> shader-read on the block.
    EXPECT_TRUE(f.HasBarrier(f.Front,
                             RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferWrite,
                             RHI::MemoryAccess::TransferRead));
    EXPECT_TRUE(f.HasBarrier(f.World.GetManagedVertexBuffer(),
                             RHI::MemoryAccess::TransferWrite,
                             RHI::MemoryAccess::ShaderRead));
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);

    // The same front is not copied again; a new stamp is.
    f.World.SetGeometryPositionPreview(geometry, f.Preview(1u));
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    f.World.SetGeometryPositionPreview(geometry, f.Preview(2u));
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 2u);

    // Clear alone keeps the shadow stale; the next position upload restores its authority.
    f.World.ClearGeometryPositionPreview(geometry);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 2u);
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);
    const auto update = f.World.UpdateGeometryChannels(
        geometry, TriangleUpload(), Graphics::GpuWorld::GeometryChannelUpdateMask{.Position = true});
    ASSERT_TRUE(update.Succeeded());
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);
}

TEST(GpuWorldPositionPreviewContract, APositionUploadDuringThePreviewIsCopiedOverAgain)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);

    // A concurrent CPU edit uploads its bytes; the observed front wins at the next head.
    ASSERT_TRUE(f.World.UpdateGeometryChannels(
        geometry, TriangleUpload(), Graphics::GpuWorld::GeometryChannelUpdateMask{.Position = true}).Succeeded());
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);
    f.World.SetGeometryPositionPreview(geometry, f.Preview(1u));
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 2u);
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);
}

TEST(GpuWorldPositionPreviewContract, CompactionDoesNotReplayAStaleShadowAndRecopiesTheFront)
{
    Fixture f;
    const auto a = f.World.UploadGeometry(TriangleUpload());
    const auto b = f.World.UploadGeometry(TriangleUpload());
    const auto c = f.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(a.IsValid() && b.IsValid() && c.IsValid());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(c, f.Preview(1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    ASSERT_TRUE(f.View(c).PositionShadowStale);

    f.World.FreeGeometry(b);
    f.World.SyncFrame();
    const auto plan = f.World.PlanManagedBufferCompaction();
    ASSERT_TRUE(plan.ShouldCompact);
    ASSERT_EQ(plan.Relocations.size(), 1u);
    ASSERT_EQ(plan.Relocations[0].Geometry, c);
    const auto viewBefore = f.View(c);
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    const auto result = f.World.ApplyManagedBufferCompaction(plan);
    ASSERT_TRUE(result.Applied);

    // The relocated block replays its texcoords, not its stale position range.
    const std::uint64_t newPositionOffset = f.BlockPositionOffset(c);
    EXPECT_EQ(newPositionOffset, plan.Relocations[0].NewVertexByteOffset);
    EXPECT_EQ(f.VertexWritesOverlapping(newPositionOffset, viewBefore.PositionByteCount, writesBefore), 0u);
    EXPECT_EQ(f.VertexWritesOverlapping(newPositionOffset + viewBefore.PositionByteCount,
                                        viewBefore.TexcoordByteCount, writesBefore), 1u);
    EXPECT_TRUE(f.View(c).PositionShadowStale);

    // The rewritten block gets the front again, at its new offset, without a stamp change.
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 2u);
    EXPECT_EQ(f.Commands.CopyBufferRecords.back().DstOffset, newPositionOffset);
    EXPECT_EQ(f.Commands.CopyBufferRecords.back().Size, kPositionBytes);
}

TEST(GpuWorldPositionPreviewContract, RebuildDoesNotReplayAStaleShadow)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    const auto view = f.View(geometry);
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    ASSERT_TRUE(f.World.RebuildGpuResources(f.Device, f.Buffers));
    const std::uint64_t positionOffset = f.BlockPositionOffset(geometry);
    EXPECT_EQ(f.VertexWritesOverlapping(positionOffset, view.PositionByteCount, writesBefore), 0u);
    EXPECT_EQ(f.VertexWritesOverlapping(positionOffset + view.PositionByteCount,
                                        view.TexcoordByteCount, writesBefore), 1u);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 2u);
}

TEST(GpuWorldPositionPreviewContract, SeamSplitBlocksGatherThroughTheUploadedRemap)
{
    Fixture f;
    // Four GPU vertices over three source rows (a split vertex duplicates row 0).
    constexpr std::array<PackedVertex, 4> kSplit{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        {0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.5f, 0.0f, 0.5f, 1.0f},
        {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
    }};
    constexpr std::array<std::uint32_t, 6> kSplitIndices{{0u, 1u, 2u, 3u, 1u, 2u}};
    Graphics::GpuWorld::GeometryUploadDesc splitDesc{};
    splitDesc.PackedVertexBytes = std::as_bytes(std::span<const PackedVertex>{kSplit});
    splitDesc.SurfaceIndices = std::span<const std::uint32_t>{kSplitIndices};
    splitDesc.VertexCount = 4u;
    splitDesc.DebugName = "position-preview-split";
    const auto geometry = f.World.UploadGeometry(splitDesc);
    ASSERT_TRUE(geometry.IsValid());
    constexpr std::array<std::uint32_t, 4> kMap{{0u, 1u, 2u, 0u}};
    constexpr std::array<std::uint32_t, 4> kBadMap{{0u, 1u, 2u, 3u}};
    EXPECT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u)), Status::InvalidInput)
        << "a 1:1 copy needs as many rows as GPU vertices";
    EXPECT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, kBadMap, 1u)), Status::InvalidInput)
        << "a map row outside the front is refused";
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, kMap, 1u)), Status::Accepted);
    // The remap was uploaded once (16 bytes), not to the managed vertex buffer.
    ASSERT_EQ(f.Device.BufferWrites.size(), writesBefore + 1u);
    const auto& mapWrite = f.Device.BufferWrites.back();
    EXPECT_NE(mapWrite.Handle, f.World.GetManagedVertexBuffer());
    EXPECT_EQ(mapWrite.Data.size(), kMap.size() * sizeof(std::uint32_t));

    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_TRUE(f.Commands.CopyBufferRecords.empty());
    ASSERT_EQ(f.Commands.DispatchRecords.size(), 1u);
    EXPECT_EQ(f.Commands.DispatchRecords.front().X, 1u);
    ASSERT_EQ(f.Commands.PushConstantSizes.size(), 1u);
    EXPECT_EQ(f.Commands.PushConstantSizes.front(), 32u);
    EXPECT_TRUE(f.HasBarrier(f.Front,
                             RHI::MemoryAccess::ShaderWrite | RHI::MemoryAccess::TransferWrite,
                             RHI::MemoryAccess::ShaderRead));
    EXPECT_TRUE(f.HasBarrier(mapWrite.Handle, RHI::MemoryAccess::TransferWrite, RHI::MemoryAccess::ShaderRead));
    EXPECT_TRUE(f.HasBarrier(f.World.GetManagedVertexBuffer(),
                             RHI::MemoryAccess::ShaderWrite,
                             RHI::MemoryAccess::ShaderRead));
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);

    // The same remap revision is not uploaded again; a new one is, and gathers again.
    f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, kMap, 1u));
    EXPECT_EQ(f.Device.BufferWrites.size(), writesBefore + 1u);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.DispatchRecords.size(), 1u);
    f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, kMap, 2u));
    EXPECT_EQ(f.Device.BufferWrites.size(), writesBefore + 2u);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.DispatchRecords.size(), 2u);
}

TEST(GpuWorldPositionPreviewContract, FreedAndInvalidGeometryRefuseOrDropThePreview)
{
    Fixture f;
    EXPECT_EQ(f.World.SetGeometryPositionPreview({}, f.Preview(1u)), Status::InvalidHandle);
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u)), Status::Accepted);
    f.World.FreeGeometry(geometry);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_TRUE(f.Commands.CopyBufferRecords.empty());
    EXPECT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(2u)), Status::InvalidHandle);
}

namespace
{
    // A seam-split block: four GPU vertices over three source rows (row 0 is duplicated).
    struct SplitFixture : Fixture
    {
        static constexpr std::array<PackedVertex, 4> kSplit{{
            {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
            {0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
            {0.0f, 0.5f, 0.0f, 0.5f, 1.0f},
            {-0.5f, -0.5f, 0.0f, 0.0f, 1.0f},
        }};
        static constexpr std::array<std::uint32_t, 6> kSplitIndices{{0u, 1u, 2u, 3u, 1u, 2u}};
        static constexpr std::array<std::uint32_t, 4> kMap{{0u, 1u, 2u, 0u}};

        [[nodiscard]] Graphics::GpuGeometryHandle UploadSplit()
        {
            Graphics::GpuWorld::GeometryUploadDesc desc{};
            desc.PackedVertexBytes = std::as_bytes(std::span<const PackedVertex>{kSplit});
            desc.SurfaceIndices = std::span<const std::uint32_t>{kSplitIndices};
            desc.VertexCount = 4u;
            desc.DebugName = "position-preview-split";
            return World.UploadGeometry(desc);
        }
        // Writes that are not to the managed vertex buffer (gather map uploads).
        [[nodiscard]] std::size_t MapWrites(const std::size_t firstRecord) const
        {
            std::size_t count = 0u;
            for (std::size_t i = firstRecord; i < Device.BufferWrites.size(); ++i)
                if (Device.BufferWrites[i].Handle != World.GetManagedVertexBuffer() &&
                    Device.BufferWrites[i].Data.size() == kMap.size() * sizeof(std::uint32_t))
                    ++count;
            return count;
        }
    };
}

TEST(GpuWorldPositionPreviewContract, ASeamSplitPreviewSurvivesARebuildWithoutAnotherSet)
{
    SplitFixture f;
    const auto geometry = f.UploadSplit();
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, SplitFixture::kMap, 1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.DispatchRecords.size(), 1u);
    ASSERT_TRUE(f.View(geometry).PositionShadowStale);
    const auto view = f.View(geometry);

    // Rebuild frees every device buffer, including the gather map: the map is uploaded
    // again, the stale position range is not replayed, and the next culling head gathers
    // into the new block although extraction has not refreshed the preview.
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    ASSERT_TRUE(f.World.RebuildGpuResources(f.Device, f.Buffers));
    EXPECT_EQ(f.MapWrites(writesBefore), 1u) << "the gather map is re-uploaded after a rebuild";
    const std::uint64_t positionOffset = f.BlockPositionOffset(geometry);
    EXPECT_EQ(f.VertexWritesOverlapping(positionOffset, view.PositionByteCount, writesBefore), 0u);
    EXPECT_EQ(f.VertexWritesOverlapping(positionOffset + view.PositionByteCount, view.TexcoordByteCount, writesBefore), 1u);
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.DispatchRecords.size(), 2u) << "the rebuilt block is gathered again";
    EXPECT_TRUE(f.Commands.CopyBufferRecords.empty());
}

TEST(GpuWorldPositionPreviewContract, ReplayRestoresTheShadowWhenNoPreviewCanRewriteTheRange)
{
    // A cleared preview leaves a stale shadow; a later compaction must not leave the
    // relocated position range undefined, so the shadow is replayed and becomes current.
    Fixture f;
    const auto a = f.World.UploadGeometry(TriangleUpload());
    const auto b = f.World.UploadGeometry(TriangleUpload());
    const auto c = f.World.UploadGeometry(TriangleUpload());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(c, f.Preview(1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_TRUE(f.View(c).PositionShadowStale);
    f.World.ClearGeometryPositionPreview(c);
    EXPECT_TRUE(f.View(c).PositionShadowStale);

    f.World.FreeGeometry(b);
    f.World.SyncFrame();
    const auto plan = f.World.PlanManagedBufferCompaction();
    ASSERT_TRUE(plan.ShouldCompact);
    const auto view = f.View(c);
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    ASSERT_TRUE(f.World.ApplyManagedBufferCompaction(plan).Applied);
    const std::uint64_t positionOffset = f.BlockPositionOffset(c);
    EXPECT_EQ(f.VertexWritesOverlapping(positionOffset, view.PositionByteCount, writesBefore), 1u)
        << "without a preview the whole shadow is replayed";
    EXPECT_FALSE(f.View(c).PositionShadowStale);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    (void)a;
}

TEST(GpuWorldPositionPreviewContract, ARefusedGatherMapUploadIsRetriedAndNeverStrandsTheBlock)
{
    SplitFixture f;
    const auto geometry = f.UploadSplit();
    f.Device.FailNextBufferCreate = true;
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, SplitFixture::kMap, 1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_TRUE(f.Commands.DispatchRecords.empty()) << "no map, no gather";
    EXPECT_FALSE(f.View(geometry).PositionShadowStale) << "the block still holds its CPU bytes";
    // The next extraction refreshes the preview: the map upload is retried.
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, SplitFixture::kMap, 1u)), Status::Accepted);
    EXPECT_EQ(f.MapWrites(writesBefore), 1u);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.DispatchRecords.size(), 1u);
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);
}

TEST(GpuWorldPositionPreviewContract, AStaleHandleDoesNotClearTheReusingSlotsPreview)
{
    Fixture f;
    const auto a = f.World.UploadGeometry(TriangleUpload());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(a, f.Preview(1u)), Status::Accepted);
    f.World.FreeGeometry(a);
    f.World.SyncFrame(); // DeferredFreeFrames is 0: the slot is reusable now
    const auto b = f.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(b.IsValid());
    ASSERT_EQ(b.Index, a.Index) << "the freed slot is reused";
    ASSERT_NE(b.Generation, a.Generation);
    ASSERT_EQ(f.World.SetGeometryPositionPreview(b, f.Preview(2u)), Status::Accepted);

    f.World.ClearGeometryPositionPreview(a); // stale: must not touch b's preview
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u) << "b's preview survived the stale clear";
    EXPECT_EQ(f.Commands.CopyBufferRecords.front().DstOffset, f.BlockPositionOffset(b));
    EXPECT_TRUE(f.View(b).PositionShadowStale);

    f.World.ClearGeometryPositionPreview(b);
    f.World.SetGeometryPositionPreview(b, f.Preview(3u));
    f.World.ClearGeometryPositionPreview(b);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 1u) << "a live handle clears its own preview";
}

TEST(GpuWorldPositionPreviewContract, AFailedGatherPipelineAfterARebuildReplaysTheShadow)
{
    SplitFixture f;
    const auto geometry = f.UploadSplit();
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u, SplitFixture::kMap, 1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.DispatchRecords.size(), 1u);
    ASSERT_TRUE(f.View(geometry).PositionShadowStale);
    const auto view = f.View(geometry);

    // The rebuild destroys the gather pipeline; its recreation fails, so the preview is
    // not recordable: the replay writes the position shadow instead of leaving the range
    // to a gather that cannot run.
    f.Device.FailNextPipelineCreate = true;
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    ASSERT_TRUE(f.World.RebuildGpuResources(f.Device, f.Buffers));
    const std::uint64_t positionOffset = f.BlockPositionOffset(geometry);
    EXPECT_EQ(f.VertexWritesOverlapping(positionOffset, view.PositionByteCount, writesBefore), 1u)
        << "without a gather pipeline the shadow is replayed";
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);
    EXPECT_FALSE(f.Device.FailNextPipelineCreate) << "the pipeline creation was attempted";

    // Once the pipeline can be created again the block is gathered at the next head.
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.DispatchRecords.size(), 2u);
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);
}
