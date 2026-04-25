#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

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

    std::span<const std::byte> VertexBytes()
    {
        return std::as_bytes(std::span<const PackedVertex>{kTriangleVerts});
    }
}

static_assert(sizeof(RHI::GpuGeometryRecord) == 64);
static_assert(sizeof(RHI::GpuInstanceStatic) == 32);
static_assert(sizeof(RHI::GpuInstanceDynamic) == 128);
static_assert(sizeof(RHI::GpuEntityConfig) == 128);
static_assert(sizeof(RHI::GpuBounds) == 64);
static_assert(sizeof(RHI::GpuLight) == 64);

TEST(GraphicsGpuWorld, Smoke_AllocateUploadSyncShutdown)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    Graphics::GpuWorld world;

    Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 64;
    init.MaxGeometryRecords = 64;
    init.MaxLights = 16;
    init.VertexBufferBytes = 1ull << 20;
    init.IndexBufferBytes = 1ull << 20;

    ASSERT_TRUE(world.Initialize(device, bufferMgr, init));
    ASSERT_TRUE(world.IsInitialized());

    const auto instance = world.AllocateInstance(42u);
    ASSERT_TRUE(instance.IsValid());

    Graphics::GpuWorld::GeometryUploadDesc upload{};
    upload.PackedVertexBytes = VertexBytes();
    upload.SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices};
    upload.LineIndices = std::span<const std::uint32_t>{};
    upload.VertexCount = static_cast<std::uint32_t>(kTriangleVerts.size());
    upload.LocalBounds.LocalSphere = {0.0f, 0.0f, 0.0f, 1.0f};
    upload.DebugName = "test-tri";

    const auto geometry = world.UploadGeometry(upload);
    ASSERT_TRUE(geometry.IsValid());

    world.SetInstanceGeometry(instance, geometry);
    world.SetInstanceMaterialSlot(instance, 0u);
    world.SetInstanceRenderFlags(instance, RHI::GpuRender_Surface | RHI::GpuRender_Visible);
    world.SetInstanceTransform(instance, glm::mat4{1.0f}, glm::mat4{1.0f});

    RHI::GpuEntityConfig config{};
    config.ColorSourceMode = 1u;
    config.UniformColor = {1.0f, 0.0f, 0.0f, 1.0f};
    world.SetEntityConfig(instance, config);

    RHI::GpuBounds bounds{};
    bounds.LocalSphere = {0.0f, 0.0f, 0.0f, 1.0f};
    bounds.WorldSphere = {0.0f, 0.0f, 0.0f, 1.0f};
    world.SetBounds(instance, bounds);

    world.SyncFrame();

    EXPECT_EQ(world.GetLiveInstanceCount(), 1u);
    EXPECT_TRUE(world.GetSceneTableBuffer().IsValid());
    EXPECT_TRUE(world.GetManagedVertexBuffer().IsValid());
    EXPECT_TRUE(world.GetManagedIndexBuffer().IsValid());

    world.Shutdown();
    EXPECT_FALSE(world.IsInitialized());
}

TEST(GraphicsCullingSystem, Smoke_InitializeBucketsAndDispatchPath)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    Graphics::GpuWorld::InitDesc init{};
    init.MaxInstances = 64;
    init.MaxGeometryRecords = 64;
    init.MaxLights = 4;
    init.VertexBufferBytes = 1ull << 20;
    init.IndexBufferBytes = 1ull << 20;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, init));

    auto instance = world.AllocateInstance(7u);
    ASSERT_TRUE(instance.IsValid());
    world.SetInstanceRenderFlags(instance, RHI::GpuRender_Surface | RHI::GpuRender_Visible);

    RHI::GpuBounds bounds{};
    bounds.WorldSphere = {0.0f, 0.0f, 0.0f, 10.0f};
    world.SetBounds(instance, bounds);
    world.SyncFrame();

    Graphics::CullingSystem culling;
    culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/src_new/culling/instance_cull.comp");

    const auto& surfaceBucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
    const auto& pointsBucket = culling.GetBucket(RHI::GpuDrawBucketKind::Points);
    EXPECT_TRUE(surfaceBucket.Indexed);
    EXPECT_TRUE(surfaceBucket.IndexedArgsBuffer.IsValid());
    EXPECT_TRUE(surfaceBucket.CountBuffer.IsValid());
    EXPECT_FALSE(pointsBucket.Indexed);
    EXPECT_TRUE(pointsBucket.NonIndexedArgsBuffer.IsValid());

    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};

    culling.ResetCounters(device.CommandContext);
    culling.DispatchCull(device.CommandContext, camera, world);

    culling.Shutdown();
    world.Shutdown();
}
