// GRAPHICS-076 Slice B — CPU-mock contract for the canonical default-recipe
// `DebugViewPass` and its renderer-integrated `"DebugViewPass"` executor
// branch.
//
// The pass-class direct contract (the `BindPipeline + PushConstants +
// Draw(3, 1, 0, 0)` shape, the disabled-selection short-circuit, and the
// missing-pipeline short-circuit) lives in `Test.DebugViewContract.cpp`
// alongside the recipe-side declaration tests. This file focuses on the
// renderer-integrated behaviour Slice B introduces:
//
//   - the executor's `"DebugViewPass"` branch records `Recorded` when the
//     world has the debug overlay enabled and the pipeline lease is
//     valid;
//   - `SkippedUnavailable` when the pipeline lease fails to create;
//   - `SkippedNonOperational` when the device is not operational;
//   - `DebugViewFallbackInvocationCount` increments when the requested
//     resource resolves through fallback, without silent failure;
//   - the recipe simply omits `"DebugViewPass"` from the stats when the
//     world has no debug overlay, so the renderer surface stays clean
//     under the default lifecycle test invariants.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.DebugViewSystem;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.DebugView;
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
// Renderer-integrated tests (executor branch + bind/push/draw shape)
// -----------------------------------------------------------------------------

TEST(DebugViewPassContract, RendererRoutesAndRecordsDebugViewPass)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{401u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    // Enabling the debug overlay flips both `world.DebugOverlayEnabled`
    // and `world.DebugPrimitives.HasTransientDebug` (see
    // `ExtractRenderWorld(...)`), which flips
    // `features.EnableDebugView` and adds `"DebugViewPass"` to the
    // default recipe's pass list.
    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 256, .Height = 144},
        .DebugOverlayEnabled = true,
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* debugViewPass = FindCommandPass(stats, "DebugViewPass");
    ASSERT_NE(debugViewPass, nullptr);
    EXPECT_EQ(debugViewPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.DebugViewPassExecutions, 1u);

    // The canonical pipeline + push-constant + draw triple must reach
    // the command context. Slice A established `BindPipelineCalls >= 7`
    // under the default features; Slice B's DebugView adds at least one
    // more bind on top whenever the debug overlay is enabled. The
    // push-constant call carries the 16-byte `DebugViewPushConstants`
    // block, observable via `PushConstantSizes`.
    EXPECT_GE(device.CommandContext.BindPipelineCalls, 8);
    bool sawDebugViewPushConstant = false;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == static_cast<std::uint32_t>(sizeof(Graphics::DebugViewPushConstants)))
        {
            sawDebugViewPushConstant = true;
            break;
        }
    }
    EXPECT_TRUE(sawDebugViewPushConstant)
        << "Expected a 16-byte DebugViewPushConstants push to reach the device "
           "command context when the canonical default-recipe DebugViewPass records.";

    renderer->Shutdown();
}

TEST(DebugViewPassContract, DebugOverlayDisabledKeepsDebugViewOutOfRecipe)
{
    // GRAPHICS-076 Slice B — the default world does not enable the
    // debug overlay, so the recipe omits `"DebugViewPass"` entirely
    // (`features.EnableDebugView = world.DebugOverlayEnabled ||
    // world.DebugPrimitives.HasTransientDebug`). This pins the
    // invariant the global lifecycle test relies on: the per-frame
    // renderer side-channels (`m_DebugViewSystem`'s settings + resolved
    // selection) cannot leak into the executor stats when the world
    // has not asked for the overlay.
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{402u, 1u};

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
    EXPECT_EQ(FindCommandPass(stats, "DebugViewPass"), nullptr);
    EXPECT_EQ(stats.DebugViewPassExecutions, 0u);
    EXPECT_EQ(stats.DebugViewFallbackInvocationCount, 0u);

    renderer->Shutdown();
}

TEST(DebugViewPassContract, MissingDebugViewPipelineLeaseSkipsUnavailable)
{
    Tests::MockDevice device;
    // GRAPHICS-076 Slice B — the canonical debug-view pipeline is
    // created immediately after present inside
    // `InitializeOperationalPassResources()` (call #24 per the documented
    // ordering, immediately after present at #23). Failing that create
    // exercises the `SkippedUnavailable`
    // path while every upstream pipeline lease (culling / depth /
    // surface / line / point / shadow / deferred / selection /
    // postprocess / present) keeps the rest of the default recipe
    // recording.
    device.FailPipelineCreateCall = 24;
    device.BackbufferHandle = RHI::TextureHandle{403u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 96},
        .DebugOverlayEnabled = true,
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* debugViewPass = FindCommandPass(stats, "DebugViewPass");
    ASSERT_NE(debugViewPass, nullptr);
    EXPECT_EQ(debugViewPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(stats.DebugViewPassExecutions, 0u);

    // Upstream pipelines must still record — a targeted failure at #24
    // leaves the present (#23) pass operational.
    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(DebugViewPassContract, NonOperationalDeviceSkipsNonOperational)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{404u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    device.Operational = false;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
        .DebugOverlayEnabled = true,
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_FALSE(stats.Execute.DeviceOperational);

    const auto* debugViewPass = FindCommandPass(stats, "DebugViewPass");
    ASSERT_NE(debugViewPass, nullptr);
    EXPECT_EQ(debugViewPass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);
    EXPECT_EQ(stats.DebugViewPassExecutions, 0u);

    renderer->Shutdown();
}

TEST(DebugViewPassContract, InvalidResourceFallsBackDeterministicallyAndIncrementsCounter)
{
    // GRAPHICS-076 Slice B — when the runtime / editor configures a
    // `RequestedResourceName` that does not resolve to a previewable
    // resource, the renderer must surface the deterministic fallback
    // through `DebugViewFallbackInvocationCount` instead of silently
    // failing. `"DebugViewRGBA"` is declared by the default recipe but
    // is explicitly excluded from the previewable set (it is the
    // `Pass.DebugView` color attachment; previewing it would alias
    // input and output), so requesting it deterministically routes
    // through the fallback path. The fallback selects the first
    // previewable resource (typically `SceneColorHDR`) so the pass
    // still records `Recorded`; the diagnostic counter signals to
    // runtime/editor that the request was non-trivially resolved.
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{405u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    renderer->SetDebugViewRequestedResourceName("DebugViewRGBA");
    EXPECT_EQ(renderer->GetDebugViewRequestedResourceName(), "DebugViewRGBA");

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 96},
        .DebugOverlayEnabled = true,
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* debugViewPass = FindCommandPass(stats, "DebugViewPass");
    ASSERT_NE(debugViewPass, nullptr);
    // Fallback succeeded against an available previewable resource, so
    // the pass still records — that is the "no silent failure"
    // guarantee. If the fallback were also unavailable the pass would
    // skip `SkippedUnavailable` instead, which we cover elsewhere.
    EXPECT_EQ(debugViewPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.DebugViewPassExecutions, 1u);
    EXPECT_EQ(stats.DebugViewFallbackInvocationCount, 1u);

    renderer->Shutdown();
}

TEST(DebugViewPassContract, DefaultRequestedResourceDoesNotIncrementFallbackCounter)
{
    // GRAPHICS-076 Slice B — when the runtime has not overridden the
    // requested resource, the default `"FrameRecipe.PresentSource"`
    // sentinel is routed through `ResolveSelection`'s
    // `requestedPresentSource` branch and does NOT mark
    // `UsedFallback = true` (it is the canonical "no real selection
    // → show present source" path, not a fallback from a missing
    // resource). This keeps the diagnostic counter quiet under the
    // common "overlay on, no selection yet" UX state.
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{406u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    EXPECT_EQ(renderer->GetDebugViewRequestedResourceName(), "FrameRecipe.PresentSource");

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 96},
        .DebugOverlayEnabled = true,
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* debugViewPass = FindCommandPass(stats, "DebugViewPass");
    ASSERT_NE(debugViewPass, nullptr);
    EXPECT_EQ(debugViewPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.DebugViewPassExecutions, 1u);
    EXPECT_EQ(stats.DebugViewFallbackInvocationCount, 0u);

    renderer->Shutdown();
}
