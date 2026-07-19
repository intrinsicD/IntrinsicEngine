#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

namespace
{
    struct PackedVertex
    {
        float Px, Py, Pz;
        float U, V;
    };

    struct SurfaceVertex
    {
        float Px, Py, Pz;
        float U, V;
        float Nx, Ny, Nz;
    };

    constexpr std::array<PackedVertex, 3> kTriangleVerts{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f},
    }};

    constexpr std::array<SurfaceVertex, 3> kSurfaceTriangleVerts{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
        { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f},
    }};

    constexpr std::array<std::uint32_t, 3> kTriangleIndices{{0u, 1u, 2u}};

    const std::array<glm::vec3, 3> kSoaPositions{{
        {-0.5f, -0.5f, 0.0f},
        { 0.5f, -0.5f, 0.0f},
        { 0.0f,  0.5f, 0.0f},
    }};

    const std::array<glm::vec2, 3> kSoaTexcoords{{
        {0.0f, 0.0f},
        {1.0f, 0.0f},
        {0.5f, 1.0f},
    }};

    const std::array<glm::vec3, 3> kSoaNormals{{
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, 1.0f},
    }};

    const std::array<std::uint32_t, 3> kSoaColors{{
        0xff0000ffu,
        0xff00ff00u,
        0xffff0000u,
    }};

    [[nodiscard]] std::span<const std::byte> VertexBytes()
    {
        return std::as_bytes(std::span<const PackedVertex>{kTriangleVerts});
    }

    [[nodiscard]] std::span<const std::byte> SurfaceVertexBytes()
    {
        return std::as_bytes(std::span<const SurfaceVertex>{kSurfaceTriangleVerts});
    }

    [[nodiscard]] Extrinsic::Graphics::GpuWorld::GeometryUploadDesc TriangleUpload()
    {
        return Extrinsic::Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = VertexBytes(),
            .PositionBytes = {},
            .TexcoordBytes = {},
            .NormalBytes = {},
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kTriangleVerts.size()),
            .LocalBounds = {},
            .DebugName = "contract-triangle",
            .PackedVertexColors = {},
        };
    }

    [[nodiscard]] Extrinsic::Graphics::GpuWorld::GeometryUploadDesc SurfaceTriangleUpload()
    {
        return Extrinsic::Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = SurfaceVertexBytes(),
            .PositionBytes = {},
            .TexcoordBytes = {},
            .NormalBytes = {},
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kSurfaceTriangleVerts.size()),
            .LocalBounds = {},
            .DebugName = "contract-surface-triangle",
            .PackedVertexColors = {},
        };
    }

    [[nodiscard]] Extrinsic::Graphics::GpuWorld::GeometryUploadDesc SoaTriangleUpload(
        const std::span<const glm::vec3> normals =
            std::span<const glm::vec3>{kSoaNormals},
        const std::span<const glm::vec2> texcoords =
            std::span<const glm::vec2>{kSoaTexcoords},
        const std::span<const glm::vec3> positions =
            std::span<const glm::vec3>{kSoaPositions})
    {
        return Extrinsic::Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = {},
            .PositionBytes = std::as_bytes(positions),
            .TexcoordBytes = std::as_bytes(texcoords),
            .NormalBytes = std::as_bytes(normals),
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kSoaPositions.size()),
            .LocalBounds = {},
            .DebugName = "contract-soa-triangle",
            .PackedVertexColors = std::span<const std::uint32_t>{kSoaColors},
        };
    }

    void ExpectResidencyContentMetadataEqual(
        const Extrinsic::Graphics::GpuGeometryResidencyView& lhs,
        const Extrinsic::Graphics::GpuGeometryResidencyView& rhs)
    {
        EXPECT_EQ(lhs.ContentRevision, rhs.ContentRevision);
        EXPECT_EQ(lhs.PositionFingerprint, rhs.PositionFingerprint);
        EXPECT_EQ(lhs.SurfaceIndexFingerprint,
                  rhs.SurfaceIndexFingerprint);
        EXPECT_EQ(lhs.TexcoordFingerprint, rhs.TexcoordFingerprint);
        EXPECT_EQ(lhs.NormalFingerprint, rhs.NormalFingerprint);
        EXPECT_EQ(lhs.PositionByteCount, rhs.PositionByteCount);
        EXPECT_EQ(lhs.SurfaceIndexByteCount, rhs.SurfaceIndexByteCount);
        EXPECT_EQ(lhs.TexcoordByteCount, rhs.TexcoordByteCount);
        EXPECT_EQ(lhs.NormalByteCount, rhs.NormalByteCount);
        EXPECT_EQ(lhs.VertexCount, rhs.VertexCount);
        EXPECT_EQ(lhs.SurfaceIndexCount, rhs.SurfaceIndexCount);
        EXPECT_EQ(lhs.StorageLane, rhs.StorageLane);
        EXPECT_EQ(lhs.PositionFormat, rhs.PositionFormat);
        EXPECT_EQ(lhs.TexcoordFormat, rhs.TexcoordFormat);
        EXPECT_EQ(lhs.NormalFormat, rhs.NormalFormat);
        EXPECT_EQ(lhs.SurfaceIndexFormat, rhs.SurfaceIndexFormat);
        EXPECT_EQ(lhs.PositionElementBytes, rhs.PositionElementBytes);
        EXPECT_EQ(lhs.PositionStrideBytes, rhs.PositionStrideBytes);
        EXPECT_EQ(lhs.TexcoordElementBytes, rhs.TexcoordElementBytes);
        EXPECT_EQ(lhs.TexcoordStrideBytes, rhs.TexcoordStrideBytes);
        EXPECT_EQ(lhs.NormalElementBytes, rhs.NormalElementBytes);
        EXPECT_EQ(lhs.NormalStrideBytes, rhs.NormalStrideBytes);
        EXPECT_EQ(lhs.SurfaceIndexElementBytes,
                  rhs.SurfaceIndexElementBytes);
        EXPECT_EQ(lhs.SurfaceIndexStrideBytes,
                  rhs.SurfaceIndexStrideBytes);
    }
}

TEST(GpuWorldLifetimeContract, InstanceFreeIsGenerationCheckedAndDeferred)
{
    Extrinsic::Tests::MockDevice device;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    init.DeferredFreeFrames = 2u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto first = world.AllocateInstance(42u);
    ASSERT_TRUE(first.IsValid());
    EXPECT_EQ(world.GetLiveInstanceCount(), 1u);

    world.FreeInstance(first);
    auto diagnostics = world.GetDiagnostics();
    EXPECT_EQ(diagnostics.Instances.LiveCount, 0u);
    EXPECT_EQ(diagnostics.Instances.PendingFreeCount, 1u);
    EXPECT_EQ(diagnostics.Instances.ReusableCount, 0u);

    EXPECT_FALSE(world.AllocateInstance(43u).IsValid());
    EXPECT_EQ(world.GetDiagnostics().Instances.OverflowCount, 1u);

    world.SyncFrame();
    EXPECT_FALSE(world.AllocateInstance(44u).IsValid());
    EXPECT_EQ(world.GetDiagnostics().Instances.PendingFreeCount, 1u);

    world.SyncFrame();
    const auto reused = world.AllocateInstance(45u);
    ASSERT_TRUE(reused.IsValid());
    EXPECT_EQ(reused.Index, first.Index);
    EXPECT_NE(reused.Generation, first.Generation);

    world.SetInstanceRenderFlags(first, Extrinsic::RHI::GpuRender_Surface);
    EXPECT_EQ(world.GetDiagnostics().Instances.StaleHandleCount, 1u);
}

TEST(GpuWorldLifetimeContract, GeometryOverflowAndStaleHandlesAreDiagnostic)
{
    Extrinsic::Tests::MockDevice device;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    EXPECT_FALSE(world.UploadGeometry(TriangleUpload()).IsValid());
    auto diagnostics = world.GetDiagnostics();
    EXPECT_EQ(diagnostics.VertexOverflowCount, 1u);
    EXPECT_EQ(diagnostics.Geometry.LiveCount, 0u);

    world.SyncFrame();
    EXPECT_EQ(world.GetDiagnostics().Geometry.ReusableCount, 1u);

    const Extrinsic::Graphics::GpuGeometryHandle invalid{99u, 1u};
    world.FreeGeometry(invalid);
    EXPECT_EQ(world.GetDiagnostics().Geometry.InvalidHandleCount, 1u);
}

TEST(GpuWorldLifetimeContract, MixedVertexStridesAreAlignedForChannelBlocks)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 2u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u << 10;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto primitive = world.UploadGeometry(TriangleUpload());
    const auto surface = world.UploadGeometry(SurfaceTriangleUpload());
    ASSERT_TRUE(primitive.IsValid());
    ASSERT_TRUE(surface.IsValid());

    const auto diagnostics = world.GetDiagnostics();
    EXPECT_EQ(diagnostics.VertexBytesUsed,
              64u + SurfaceVertexBytes().size_bytes())
        << "32-byte surface vertices uploaded after a 20-byte primitive view "
           "must start a separately aligned managed channel block";

    const auto managed = world.GetManagedBufferDiagnostics();
    EXPECT_EQ(managed.Vertex.FragmentedBytes, 4u);
}

TEST(GpuWorldLifetimeContract, UpdatingSingleVertexChannelWritesOnlyThatChannel)
{
    Extrinsic::Tests::MockDevice device;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u << 10;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto geometry = world.UploadGeometry(SoaTriangleUpload());
    ASSERT_TRUE(geometry.IsValid());
    world.SubmitPendingUploadBarriers(device.CommandContext);

    const std::array<glm::vec3, 3> updatedNormals{{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    }};
    device.BufferWrites.clear();
    device.CommandContext.BufferBarrierCalls.clear();

    const auto update = world.UpdateGeometryChannels(
        geometry,
        SoaTriangleUpload(std::span<const glm::vec3>{updatedNormals}),
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{
            .Normal = true,
        });

    EXPECT_EQ(update.Status,
              Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::Updated);
    EXPECT_FALSE(update.UploadedChannels.Position);
    EXPECT_FALSE(update.UploadedChannels.Texcoord);
    EXPECT_TRUE(update.UploadedChannels.Normal);
    EXPECT_FALSE(update.UploadedChannels.Color);
    EXPECT_FALSE(update.GeometryRecordUpdated);

    ASSERT_EQ(device.BufferWrites.size(), 1u);
    const auto& write = device.BufferWrites.front();
    EXPECT_EQ(write.Handle, world.GetManagedVertexBuffer());
    EXPECT_EQ(write.Offset,
              std::as_bytes(std::span<const glm::vec3>{kSoaPositions}).size_bytes() +
                  std::as_bytes(std::span<const glm::vec2>{kSoaTexcoords}).size_bytes());
    const std::span<const std::byte> expectedNormalBytes =
        std::as_bytes(std::span<const glm::vec3>{updatedNormals});
    EXPECT_EQ(write.Data.size(), expectedNormalBytes.size_bytes());
    EXPECT_EQ(write.Data,
              std::vector<std::byte>(expectedNormalBytes.begin(),
                                     expectedNormalBytes.end()));

    world.SubmitPendingUploadBarriers(device.CommandContext);
    ASSERT_EQ(device.CommandContext.BufferBarrierCalls.size(), 1u);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls.front().Buffer,
              world.GetManagedVertexBuffer());
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls.front().Before,
              Extrinsic::RHI::MemoryAccess::TransferWrite);
    EXPECT_EQ(device.CommandContext.BufferBarrierCalls.front().After,
              Extrinsic::RHI::MemoryAccess::ShaderRead);
}

TEST(GpuWorldLifetimeContract, ResidencyViewTracksCanonicalSoaContentAndSharedIndexSlice)
{
    Extrinsic::Tests::MockDevice device;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 2u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u << 10;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto decoy = world.UploadGeometry(TriangleUpload());
    const auto geometry = world.UploadGeometry(SoaTriangleUpload());
    ASSERT_TRUE(decoy.IsValid());
    ASSERT_TRUE(geometry.IsValid());

    Extrinsic::Graphics::GpuGeometryResidencyView decoyView{};
    ASSERT_TRUE(world.TryGetGeometryResidencyView(decoy, decoyView));
    EXPECT_EQ(decoyView.NormalFingerprint, 0u);
    EXPECT_EQ(decoyView.NormalByteCount, 0u);
    EXPECT_EQ(decoyView.NormalFormat, Extrinsic::RHI::Format::Undefined);

    Extrinsic::Graphics::GpuGeometryResidencyView before{};
    ASSERT_TRUE(world.TryGetGeometryResidencyView(geometry, before));
    ASSERT_TRUE(before.IndexBuffer.IsValid());
    EXPECT_EQ(before.IndexBuffer, world.GetManagedIndexBuffer());
    EXPECT_EQ(before.Record.SurfaceFirstIndex, kTriangleIndices.size());
    EXPECT_GT(before.Record.SurfaceFirstIndex, 0u);
    EXPECT_EQ(before.Record.SurfaceIndexCount, kTriangleIndices.size());
    EXPECT_NE(before.ContentRevision, 0u);
    EXPECT_NE(before.PositionFingerprint, 0u);
    EXPECT_NE(before.SurfaceIndexFingerprint, 0u);
    EXPECT_NE(before.TexcoordFingerprint, 0u);
    EXPECT_NE(before.NormalFingerprint, 0u);
    EXPECT_EQ(before.PositionByteCount,
              std::as_bytes(
                  std::span<const glm::vec3>{kSoaPositions}).size_bytes());
    EXPECT_EQ(before.SurfaceIndexByteCount,
              std::as_bytes(
                  std::span<const std::uint32_t>{kTriangleIndices}).size_bytes());
    EXPECT_EQ(before.TexcoordByteCount,
              std::as_bytes(
                  std::span<const glm::vec2>{kSoaTexcoords}).size_bytes());
    EXPECT_EQ(before.NormalByteCount,
              std::as_bytes(
                  std::span<const glm::vec3>{kSoaNormals}).size_bytes());
    EXPECT_EQ(before.VertexCount, kSoaPositions.size());
    EXPECT_EQ(before.SurfaceIndexCount, kTriangleIndices.size());
    EXPECT_EQ(before.StorageLane,
              Extrinsic::Graphics::GpuWorld::GeometryStorageLane::UniformSoA);
    EXPECT_EQ(before.PositionFormat, Extrinsic::RHI::Format::RGB32_FLOAT);
    EXPECT_EQ(before.PositionElementBytes, sizeof(float) * 3u);
    EXPECT_EQ(before.PositionStrideBytes, sizeof(float) * 3u);
    EXPECT_EQ(before.TexcoordFormat, Extrinsic::RHI::Format::RG32_FLOAT);
    EXPECT_EQ(before.TexcoordElementBytes, sizeof(float) * 2u);
    EXPECT_EQ(before.TexcoordStrideBytes, sizeof(float) * 2u);
    EXPECT_EQ(before.NormalFormat, Extrinsic::RHI::Format::RGB32_FLOAT);
    EXPECT_EQ(before.NormalElementBytes, sizeof(float) * 3u);
    EXPECT_EQ(before.NormalStrideBytes, sizeof(float) * 3u);
    EXPECT_EQ(before.SurfaceIndexFormat, Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(before.SurfaceIndexElementBytes, sizeof(std::uint32_t));
    EXPECT_EQ(before.SurfaceIndexStrideBytes, sizeof(std::uint32_t));

    const std::array<glm::vec2, 3> negativeZeroTexcoords{{
        {-0.0f, -0.0f},
        {1.0f, -0.0f},
        {0.5f, 1.0f},
    }};
    const std::array<glm::vec3, 3> negativeZeroNormals{{
        {-0.0f, -0.0f, 1.0f},
        {-0.0f, -0.0f, 1.0f},
        {-0.0f, -0.0f, 1.0f},
    }};
    const auto canonicalZeroUpdate = world.UpdateGeometryChannels(
        geometry,
        SoaTriangleUpload(
            std::span<const glm::vec3>{negativeZeroNormals},
            std::span<const glm::vec2>{negativeZeroTexcoords}),
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{
            .Texcoord = true,
            .Normal = true,
        });
    ASSERT_EQ(
        canonicalZeroUpdate.Status,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::Updated);

    Extrinsic::Graphics::GpuGeometryResidencyView afterCanonicalZero{};
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(
            geometry,
            afterCanonicalZero));
    EXPECT_GT(afterCanonicalZero.ContentRevision, before.ContentRevision);
    EXPECT_EQ(afterCanonicalZero.PositionFingerprint,
              before.PositionFingerprint);
    EXPECT_EQ(afterCanonicalZero.SurfaceIndexFingerprint,
              before.SurfaceIndexFingerprint);
    EXPECT_EQ(afterCanonicalZero.TexcoordFingerprint,
              before.TexcoordFingerprint);
    EXPECT_EQ(afterCanonicalZero.NormalFingerprint,
              before.NormalFingerprint);

    const std::array<glm::vec2, 3> updatedTexcoords{{
        {0.1f, 0.2f},
        {0.8f, 0.2f},
        {0.5f, 0.9f},
    }};
    const std::array<glm::vec3, 3> updatedNormals{{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    }};
    const auto originalHandle = geometry;
    const auto update = world.UpdateGeometryChannels(
        geometry,
        SoaTriangleUpload(
            std::span<const glm::vec3>{updatedNormals},
            std::span<const glm::vec2>{updatedTexcoords}),
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{
            .Texcoord = true,
            .Normal = true,
        });
    ASSERT_EQ(
        update.Status,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::Updated);
    EXPECT_EQ(geometry, originalHandle);

    Extrinsic::Graphics::GpuGeometryResidencyView after{};
    ASSERT_TRUE(world.TryGetGeometryResidencyView(geometry, after));
    EXPECT_GT(after.ContentRevision, afterCanonicalZero.ContentRevision);
    EXPECT_EQ(after.PositionFingerprint,
              afterCanonicalZero.PositionFingerprint);
    EXPECT_EQ(after.SurfaceIndexFingerprint,
              afterCanonicalZero.SurfaceIndexFingerprint);
    EXPECT_NE(after.TexcoordFingerprint,
              afterCanonicalZero.TexcoordFingerprint);
    EXPECT_NE(after.NormalFingerprint,
              afterCanonicalZero.NormalFingerprint);
    EXPECT_EQ(after.Record.SurfaceFirstIndex,
              before.Record.SurfaceFirstIndex);
    EXPECT_EQ(after.IndexBuffer, before.IndexBuffer);

    ASSERT_TRUE(world.RebuildGpuResources(device, buffers));
    Extrinsic::Graphics::GpuGeometryResidencyView afterRebuild{};
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(
            geometry,
            afterRebuild));
    ExpectResidencyContentMetadataEqual(after, afterRebuild);
    EXPECT_EQ(afterRebuild.Record.SurfaceFirstIndex,
              after.Record.SurfaceFirstIndex);
    ASSERT_TRUE(afterRebuild.IndexBuffer.IsValid());
    EXPECT_EQ(afterRebuild.IndexBuffer,
              world.GetManagedIndexBuffer());

    world.FreeGeometry(decoy);
    world.SyncFrame();
    const auto compactionPlan = world.PlanManagedBufferCompaction(
        Extrinsic::Graphics::GpuWorld::CompactionPlanDesc{
            .Enabled = true,
            .AllowWhilePendingFrees = false,
            .MinFragmentationRatio = 0.0f,
            .MinRecoverableBytes = 1u,
        });
    ASSERT_TRUE(compactionPlan.ShouldCompact);
    const auto compaction =
        world.ApplyManagedBufferCompaction(compactionPlan);
    ASSERT_TRUE(compaction.Applied);

    Extrinsic::Graphics::GpuGeometryResidencyView afterCompaction{};
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(
            geometry,
            afterCompaction));
    ExpectResidencyContentMetadataEqual(
        afterRebuild,
        afterCompaction);
    EXPECT_EQ(afterCompaction.Record.SurfaceFirstIndex, 0u);
    EXPECT_EQ(afterCompaction.IndexBuffer,
              afterRebuild.IndexBuffer);

    const std::array<glm::vec3, 3> updatedPositions{{
        {-0.25f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.25f, 0.0f},
    }};
    const auto postCompactionUpdate = world.UpdateGeometryChannels(
        geometry,
        SoaTriangleUpload(
            std::span<const glm::vec3>{updatedNormals},
            std::span<const glm::vec2>{updatedTexcoords},
            std::span<const glm::vec3>{updatedPositions}),
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{
            .Position = true,
        });
    ASSERT_EQ(
        postCompactionUpdate.Status,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::Updated);
    Extrinsic::Graphics::GpuGeometryResidencyView afterPositionUpdate{};
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(
            geometry,
            afterPositionUpdate));
    EXPECT_GT(afterPositionUpdate.ContentRevision,
              afterCompaction.ContentRevision);
    EXPECT_NE(afterPositionUpdate.PositionFingerprint,
              afterCompaction.PositionFingerprint);
    EXPECT_EQ(afterPositionUpdate.SurfaceIndexFingerprint,
              afterCompaction.SurfaceIndexFingerprint);
    EXPECT_EQ(afterPositionUpdate.TexcoordFingerprint,
              afterCompaction.TexcoordFingerprint);
    EXPECT_EQ(afterPositionUpdate.NormalFingerprint,
              afterCompaction.NormalFingerprint);
}

TEST(GpuWorldLifetimeContract, RejectedUpdatesPreserveResidencyAndStaleViewsClear)
{
    Extrinsic::Tests::MockDevice device;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u << 10;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto geometry = world.UploadGeometry(SoaTriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    Extrinsic::Graphics::GpuGeometryResidencyView before{};
    ASSERT_TRUE(world.TryGetGeometryResidencyView(geometry, before));

    constexpr std::array<std::uint32_t, 3> changedTopology{{0u, 2u, 1u}};
    auto changed = SoaTriangleUpload();
    changed.SurfaceIndices =
        std::span<const std::uint32_t>{changedTopology};
    const auto rejected = world.UpdateGeometryChannels(
        geometry,
        changed,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{
            .Normal = true,
        });
    EXPECT_EQ(
        rejected.Status,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::
            FullUploadRequired);

    Extrinsic::Graphics::GpuGeometryResidencyView afterRejected{};
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(
            geometry,
            afterRejected));
    ExpectResidencyContentMetadataEqual(before, afterRejected);

    const auto noChannels = world.UpdateGeometryChannels(
        geometry,
        changed,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{});
    EXPECT_EQ(
        noChannels.Status,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::
            NoChannels);
    Extrinsic::Graphics::GpuGeometryResidencyView afterNoChannels{};
    ASSERT_TRUE(
        world.TryGetGeometryResidencyView(
            geometry,
            afterNoChannels));
    ExpectResidencyContentMetadataEqual(before, afterNoChannels);

    world.FreeGeometry(geometry);
    Extrinsic::Graphics::GpuGeometryResidencyView cleared = before;
    EXPECT_FALSE(world.TryGetGeometryResidencyView(geometry, cleared));
    const Extrinsic::Graphics::GpuGeometryResidencyView empty{};
    ExpectResidencyContentMetadataEqual(cleared, empty);
    EXPECT_FALSE(cleared.IndexBuffer.IsValid());
    EXPECT_EQ(cleared.Record.SurfaceIndexCount, 0u);
    EXPECT_EQ(cleared.Record.SurfaceFirstIndex, 0u);
    EXPECT_EQ(cleared.SurfaceIndexFormat,
              Extrinsic::RHI::Format::Undefined);
    EXPECT_EQ(cleared.SurfaceIndexElementBytes, 0u);

    world.SyncFrame();
    const auto replacement = world.UploadGeometry(SoaTriangleUpload());
    ASSERT_TRUE(replacement.IsValid());
    EXPECT_EQ(replacement.Index, geometry.Index);
    EXPECT_NE(replacement.Generation, geometry.Generation);

    cleared = before;
    EXPECT_FALSE(world.TryGetGeometryResidencyView(geometry, cleared));
    ExpectResidencyContentMetadataEqual(cleared, empty);
    EXPECT_FALSE(cleared.IndexBuffer.IsValid());
}

TEST(GpuWorldLifetimeContract, VertexCountChangeRequiresFullGeometryUpload)
{
    Extrinsic::Tests::MockDevice device;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 1u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    init.VertexBufferBytes = 1u << 10;
    init.IndexBufferBytes = 1u << 10;
    init.DeferredFreeFrames = 0u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    const auto geometry = world.UploadGeometry(SoaTriangleUpload());
    ASSERT_TRUE(geometry.IsValid());

    auto changedCount = SoaTriangleUpload();
    changedCount.VertexCount += 1u;
    device.BufferWrites.clear();

    const auto update = world.UpdateGeometryChannels(
        geometry,
        changedCount,
        Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateMask{
            .Normal = true,
        });

    EXPECT_EQ(update.Status,
              Extrinsic::Graphics::GpuWorld::GeometryChannelUpdateStatus::FullUploadRequired);
    EXPECT_FALSE(update.UploadedChannels.Any());
    EXPECT_TRUE(device.BufferWrites.empty());
}

TEST(GpuWorldLifetimeContract, NullDeviceModeKeepsCpuLifetimeDiagnosticsObservable)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    Extrinsic::RHI::BufferManager buffers{device};
    Extrinsic::Graphics::GpuWorld world;

    Extrinsic::Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 2u;
    init.MaxGeometryRecords = 1u;
    init.MaxLights = 1u;
    ASSERT_TRUE(world.Initialize(device, buffers, init));

    EXPECT_TRUE(world.GetDiagnostics().NullDevice);
    EXPECT_FALSE(world.GetSceneTableBuffer().IsValid());

    const auto instance = world.AllocateInstance(7u);
    ASSERT_TRUE(instance.IsValid());
    EXPECT_EQ(world.GetDiagnostics().Instances.LiveCount, 1u);

    world.SyncFrame();
    EXPECT_EQ(world.GetDiagnostics().Instances.LiveCount, 1u);
}
