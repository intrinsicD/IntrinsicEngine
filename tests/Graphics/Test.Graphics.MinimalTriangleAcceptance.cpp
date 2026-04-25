#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <entt/entity/registry.hpp>
#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.Graphics.ColormapSystem;
import Extrinsic.Graphics.Component.GpuSceneSlot;
import Extrinsic.Graphics.Component.Material;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.GpuWorld;
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
    culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/src_new/culling/instance_cull.comp");

    const auto baseType = matSys.FindType("StandardPBR");
    ASSERT_TRUE(baseType.IsValid());

    entt::registry registry;
    const auto e = registry.create();

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

    auto& gpuSlot = registry.emplace<Graphics::Components::GpuSceneSlot>(e);
    gpuSlot.SetInstanceHandle(instance);
    gpuSlot.SetGeometryHandle(geometry);

    world.SetInstanceGeometry(instance, geometry);

    Graphics::MaterialParams materialParams{};
    materialParams.BaseColorFactor = {0.8f, 0.7f, 0.6f, 1.0f};
    auto baseLease = matSys.CreateInstance(baseType, materialParams);
    ASSERT_TRUE(baseLease.IsValid());
    registry.emplace<Graphics::Components::MaterialInstance>(e, Graphics::Components::MaterialInstance{.Lease = std::move(baseLease)});

    registry.emplace<Graphics::Components::RenderSurface>(e);
    registry.emplace<Graphics::Components::VisualizationConfig>(
        e,
        Graphics::Components::VisualizationConfig{
            .Source = Graphics::Components::VisualizationConfig::ColorSource::UniformColor,
            .Color = {1.0f, 0.1f, 0.1f, 1.0f},
        });

    registry.emplace<ECS::Components::Transform::WorldMatrix>(e, ECS::Components::Transform::WorldMatrix{.Matrix = glm::mat4{1.0f}});

    ECS::Components::Culling::Bounds localBounds{};
    localBounds.LocalBoundingSphere.Center = {0.0f, 0.0f, 0.0f};
    localBounds.LocalBoundingSphere.Radius = 1.0f;
    registry.emplace<ECS::Components::Culling::Bounds>(e, localBounds);

    visSync.Sync(registry, matSys, colorSys, world);
    matSys.SyncGpuBuffer();
    transformSync.SyncGpuBuffer(registry, world);
    world.SetMaterialBuffer(matSys.GetBuffer(), matSys.GetCapacity());
    world.SyncFrame();

    const auto& materialInst = registry.get<Graphics::Components::MaterialInstance>(e);
    EXPECT_NE(materialInst.EffectiveSlot, 0u);
    EXPECT_EQ(visSync.GetOverrideLeaseCount(), 1u);

    Graphics::CullingPass cullPass{culling};
    Graphics::DeferredSystem deferred;
    deferred.Initialize();
    Graphics::GBufferPass gbufferPass{deferred};
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
    matSys.Shutdown();
    world.Shutdown();
}
