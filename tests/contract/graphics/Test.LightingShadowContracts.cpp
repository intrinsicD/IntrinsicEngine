#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.DeferredSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.LightSystem;
import Extrinsic.Graphics.Pass.Deferred.Lighting;
import Extrinsic.Graphics.Pass.Shadows;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    enum class EventKind
    {
        BindPipeline,
        BindIndexBuffer,
        PushConstants,
        Draw,
        DrawIndexedIndirectCount,
    };

    struct Event
    {
        EventKind Kind{};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events;
        RHI::PipelineHandle LastPipeline{};
        RHI::BufferHandle LastIndexBuffer{};
        RHI::BufferHandle LastIndirectArgs{};
        RHI::BufferHandle LastIndirectCount{};
        std::uint32_t LastMaxDrawCount = 0;
        std::uint32_t LastDrawVertexCount = 0;
        std::vector<std::byte> LastPushConstants{};

        void Begin() override {}
        void End() override {}
        void BeginRenderPass(const RHI::RenderPassDesc&) override {}
        void EndRenderPass() override {}
        void SetViewport(float, float, float, float, float, float) override {}
        void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
        void BindPipeline(RHI::PipelineHandle pipeline) override
        {
            Events.push_back({.Kind = EventKind::BindPipeline});
            LastPipeline = pipeline;
        }
        void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t, RHI::IndexType) override
        {
            Events.push_back({.Kind = EventKind::BindIndexBuffer});
            LastIndexBuffer = buffer;
        }
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::PushConstants});
            LastPushConstants.resize(size);
            if (size > 0u && data != nullptr)
            {
                std::memcpy(LastPushConstants.data(), data, size);
            }
        }
        void Draw(std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::Draw});
            LastDrawVertexCount = vertexCount;
        }
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle argBuffer, std::uint64_t,
                                      RHI::BufferHandle countBuffer, std::uint64_t,
                                      std::uint32_t maxDrawCount) override
        {
            Events.push_back({.Kind = EventKind::DrawIndexedIndirectCount});
            LastIndirectArgs = argBuffer;
            LastIndirectCount = countBuffer;
            LastMaxDrawCount = maxDrawCount;
        }
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    Graphics::GpuWorld::InitDesc TinyWorldDesc()
    {
        Graphics::GpuWorld::InitDesc init{};
        init.MaxInstances = 4;
        init.MaxGeometryRecords = 4;
        init.MaxLights = 4;
        init.VertexBufferBytes = 4096;
        init.IndexBufferBytes = 4096;
        return init;
    }
}

TEST(GraphicsLightingShadowContracts, LightDefaultsApplyToCameraAndFallbackDirectional)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));

    Graphics::LightSystem lights;
    lights.Initialize();

    RHI::CameraUBO camera{};
    lights.ApplyTo(camera);
    EXPECT_EQ(camera.LightDirAndIntensity, glm::vec4(0.f, -1.f, 0.f, 1.f));
    EXPECT_EQ(camera.LightColor, glm::vec4(1.f, 1.f, 1.f, 0.f));
    EXPECT_EQ(camera.AmbientColorAndIntensity, glm::vec4(0.2f, 0.2f, 0.2f, 1.f));

    const std::vector<Graphics::LightSnapshot> snapshots{
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Point,
            .Position = {1.f, 2.f, 3.f},
            .Range = 8.f,
            .Intensity = 2.f,
            .Color = {0.5f, 0.25f, 1.f},
        },
        Graphics::LightSnapshot{
            .LightType = static_cast<Graphics::LightSnapshot::Type>(255u),
        },
    };

    const Graphics::LightEnvironmentPacket packet = lights.BuildEnvironmentPacket(snapshots);
    EXPECT_EQ(packet.UploadedLightCount, 2u);
    EXPECT_EQ(packet.UnsupportedLightCount, 1u);
    EXPECT_TRUE(packet.UsedFallbackDirectional);

    lights.SyncGpuBuffer(snapshots, world);
    EXPECT_EQ(world.GetLightCount(), 2u);
    const Graphics::LightSyncDiagnostics diagnostics = lights.GetDiagnostics();
    EXPECT_EQ(diagnostics.UploadedLightCount, 2u);
    EXPECT_EQ(diagnostics.UnsupportedLightCount, 1u);
    EXPECT_TRUE(diagnostics.UsedFallbackDirectional);

    world.Shutdown();
}

TEST(GraphicsLightingShadowContracts, DirectionalSnapshotSuppressesFallbackLight)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));

    Graphics::LightSystem lights;
    lights.Initialize();

    const std::vector<Graphics::LightSnapshot> snapshots{
        Graphics::LightSnapshot{
            .LightType = Graphics::LightSnapshot::Type::Directional,
            .Direction = {0.f, -2.f, 0.f},
            .Intensity = 3.f,
            .Color = {1.f, 0.5f, 0.25f},
        },
    };

    lights.SyncGpuBuffer(snapshots, world);
    EXPECT_EQ(world.GetLightCount(), 1u);
    EXPECT_FALSE(lights.GetDiagnostics().UsedFallbackDirectional);

    world.Shutdown();
}

TEST(GraphicsLightingShadowContracts, ShadowParamsClampCascadesAndPackCameraAtlasState)
{
    MockDevice device;
    RHI::SamplerManager samplerMgr{device};
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};
    Graphics::ShadowSystem shadows;
    shadows.Initialize(device, textureMgr, samplerMgr);

    shadows.SetParams(Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 6u,
        .AtlasResolution = 512u,
        .DepthBias = 0.002f,
        .NormalBias = 0.03f,
        .PcfRadius = 2.0f,
        .SplitLambda = 0.75f,
    });

    Graphics::ShadowCascadeData cascades{};
    cascades.ViewProj[0] = glm::mat4{2.f};
    cascades.Splits[0] = 0.1f;
    cascades.Splits[1] = 0.35f;
    cascades.Splits[2] = 0.7f;
    cascades.CascadeCount = 6u;
    shadows.SetCascadeData(cascades);

    const Graphics::ShadowAtlasDesc atlas = shadows.BuildAtlasDesc();
    EXPECT_TRUE(atlas.Enabled);
    EXPECT_EQ(atlas.CascadeCount, RHI::kMaxShadowCascades);
    EXPECT_EQ(atlas.Width, 512u * RHI::kMaxShadowCascades);
    EXPECT_EQ(atlas.Height, 512u);
    EXPECT_EQ(shadows.GetDiagnostics().UnsupportedCascadeCount, 2u);

    RHI::CameraUBO camera{};
    shadows.ApplyTo(camera);
    EXPECT_EQ(camera.ShadowCascadeMatrices[0], glm::mat4{2.f});
    EXPECT_EQ(camera.ShadowCascadeSplitsAndCount, glm::vec4(0.1f, 0.35f, 0.7f, 4.f));
    EXPECT_EQ(camera.ShadowBiasAndFilter, glm::vec4(0.002f, 0.03f, 2.0f, 0.75f));
    EXPECT_EQ(camera.ShadowAtlasSizeAndFlags, glm::vec4(2048.f, 512.f, 1.f, 0.f));
}

TEST(GraphicsLightingShadowContracts, ShadowPassSkipsDisabledShadowsAndUsesShadowBucketWhenEnabled)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    RHI::SamplerManager samplerMgr{device};
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};
    Graphics::ShadowSystem shadows;
    shadows.Initialize(device, textureMgr, samplerMgr);
    Graphics::ShadowPass pass{shadows};
    const RHI::PipelineHandle pipeline{301u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    RecordingCommandContext disabledCmd;
    pass.Execute(disabledCmd, camera, world, culling, 9u);
    EXPECT_TRUE(disabledCmd.Events.empty());

    shadows.SetParams(Graphics::ShadowParams{.Enabled = true, .CascadeCount = 2u, .AtlasResolution = 256u});
    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 9u);

    ASSERT_EQ(cmd.Events.size(), 4u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::BindIndexBuffer);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[3].Kind, EventKind::DrawIndexedIndirectCount);
    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastIndexBuffer, world.GetManagedIndexBuffer());

    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::ShadowOpaque);
    EXPECT_EQ(cmd.LastIndirectArgs, bucket.IndexedArgsBuffer);
    EXPECT_EQ(cmd.LastIndirectCount, bucket.CountBuffer);
    EXPECT_EQ(cmd.LastMaxDrawCount, bucket.Capacity);

    ASSERT_EQ(cmd.LastPushConstants.size(), sizeof(RHI::GpuScenePushConstants));
    RHI::GpuScenePushConstants pc{};
    std::memcpy(&pc, cmd.LastPushConstants.data(), sizeof(pc));
    EXPECT_EQ(pc.SceneTableBDA, world.GetSceneTableBDA());
    EXPECT_EQ(pc.FrameIndex, 9u);
    EXPECT_EQ(pc.DrawBucket, static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::ShadowOpaque));

    culling.Shutdown();
    world.Shutdown();
}

TEST(GraphicsLightingShadowContracts, DeferredLightingRecordsFullscreenDrawWhenInitialized)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));

    Graphics::DeferredSystem deferred;
    // GRAPHICS-072 Slice C — DeferredLightingPass takes a `ShadowSystem&`
    // so `Execute(...)` can publish the atlas bindless index through push
    // constants. The system is constructed and left uninitialized for this
    // contract test — `GetAtlasBindlessIndex()` returns
    // `kInvalidBindlessIndex` in that state, which keeps the pushed
    // payload deterministic without requiring a backing TextureManager.
    Graphics::ShadowSystem shadows;
    Graphics::DeferredLightingPass pass{deferred, shadows};
    const RHI::PipelineHandle pipeline{401u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    RecordingCommandContext uninitializedCmd;
    pass.Execute(uninitializedCmd, camera, world);
    EXPECT_TRUE(uninitializedCmd.Events.empty());

    deferred.Initialize();
    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, world);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::Draw);
    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastDrawVertexCount, 3u);
    ASSERT_EQ(cmd.LastPushConstants.size(), 16u);

    deferred.Shutdown();
    world.Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-073 Slice B — `ShadowSystem`-owned atlas + `sampler2DShadow`-
// bindable sampler. The system lazily allocates both on the first
// `SetParams(...)` call that enables shadows so the operational
// `Initialize(...)` path (which runs with shadows disabled by default)
// does not allocate a 2048×2048 D32 atlas that no caller would sample.
// ---------------------------------------------------------------------------

TEST(GraphicsLightingShadowContracts, ShadowSystemDoesNotAllocateAtlasWhileDisabled)
{
    MockDevice device;
    device.Operational = true;
    RHI::SamplerManager samplerMgr{device};
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::ShadowSystem shadows;
    shadows.Initialize(device, textureMgr, samplerMgr);

    EXPECT_FALSE(shadows.GetAtlasTexture().IsValid());
    EXPECT_FALSE(shadows.GetAtlasSampler().IsValid());
    EXPECT_FALSE(shadows.GetAllocatedAtlasDesc().Enabled);
    EXPECT_EQ(device.CreateTextureCount, 0);
    EXPECT_EQ(device.CreateSamplerCount, 0);
}

TEST(GraphicsLightingShadowContracts, ShadowSystemAllocatesAtlasAndSamplerWhenShadowsEnabled)
{
    MockDevice device;
    device.Operational = true;
    RHI::SamplerManager samplerMgr{device};
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::ShadowSystem shadows;
    shadows.Initialize(device, textureMgr, samplerMgr);

    shadows.SetParams(Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 3u,
        .AtlasResolution = 512u,
    });

    EXPECT_TRUE(shadows.GetAtlasTexture().IsValid());
    EXPECT_TRUE(shadows.GetAtlasSampler().IsValid());

    const Graphics::ShadowAtlasDesc allocated = shadows.GetAllocatedAtlasDesc();
    EXPECT_TRUE(allocated.Enabled);
    EXPECT_EQ(allocated.CascadeCount, 3u);
    EXPECT_EQ(allocated.Resolution, 512u);
    EXPECT_EQ(allocated.Width, 512u * 3u);
    EXPECT_EQ(allocated.Height, 512u);

    EXPECT_EQ(device.CreateTextureCount, 1);
    EXPECT_EQ(device.CreateSamplerCount, 1);

    // GRAPHICS-073 Slice B — second `SetParams(...)` keeps the atlas
    // byte-identical (no realloc). An explicit `Resize()` seam is a
    // GRAPHICS-072 follow-up; today, atlas resizes route through
    // `Shutdown()` + `Initialize(...)`.
    const RHI::TextureHandle initialAtlas = shadows.GetAtlasTexture();
    shadows.SetParams(Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 2u,
        .AtlasResolution = 1024u,
    });
    EXPECT_EQ(shadows.GetAtlasTexture(), initialAtlas);
    EXPECT_EQ(device.CreateTextureCount, 1);
}

TEST(GraphicsLightingShadowContracts, ShadowSystemReleasesAtlasAndSamplerOnShutdown)
{
    MockDevice device;
    device.Operational = true;
    RHI::SamplerManager samplerMgr{device};
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::ShadowSystem shadows;
    shadows.Initialize(device, textureMgr, samplerMgr);
    shadows.SetParams(Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 2u,
        .AtlasResolution = 256u,
    });
    EXPECT_TRUE(shadows.GetAtlasTexture().IsValid());

    shadows.Shutdown();

    EXPECT_FALSE(shadows.GetAtlasTexture().IsValid());
    EXPECT_FALSE(shadows.GetAtlasSampler().IsValid());
    EXPECT_FALSE(shadows.IsInitialized());
    EXPECT_EQ(device.DestroyTextureCount, 1);
}

TEST(GraphicsLightingShadowContracts, ShadowPassRecordsMissingCasterDiagnosticWhenBucketEmpty)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::SamplerManager samplerMgr{device};
    RHI::TextureManager textureMgr{device, device.GetBindlessHeap()};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    // GRAPHICS-073 Slice B — intentionally skip `culling.Initialize(...)` so
    // `GetBucket(ShadowOpaque)` returns the default-constructed bucket with
    // `Capacity == 0`. This is the "casters never extracted" condition the
    // missing-caster diagnostic is for.
    Graphics::CullingSystem culling;

    Graphics::ShadowSystem shadows;
    shadows.Initialize(device, textureMgr, samplerMgr);
    shadows.SetParams(Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 2u,
        .AtlasResolution = 256u,
    });

    Graphics::ShadowPass pass{shadows};
    pass.SetPipeline(RHI::PipelineHandle{401u, 1u});

    RHI::CameraUBO camera{};
    RecordingCommandContext cmd;

    EXPECT_EQ(shadows.GetDiagnostics().MissingCasterCount, 0u);
    pass.Execute(cmd, camera, world, culling, 0u);
    EXPECT_TRUE(cmd.Events.empty());
    EXPECT_EQ(shadows.GetDiagnostics().MissingCasterCount, 1u);

    // Repeated executions keep incrementing — the diagnostic is a counter,
    // not a one-shot flag.
    pass.Execute(cmd, camera, world, culling, 1u);
    EXPECT_TRUE(cmd.Events.empty());
    EXPECT_EQ(shadows.GetDiagnostics().MissingCasterCount, 2u);

    // Disabling shadows turns the pass into the Slice A early-return
    // (`!IsEnabled()`) which does NOT touch the missing-caster counter.
    shadows.SetParams(Graphics::ShadowParams{});
    pass.Execute(cmd, camera, world, culling, 2u);
    EXPECT_TRUE(cmd.Events.empty());
    EXPECT_EQ(shadows.GetDiagnostics().MissingCasterCount, 2u);

    world.Shutdown();
}

