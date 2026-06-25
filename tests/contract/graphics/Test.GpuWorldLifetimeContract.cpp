#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.BufferManager;
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
        const std::span<const glm::vec3> normals = std::span<const glm::vec3>{kSoaNormals})
    {
        return Extrinsic::Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = {},
            .PositionBytes = std::as_bytes(std::span<const glm::vec3>{kSoaPositions}),
            .TexcoordBytes = std::as_bytes(std::span<const glm::vec2>{kSoaTexcoords}),
            .NormalBytes = std::as_bytes(normals),
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kSoaPositions.size()),
            .LocalBounds = {},
            .DebugName = "contract-soa-triangle",
            .PackedVertexColors = std::span<const std::uint32_t>{kSoaColors},
        };
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
