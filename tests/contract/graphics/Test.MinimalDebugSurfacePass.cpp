// GRAPHICS-032B — CPU-mock contract for the MinimalDebugSurface pass and
// its renderer-integrated executor branch.
//
// The pass-class tests mirror Test.SurfacePassContracts (RecordingCommandContext
// + GpuWorld + CullingSystem) to assert the ordered command stream against the
// SurfaceOpaque bucket.
//
// The renderer-integrated tests mirror Test.RendererFrameLifecycle to exercise
// the executor route through SetFrameRecipe(MinimalDebug) and assert the three
// diagnostic counters declared by GRAPHICS-032A (now driven by 032B for the
// surface pass).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.CullingSystem;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.Surface.MinimalDebug;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
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
        void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
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
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

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

    [[nodiscard]] const Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Graphics::RenderGraphFrameStats& stats,
        const std::string& name)
    {
        for (const auto& pass : stats.CommandRecords.Passes)
        {
            if (pass.Name == name)
            {
                return &pass;
            }
        }
        return nullptr;
    }
}

// -----------------------------------------------------------------------------
// Pass-class direct tests
// -----------------------------------------------------------------------------

TEST(MinimalDebugSurfacePassContract, ExecuteRecordsSurfaceOpaqueIndirectDrawInOrder)
{
    MockDevice device;
    RHI::BufferManager bufferMgr{device};
    RHI::PipelineManager pipelineMgr{device};

    Graphics::GpuWorld world;
    ASSERT_TRUE(world.Initialize(device, bufferMgr, TinyWorldDesc()));
    world.SyncFrame();

    Graphics::CullingSystem culling;
    ASSERT_TRUE(culling.Initialize(device, bufferMgr, pipelineMgr, "shaders/culling/instance_cull.comp"));

    Graphics::MinimalDebugSurfacePass pass;

    RecordingCommandContext noPipelineCmd;
    RHI::CameraUBO camera{};
    pass.Execute(noPipelineCmd, camera, world, culling, 7u);
    EXPECT_TRUE(noPipelineCmd.Events.empty())
        << "MinimalDebugSurface must short-circuit when its slot-0 pipeline is unset.";

    const RHI::PipelineHandle pipeline{4242u, 1u};
    pass.SetPipeline(pipeline);
    EXPECT_EQ(pass.GetPipeline(), pipeline);

    RecordingCommandContext cmd;
    pass.Execute(cmd, camera, world, culling, 7u);

    ASSERT_EQ(cmd.Events.size(), 4u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::BindIndexBuffer);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[3].Kind, EventKind::DrawIndexedIndirectCount);

    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastIndexBuffer, world.GetManagedIndexBuffer());

    ASSERT_EQ(cmd.LastPushConstants.size(), sizeof(RHI::GpuScenePushConstants));
    RHI::GpuScenePushConstants pc{};
    std::memcpy(&pc, cmd.LastPushConstants.data(), sizeof(pc));
    EXPECT_EQ(pc.SceneTableBDA, world.GetSceneTableBDA());
    EXPECT_EQ(pc.FrameIndex, 7u);
    EXPECT_EQ(pc.DrawBucket, static_cast<std::uint32_t>(RHI::GpuDrawBucketKind::SurfaceOpaque));

    const auto& bucket = culling.GetBucket(RHI::GpuDrawBucketKind::SurfaceOpaque);
    EXPECT_EQ(cmd.LastIndirectArgs, bucket.IndexedArgsBuffer);
    EXPECT_EQ(cmd.LastIndirectCount, bucket.CountBuffer);
    EXPECT_EQ(cmd.LastMaxDrawCount, bucket.Capacity);

    culling.Shutdown();
    world.Shutdown();
}

// -----------------------------------------------------------------------------
// Renderer-integrated tests (executor branch + counters)
// -----------------------------------------------------------------------------

TEST(MinimalDebugSurfacePassContract, RendererRoutesAndIncrementsExecutionsCounter)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{511u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);
    EXPECT_EQ(renderer->GetFrameRecipe(), Core::Config::FrameRecipeKind::MinimalDebug);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 256, .Height = 192},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    // The minimal recipe declares exactly two passes; the default recipe must
    // not have been built alongside.
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass"), nullptr)
        << "MinimalDebug recipe must not pull in default-recipe passes.";
    EXPECT_EQ(FindCommandPass(stats, "CullingPass"), nullptr)
        << "MinimalDebug recipe must not pull in the default-recipe culling pass.";

    const auto* surfacePass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugSurfacePassName});
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 1u);
    EXPECT_EQ(stats.MinimalRecipeMissingPrerequisiteCount, 0u)
        << "Operational MockDevice with valid imports leaves the prerequisite "
           "counter at zero from both the recipe build and the executor route.";

    renderer->Shutdown();
}

TEST(MinimalDebugSurfacePassContract, MissingSlotZeroPipelineLeaseSkipsUnavailableAndIncrementsCounter)
{
    Tests::MockDevice device;
    // The slot-0 default-debug-surface pipeline is the third pipeline created
    // by InitializeOperationalPassResources (culling, depth-prepass, then
    // default-debug-surface). FailPipelineCreateCall=3 makes the slot-0 lease
    // fail, which deterministically exercises the SkippedUnavailable record
    // path for the MinimalDebugSurface pass.
    device.FailPipelineCreateCall = 3;

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 96},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* surfacePass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugSurfacePassName});
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 0u);
    EXPECT_GE(stats.MinimalRecipeMissingPrerequisiteCount, 1u)
        << "Missing slot-0 pipeline lease must bump MinimalRecipeMissingPrerequisiteCount at the record site.";

    renderer->Shutdown();
}

TEST(MinimalDebugSurfacePassContract, MissingCullingBucketSkipsUnavailableAndIncrementsCounter)
{
    Tests::MockDevice device;
    // FailPipelineCreateCall=1 trips the culling-system pipeline creation, so
    // CullingSystem::Initialize() fails and the SurfaceOpaque bucket is empty.
    // The minimal recipe's record path must bump MinimalRecipeMissingPrerequisiteCount.
    device.FailPipelineCreateCall = 1;

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 96},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* surfacePass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugSurfacePassName});
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 0u);
    EXPECT_GE(stats.MinimalRecipeMissingPrerequisiteCount, 1u)
        << "Missing SurfaceOpaque bucket residency must bump MinimalRecipeMissingPrerequisiteCount.";

    renderer->Shutdown();
}

TEST(MinimalDebugSurfacePassContract, NonOperationalDeviceSkipsNonOperational)
{
    // Initialize with an operational MockDevice so the slot-0 pipeline lease
    // and CullingSystem resources land, then flip the device to non-operational
    // before ExecuteFrame to exercise the SkippedNonOperational record path.
    Tests::MockDevice device;

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

    device.Operational = false;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_FALSE(stats.Execute.DeviceOperational);

    const auto* surfacePass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugSurfacePassName});
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);

    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 0u);

    renderer->Shutdown();
}
