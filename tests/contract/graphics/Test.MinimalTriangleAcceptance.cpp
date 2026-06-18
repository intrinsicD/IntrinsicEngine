#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <span>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Material;
import Extrinsic.Graphics.MaterialSystem;
import Extrinsic.Graphics.Pass.Culling;
import Extrinsic.Graphics.Pass.Deferred.GBuffers;
import Extrinsic.Graphics.TransformSyncSystem;
import Extrinsic.Graphics.VisualizationSyncSystem;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
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
    constexpr std::uint32_t kColorSourceScalarField = 2u;
    constexpr std::uint32_t kColorSourcePerElementRgba = 3u;

    std::span<const std::byte> VertexBytes()
    {
        return std::as_bytes(std::span<const PackedVertex>{kTriangleVerts});
    }

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        int BindIndexBufferCalls = 0;
        int DrawIndexedIndirectCountCalls = 0;
        int DispatchCalls = 0;
        int FillBufferCalls = 0;

        RHI::BufferHandle LastBoundIndexBuffer{};
        RHI::BufferHandle LastIndirectArgs{};
        RHI::BufferHandle LastIndirectCount{};

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle) override {}

        void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t, RHI::IndexType) override
        {
            ++BindIndexBufferCalls;
            LastBoundIndexBuffer = buffer;
        }

        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}

        void DrawIndexedIndirectCount(RHI::BufferHandle argBuffer, std::uint64_t,
                                      RHI::BufferHandle countBuffer, std::uint64_t,
                                      std::uint32_t) override
        {
            ++DrawIndexedIndirectCountCalls;
            LastIndirectArgs = argBuffer;
            LastIndirectCount = countBuffer;
        }

        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t,
                               RHI::BufferHandle, std::uint64_t,
                               std::uint32_t) override {}

        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override
        {
            ++DispatchCalls;
        }

        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}

        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override
        {
            ++FillBufferCalls;
        }

        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle,
                        std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t,
                                 RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };
}

TEST(GraphicsMinimalAcceptance, Triangle_FirstImplementationContract)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    Graphics::GpuWorld::InitDesc worldInit{};
    worldInit.MaxInstances = 64;
    worldInit.MaxGeometryRecords = 64;
    worldInit.MaxLights = 8;
    worldInit.VertexBufferBytes = 1ull << 20;
    worldInit.IndexBufferBytes = 1ull << 20;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, worldInit));

    Graphics::MaterialSystem matSys;
    matSys.Initialize(device, bufferMgr);

    Graphics::VisualizationSyncSystem visSync;
    visSync.Initialize(matSys, device);

    Graphics::ColormapSystem colorSys; // Not required for UniformColor test path.

    Graphics::TransformSyncSystem transformSync;
    transformSync.Initialize();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    const auto baseType = matSys.FindType("StandardPBR");
    ASSERT_TRUE(baseType.IsValid());

    const auto instance = world.AllocateInstance(1u);
    ASSERT_TRUE(instance.IsValid());

    Graphics::GpuWorld::GeometryUploadDesc upload{};
    upload.PackedVertexBytes = VertexBytes();
    upload.SurfaceIndices = std::span<const std::uint32_t>{kTriangleIndices};
    upload.VertexCount = static_cast<std::uint32_t>(kTriangleVerts.size());
    upload.LocalBounds.LocalSphere = {0.0f, 0.0f, 0.0f, 1.0f};
    upload.DebugName = "acceptance-triangle";

    const auto geometry = world.UploadGeometry(upload);
    ASSERT_TRUE(geometry.IsValid());

    Graphics::Components::GpuSceneSlot gpuSlot{};
    gpuSlot.SetInstanceHandle(instance);
    gpuSlot.SetGeometryHandle(geometry);

    world.SetInstanceGeometry(instance, geometry);

    Graphics::MaterialParams materialParams{};
    materialParams.BaseColorFactor = {0.8f, 0.7f, 0.6f, 1.0f};
    auto baseLease = matSys.CreateInstance(baseType, materialParams);
    ASSERT_TRUE(baseLease.IsValid());
    Graphics::Components::MaterialInstance materialInstance{
        .Lease = std::move(baseLease),
        .TintOverride = std::nullopt,
        .EffectiveSlot = 0u,
    };

    Graphics::Components::VisualizationConfig visualization{
        .Source = Graphics::Components::VisualizationConfig::ColorSource::UniformColor,
        .Color = {1.0f, 0.1f, 0.1f, 1.0f},
        .ScalarFieldName = std::string{},
        .Scalar = {},
        .ScalarDomain = Graphics::Components::VisualizationConfig::Domain::Vertex,
        .ColorBufferName = std::string{},
    };

    std::array<Graphics::VisualizationSyncRecord, 1> visualizationRecords{{
        Graphics::VisualizationSyncRecord{
            .StableId = 1u,
            .Material = &materialInstance,
            .GpuSlot = &gpuSlot,
            .Visualization = &visualization,
        },
    }};

    visSync.Sync(visualizationRecords, matSys, colorSys, world);
    matSys.SyncGpuBuffer();

    RHI::GpuBounds bounds{};
    bounds.LocalSphere = {0.0f, 0.0f, 0.0f, 1.0f};
    bounds.WorldSphere = {0.0f, 0.0f, 0.0f, 1.0f};

    const std::array<Graphics::TransformSyncRecord, 1> transformRecords{{
        Graphics::TransformSyncRecord{
            .Instance = instance,
            .Model = glm::mat4{1.0f},
            .RenderFlags = RHI::GpuRender_Visible | RHI::GpuRender_Surface | RHI::GpuRender_Opaque,
            .Bounds = bounds,
            .MaterialSlot = materialInstance.EffectiveSlot,
            .HasMaterialSlot = true,
        },
    }};
    transformSync.SyncGpuBuffer(transformRecords, world);
    world.SetMaterialBuffer(matSys.GetBuffer(), matSys.GetCapacity());
    world.SyncFrame();

    EXPECT_NE(materialInstance.EffectiveSlot, 0u);
    EXPECT_EQ(visSync.GetOverrideLeaseCount(), 1u);

    Graphics::CullingPass cullPass{culling};
    Graphics::DeferredSystem deferred;
    deferred.Initialize();
    Graphics::DeferredGBufferPass gbufferPass{deferred};
    gbufferPass.SetPipeline(RHI::PipelineHandle{11u, 1u});

    RecordingCommandContext cmd;
    RHI::CameraUBO camera{};
    camera.ViewProj = glm::mat4{1.0f};

    cullPass.Execute(cmd, camera, world);
    gbufferPass.Execute(cmd, camera, world, culling, 0u);

    EXPECT_GT(cmd.FillBufferCalls, 0);
    EXPECT_GT(cmd.DispatchCalls, 0);
    EXPECT_EQ(cmd.BindIndexBufferCalls, 1);
    EXPECT_EQ(cmd.LastBoundIndexBuffer, world.GetManagedIndexBuffer());
    EXPECT_EQ(cmd.DrawIndexedIndirectCountCalls, 1);

    const auto& surfaceBucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
    EXPECT_EQ(cmd.LastIndirectArgs, surfaceBucket.IndexedArgsBuffer);
    EXPECT_EQ(cmd.LastIndirectCount, surfaceBucket.CountBuffer);

    deferred.Shutdown();
    culling.Shutdown();
    transformSync.Shutdown();
    visSync.Shutdown();
    materialInstance.Lease = {};
    matSys.Shutdown();
    world.Shutdown();
}

TEST(GraphicsMinimalAcceptance, VisualizationSyncWritesLineWidthConfig)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};

    Graphics::GpuWorld world;
    Graphics::GpuWorld::InitDesc worldInit{};
    worldInit.MaxInstances = 4;
    worldInit.MaxGeometryRecords = 1;
    worldInit.MaxLights = 1;
    worldInit.VertexBufferBytes = 1024;
    worldInit.IndexBufferBytes = 1024;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, worldInit));

    Graphics::MaterialSystem matSys;
    matSys.Initialize(device, bufferMgr);

    Graphics::VisualizationSyncSystem visSync;
    visSync.Initialize(matSys, device);

    Graphics::ColormapSystem colorSys;

    const auto instance = world.AllocateInstance(41u);
    ASSERT_TRUE(instance.IsValid());

    Graphics::Components::GpuSceneSlot gpuSlot{};
    gpuSlot.SetInstanceHandle(instance);
    const RHI::BufferHandle widthBuffer{77u, 1u};
    gpuSlot.Upsert("edge_widths", widthBuffer, 2u, sizeof(float));

    Graphics::Components::MaterialInstance materialInstance{};
    Graphics::Components::RenderEdges edges{};
    edges.WidthSource = std::string{"edge_widths"};

    std::array<Graphics::VisualizationSyncRecord, 1> records{{
        Graphics::VisualizationSyncRecord{
            .StableId = 41u,
            .Material = &materialInstance,
            .GpuSlot = &gpuSlot,
            .Edges = &edges,
        },
    }};

    visSync.Sync(records, matSys, colorSys, world);

    RHI::GpuEntityConfig config = world.GetEntityConfigForTest(instance);
    EXPECT_FLOAT_EQ(config.Line.LineWidth, 1.0f);
    EXPECT_EQ(config.Line.LineWidthBDA, device.GetBufferDeviceAddress(widthBuffer));

    edges.WidthSource = 3.5f;
    visSync.Sync(records, matSys, colorSys, world);

    config = world.GetEntityConfigForTest(instance);
    EXPECT_FLOAT_EQ(config.Line.LineWidth, 3.5f);
    EXPECT_EQ(config.Line.LineWidthBDA, 0u);

    visSync.Shutdown();
    matSys.Shutdown();
    world.Shutdown();
}

TEST(GraphicsMinimalAcceptance, VisualizationSyncWritesEquivalentLinePointColorSourceConfig)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};

    Graphics::GpuWorld world;
    Graphics::GpuWorld::InitDesc worldInit{};
    worldInit.MaxInstances = 8;
    worldInit.MaxGeometryRecords = 1;
    worldInit.MaxLights = 1;
    worldInit.VertexBufferBytes = 1024;
    worldInit.IndexBufferBytes = 1024;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, worldInit));

    Graphics::MaterialSystem matSys;
    matSys.Initialize(device, bufferMgr);

    Graphics::VisualizationSyncSystem visSync;
    visSync.Initialize(matSys, device);

    Graphics::ColormapSystem colorSys;

    const RHI::BufferHandle scalarBuffer{91u, 1u};
    const RHI::BufferHandle colorBuffer{92u, 1u};
    constexpr std::uint32_t kElementCount = 7u;

    const auto surfaceInstance = world.AllocateInstance(51u);
    const auto lineInstance = world.AllocateInstance(52u);
    const auto pointInstance = world.AllocateInstance(53u);
    ASSERT_TRUE(surfaceInstance.IsValid());
    ASSERT_TRUE(lineInstance.IsValid());
    ASSERT_TRUE(pointInstance.IsValid());

    Graphics::Components::GpuSceneSlot surfaceSlot{};
    Graphics::Components::GpuSceneSlot lineSlot{};
    Graphics::Components::GpuSceneSlot pointSlot{};
    surfaceSlot.SetInstanceHandle(surfaceInstance);
    lineSlot.SetInstanceHandle(lineInstance);
    pointSlot.SetInstanceHandle(pointInstance);
    for (auto* slot : {&surfaceSlot, &lineSlot, &pointSlot})
    {
        slot->Upsert("curvature", scalarBuffer, kElementCount, sizeof(float));
        slot->Upsert("colors", colorBuffer, kElementCount, sizeof(glm::vec4));
    }

    Graphics::Components::MaterialInstance surfaceMaterial{};
    Graphics::Components::MaterialInstance lineMaterial{};
    Graphics::Components::MaterialInstance pointMaterial{};
    Graphics::Components::RenderEdges edges{};
    Graphics::Components::RenderPoints points{};

    Graphics::Components::VisualizationConfig scalarVis{};
    scalarVis.Source =
        Graphics::Components::VisualizationConfig::ColorSource::ScalarField;
    scalarVis.ScalarFieldName = "curvature";
    scalarVis.Scalar.RangeMin = -2.0f;
    scalarVis.Scalar.RangeMax = 5.0f;
    scalarVis.Scalar.BinCount = 4u;
    scalarVis.Scalar.Isolines.Num = 3u;
    scalarVis.Scalar.Isolines.Width = 2.25f;
    scalarVis.Scalar.Isolines.Color = {0.2f, 0.3f, 0.4f, 0.8f};
    scalarVis.ScalarDomain =
        Graphics::Components::VisualizationConfig::Domain::Vertex;

    std::array<Graphics::VisualizationSyncRecord, 3> records{{
        Graphics::VisualizationSyncRecord{
            .StableId = 51u,
            .Material = &surfaceMaterial,
            .GpuSlot = &surfaceSlot,
            .Visualization = &scalarVis,
        },
        Graphics::VisualizationSyncRecord{
            .StableId = 52u,
            .Material = &lineMaterial,
            .GpuSlot = &lineSlot,
            .Visualization = &scalarVis,
            .Edges = &edges,
        },
        Graphics::VisualizationSyncRecord{
            .StableId = 53u,
            .Material = &pointMaterial,
            .GpuSlot = &pointSlot,
            .Visualization = &scalarVis,
            .Points = &points,
        },
    }};

    visSync.Sync(records, matSys, colorSys, world);

    const auto expectScalarConfig = [&](const Graphics::GpuInstanceHandle instance)
    {
        const RHI::GpuEntityConfig config = world.GetEntityConfigForTest(instance);
        EXPECT_EQ(config.ColorSourceMode, kColorSourceScalarField);
        EXPECT_EQ(config.ScalarBDA, device.GetBufferDeviceAddress(scalarBuffer));
        EXPECT_EQ(config.ColorBDA, 0u);
        EXPECT_EQ(config.ElementCount, kElementCount);
        EXPECT_EQ(config.ColormapID, colorSys.GetBindlessIndex(scalarVis.Scalar.Map));
        EXPECT_FLOAT_EQ(config.ScalarRangeMin, -2.0f);
        EXPECT_FLOAT_EQ(config.ScalarRangeMax, 5.0f);
        EXPECT_EQ(config.BinCount, 4u);
        EXPECT_FLOAT_EQ(config.IsolineCount, 3.0f);
        EXPECT_FLOAT_EQ(config.IsolineWidth, 2.25f);
        EXPECT_EQ(config.VisDomain, 0u);
    };
    expectScalarConfig(surfaceInstance);
    expectScalarConfig(lineInstance);
    expectScalarConfig(pointInstance);

    Graphics::Components::VisualizationConfig colorVis = scalarVis;
    colorVis.Source =
        Graphics::Components::VisualizationConfig::ColorSource::PerVertexBuffer;
    colorVis.ColorBufferName = "colors";
    for (auto& record : records)
    {
        record.Visualization = &colorVis;
    }

    visSync.Sync(records, matSys, colorSys, world);

    const auto expectColorConfig = [&](const Graphics::GpuInstanceHandle instance)
    {
        const RHI::GpuEntityConfig config = world.GetEntityConfigForTest(instance);
        EXPECT_EQ(config.ColorSourceMode, kColorSourcePerElementRgba);
        EXPECT_EQ(config.ScalarBDA, 0u);
        EXPECT_EQ(config.ColorBDA, device.GetBufferDeviceAddress(colorBuffer));
        EXPECT_EQ(config.ElementCount, kElementCount);
        EXPECT_EQ(config.VisDomain, 0u);
    };
    expectColorConfig(surfaceInstance);
    expectColorConfig(lineInstance);
    expectColorConfig(pointInstance);

    visSync.Shutdown();
    matSys.Shutdown();
    world.Shutdown();
}
