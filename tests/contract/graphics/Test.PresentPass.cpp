// GRAPHICS-076 Slice A — CPU-mock contract for the canonical default-recipe
// `PresentPass` and its renderer-integrated `"Present"` executor branch.
//
// The pass-class tests use a local `RecordingCommandContext` plus a direct
// `Execute` invocation to assert the ordered command stream: `BindPipeline`
// followed by `Draw(3, 1, 0, 0)`.
//
// The renderer-integrated tests assert that the default recipe routes
// `"Present"` through `RecordPresentPass(...)` and records `Recorded`
// when the present pipeline lease is valid, falls back to
// `SkippedUnavailable` when the lease is missing, and falls back to
// `SkippedNonOperational` when the device is not operational. The
// per-frame device-side `BindPipelineCalls` assertion catches the
// canonical present bind/draw shape reaching the command context.
//
// The lifecycle-test refresh in `Test.RendererFrameLifecycle.cpp` keeps
// the global "every default-recipe pass has a structured status" invariant
// in step with this new routed pass; this file is the focused contract
// pin for the present-side behavior.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.Present;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

#include "MockRHI.hpp"

using namespace Extrinsic;
using Tests::MockDevice;

namespace
{
    enum class EventKind
    {
        BindPipeline,
        Draw,
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
        std::uint32_t LastVertexCount = 0;
        std::uint32_t LastInstanceCount = 0;
        std::uint32_t LastFirstVertex = 0;
        std::uint32_t LastFirstInstance = 0;

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
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
        void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
        void Draw(std::uint32_t vertexCount,
                  std::uint32_t instanceCount,
                  std::uint32_t firstVertex,
                  std::uint32_t firstInstance) override
        {
            Events.push_back({.Kind = EventKind::Draw});
            LastVertexCount = vertexCount;
            LastInstanceCount = instanceCount;
            LastFirstVertex = firstVertex;
            LastFirstInstance = firstInstance;
        }
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle,
                                      std::uint64_t, std::uint32_t) override {}
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

TEST(PresentPassContract, ExecuteRecordsBindPipelineThenFullscreenDrawInOrder)
{
    Graphics::PresentPass pass;

    RecordingCommandContext noPipelineCmd;
    pass.Execute(noPipelineCmd);
    EXPECT_TRUE(noPipelineCmd.Events.empty())
        << "Canonical PresentPass must short-circuit when its pipeline "
           "lease is unset.";

    const RHI::PipelineHandle pipeline{1024u, 7u};
    pass.SetPipeline(pipeline);

    RecordingCommandContext cmd;
    pass.Execute(cmd);

    ASSERT_EQ(cmd.Events.size(), 2u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::Draw);

    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastVertexCount, 3u);
    EXPECT_EQ(cmd.LastInstanceCount, 1u);
    EXPECT_EQ(cmd.LastFirstVertex, 0u);
    EXPECT_EQ(cmd.LastFirstInstance, 0u);
}

// -----------------------------------------------------------------------------
// Renderer-integrated tests (executor branch + bind/draw shape)
// -----------------------------------------------------------------------------

TEST(PresentPassContract, RendererRoutesAndRecordsPresentPass)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{301u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 256, .Height = 144},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    // The lifecycle test in `Test.RendererFrameLifecycle.cpp` asserts the
    // exact global `BindPipelineCalls` count after Slice A bumps the
    // value by one for the present bind. This focused test only requires
    // that the present bind reached the command context (at least one
    // additional bind beyond the four routed draw passes that bind
    // canonical pipelines under the default features).
    EXPECT_GE(device.CommandContext.BindPipelineCalls, 7);

    renderer->Shutdown();
}

TEST(PresentPassContract, MissingPresentPipelineLeaseSkipsUnavailable)
{
    Tests::MockDevice device;
    // GRAPHICS-076 Slice A — the canonical present pipeline is created
    // after postprocess inside `InitializeOperationalPassResources()` (call
    // #23 per the documented ordering in `Test.RendererFrameLifecycle.cpp`).
    // Failing that create exercises the `SkippedUnavailable` path while
    // every upstream pipeline lease (culling / depth / surface / line /
    // point / shadow / deferred / selection / postprocess) keeps the
    // rest of the default recipe recording.
    device.FailPipelineCreateCall = 23;
    device.BackbufferHandle = RHI::TextureHandle{302u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

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

    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    // The forward / postprocess passes' pipelines were created
    // independently before the targeted failure point, so they must
    // still record.
    const auto* surfacePass = FindCommandPass(stats, "SurfacePass");
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(PresentPassContract, NonOperationalDeviceSkipsNonOperational)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{303u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

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

    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);

    renderer->Shutdown();
}
