#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.ShadowSystem;
import Extrinsic.RHI.Bindless;
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
    // GRAPHICS-071 — the default-recipe retained forward surface/line/point
    // passes now record their bind/draw shape under the forward lighting path.
    // Remaining unwired passes still soft-skip with SkippedUnavailable.
    EXPECT_EQ(stats.CommandRecords.Recorded, 5u);
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
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
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
    // GRAPHICS-071 — culling pipeline plus depth/surface/line/point draw
    // pipelines each bind once. The draw passes all carry scene push constants.
    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 5);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 5);
    ASSERT_EQ(device.CommandContext.PushConstantSizes.size(), 5u);
    EXPECT_EQ(device.CommandContext.PushConstantSizes[0], sizeof(Extrinsic::RHI::GpuCullPushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[1], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[2], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[3], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.PushConstantSizes[4], sizeof(Extrinsic::RHI::GpuScenePushConstants));
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.LastDispatch.X,
              (Extrinsic::RHI::kMaxIndirectDrawCount + Extrinsic::RHI::kGpuCullDispatchGroupSize - 1u) /
                  Extrinsic::RHI::kGpuCullDispatchGroupSize);
    EXPECT_EQ(device.CommandContext.LastDispatch.Y, 1u);
    EXPECT_EQ(device.CommandContext.LastDispatch.Z, 1u);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 3);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 3);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 1);
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
    // GRAPHICS-071 — five routed passes (Culling/DepthPrepass/Surface/Line/Point)
    // after the operational rebuild publishes the forward pass pipeline leases.
    // Remaining unwired passes soft-skip with SkippedUnavailable.
    EXPECT_EQ(stats.CommandRecords.Recorded, 5u);
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
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 3);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 1);

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
    EXPECT_EQ(stats.CommandRecords.Recorded, 4u);
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
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(device.CommandContext.DispatchCalls, 1);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 1);

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

    // GRAPHICS-071 — default features select the forward lighting path
    // (`CompositionPass` is not declared in forward mode), and the retained
    // surface/line/point passes are routed by the renderer.
    // Every entry below must carry a structured status so future routing
    // changes can't silently regress to a no-op.
    static constexpr const char* kRoutedPasses[] = {"CullingPass", "DepthPrepass", "SurfacePass", "LinePass", "PointPass"};
    static constexpr const char* kSoftSkippedPasses[] = {
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

// ---------------------------------------------------------------------------
// GRAPHICS-071 — default-recipe retained line/point pipeline leases + republish
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ForwardLinePointPipelinesSurviveOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{183u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialLinePipeline = renderer->GetForwardLinePipeline();
    const Extrinsic::RHI::PipelineHandle initialPointPipeline = renderer->GetForwardPointPipeline();
    EXPECT_TRUE(initialLinePipeline.IsValid());
    EXPECT_TRUE(initialPointPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialLineDesc = renderer->GetForwardLinePipelineDesc();
    EXPECT_TRUE(initialLineDesc.VertexShaderPath.ends_with("shaders/line.vert.spv"))
        << initialLineDesc.VertexShaderPath;
    EXPECT_TRUE(initialLineDesc.FragmentShaderPath.ends_with("shaders/line.frag.spv"))
        << initialLineDesc.FragmentShaderPath;
    EXPECT_EQ(initialLineDesc.PrimitiveTopology, Extrinsic::RHI::Topology::LineList);
    EXPECT_EQ(initialLineDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_TRUE(initialLineDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialLineDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialLineDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::LessEqual);
    EXPECT_TRUE(initialLineDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialLineDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialLineDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialLineDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    const Extrinsic::RHI::PipelineDesc initialPointDesc = renderer->GetForwardPointPipelineDesc();
    EXPECT_TRUE(initialPointDesc.VertexShaderPath.ends_with("shaders/point.vert.spv"))
        << initialPointDesc.VertexShaderPath;
    EXPECT_TRUE(initialPointDesc.FragmentShaderPath.ends_with("shaders/point_retained.frag.spv"))
        << initialPointDesc.FragmentShaderPath;
    EXPECT_EQ(initialPointDesc.PrimitiveTopology, Extrinsic::RHI::Topology::PointList);
    EXPECT_EQ(initialPointDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_TRUE(initialPointDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialPointDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialPointDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::LessEqual);
    EXPECT_TRUE(initialPointDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialPointDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialPointDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialPointDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetForwardLinePipeline().IsValid());
    EXPECT_TRUE(renderer->GetForwardPointPipeline().IsValid());
    EXPECT_TRUE(PipelineDescBytesEqual(initialLineDesc, renderer->GetForwardLinePipelineDesc()));
    EXPECT_TRUE(PipelineDescBytesEqual(initialPointDesc, renderer->GetForwardPointPipelineDesc()));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-073 Slice A — default-recipe depth-only shadow pipeline lease +
// republish. The CPU/null contract here is the byte-identical descriptor
// across the initial init and `RebuildOperationalResources()`; Slice B adds
// the `ShadowSystem`-owned atlas/sampler + `FrameRecipeShadowSizing` import
// seam.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ShadowPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{185u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialShadowPipeline = renderer->GetShadowPipeline();
    EXPECT_TRUE(initialShadowPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialShadowDesc = renderer->GetShadowPipelineDesc();
    EXPECT_TRUE(initialShadowDesc.VertexShaderPath.ends_with("shaders/depth_prepass.vert.spv"))
        << initialShadowDesc.VertexShaderPath;
    EXPECT_TRUE(initialShadowDesc.FragmentShaderPath.empty()) << initialShadowDesc.FragmentShaderPath;
    EXPECT_EQ(initialShadowDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialShadowDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_TRUE(initialShadowDesc.DepthStencil.DepthTestEnable);
    EXPECT_TRUE(initialShadowDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialShadowDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::LessEqual);
    EXPECT_FALSE(initialShadowDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialShadowDesc.ColorTargetCount, 0u);
    EXPECT_EQ(initialShadowDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialShadowDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    EXPECT_TRUE(renderer->GetShadowPipeline().IsValid());
    EXPECT_TRUE(PipelineDescBytesEqual(initialShadowDesc, renderer->GetShadowPipelineDesc()));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-073 Slice B — `ShadowSystem`-owned atlas + sampler. Once shadows
// are enabled the atlas handle must stay byte-identical across an operational
// rebuild, because `RebuildOperationalResources()` only recreates pipeline +
// culling resources; the texture manager + ShadowSystem hold the atlas across
// the boundary. The runtime extraction publisher will eventually call
// `SetParams(...)` once shadow-casters arrive; in this test we mutate the
// ShadowSystem directly via `GetShadowSystem()` to avoid spinning up a full
// extraction harness.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, ShadowAtlasSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{186u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::Graphics::ShadowSystem& shadows = renderer->GetShadowSystem();
    shadows.SetParams(Extrinsic::Graphics::ShadowParams{
        .Enabled = true,
        .CascadeCount = 2u,
        .AtlasResolution = 512u,
    });

    const Extrinsic::RHI::TextureHandle atlasHandle = shadows.GetAtlasTexture();
    EXPECT_TRUE(atlasHandle.IsValid());
    const Extrinsic::RHI::SamplerHandle samplerHandle = shadows.GetAtlasSampler();
    EXPECT_TRUE(samplerHandle.IsValid());
    const Extrinsic::Graphics::ShadowAtlasDesc initialAtlas = shadows.GetAllocatedAtlasDesc();
    EXPECT_TRUE(initialAtlas.Enabled);
    EXPECT_EQ(initialAtlas.Width, 512u * 2u);
    EXPECT_EQ(initialAtlas.Height, 512u);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));

    EXPECT_EQ(shadows.GetAtlasTexture(), atlasHandle);
    EXPECT_EQ(shadows.GetAtlasSampler(), samplerHandle);
    const Extrinsic::Graphics::ShadowAtlasDesc afterRebuild = shadows.GetAllocatedAtlasDesc();
    EXPECT_EQ(afterRebuild.Width, initialAtlas.Width);
    EXPECT_EQ(afterRebuild.Height, initialAtlas.Height);
    EXPECT_EQ(afterRebuild.CascadeCount, initialAtlas.CascadeCount);

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

// GRAPHICS-071 — retained line/point pass routing is fail-closed on culling

// output availability. This preserves the transient-debug split: invalid or
// absent retained buckets soft-skip here rather than routing debug packets
// through the retained line/point lanes.
TEST(RendererFrameLifecycle, ForwardLinePointPassesSkipUnavailableWhenCullOutputMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.FailPipelineCreateCall = 1; // Cull compute pipeline creation fails.
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{184u, 1u};

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
    ASSERT_NE(FindCommandPass(stats, "LinePass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "LinePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    ASSERT_NE(FindCommandPass(stats, "PointPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "PointPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(device.CommandContext.DrawIndexedIndirectCountCalls, 0);
    EXPECT_EQ(device.CommandContext.DrawIndirectCountCalls, 0);

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-072 Slice A — default-recipe deferred GBuffer pipeline lease +
// republish, deferred-mode `"SurfacePass"` executor branch routing, and
// fail-closed `SkippedUnavailable` taxonomy when the pipeline lease is
// missing. Slice B owns the `"CompositionPass"` executor branch (deferred
// lighting); Slice C owns the shadow-atlas descriptor binding and the
// end-to-end shadow-casting recorded-for-both-passes test.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, DeferredGBufferPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{281u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetDeferredGBufferPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetDeferredGBufferPipelineDesc();

    // GRAPHICS-072 Slice A — the deferred GBuffer pipeline MUST select
    // shaders that declare a `layout(push_constant) ScenePC` block matching
    // `RHI::GpuScenePushConstants` byte-for-byte, because
    // `DeferredGBufferPass::Execute` pushes that struct verbatim. The
    // legacy `assets/shaders/surface.vert` + `surface_gbuffer.frag` pair
    // declares the pre-GpuScene `mat4 Model + Ptr*` push block and must
    // never be referenced here (the `SceneTableBDA` pointer would land in
    // `mat4 Model[0]` and every BDA dereference would read garbage on a
    // real Vulkan run). See the renderer README "Shader push-constant
    // compatibility policy" subsection.
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/forward/default_debug_surface.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/deferred/default_debug_gbuffer.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_EQ(initialDesc.ColorTargetCount, 3u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::RGBA8_UNORM);
    EXPECT_EQ(initialDesc.ColorTargetFormats[2], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetDeferredGBufferPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetDeferredGBufferPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// GRAPHICS-072 Slice A — when `SetLightingPath(Deferred)` is in effect and
// the operational publisher has produced both the cull-output and the GBuffer
// pipeline, the deferred-mode `"SurfacePass"` executor branch records the
// GBuffer pass's bind/draw shape and reports `Recorded`. The companion
// `"CompositionPass"` (deferred lighting) is owned by Slice B and currently
// soft-skips to `SkippedUnavailable`.
TEST(RendererFrameLifecycle, DeferredSurfacePassRecordsWhenLightingPathIsDeferred)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{282u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
    EXPECT_EQ(renderer->GetLightingPath(),
              Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

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
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    // GRAPHICS-072 Slice B — the `"CompositionPass"` executor branch now
    // records the deferred lighting pass's `Bind/Push/Draw(3,1,0,0)`
    // fullscreen shape.
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

// GRAPHICS-072 Slice A — when the deferred surface pipeline could not be
// created (here, the device fails the corresponding `CreatePipeline` call),
// the deferred-mode `"SurfacePass"` executor branch reports
// `SkippedUnavailable` rather than recording against an unset pipeline
// handle. Mirrors the forward-path
// `ForwardSurfacePassSkipsUnavailableWhenCullOutputMissing` policy.
TEST(RendererFrameLifecycle, DeferredSurfacePassSkipsUnavailableWhenPipelineMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    // The deferred GBuffer pipeline is the eighth (1-indexed) pipeline
    // created by the operational publisher: 1 culling compute, 2 depth
    // prepass, 3 default debug surface, 4 minimal visible triangle, 5
    // forward surface, 6 forward line, 7 forward point, 8 shadow,
    // 9 deferred GBuffer. Fail call #9 so all upstream pipelines succeed
    // (including culling, which keeps `m_CullingOutputAvailable=true`) but
    // the GBuffer lease is left empty. The `SkippedUnavailable` taxonomy
    // distinguishes this from the `SkippedNonOperational` path.
    device.FailPipelineCreateCall = 9;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{283u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
    EXPECT_FALSE(renderer->GetDeferredGBufferPipeline().IsValid());

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
    // GRAPHICS-072 Slice B — when the GBuffer pass cannot record (here,
    // because its pipeline lease is missing), the deferred composition pass
    // must mirror the `SkippedUnavailable` taxonomy rather than lighting
    // against uninitialized SceneNormal/Albedo/Material0 attachments. The
    // lighting pipeline itself was created successfully (call #10), so this
    // test pins the GBuffer-prerequisite gate in `RecordDeferredLightingPass`,
    // not the lighting-pipeline gate covered by
    // `DeferredLightingPassSkipsUnavailableWhenPipelineMissing`.
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_TRUE(renderer->GetDeferredLightingPipeline().IsValid());
    // Other passes (DepthPrepass, LinePass, PointPass) still record because
    // their pipelines were created successfully.

    renderer->Shutdown();
}

// GRAPHICS-072 Slice B — the deferred lighting pipeline must survive
// `RebuildOperationalResources()` byte-identically. The descriptor is also
// asserted shader-path-explicit so the shader push-constant compatibility
// policy (see `src/graphics/renderer/README.md`) is enforced at the contract
// level: the lighting pipeline MUST pair `post_fullscreen.vert.spv` with
// `deferred/lighting.frag.spv`, whose `layout(push_constant, scalar)
// PushConstants { uint64_t SceneTableBDA; uint _pad0; uint _pad1; }` block
// matches `DeferredLightingPushConstants` byte-for-byte. The legacy
// `assets/shaders/deferred_lighting.frag` declares a much larger Push block
// plus multiple descriptor sets and would silently misinterpret the pushed
// bytes — referencing it here is a known footgun that this test catches.
TEST(RendererFrameLifecycle, DeferredLightingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{284u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetDeferredLightingPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetDeferredLightingPipelineDesc();

    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/deferred/lighting.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA16_FLOAT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::Undefined);
    EXPECT_EQ(initialDesc.PushConstantSize, 16u);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetDeferredLightingPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetDeferredLightingPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// GRAPHICS-072 Slice B — when the deferred lighting pipeline could not be
// created, the `"CompositionPass"` executor branch reports
// `SkippedUnavailable` rather than recording against an unset pipeline
// handle. The GBuffer pipeline must remain available so `"SurfacePass"`
// still records `Recorded`; only the composition pass should soft-skip.
TEST(RendererFrameLifecycle, DeferredLightingPassSkipsUnavailableWhenPipelineMissing)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    // Publisher pipeline order (1-indexed): 1 culling compute, 2 depth
    // prepass, 3 default debug surface, 4 minimal visible triangle,
    // 5 forward surface, 6 forward line, 7 forward point, 8 shadow,
    // 9 deferred GBuffer, 10 deferred lighting. Failing call #10 leaves
    // the lighting lease empty while every upstream pipeline (including
    // the GBuffer at #9) succeeds.
    device.FailPipelineCreateCall = 10;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{285u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);
    EXPECT_TRUE(renderer->GetDeferredGBufferPipeline().IsValid());
    EXPECT_FALSE(renderer->GetDeferredLightingPipeline().IsValid());

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
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::SkippedUnavailable);

    renderer->Shutdown();
}

// GRAPHICS-072 Slice B — between the deferred-mode `"SurfacePass"`
// (GBuffer write) and `"CompositionPass"` (GBuffer read), the frame-graph
// compiler MUST emit `ColorAttachment → ShaderReadOnly` layout transitions
// for the three GBuffer textures (`SceneNormal`, `Albedo`, `Material0`).
// The MockCommandContext interleaves `TextureBarrier` events into its
// `Events` log alongside `BindPipeline`/`DrawIndexedIndirectCount`/etc.,
// so the test can sequence: the GBuffer pass's `DrawIndexedIndirectCount`
// must precede a run of three `ColorAttachment → ShaderReadOnly`
// `TextureBarrier` events on three *distinct* texture handles, which must
// precede the lighting pass's next `BindPipeline` event.
TEST(RendererFrameLifecycle, DeferredGBufferToCompositionEmitsColorToShaderReadBarriers)
{
    using EventKind = Extrinsic::Tests::MockCommandContext::EventKind;

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{286u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "SurfacePass"), nullptr);
    ASSERT_EQ(FindCommandPass(stats, "SurfacePass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    ASSERT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // GPU order for the deferred frame (mock-visible events only — the
    // mock does not tally non-indirect `Draw` calls):
    //   1) Culling: BindPipeline → PushConstants → Dispatch
    //   2) DepthPrepass: BindPipeline → BindIndexBuffer → PushConstants
    //      → DrawIndexedIndirectCount (first DIIC)
    //   3) GBuffer (deferred surface pass): BindPipeline → BindIndexBuffer
    //      → PushConstants → DrawIndexedIndirectCount (second DIIC)
    //   4) Cross-pass barriers (ColorAttachment → ShaderReadOnly ×3 for
    //      SceneNormal/Albedo/Material0).
    //   5) Lighting (CompositionPass): BindPipeline → PushConstants → Draw
    int gbufferDrawEvent = -1;
    {
        int dIIC = 0;
        for (int i = 0; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
        {
            if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
                EventKind::DrawIndexedIndirectCount)
            {
                ++dIIC;
                if (dIIC == 2) { gbufferDrawEvent = i; break; }
            }
        }
    }
    ASSERT_GE(gbufferDrawEvent, 0)
        << "Expected a second DrawIndexedIndirectCount event for the deferred GBuffer pass";

    int compositionBindEvent = -1;
    for (int i = gbufferDrawEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::BindPipeline)
        {
            compositionBindEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionBindEvent, 0)
        << "Expected a CompositionPass BindPipeline event after the GBuffer pass";

    // Walk the events between the GBuffer draw and the composition bind,
    // pulling barrier records out of `TextureBarrierCalls` in lockstep
    // with `EventKind::TextureBarrier` markers. Count distinct texture
    // handles transitioning ColorAttachment → ShaderReadOnly.
    int textureBarrierIndex = 0;
    for (int i = 0; i <= gbufferDrawEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::TextureBarrier)
        {
            ++textureBarrierIndex;
        }
    }
    std::vector<Extrinsic::RHI::TextureHandle> crossPassColorToShaderRead;
    for (int i = gbufferDrawEvent + 1; i < compositionBindEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] !=
            EventKind::TextureBarrier)
        {
            continue;
        }
        ASSERT_LT(textureBarrierIndex,
                  static_cast<int>(device.CommandContext.TextureBarrierCalls.size()))
            << "TextureBarrier event has no matching entry in TextureBarrierCalls";
        const auto& barrier =
            device.CommandContext.TextureBarrierCalls[static_cast<std::size_t>(textureBarrierIndex)];
        ++textureBarrierIndex;
        if (barrier.Before == Extrinsic::RHI::TextureLayout::ColorAttachment &&
            barrier.After == Extrinsic::RHI::TextureLayout::ShaderReadOnly)
        {
            crossPassColorToShaderRead.push_back(barrier.Texture);
        }
    }

    EXPECT_EQ(crossPassColorToShaderRead.size(), 3u)
        << "Expected ColorAttachment→ShaderReadOnly barriers for SceneNormal, Albedo, Material0";
    // Distinct handles — the recipe declares SceneNormal/Albedo/Material0 as
    // three separate transient color attachments, not a single shared one.
    std::vector<Extrinsic::RHI::TextureHandle> unique = crossPassColorToShaderRead;
    std::sort(unique.begin(), unique.end(),
              [](const Extrinsic::RHI::TextureHandle& a, const Extrinsic::RHI::TextureHandle& b) {
                  if (a.Index != b.Index) { return a.Index < b.Index; }
                  return a.Generation < b.Generation;
              });
    unique.erase(std::unique(unique.begin(), unique.end(),
                             [](const Extrinsic::RHI::TextureHandle& a, const Extrinsic::RHI::TextureHandle& b) {
                                 return a.Index == b.Index && a.Generation == b.Generation;
                             }),
                 unique.end());
    EXPECT_EQ(unique.size(), 3u)
        << "Expected three distinct GBuffer textures transitioning ColorAttachment → ShaderReadOnly";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-072 Slice C — deferred lighting shadow-atlas binding.
//
// The deferred lighting pass samples the `ShadowSystem`-owned atlas through
// the engine's bindless heap; the legacy `set 1, binding 1` `sampler2DShadow`
// model from `assets/shaders/deferred_lighting.frag` cannot be honored on
// the promoted Vulkan pipeline layout (which declares only the bindless set
// at `set = 0`), so the wiring publishes the atlas slot through
// `DeferredLightingPushConstants::ShadowAtlasBindlessIndex` instead. These
// tests pin the two contracts that fall out:
//   1) the recipe emits a `DepthAttachment → ShaderReadOnly` layout
//      transition for the shadow atlas before `CompositionPass` records;
//   2) end-to-end, both `ShadowPass` and `CompositionPass` record
//      `Recorded`, and the bindless index pushed by the lighting pass
//      matches `ShadowSystem::GetAtlasBindlessIndex()`.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, DeferredLightingShadowAtlasTransitionsDepthToShaderReadBeforeComposition)
{
    using EventKind = Extrinsic::Tests::MockCommandContext::EventKind;

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{287u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    // Enable shadows so the recipe declares ShadowPass + the
    // CompositionPass shadow-atlas read. Allocating the atlas through
    // `SetParams` lets `FrameRecipeImports::ShadowAtlas` carry the
    // ShadowSystem-owned handle into the recipe, so the barrier the test
    // looks for transitions that specific texture.
    Extrinsic::Graphics::ShadowSystem& shadows = renderer->GetShadowSystem();
    shadows.SetParams(Extrinsic::Graphics::ShadowParams{
        .Enabled         = true,
        .CascadeCount    = 2u,
        .AtlasResolution = 256u,
    });
    const Extrinsic::RHI::TextureHandle shadowAtlasHandle = shadows.GetAtlasTexture();
    ASSERT_TRUE(shadowAtlasHandle.IsValid())
        << "ShadowSystem must lazily allocate the atlas after SetParams enables shadows";

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    // The default ExtractRenderWorld does not populate Shadows from the
    // ShadowSystem (runtime publishes that separately). Mirror the runtime
    // contract here so `DeriveDefaultFrameRecipeFeatures` flips
    // `EnableShadows` on for this frame.
    world.Shadows.Enabled         = true;
    world.Shadows.CascadeCount    = 2u;
    world.Shadows.AtlasResolution = 256u;
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    ASSERT_NE(FindCommandPass(stats, "ShadowPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ShadowPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // Locate the lighting pass's bind in the event stream — the
    // `CompositionPass` body opens with `BindPipeline` for the deferred
    // lighting pipeline. The shadow-atlas barrier MUST precede that bind.
    // The DeferredGBufferToCompositionEmitsColorToShaderReadBarriers test
    // covers the SceneNormal/Albedo/Material0 transitions emitted at the
    // same boundary; this test adds the parallel shadow-atlas check.
    int gbufferDrawEvent = -1;
    {
        int dIIC = 0;
        for (int i = 0; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
        {
            if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
                EventKind::DrawIndexedIndirectCount)
            {
                ++dIIC;
                // (1) DepthPrepass DIIC, (2) ShadowPass DIIC, (3) GBuffer DIIC.
                if (dIIC == 3) { gbufferDrawEvent = i; break; }
            }
        }
    }
    ASSERT_GE(gbufferDrawEvent, 0)
        << "Expected a third DrawIndexedIndirectCount event for the deferred "
           "GBuffer pass (after DepthPrepass and ShadowPass)";

    int compositionBindEvent = -1;
    for (int i = gbufferDrawEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::BindPipeline)
        {
            compositionBindEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionBindEvent, 0)
        << "Expected a CompositionPass BindPipeline event after the GBuffer pass";

    // Walk barrier records in lockstep with TextureBarrier events between
    // gbufferDrawEvent and compositionBindEvent. Track records that
    // transition the ShadowSystem-owned atlas from DepthAttachment →
    // ShaderReadOnly.
    int textureBarrierIndex = 0;
    for (int i = 0; i <= gbufferDrawEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::TextureBarrier)
        {
            ++textureBarrierIndex;
        }
    }
    bool sawShadowAtlasDepthToShaderRead = false;
    for (int i = gbufferDrawEvent + 1; i < compositionBindEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] !=
            EventKind::TextureBarrier)
        {
            continue;
        }
        ASSERT_LT(textureBarrierIndex,
                  static_cast<int>(device.CommandContext.TextureBarrierCalls.size()))
            << "TextureBarrier event has no matching entry in TextureBarrierCalls";
        const auto& barrier =
            device.CommandContext.TextureBarrierCalls[static_cast<std::size_t>(textureBarrierIndex)];
        ++textureBarrierIndex;
        if (barrier.Texture == shadowAtlasHandle &&
            barrier.Before == Extrinsic::RHI::TextureLayout::DepthAttachment &&
            barrier.After == Extrinsic::RHI::TextureLayout::ShaderReadOnly)
        {
            sawShadowAtlasDepthToShaderRead = true;
        }
    }
    EXPECT_TRUE(sawShadowAtlasDepthToShaderRead)
        << "Expected a DepthAttachment → ShaderReadOnly barrier on the "
           "ShadowSystem-owned shadow atlas between the GBuffer pass and "
           "the deferred lighting pass";

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, DeferredLightingPushConstantsCarryShadowAtlasBindlessIndex)
{
    using EventKind = Extrinsic::Tests::MockCommandContext::EventKind;

    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{288u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetLightingPath(Extrinsic::Graphics::FrameRecipeLightingPath::Deferred);

    Extrinsic::Graphics::ShadowSystem& shadows = renderer->GetShadowSystem();
    shadows.SetParams(Extrinsic::Graphics::ShadowParams{
        .Enabled         = true,
        .CascadeCount    = 2u,
        .AtlasResolution = 256u,
    });
    const Extrinsic::RHI::BindlessIndex shadowAtlasBindlessIndex =
        shadows.GetAtlasBindlessIndex();
    ASSERT_NE(shadowAtlasBindlessIndex, Extrinsic::RHI::kInvalidBindlessIndex)
        << "ShadowSystem must register the atlas in the bindless heap after "
           "SetParams enables shadows";

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 72},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    world.Shadows.Enabled         = true;
    world.Shadows.CascadeCount    = 2u;
    world.Shadows.AtlasResolution = 256u;
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    // End-to-end shadow-casting contract: both passes record Recorded.
    ASSERT_NE(FindCommandPass(stats, "ShadowPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "ShadowPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);
    ASSERT_NE(FindCommandPass(stats, "CompositionPass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "CompositionPass")->Status,
              Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // Find the lighting pass's PushConstants payload. The CompositionPass
    // body's first BindPipeline marks the start; the next PushConstants
    // event after it carries the deferred lighting push-constant block.
    // Map the event index to PushConstantPayloads[] by counting prior
    // PushConstants events in submission order.
    int gbufferDrawEvent = -1;
    {
        int dIIC = 0;
        for (int i = 0; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
        {
            if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
                EventKind::DrawIndexedIndirectCount)
            {
                ++dIIC;
                if (dIIC == 3) { gbufferDrawEvent = i; break; }
            }
        }
    }
    ASSERT_GE(gbufferDrawEvent, 0);

    int compositionBindEvent = -1;
    for (int i = gbufferDrawEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::BindPipeline)
        {
            compositionBindEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionBindEvent, 0);

    int compositionPushConstantsEvent = -1;
    for (int i = compositionBindEvent + 1; i < static_cast<int>(device.CommandContext.Events.size()); ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::PushConstants)
        {
            compositionPushConstantsEvent = i;
            break;
        }
    }
    ASSERT_GE(compositionPushConstantsEvent, 0)
        << "Expected a PushConstants event after the deferred lighting BindPipeline";

    int payloadIndex = 0;
    for (int i = 0; i < compositionPushConstantsEvent; ++i)
    {
        if (device.CommandContext.Events[static_cast<std::size_t>(i)] ==
            EventKind::PushConstants)
        {
            ++payloadIndex;
        }
    }
    ASSERT_LT(static_cast<std::size_t>(payloadIndex),
              device.CommandContext.PushConstantPayloads.size())
        << "PushConstants event has no matching entry in PushConstantPayloads";

    const auto& payload = device.CommandContext.PushConstantPayloads[
        static_cast<std::size_t>(payloadIndex)];
    // The deferred lighting block: 8 bytes SceneTableBDA + 4 bytes
    // ShadowAtlasBindlessIndex + 4 bytes padding.
    ASSERT_EQ(payload.size(), 16u);
    std::uint32_t pushedShadowIndex = 0u;
    std::memcpy(&pushedShadowIndex, payload.data() + sizeof(std::uint64_t),
                sizeof(std::uint32_t));
    EXPECT_EQ(pushedShadowIndex, shadowAtlasBindlessIndex)
        << "DeferredLightingPass::Execute must push ShadowSystem::GetAtlasBindlessIndex() "
           "as the ShadowAtlasBindlessIndex push-constant field (the bindless-heap "
           "equivalent of the legacy `set 1, binding 1` shadow-atlas binding)";

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice A — default-recipe EntityId selection pipeline lease +
// republish. The CPU/null contract here is the byte-identical descriptor
// across the initial init and `RebuildOperationalResources()`, plus the
// shader-path and color-target assertions that catch the GpuScene-aware
// shader-pair contract (the legacy `assets/shaders/pick_id.{vert,frag}` is a
// known footgun because it declares the pre-GpuScene `mat4 Model +
// PtrPositions + ... + uint EntityID` push block and would silently
// misinterpret the `RHI::GpuScenePushConstants` bytes that
// `EntityIdPass::Execute` pushes).
//
// Render-pass compatibility: GRAPHICS-074's recipe-side follow-up reordered
// `BuildDefaultFrameRecipe` so `PickingPass` runs *after* `DepthPrepass` and
// declares `Read(SceneDepth, DepthRead)`. The framegraph compiler therefore
// emits a render pass with a `D32_FLOAT` depth attachment in read-only state,
// so this pipeline mirrors the depth-equal / depth-write-off shape the
// forward and deferred GBuffer pipelines use. The depth-equal test guarantees
// only the nearest-surface fragment wins each pixel — without it the readback
// drain would return wrong IDs for any pixel covered by more than one draw.
// The recipe gates picking on `EnablePicking && EnableDepthPrepass`, so the
// pipeline is only requested when a populated `SceneDepth` is available.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, EntityIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{287u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionEntityIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionEntityIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/entity_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/entity_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    // Render-pass compatibility with the recipe-declared depth-read
    // PickingPass: depth-test on, depth-equal, depth-write off, D32_FLOAT.
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionEntityIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionEntityIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice B — default-recipe Face / Edge / Point selection ID
// pipeline lease + republish. Each test mirrors the EntityId pipeline check:
// the operational publisher creates the pipeline on initial init, the
// descriptor matches the shader-pair contract (the legacy
// `assets/shaders/pick_mesh.{vert,frag}` / `pick_line.{vert,frag}` /
// `pick_point.{vert,frag}` shaders are pre-GpuScene footguns and are
// deliberately *not* used here — see `src/graphics/renderer/README.md`
// "Shader push-constant compatibility policy"), and
// `RebuildOperationalResources()` republishes a byte-identical descriptor.
// All three pipelines share the EntityId pipeline's depth-equal / depth-write-
// off / two-R32_UINT-color-target render-pass shape so they can be bound
// inside the same recipe-declared `PickingPass` render pass; they differ only
// in primitive topology and cull mode.
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, FaceIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{288u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionFaceIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionFaceIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/face_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/face_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::Back);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionFaceIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionFaceIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, EdgeIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{289u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionEdgeIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionEdgeIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/edge_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/edge_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::LineList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionEdgeIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionEdgeIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, PointIdPickingPipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{290u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionPointIdPipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionPointIdPipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/selection/point_id.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection/point_id.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::PointList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_EQ(initialDesc.Rasterizer.Winding, Extrinsic::RHI::FrontFace::CounterClockwise);
    EXPECT_TRUE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_EQ(initialDesc.DepthStencil.DepthFunc, Extrinsic::RHI::DepthOp::Equal);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_FALSE(initialDesc.ColorBlend[1].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 2u);
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.ColorTargetFormats[1], Extrinsic::RHI::Format::R32_UINT);
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    EXPECT_EQ(initialDesc.PushConstantSize, sizeof(Extrinsic::RHI::GpuScenePushConstants));

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionPointIdPipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionPointIdPipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

// ---------------------------------------------------------------------------
// GRAPHICS-074 Slice C — default-recipe selection outline pipeline lease +
// republish, and executor-branch routing. The outline pipeline is a fullscreen
// quad (`post_fullscreen.vert` + `selection_outline.frag`) bound by the
// `"SelectionOutlinePass"` executor branch when the recipe's
// `features.EnableSelectionOutline` is true (driven by
// `world.Selection.HasHovered || !world.Selection.SelectedStableIds.empty()`
// in `DeriveDefaultFrameRecipeFeatures`).
// ---------------------------------------------------------------------------

TEST(RendererFrameLifecycle, SelectionOutlinePipelineSurvivesOperationalRebuild)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{291u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    const Extrinsic::RHI::PipelineHandle initialPipeline = renderer->GetSelectionOutlinePipeline();
    EXPECT_TRUE(initialPipeline.IsValid());

    const Extrinsic::RHI::PipelineDesc initialDesc = renderer->GetSelectionOutlinePipelineDesc();
    EXPECT_TRUE(initialDesc.VertexShaderPath.ends_with(
        "shaders/post_fullscreen.vert.spv"))
        << initialDesc.VertexShaderPath;
    EXPECT_TRUE(initialDesc.FragmentShaderPath.ends_with(
        "shaders/selection_outline.frag.spv"))
        << initialDesc.FragmentShaderPath;
    EXPECT_EQ(initialDesc.PrimitiveTopology, Extrinsic::RHI::Topology::TriangleList);
    EXPECT_EQ(initialDesc.Rasterizer.Culling, Extrinsic::RHI::CullMode::None);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthTestEnable);
    EXPECT_FALSE(initialDesc.DepthStencil.DepthWriteEnable);
    EXPECT_FALSE(initialDesc.ColorBlend[0].Enable);
    EXPECT_EQ(initialDesc.ColorTargetCount, 1u);
    // Color target matches the recipe's `SelectionOutline` texture, which is
    // allocated with `FrameRecipeSizing::BackbufferFormat`; the MockDevice
    // does not override `GetBackbufferFormat()` so the renderer's stored
    // format is `RHI::Format::RGBA8_UNORM`.
    EXPECT_EQ(initialDesc.ColorTargetFormats[0], Extrinsic::RHI::Format::RGBA8_UNORM);
    // Render-pass-compatible with the recipe-declared depth attachment
    // (`builder.Read(SceneDepth, DepthRead)`) even though the pipeline does
    // not test or write depth itself.
    EXPECT_EQ(initialDesc.DepthTargetFormat, Extrinsic::RHI::Format::D32_FLOAT);
    // Matches `SelectionOutlinePushConstants` in `Pass.Selection.Outline.cpp`,
    // which mirrors the `selection_outline.frag` `Push` block byte-for-byte
    // (vec4 OutlineColor + vec4 HoverColor + 12 floats/uints + uint[16]
    // SelectedIds = 144 bytes under Vulkan std430). The pass body pushes a
    // zero-initialised instance every frame so the shader sees defined
    // values rather than stale push memory left by a prior draw.
    EXPECT_EQ(initialDesc.PushConstantSize, 144u);

    EXPECT_TRUE(renderer->RebuildOperationalResources(device));
    const Extrinsic::RHI::PipelineHandle rebuiltPipeline = renderer->GetSelectionOutlinePipeline();
    EXPECT_TRUE(rebuiltPipeline.IsValid());
    const Extrinsic::RHI::PipelineDesc rebuiltDesc = renderer->GetSelectionOutlinePipelineDesc();
    EXPECT_TRUE(PipelineDescBytesEqual(initialDesc, rebuiltDesc));

    renderer->Shutdown();
}

TEST(RendererFrameLifecycle, SelectionOutlinePassRecordsWhenSelectableEntityPresent)
{
    Extrinsic::Tests::MockDevice device;
    device.Operational = true;
    device.BackbufferHandle = Extrinsic::RHI::TextureHandle{292u, 1u};

    std::unique_ptr<Extrinsic::Graphics::IRenderer> renderer = Extrinsic::Graphics::CreateRenderer();
    renderer->Initialize(device);

    Extrinsic::RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 240},
    };
    Extrinsic::Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    // Force `features.EnableSelectionOutline = true` so the recipe declares
    // `SelectionOutlinePass`. Without this the recipe drops the pass and
    // `FindCommandPass(stats, "SelectionOutlinePass")` would correctly return
    // null — covering only the "outline is gated off" path, not Slice C's
    // executor-route contract.
    world.Selection.HasHovered = true;
    world.Selection.HoveredStableId = 42u;

    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Extrinsic::Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const Extrinsic::Graphics::RenderGraphCommandPassStats* outlinePass =
        FindCommandPass(stats, "SelectionOutlinePass");
    ASSERT_NE(outlinePass, nullptr);
    EXPECT_EQ(outlinePass->Status, Extrinsic::Graphics::RenderCommandPassStatus::Recorded);

    // GRAPHICS-074 Slice C — the outline pass must push deterministic
    // 144-byte zero-initialised push constants before its fullscreen draw
    // so the shader never reads stale push memory from a prior pass. Walk
    // the captured `PushConstants(...)` payloads and require at least one
    // 144-byte all-zero payload (the outline pass's contribution) to
    // appear; earlier passes in the same frame contribute their own
    // (`GpuScenePushConstants`, etc.) so we cannot just assert payload
    // count == 1.
    bool foundOutlinePush = false;
    for (const std::vector<std::byte>& payload : device.CommandContext.PushConstantPayloads)
    {
        if (payload.size() != 144u)
        {
            continue;
        }
        bool allZero = true;
        for (const std::byte b : payload)
        {
            if (b != std::byte{0})
            {
                allZero = false;
                break;
            }
        }
        if (allZero)
        {
            foundOutlinePush = true;
            break;
        }
    }
    EXPECT_TRUE(foundOutlinePush)
        << "SelectionOutlinePass must push a deterministic 144-byte zero-"
        << "initialised SelectionOutlinePushConstants block before its draw.";

    renderer->Shutdown();
}

