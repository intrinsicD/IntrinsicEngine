#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

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

namespace
{
    [[nodiscard]] bool ContainsTextureBarrier(const Extrinsic::Tests::MockCommandContext& context,
                                              const Extrinsic::RHI::TextureHandle handle)
    {
        for (const auto& barrier : context.TextureBarrierCalls)
        {
            if (barrier.Texture == handle)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] int FindEventIndex(const Extrinsic::Tests::MockCommandContext& context,
                                     const Extrinsic::Tests::MockCommandContext::EventKind kind,
                                     const int start = 0)
    {
        for (int i = start; i < static_cast<int>(context.Events.size()); ++i)
        {
            if (context.Events[static_cast<std::size_t>(i)] == kind)
            {
                return i;
            }
        }
        return -1;
    }

    [[nodiscard]] const Extrinsic::Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Extrinsic::Graphics::RenderGraphFrameStats& stats,
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

TEST(RendererFrameLifecycle, UsesDeviceFrameLifecycleBackbufferAndCommandContext)
{
    Extrinsic::Tests::MockDevice device;
    device.NextFrame = Extrinsic::RHI::FrameHandle{.FrameIndex = 5u, .SwapchainImageIndex = 2u};
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    EXPECT_EQ(device.BeginFrameCount, 1);
    EXPECT_EQ(frame.FrameIndex, 5u);
    EXPECT_EQ(frame.SwapchainImageIndex, 2u);

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    // GRAPHICS-070 — the default-recipe forward surface pass now records its
    // bind/draw shape under the forward lighting path, so the renderer
    // reports three routed passes (CullingPass + DepthPrepass + SurfacePass)
    // instead of two. Remaining unwired passes still soft-skip with
    // SkippedUnavailable.
    EXPECT_EQ(stats.CommandRecords.Recorded, 3u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable, 1u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "Present"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "Present")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(device.GetBackbufferHandleCount, 1);
    EXPECT_EQ(device.LastBackbufferFrame.FrameIndex, frame.FrameIndex);
    EXPECT_EQ(device.LastBackbufferFrame.SwapchainImageIndex, frame.SwapchainImageIndex);
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);
    EXPECT_TRUE(ContainsTextureBarrier(device.CommandContext, device.BackbufferHandle));
    EXPECT_GE(device.CommandContext.FillBufferCalls, 8);
    // GRAPHICS-070 — culling pipeline + depth prepass pipeline + forward
    // surface pipeline each bind once with one push-constant block.
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 3);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 3);
    ASSERT_EQ(device.CommandContext.PushConstantSizes.size(), 3u);
    EXPECT_EQ(device.CommandContext.PushConstantSizes[0], sizeof(Extrinsic::RHI::GpuCullPushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[1], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[2], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.LastDispatch.X,
              (Extrinsic::RHI::kMaxIndirectDrawCount + Extrinsic::RHI::kGpuCullDispatchGroupSize - 1u) /
                  Extrinsic::RHI::kGpuCullDispatchGroupSize);
    EXPECT_EQ(device.CommandContext.LastDispatch.Y, 1u);
    EXPECT_EQ(device.CommandContext.LastDispatch.Z, 1u);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 2);
    EXPECT_EQ(device.CommandContext.LastMaxDrawCount, Extrinsic::RHI::kMaxIndirectDrawCount);

    const int dispatchEvent = FindEventIndex(device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::Dispatch);
    const int drawEvent = FindEventIndex(device.CommandContext,
        Extrinsic::Tests::MockCommandContext::EventKind::DrawIndexedIndirectCount);
    ASSERT_GE(dispatchEvent, 0);
    ASSERT_GE(drawEvent, 0);
    EXPECT_LT(dispatchEvent, drawEvent);

    const std::uint64_t completedFrame = renderer->EndFrame(frame);
    EXPECT_EQ(completedFrame, 1u);
    EXPECT_EQ(device.EndFrameCount, 1);
    EXPECT_EQ(device.PresentCount, 0) << "Runtime remains responsible for presentation.";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, InvalidDeviceBackbufferReportsRecipeDiagnostic)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = {};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Compile.Succeeded);
    EXPECT_FALSE(stats.Execute.Succeeded);
    EXPECT_NE(stats.Diagnostic.find("Backbuffer"), std::string::npos);
    EXPECT_EQ(device.GetBackbufferHandleCount, 1);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// GRAPHICS-033E: after a successful recipe build + compile, the renderer must
// publish the recipe-aware validation outcome to the device exactly once via
// `IDevice::NoteRecipeGraphValidation(bool)`. A clean compile of the default
// recipe yields `true`.
// -----------------------------------------------------------------------------
TEST(RendererFrameLifecycle, PublishesRecipeGraphValidationOnSuccessfulCompile)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{77u, 3u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_EQ(device.RecipeGraphValidationCalls.size(), 1u);
    EXPECT_TRUE(device.RecipeGraphValidationCalls.front());

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// GRAPHICS-033E: when the recipe build fails (e.g. invalid backbuffer handle),
// the renderer publishes `false` so the operational gate cannot inherit a
// stale-clean state from a prior compile.
// -----------------------------------------------------------------------------
TEST(RendererFrameLifecycle, PublishesFailClosedRecipeValidationOnRecipeBuildFailure)
{
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = {};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.Compile.Succeeded);
    ASSERT_EQ(device.RecipeGraphValidationCalls.size(), 1u);
    EXPECT_FALSE(device.RecipeGraphValidationCalls.front());

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, NonOperationalDeviceSkipsCullingCommandsButExecutesGraph)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{91u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_FALSE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.CommandRecords.Recorded, 0u);
    // GRAPHICS-018 §4: a non-operational device skips every routed pass with
    // SkippedNonOperational so CPU CI surfaces accidental operational claims.
    EXPECT_EQ(stats.CommandRecords.SkippedUnavailable, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedNonOperational);
    EXPECT_GE(stats.CommandRecords.SkippedNonOperational, 2u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    ASSERT_NE(FindCommandPass(stats, "Present"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "Present")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedNonOperational);
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);
    EXPECT_EQ(device.CommandContext.FillBufferCalls, 0);
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 0);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 0);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 0);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, OperationalRebuildAfterNonOperationalStartupRecordsRoutedCommands)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = false;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{93u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    EXPECT_FALSE(renderer->GetMaterialSystem().GetBuffer().IsValid());
    EXPECT_FALSE(renderer->GetGpuWorld().GetSceneTableBuffer().IsValid());

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 160, .Height = 90},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);
    const Extrinsic::Graphics::RenderGraphFrameStats& nonOperationalStats =
        renderer->GetLastRenderGraphStats();
    EXPECT_GE(nonOperationalStats.CommandRecords.SkippedNonOperational, 2u);

    device.Operational = true;
    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetMaterialSystem().GetBuffer().IsValid());
    EXPECT_TRUE(renderer->GetGpuWorld().GetSceneTableBuffer().IsValid());
    EXPECT_EQ(renderer->GetMaterialSystem().GetDiagnostics().Capacity, 256u);

    device.CommandContext = Extrinsic::Tests::MockCommandContext{};
    frame = {};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    // GRAPHICS-070 — three routed passes (Culling/DepthPrepass/SurfacePass)
    // after the operational rebuild publishes the forward surface pipeline
    // lease. Remaining unwired passes soft-skip with SkippedUnavailable.
    EXPECT_EQ(stats.CommandRecords.Recorded, 3u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 2);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, DepthPrepassPipelineFailureSkipsUnavailableCommandPass)
{
    Extrinsic::Tests::MockDevice device;
    device.FailPipelineCreateCall = 2;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    // GRAPHICS-070 — even when the depth-prepass pipeline lease is missing,
    // the forward surface pipeline lease is independently created in
    // `InitializeOperationalPassResources()` and the surface pass still
    // records its bind/draw shape. The depth prepass entry continues to
    // report `SkippedUnavailable` so its missing lease cannot regress
    // silently.
    EXPECT_EQ(stats.CommandRecords.Recorded, 2u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable, 1u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 1);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, CullingPipelineFailureSkipsRoutedCommandPassesUnavailable)
{
    Extrinsic::Tests::MockDevice device;
    device.FailPipelineCreateCall = 1;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(stats.CommandRecords.Recorded, 0u);
    // Culling pipeline failure leaves both routed passes unavailable; all
    // remaining passes also report SkippedUnavailable since the device is
    // operational but their command bodies are not yet wired.
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_EQ(stats.CommandRecords.Skipped, stats.CommandRecords.SkippedUnavailable);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable, 2u);
    ASSERT_NE(FindCommandPass(stats, "CullingPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CullingPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 0);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, BeginFrameWithoutDeviceReportsLifecycleDiagnostic)
{
    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_FALSE(stats.LifecycleDiagnostic.empty());
    EXPECT_TRUE(stats.Diagnostic.empty());
}

TEST(RendererFrameLifecycle, BeginFrameSkipDoesNotRecordCommands)
{
    Extrinsic::Tests::MockDevice device;
    device.BeginFrameResult = false;

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    EXPECT_FALSE(renderer->BeginFrame(frame));
    EXPECT_EQ(device.BeginFrameCount, 1);
    EXPECT_EQ(device.GetBackbufferHandleCount, 0);
    EXPECT_EQ(device.CommandContext.BeginCalls, 0);
    EXPECT_EQ(device.CommandContext.EndCalls, 0);

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, FrameRecipePassesAllProduceStructuredCommandRecordStatuses)
{
    // GRAPHICS-018 §4 contract: every pass emitted by the default FrameRecipe
    // produces a structured RenderGraphCommandPassStats entry. Routed passes
    // (CullingPass, DepthPrepass) record real Vulkan-flavored command traffic
    // through the RHI seam; the remaining pass command bodies are not yet
    // wired and must soft-skip with SkippedUnavailable so the renderer reports
    // complete per-pass status. Render-graph bracketing remains in effect:
    // command-context Begin/End wrap the per-pass routing exactly once.
    Extrinsic::Tests::MockDevice device;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{200u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 256, .Height = 256},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(device.CommandContext.BeginCalls, 1);
    EXPECT_EQ(device.CommandContext.EndCalls, 1);

    // GRAPHICS-070 — default features now select the forward lighting path
    // (`CompositionPass` is not declared in forward mode), so the routed set
    // grows by `SurfacePass` and the soft-skipped set drops `CompositionPass`.
    // Every entry below must carry a structured status so future routing
    // changes can't silently regress to a no-op.
    static constexpr const char* kRoutedPasses[] = {"CullingPass", "DepthPrepass", "SurfacePass"};
    static constexpr const char* kSoftSkippedPasses[] = {
        "LinePass",
        "PointPass",
        "PostProcessPass",
        "ImGuiPass",
        "Present",
    };
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass"), nullptr)
        << "CompositionPass is forward-mode-disabled per GRAPHICS-070; "
           "GRAPHICS-072 reintroduces it when deferred wiring lands.";

    for (const char* name : kRoutedPasses)
    {
        ASSERT_NE(FindCommandPass(stats, name), nullptr) << name;
        EXPECT_EQ(FindCommandPass(stats, name)->Status,
                  Extrinsic::Graphics::RenderCommandPassStatus::Recorded) << name;
    }
    for (const char* name : kSoftSkippedPasses)
    {
        ASSERT_NE(FindCommandPass(stats, name), nullptr) << name;
        EXPECT_EQ(FindCommandPass(stats, name)->Status,
                  Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable) << name;
    }

    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    EXPECT_GE(stats.CommandRecords.SkippedUnavailable,
              sizeof(kSoftSkippedPasses) / sizeof(kSoftSkippedPasses[0]));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-031A — canonical missing-material fallback pipeline lease
// ---------------------------------------------------------------------------

namespace
{
    [[nodiscard]] bool PipelineDescBytesEqual(const Extrinsic::RHI::PipelineDesc& lhs,
                                              const Extrinsic::RHI::PipelineDesc& rhs) noexcept
    {
        if (lhs.VertexShaderPath != rhs.VertexShaderPath) return false;
        if (lhs.FragmentShaderPath != rhs.FragmentShaderPath) return false;
        if (lhs.ComputeShaderPath != rhs.ComputeShaderPath) return false;
        if (lhs.PrimitiveTopology != rhs.PrimitiveTopology) return false;
        if (lhs.Rasterizer.Culling != rhs.Rasterizer.Culling) return false;
        if (lhs.Rasterizer.Winding != rhs.Rasterizer.Winding) return false;
        if (lhs.Rasterizer.Fill != rhs.Rasterizer.Fill) return false;
        if (lhs.DepthStencil.DepthTestEnable != rhs.DepthStencil.DepthTestEnable) return false;
        if (lhs.DepthStencil.DepthWriteEnable != rhs.DepthStencil.DepthWriteEnable) return false;
        if (lhs.DepthStencil.DepthFunc != rhs.DepthStencil.DepthFunc) return false;
        if (lhs.DepthStencil.StencilEnable != rhs.DepthStencil.StencilEnable) return false;
        if (lhs.ColorTargetCount != rhs.ColorTargetCount) return false;
        for (std::uint32_t i = 0u; i < lhs.ColorTargetCount; ++i)
        {
            if (lhs.ColorBlend[i].Enable != rhs.ColorBlend[i].Enable) return false;
            if (lhs.ColorTargetFormats[i] != rhs.ColorTargetFormats[i]) return false;
        }
        if (lhs.DepthTargetFormat != rhs.DepthTargetFormat) return false;
        if (lhs.PushConstantSize != rhs.PushConstantSize) return false;
        return true;
    }
}

TEST(RendererFrameLifecycle, DefaultDebugSurfacePipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{173u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetDefaultDebugSurfacePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetDefaultDebugSurfacePipelineDesc();
    // The descriptor must reference the compiled SPIR-V artifact emitted by
    // intrinsic_add_glsl_shaders(), not the raw GLSL source — VulkanDevice::
    // CreatePipeline() reads the path verbatim as a SPIR-V binary.
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/forward/default_debug_surface.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/forward/default_debug_surface.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Less);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetDefaultDebugSurfacePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetDefaultDebugSurfacePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-070 — default-recipe forward surface pipeline lease + republish
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ForwardSurfacePipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{181u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetForwardSurfacePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetForwardSurfacePipelineDesc();
    // The descriptor must reference the SPIR-V emitted by intrinsic_add_glsl_shaders()
    // and the depth-prepass-on path documented in
    // docs/architecture/rendering-three-pass.md. The shader pair must also
    // observe the GpuScene push-constant contract that
    // `ForwardSurfacePass::Execute()` pushes — the canonical GpuScene-aware
    // forward shader pair (`forward/default_debug_surface.{vert,frag}`)
    // matches `sizeof(GpuScenePushConstants)` and the BDA-only descriptor
    // layout. The legacy `surface.vert/frag` pair is incompatible
    // (mat4 Model + PtrPositions push block + set=2/3 SSBOs).
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/forward/default_debug_surface.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/forward/default_debug_surface.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetForwardSurfacePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetForwardSurfacePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// GRAPHICS-070 — when culling output is unavailable (cull pipeline creation
// failed), the executor reports the forward surface pass as
// SkippedUnavailable rather than recording a draw against an empty bucket.
TEST(RendererFrameLifecycle, ForwardSurfacePassSkipsUnavailableWhenCullOutputMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.FailPipelineCreateCall = 1; // Cull compute pipeline creation fails.
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{182u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);

    renderer->Shutdown();
}

