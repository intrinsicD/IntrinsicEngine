// GRAPHICS-156 (ADR 0030 decision 5): a GPU method's position ring front is shown by
// GpuWorld through a copy (or a seam gather) into the geometry block at the culling head.
// The block's CPU shadow is stale from then on: compaction and rebuild replay skip the
// position range, a cleared preview leaves the shadow stale until the next position upload,
// and that upload makes the CPU bytes authoritative again.
#include <algorithm>
#include <array>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <memory>
#include <gtest/gtest.h>

import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.SceneHandles;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

#include "GeometryResidencyFingerprint.hpp"
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

// RUNTIME-293 (ADR 0030 decision 6): Accept of GPU-authored positions patches only the
// position range of the shadow, keeps the copied front in the block (no upload), and ends
// the preview; a block that does not hold the front copies it once at the next culling head;
// a CPU position upload supersedes that pending copy.
namespace
{
    constexpr std::array<PackedVertex, 3> kShiftedVerts{{
        {0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        {1.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        {1.0f, 0.5f, 0.0f, 0.5f, 1.0f},
    }};

    [[nodiscard]] std::vector<std::byte> ShiftedPositionBytes()
    {
        std::vector<std::byte> bytes(kPositionBytes);
        for (std::size_t i = 0; i < kShiftedVerts.size(); ++i)
        {
            const float p[3]{kShiftedVerts[i].Px, kShiftedVerts[i].Py, kShiftedVerts[i].Pz};
            std::memcpy(bytes.data() + i * sizeof(p), p, sizeof(p));
        }
        return bytes;
    }

    [[nodiscard]] std::uint64_t ShiftedFingerprint()
    {
        return Extrinsic::Tests::GeometryFloat32Fingerprint(
            {kShiftedVerts[0].Px, kShiftedVerts[0].Py, kShiftedVerts[0].Pz, kShiftedVerts[1].Px, kShiftedVerts[1].Py,
             kShiftedVerts[1].Pz, kShiftedVerts[2].Px, kShiftedVerts[2].Py, kShiftedVerts[2].Pz});
    }

    [[nodiscard]] Graphics::GpuWorld::GeometryPositionCommitDesc Commit(const Fixture& f,
                                                                       const std::span<const std::byte> bytes,
                                                                       const std::uint64_t stamp)
    {
        return Graphics::GpuWorld::GeometryPositionCommitDesc{
            .PositionBytes = bytes,
            .Source = f.Front,
            .SourceOffsetBytes = 0u,
            .SourceRowCount = static_cast<std::uint32_t>(kTriangleVerts.size()),
            .Stamp = stamp,
        };
    }
    using CommitStatus = Graphics::GpuWorld::GeometryPositionCommitStatus;
}

TEST(GpuWorldPositionCommitContract, CommitOfACopiedFrontPatchesOnlyThePositionShadowWithoutAnUpload)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    ASSERT_TRUE(geometry.IsValid());
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(1u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    const auto before = f.View(geometry);
    ASSERT_TRUE(before.PositionShadowStale);
    const std::size_t writesBefore = f.Device.BufferWrites.size();

    const auto bytes = ShiftedPositionBytes();
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, Commit(f, bytes, 1u)), CommitStatus::Committed);
    const auto after = f.View(geometry);
    EXPECT_FALSE(after.PositionShadowStale);
    EXPECT_EQ(after.PositionFingerprint, ShiftedFingerprint());
    EXPECT_EQ(after.TexcoordFingerprint, before.TexcoordFingerprint) << "other channels are untouched";
    EXPECT_EQ(after.TexcoordByteCount, before.TexcoordByteCount);
    EXPECT_NE(after.ContentRevision, before.ContentRevision);
    EXPECT_EQ(f.Device.BufferWrites.size(), writesBefore) << "nothing is uploaded";

    // The preview is over: no further copy, and a later position upload behaves as usual.
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    EXPECT_EQ(f.Device.BufferWrites.size(), writesBefore);
    // The patched shadow is what a rebuild replays.
    const std::size_t rebuildFrom = f.Device.BufferWrites.size();
    ASSERT_TRUE(f.World.RebuildGpuResources(f.Device, f.Buffers));
    EXPECT_EQ(f.VertexWritesOverlapping(f.BlockPositionOffset(geometry), before.PositionByteCount, rebuildFrom), 1u);
    bool replayedShifted = false;
    for (std::size_t i = rebuildFrom; i < f.Device.BufferWrites.size(); ++i)
    {
        const auto& write = f.Device.BufferWrites[i];
        if (write.Handle != f.World.GetManagedVertexBuffer() || write.Data.size() < bytes.size()) continue;
        replayedShifted |= std::equal(bytes.begin(), bytes.end(), write.Data.begin());
    }
    EXPECT_TRUE(replayedShifted);
}

TEST(GpuWorldPositionCommitContract, CommitWithoutACopiedFrontCopiesItOnceThenEndsThePreview)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    const auto bytes = ShiftedPositionBytes();
    const std::size_t writesBefore = f.Device.BufferWrites.size();
    // Never previewed (an automatic Accept before any extraction): the block is stale until
    // the copy lands.
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, Commit(f, bytes, 7u)), CommitStatus::CopyPending);
    EXPECT_TRUE(f.View(geometry).PositionShadowStale);
    EXPECT_EQ(f.View(geometry).PositionFingerprint, ShiftedFingerprint());
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    EXPECT_EQ(f.Commands.CopyBufferRecords.back().Src, f.Front);
    EXPECT_EQ(f.Commands.CopyBufferRecords.back().DstOffset, f.BlockPositionOffset(geometry));
    EXPECT_FALSE(f.View(geometry).PositionShadowStale) << "the shadow describes the copied block";
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 1u) << "one copy, then the preview is over";
    EXPECT_EQ(f.VertexWritesOverlapping(f.BlockPositionOffset(geometry), kPositionBytes, writesBefore), 0u)
        << "no CPU upload of the positions";

    // A newer front published after the last copy is not what the block holds either.
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, f.Preview(8u)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 2u);
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, Commit(f, bytes, 9u)), CommitStatus::CopyPending);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 3u);
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);
}

// RUNTIME-311: a pending commit copy holds the accepted front's residency lease. Once the copy
// is recorded (or the commit is superseded) the lease is retired, and it is released only after
// the frame that may have recorded the copy completed (`GetFramesInFlight`), however late the
// culling head runs; Shutdown drops every lease.
TEST(GpuWorldPositionCommitContract, APendingCopyHoldsTheFrontLeaseUntilItsFrameCompleted)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    const auto bytes = ShiftedPositionBytes();
    auto lease = std::make_shared<int>(0);
    auto commit = Commit(f, bytes, 7u);
    commit.SourceLease = lease;
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, commit), CommitStatus::CopyPending);
    commit.SourceLease.reset();
    // Minimized frames: no culling head records the copy, the lease stays held.
    for (int frame = 0; frame < 5; ++frame) { ++f.Device.GlobalFrameNumber; f.World.SyncFrame(); }
    EXPECT_GT(lease.use_count(), 1) << "the pending copy holds the front";
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);
    const auto recorded = f.Device.GlobalFrameNumber;
    for (std::uint64_t frame = 0; frame <= f.Device.FramesInFlight; ++frame)
    {
        f.World.SyncFrame();
        EXPECT_GT(lease.use_count(), 1) << "the recorded copy may still read the front at frame " << f.Device.GlobalFrameNumber;
        ++f.Device.GlobalFrameNumber;
    }
    ASSERT_GT(f.Device.GlobalFrameNumber - recorded, f.Device.FramesInFlight);
    f.World.SyncFrame();
    EXPECT_EQ(lease.use_count(), 1) << "released once the copy's frame completed";

    // Superseded before any copy: retired the same way, and Shutdown drops what is left.
    auto second = std::make_shared<int>(0);
    auto again = Commit(f, bytes, 9u);
    again.SourceLease = second;
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, again), CommitStatus::CopyPending);
    again.SourceLease.reset();
    ASSERT_TRUE(f.World.UpdateGeometryChannels(
        geometry, TriangleUpload(), Graphics::GpuWorld::GeometryChannelUpdateMask{.Position = true}).Succeeded());
    f.World.SyncFrame();
    EXPECT_GT(second.use_count(), 1) << "retired, not yet released";
    f.World.Shutdown();
    EXPECT_EQ(second.use_count(), 1);
}

// RUNTIME-311: a commit that replaces a pending commit, a freed geometry and a device-loss
// rebuild each stop holding the earlier front lease: the first two retire it (released after
// the frames in flight), the rebuild drops the retire list (the lost device's copies are gone)
// while a still-pending copy keeps its own lease for the re-recorded copy.
TEST(GpuWorldPositionCommitContract, ReplacedFreedAndRebuiltCommitsRetireTheirFrontLeases)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    const auto bytes = ShiftedPositionBytes();
    const auto commitWith = [&](const std::shared_ptr<int>& lease, const std::uint64_t stamp) {
        auto commit = Commit(f, bytes, stamp);
        commit.SourceLease = lease;
        return f.World.CommitGeometryPositions(geometry, commit);
    };
    const auto settle = [&] {
        for (std::uint64_t frame = 0; frame <= f.Device.FramesInFlight + 1u; ++frame)
        {
            ++f.Device.GlobalFrameNumber;
            f.World.SyncFrame();
        }
    };
    auto first = std::make_shared<int>(0), second = std::make_shared<int>(0);
    ASSERT_EQ(commitWith(first, 3u), CommitStatus::CopyPending);
    ASSERT_EQ(commitWith(second, 4u), CommitStatus::CopyPending) << "a later Accept replaces the pending copy";
    EXPECT_GT(first.use_count(), 1) << "retired, not yet released";
    EXPECT_GT(second.use_count(), 1) << "held by the pending copy";
    settle();
    EXPECT_EQ(first.use_count(), 1) << "the replaced copy's lease is released after the frames in flight";
    EXPECT_GT(second.use_count(), 1);

    // A device-loss rebuild drops retired leases; the pending copy keeps its own.
    auto retired = std::make_shared<int>(0);
    ASSERT_EQ(commitWith(retired, 5u), CommitStatus::CopyPending);
    EXPECT_EQ(second.use_count(), 2) << "only the retire list holds the replaced lease now";
    ASSERT_TRUE(f.World.RebuildGpuResources(f.Device, f.Buffers));
    EXPECT_EQ(second.use_count(), 1) << "the lost device's recorded copies are gone";
    EXPECT_GT(retired.use_count(), 1) << "the pending copy is recorded again on the new resources";

    // Freeing the geometry retires the pending copy's lease.
    f.World.FreeGeometry(geometry);
    EXPECT_GT(retired.use_count(), 1);
    settle();
    EXPECT_EQ(retired.use_count(), 1);
}

TEST(GpuWorldPositionCommitContract, ACpuPositionUploadSupersedesAPendingCommitCopy)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    const auto bytes = ShiftedPositionBytes();
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, Commit(f, bytes, 3u)), CommitStatus::CopyPending);
    // Undo right after Accept: the CPU rows upload and the old front must not land over them.
    ASSERT_TRUE(f.World.UpdateGeometryChannels(
        geometry, TriangleUpload(), Graphics::GpuWorld::GeometryChannelUpdateMask{.Position = true}).Succeeded());
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_TRUE(f.Commands.CopyBufferRecords.empty());
}

TEST(GpuWorldPositionCommitContract, CommitRefusesBytesThatDoNotFitAndSeamSplitBlocks)
{
    Fixture f;
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    const auto bytes = ShiftedPositionBytes();
    EXPECT_EQ(f.World.CommitGeometryPositions(geometry, Commit(f, std::span<const std::byte>{bytes}.first(12u), 1u)),
              CommitStatus::InvalidInput);
    EXPECT_EQ(f.World.CommitGeometryPositions(Graphics::GpuGeometryHandle{}, Commit(f, bytes, 1u)),
              CommitStatus::InvalidHandle);
    // A block that neither holds the front nor names a source to copy from cannot commit.
    auto noSource = Commit(f, bytes, 1u);
    noSource.Source = {};
    EXPECT_EQ(f.World.CommitGeometryPositions(geometry, noSource), CommitStatus::InvalidInput);
    // A seam-split block (gather preview) commits through the ordinary upload.
    constexpr std::array<PackedVertex, 4> kSplit{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        {0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.5f, 0.0f, 0.5f, 1.0f},
        {-0.5f, -0.5f, 0.0f, 1.0f, 1.0f},
    }};
    constexpr std::array<std::uint32_t, 6> kSplitIndices{{0u, 1u, 2u, 3u, 1u, 2u}};
    Graphics::GpuWorld::GeometryUploadDesc split{};
    split.PackedVertexBytes = std::as_bytes(std::span<const PackedVertex>{kSplit});
    split.SurfaceIndices = std::span<const std::uint32_t>{kSplitIndices};
    split.VertexCount = 4u;
    const auto seam = f.World.UploadGeometry(split);
    ASSERT_TRUE(seam.IsValid());
    constexpr std::array<std::uint32_t, 4> kMap{{0u, 1u, 2u, 0u}};
    ASSERT_EQ(f.World.SetGeometryPositionPreview(seam, f.Preview(1u, kMap, 1u)), Status::Accepted);
    std::vector<std::byte> fourRows(4u * 12u);
    EXPECT_EQ(f.World.CommitGeometryPositions(seam, Commit(f, fourRows, 1u)), CommitStatus::InvalidInput);
}

// RUNTIME-293 review (P1): a ring slot is recycled with the same buffer. When extraction
// missed the intermediate publications, the block holds the first copy of slot A while the
// accepted front is A republished with new bytes: the publication stamp tells them apart, so
// Accept copies again instead of keeping the old bytes. Stamps come from the residency.
TEST(GpuWorldPositionCommitContract, ARecycledSlotRepublishedAfterTheLastCopyIsCopiedAgainNotKept)
{
    Fixture f;
    f.Device.FramesInFlight = 2u;
    Graphics::GpuPropertyResidency residency(f.Device);
    const Graphics::GpuPropertyKey key{.Scope = 1u, .Owner = 293u, .Domain = 2u, .ValueKind = 3u, .Name = "v:position"};
    const Graphics::GpuPropertyLayout layout{.Scalar = Graphics::GpuScalarType::Float32, .Channels = 3u, .Stride = 0u,
                                             .Count = static_cast<std::uint32_t>(kTriangleVerts.size())};
    const auto geometry = f.World.UploadGeometry(TriangleUpload());
    const auto bytes = ShiftedPositionBytes();
    const auto previewOf = [&](const Graphics::GpuPropertyView& front) {
        return Graphics::GpuWorld::GeometryPositionPreviewDesc{
            .Source = front.Buffer, .SourceOffsetBytes = 0u,
            .SourceRowCount = static_cast<std::uint32_t>(kTriangleVerts.size()), .Stamp = front.Publication};
    };
    const auto commitOf = [&](const Graphics::GpuPropertyView& front) {
        return Graphics::GpuWorld::GeometryPositionCommitDesc{
            .PositionBytes = bytes, .Source = front.Buffer, .SourceOffsetBytes = 0u,
            .SourceRowCount = static_cast<std::uint32_t>(kTriangleVerts.size()), .Stamp = front.Publication};
    };

    // Publication 1 in slot A is shown and copied.
    ASSERT_TRUE(residency.AcquireBack(key, layout, 2u));
    ASSERT_TRUE(residency.Publish(key));
    auto first = *residency.Front(key);
    first.Lease.reset(); // the renderer observes, it does not lease
    residency.MarkObserved(key);
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, previewOf(first)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 1u);

    // Extraction misses the next frames: B is published, then A is recycled and republished.
    ASSERT_TRUE(residency.AcquireBack(key, layout, 2u));
    ASSERT_TRUE(residency.Publish(key));
    f.Device.GlobalFrameNumber = 3u;
    const auto recycled = residency.AcquireBack(key, layout, 2u);
    ASSERT_TRUE(recycled.has_value());
    ASSERT_EQ(recycled->Buffer, first.Buffer) << "the scenario needs slot A back";
    ASSERT_TRUE(residency.Publish(key));
    const auto third = *residency.Front(key);
    ASSERT_EQ(third.Buffer, first.Buffer);
    ASSERT_NE(third.Publication, first.Publication);

    // Accept of the third publication: the block holds the first one's bytes.
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, commitOf(third)), CommitStatus::CopyPending);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 2u) << "the republished slot is copied again";
    EXPECT_EQ(f.Commands.CopyBufferRecords.back().Src, first.Buffer);
    EXPECT_FALSE(f.View(geometry).PositionShadowStale);
    EXPECT_EQ(f.View(geometry).PositionFingerprint, ShiftedFingerprint());

    // Control: accepting the very publication the block copied keeps it without a copy.
    ASSERT_TRUE(residency.AcquireBack(key, layout, 2u));
    ASSERT_TRUE(residency.Publish(key));
    const auto fourth = *residency.Front(key);
    ASSERT_EQ(f.World.SetGeometryPositionPreview(geometry, previewOf(fourth)), Status::Accepted);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    ASSERT_EQ(f.Commands.CopyBufferRecords.size(), 3u);
    ASSERT_EQ(f.World.CommitGeometryPositions(geometry, commitOf(fourth)), CommitStatus::Committed);
    f.World.SubmitPendingUploadBarriers(f.Commands);
    EXPECT_EQ(f.Commands.CopyBufferRecords.size(), 3u);
    residency.Discard(key);
}
