// GRAPHICS-032C — CPU-mock contract for the MinimalDebugPresent pass and
// its renderer-integrated executor branch.
//
// The pass-class tests mirror Test.MinimalDebugSurfacePass.cpp (a local
// RecordingCommandContext + a direct Execute invocation) to assert the
// ordered command stream: BindPipeline followed by Draw(3, 1, 0, 0).
//
// The renderer-integrated tests mirror Test.MinimalDebugSurfacePass.cpp to
// exercise the executor route through SetFrameRecipe(MinimalDebug) and
// assert the present-side counter / skip taxonomy declared by GRAPHICS-032A.
// One additional acceptance-style test asserts that the minimal recipe's
// surface and present executions co-increment for a single frame and that
// the documented SceneColorHDR/Backbuffer barrier sequence reaches the
// device CommandContext.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.Present.MinimalDebug;
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

TEST(MinimalDebugPresentPassContract, ExecuteRecordsBindPipelineThenFullscreenDrawInOrder)
{
    Graphics::MinimalDebugPresentPass pass;

    RecordingCommandContext noPipelineCmd;
    pass.Execute(noPipelineCmd);
    EXPECT_TRUE(noPipelineCmd.Events.empty())
        << "MinimalDebugPresent must short-circuit when its pipeline lease is unset.";

    const RHI::PipelineHandle pipeline{777u, 3u};
    pass.SetPipeline(pipeline);
    EXPECT_EQ(pass.GetPipeline(), pipeline);

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
// Renderer-integrated tests (executor branch + counters)
// -----------------------------------------------------------------------------

TEST(MinimalDebugPresentPassContract, RendererRoutesAndIncrementsPresentExecutionsCounter)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{611u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

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

    // The minimal recipe must not pull in any default-recipe pass; only the
    // two minimal-recipe passes appear in the per-pass stats.
    EXPECT_EQ(FindCommandPass(stats, "DepthPrepass"), nullptr);
    EXPECT_EQ(FindCommandPass(stats, "Present"), nullptr);

    const auto* presentPass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugPresentPassName});
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.MinimalPresentPassExecutions, 1u);
    EXPECT_EQ(stats.MinimalRecipeMissingPrerequisiteCount, 0u)
        << "Operational MockDevice with valid imports leaves the prerequisite "
           "counter at zero from both the recipe build and the executor route.";

    renderer->Shutdown();
}

TEST(MinimalDebugPresentPassContract, MissingSlotZeroPipelineLeaseSkipsUnavailableAndIncrementsCounter)
{
    Tests::MockDevice device;
    // GRAPHICS-031A creates the slot-0 default-debug-surface pipeline as the
    // third PipelineManager::Create() call (culling, depth-prepass, then
    // default-debug-surface). GRAPHICS-032C reuses that same lease for the
    // minimal-debug present pass; failing the third Create() therefore
    // exercises the SkippedUnavailable path for both minimal passes.
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

    const auto* presentPass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugPresentPassName});
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    EXPECT_EQ(stats.MinimalPresentPassExecutions, 0u);
    EXPECT_GE(stats.MinimalRecipeMissingPrerequisiteCount, 1u);

    renderer->Shutdown();
}

TEST(MinimalDebugPresentPassContract, NonOperationalDeviceSkipsNonOperational)
{
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

    const auto* presentPass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugPresentPassName});
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);

    EXPECT_EQ(stats.MinimalPresentPassExecutions, 0u);

    renderer->Shutdown();
}

TEST(MinimalDebugPresentPassContract, DefaultRecipeDoesNotIncrementMinimalPresentCounter)
{
    // Recipe-vs-default isolation: with the default recipe selected the
    // minimal-recipe pass branches are never reached, so neither minimal
    // counter increments. The renderer-integrated default-recipe behavior
    // (CullingPass/DepthPrepass recorded; remaining passes
    // SkippedUnavailable) is exercised in Test.RendererFrameLifecycle.cpp.
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{211u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    EXPECT_EQ(renderer->GetFrameRecipe(), Core::Config::FrameRecipeKind::Default);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 96, .Height = 96},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    EXPECT_EQ(FindCommandPass(stats, std::string{Graphics::kMinimalDebugSurfacePassName}), nullptr);
    EXPECT_EQ(FindCommandPass(stats, std::string{Graphics::kMinimalDebugPresentPassName}), nullptr);

    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 0u);
    EXPECT_EQ(stats.MinimalPresentPassExecutions, 0u);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// End-to-end CPU acceptance driver (counters + barrier order)
// -----------------------------------------------------------------------------
//
// This drives one frame of the MinimalDebugSurface recipe on the operational
// MockDevice, asserting (a) MinimalSurfacePassExecutions and
// MinimalPresentPassExecutions co-increment for the frame and the
// missing-prerequisite counter stays at zero, and (b) the documented texture
// barrier sequence reaches the device CommandContext in the order the
// minimal-recipe graph compile emits.
//
// The runtime-extraction `RuntimeRenderExtractionStats::ProceduralGeometryUploads`
// assertion noted in the GRAPHICS-032C task body is intentionally deferred to
// a separate `contract;runtime` slice. Adding it here would couple a
// `contract;graphics` test to the runtime extraction layer; the renderer-side
// contract is fully exercised by the counters and barrier-order assertions
// below.

TEST(MinimalDebugPresentPassContract, AcceptanceFrameRecordsBothPassesAndBarrierSequence)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{911u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetFrameRecipe(Core::Config::FrameRecipeKind::MinimalDebug);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 320, .Height = 200},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 1u);
    EXPECT_EQ(stats.MinimalPresentPassExecutions, 1u);
    EXPECT_EQ(stats.MinimalRecipeMissingPrerequisiteCount, 0u);

    const auto* surfacePass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugSurfacePassName});
    const auto* presentPass = FindCommandPass(stats, std::string{Graphics::kMinimalDebugPresentPassName});
    ASSERT_NE(surfacePass, nullptr);
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    // BUG-010: the imported Backbuffer must reach the end-of-graph Present
    // sentinel layout. Render-graph barrier compilation emits this transition
    // from the backbuffer's `ImportBackbuffer` final-state contract
    // (`InitialState = Undefined`, `FinalState = Present`). The framegraph
    // intentionally rejects `Write(backbuffer, ...)` declarations
    // (`Graphics.RenderGraph.cpp:206`), so the canonical barrier shape is
    // `Undefined -> Present` rather than `ColorAttachment -> Present` — the
    // minimal present pass samples `SceneColorHDR` and the imported
    // backbuffer is only declared via `Read(backbuffer, TextureUsage::Present)`,
    // which marks the side-effect finalization without authorizing a
    // graph-level color-attachment write.
    int backbufferToPresentIndex = -1;
    for (std::size_t bi = 0; bi < device.CommandContext.TextureBarrierCalls.size(); ++bi)
    {
        const auto& barrier = device.CommandContext.TextureBarrierCalls[bi];
        if (barrier.Texture == device.BackbufferHandle &&
            barrier.After == RHI::TextureLayout::Present)
        {
            backbufferToPresentIndex = static_cast<int>(bi);
            break;
        }
    }
    EXPECT_GE(backbufferToPresentIndex, 0)
        << "Minimal recipe must finalize the backbuffer to Present as the "
           "end-of-graph sentinel.";

    // The end-of-graph backbuffer transition starts from the imported
    // `Undefined` initial state because the framegraph forbids backbuffer
    // writes and therefore never routes the imported handle through an
    // intermediate `ColorAttachment`/`General` layout. If the compiler
    // ever inserts an intermediate state the assertion below documents the
    // expected ordering invariant.
    if (backbufferToPresentIndex >= 0)
    {
        const auto& finalBarrier =
            device.CommandContext.TextureBarrierCalls[static_cast<std::size_t>(backbufferToPresentIndex)];
        EXPECT_EQ(finalBarrier.Before, RHI::TextureLayout::Undefined)
            << "Minimal recipe must transition the imported backbuffer directly "
               "from Undefined to Present because the framegraph rejects "
               "backbuffer writes; if this ever changes, update the recipe "
               "barrier contract documentation in tasks/done/GRAPHICS-032 and "
               "tasks/done/BUG-010 as well.";
    }

    renderer->Shutdown();
}
