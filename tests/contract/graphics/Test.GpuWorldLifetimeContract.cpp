#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

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

    constexpr std::array<PackedVertex, 3> kTriangleVerts{{
        {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
        { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
        { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f},
    }};

    constexpr std::array<std::uint32_t, 3> kTriangleIndices{{0u, 1u, 2u}};

    [[nodiscard]] std::span<const std::byte> VertexBytes()
    {
        return std::as_bytes(std::span<const PackedVertex>{kTriangleVerts});
    }

    [[nodiscard]] Extrinsic::Graphics::GpuWorld::GeometryUploadDesc TriangleUpload()
    {
        return Extrinsic::Graphics::GpuWorld::GeometryUploadDesc{
            .PackedVertexBytes = VertexBytes(),
            .SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(kTriangleVerts.size()),
            .LocalBounds = {},
            .DebugName = "contract-triangle",
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

