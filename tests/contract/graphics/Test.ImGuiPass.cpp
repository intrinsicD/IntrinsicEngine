// GRAPHICS-079 Slice A — CPU-mock contract for the canonical default-recipe
// `Pass.ImGui` and its renderer-integrated `"ImGuiPass"` executor branch.
//
// Before this slice the recipe declared `"ImGuiPass"` every default-recipe
// frame (`features.EnableImGui` defaults true) but the executor had no
// branch for it, so the pass name fell through to the soft-skip default.
// Slice A wires the renderer-owned `ImGuiPass` consumer over the engine-owned
// `ImGuiOverlaySystem` handed in via `SetImGuiOverlaySystem`, creates the
// `m_ImGuiPipelineLease`, and routes the branch through `RecordImGuiPass`.
//
// Critical safety invariant pinned here: the overlay draw is a
// `BindPipeline + DrawIndexed` sequence that is only valid inside a render
// pass, but the default recipe currently declares `"ImGuiPass"` as a
// `Read(FrameRecipe.PresentSource) + SideEffect()` node with NO color
// attachment, so the executor begins no render pass for it. Recording the
// draw there would be invalid command-buffer usage on Vulkan, so until
// Slice D promotes ImGui to write `FrameRecipe.PresentSource` the route must
// stay skipped even when an operational device has an attached overlay with
// submitted work. These tests therefore pin `SkippedUnavailable` for the
// attached-overlay cases; the `Recorded` proof is owned by Slice D (and the
// `gpu;vulkan` smoke), where the render-pass scope exists. `RecordImGuiPass`
// gates the `Recorded` path on the live `activeRenderPass.HasAttachments`
// signal, so it turns on automatically once the write-topology lands.
//
// The recipe-declaration shape (ImGui reads `FrameRecipe.PresentSource`, does
// not write `Backbuffer`) and the render-graph rejection of a non-present
// `Backbuffer` write are covered by `Test.ImGuiPresentContract.cpp`.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.ImGuiOverlaySystem;
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

    // One accepted overlay draw list (non-zero command/vertex/index counts)
    // so `ImGuiOverlaySystem::HasOverlayWork()` becomes true after submission.
    [[nodiscard]] Graphics::ImGuiOverlayFrame MakeOverlayFrameWithWork()
    {
        Graphics::ImGuiOverlayFrame frame{};
        frame.Enabled = true;
        frame.DisplayWidth = 256u;
        frame.DisplayHeight = 144u;
        frame.DrawLists.push_back(Graphics::ImGuiOverlayDrawList{
            .CommandCount = 1u,
            .VertexCount = 4u,
            .IndexCount = 6u,
            .UsesUserTexture = false,
        });
        return frame;
    }

    [[nodiscard]] Graphics::RenderWorld DriveDefaultFrame(Graphics::IRenderer& renderer,
                                                          const RHI::FrameHandle& frame)
    {
        const Graphics::RenderFrameInput input{
            .Viewport = {.Width = 256, .Height = 144},
        };
        Graphics::RenderWorld world = renderer.ExtractRenderWorld(input);
        renderer.PrepareFrame(world);
        renderer.ExecuteFrame(frame, world);
        return world;
    }
}

TEST(ImGuiPassContract, AttachedOverlayWithWorkSkipsUnavailableWithoutRenderTargetAfterInitialize)
{
    // The core safety case: an operational device with an attached overlay
    // that has submitted work must NOT record the overlay draw, because the
    // default recipe declares `"ImGuiPass"` SideEffect-only (no color
    // attachment → no render pass). Recording here would be invalid on
    // Vulkan; the explicit route reports `SkippedUnavailable` (not a false
    // `Recorded`, and not the soft-skip default).
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{501u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    EXPECT_EQ(renderer->GetFrameRecipe(), Core::Config::FrameRecipeKind::Default);

    // Engine-owned overlay system handed in AFTER Initialize(): the pipeline
    // lease already exists, so `SetImGuiOverlaySystem` binds it to the freshly
    // constructed pass. Only the missing render-pass attachment blocks
    // recording.
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());
    ASSERT_TRUE(overlay.HasOverlayWork());
    renderer->SetImGuiOverlaySystem(&overlay);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    // The canonical present pass still finalizes the backbuffer; ImGui's skip
    // is specific to its missing render target, not a recipe-wide regression.
    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, AttachedOverlayWithWorkSkipsUnavailableWhenAttachedBeforeInitialize)
{
    // Reverse attach order: the runtime may hand the overlay in before the
    // renderer initializes. The pass is emplaced without a pipeline, and
    // `InitializeOperationalPassResources` binds the lease to it once created;
    // the route still skips because the recipe gives ImGui no render target.
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{502u, 1u};

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->SetImGuiOverlaySystem(&overlay);
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, NoOverlaySystemAttachedSkipsUnavailable)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{503u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    // The pass is declared every default-recipe frame, but with no overlay
    // system attached the explicit route reports `SkippedUnavailable` rather
    // than falling through to the executor soft-skip default.
    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    // Present still records — the missing overlay does not regress the rest of
    // the default recipe.
    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, AttachedOverlayWithoutSubmittedWorkSkipsUnavailable)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{504u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    // Initialized overlay but no submitted frame, so `HasOverlayWork()` is
    // false; the helper must surface `SkippedUnavailable`.
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    ASSERT_FALSE(overlay.HasOverlayWork());
    renderer->SetImGuiOverlaySystem(&overlay);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, DetachingOverlaySystemSkipsUnavailable)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{505u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());
    renderer->SetImGuiOverlaySystem(&overlay);
    renderer->SetImGuiOverlaySystem(nullptr);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, NonOperationalDeviceSkipsNonOperational)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{506u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());
    renderer->SetImGuiOverlaySystem(&overlay);

    device.Operational = false;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_FALSE(stats.Execute.DeviceOperational);

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, RouteStaysWiredWithoutRecordingAfterOperationalRebuild)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{507u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());
    renderer->SetImGuiOverlaySystem(&overlay);

    // Rebuilding operational resources recreates the ImGui pipeline lease and
    // re-binds the consumer pass. The route stays wired (and Present keeps
    // recording), but ImGui still skips because the recipe gives it no render
    // target yet.
    ASSERT_TRUE(renderer->RebuildOperationalResources(device));

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}
