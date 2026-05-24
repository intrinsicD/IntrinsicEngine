// GRAPHICS-078 — CPU-mock contract for the canonical default-recipe
// `VisualizationOverlayPass` and its renderer-integrated
// `"VisualizationOverlayPass"` executor branch.
//
// Slice A pins the recipe-declaration shape (pass appears in the
// recipe only when at least one visualization-overlay packet exists
// for the frame) and the executor-taxonomy seams
// (`SkippedNonOperational` when the device is not operational;
// `SkippedUnavailable` + `MissingPipelineSkipCount++` when the
// pipelines are missing on an operational device). Mirrors the
// equivalent Slice A pins from `Test.TransientDebugSurfacePass.cpp`.
//
// Slice B promotes the vector-field lane from `SkippedUnavailable`
// to `Recorded` (pipelines #32 + #33); Slice C extends to the
// isoline lane (pipelines #34 + #35); Slice D adds the opt-in
// `gpu;vulkan` pixel-readback smoke.

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.VisualizationOverlay;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;

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

    void SubmitOneVectorField(Graphics::IRenderer& renderer)
    {
        // A single non-empty vector-field-overlay packet span is enough
        // to flip `features.EnableVisualizationOverlay` and add the
        // pass to the recipe. Slice A only pins the recipe-declaration
        // and executor-taxonomy shapes, so the packet's BDA / Domain /
        // ElementCount fields are immaterial here; the recipe's
        // `EnableVisualizationOverlay` gate keys on span emptiness, not
        // packet contents.
        static Graphics::VectorFieldOverlayPacket sPackets[1] = {
            Graphics::VectorFieldOverlayPacket{
                .Name         = "Test.VectorField",
                .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
                .ElementCount = 1u,
                .Scale        = 1.0f,
            },
        };

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .VisualizationVectorFields =
                std::span<const Graphics::VectorFieldOverlayPacket>{sPackets, 1u},
        });
    }
}

// -----------------------------------------------------------------------------
// Recipe-declaration tests
// -----------------------------------------------------------------------------

TEST(VisualizationOverlayPassContract, RecipeDeclaresPassWhenOverlayPacketsExist)
{
    // GRAPHICS-078 Slice A — derived `features.EnableVisualizationOverlay`
    // must flip true when at least one of the two visualization-overlay
    // kinds is non-empty, and `DescribeDefaultFrameRecipe(...)` must
    // include the pass exactly once with the canonical reads/writes.
    Graphics::RenderWorld world{};
    const std::array<Graphics::VectorFieldOverlayPacket, 1> vectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 4u,
            .Scale        = 1.0f,
        },
    }};
    world.Visualization.VectorFields = vectorFields;
    world.Visualization.HasVisualizationPackets = true;

    const Graphics::FrameRecipeFeatures features = Graphics::DeriveDefaultFrameRecipeFeatures(world);
    EXPECT_TRUE(features.EnableVisualizationOverlay);

    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);

    std::uint32_t occurrences = 0u;
    const Graphics::FrameRecipePassDeclaration* match = nullptr;
    for (const auto& pass : description.Passes)
    {
        if (pass.Kind == Graphics::FrameRecipePassKind::VisualizationOverlay)
        {
            ++occurrences;
            match = &pass;
        }
    }
    ASSERT_EQ(occurrences, 1u);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->Name, std::string_view{"VisualizationOverlayPass"});
    EXPECT_TRUE(match->Enabled);
    EXPECT_FALSE(match->FinalizesBackbuffer);

    bool readsSceneColor = false;
    bool readsSceneDepth = false;
    for (const std::string_view name : match->Reads)
    {
        if (name == "SceneColorHDR") readsSceneColor = true;
        if (name == "SceneDepth") readsSceneDepth = true;
    }
    EXPECT_TRUE(readsSceneColor);
    EXPECT_TRUE(readsSceneDepth);

    bool writesSceneColor = false;
    for (const std::string_view name : match->Writes)
    {
        if (name == "SceneColorHDR") writesSceneColor = true;
    }
    EXPECT_TRUE(writesSceneColor);
}

TEST(VisualizationOverlayPassContract, RecipeOmitsPassWhenNoOverlayPackets)
{
    // GRAPHICS-078 Slice A — empty spans across both kinds must keep
    // `features.EnableVisualizationOverlay` false, and the recipe must
    // surface the pass as `Enabled=false` (declared but dropped) so the
    // executor never sees the branch. The compiled graph in turn must
    // omit the pass entirely from `CommandRecords.Passes` when the
    // renderer runs a clean default frame, and the
    // `VisualizationOverlayUpload` counters must stay at zero.
    const Graphics::RenderWorld world{};
    const Graphics::FrameRecipeFeatures features = Graphics::DeriveDefaultFrameRecipeFeatures(world);
    EXPECT_FALSE(features.EnableVisualizationOverlay);

    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);
    const Graphics::FrameRecipePassDeclaration* match = nullptr;
    for (const auto& pass : description.Passes)
    {
        if (pass.Kind == Graphics::FrameRecipePassKind::VisualizationOverlay)
        {
            match = &pass;
            break;
        }
    }
    ASSERT_NE(match, nullptr);
    EXPECT_FALSE(match->Enabled);

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{780u, 1u};
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 128},
    };
    Graphics::RenderWorld extracted = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(extracted);
    renderer->ExecuteFrame(frame, extracted);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(FindCommandPass(stats, "VisualizationOverlayPass"), nullptr);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// Renderer-integrated executor-branch tests
// -----------------------------------------------------------------------------

TEST(VisualizationOverlayPassContract, ExecutorReportsSkippedUnavailableWithoutPipelines)
{
    // GRAPHICS-078 Slice A — visualization overlay packets submitted on
    // an operational device but no pipelines wired yet: executor records
    // `Status = SkippedUnavailable` and
    // `MissingPipelineSkipCount` increments by exactly 1. The
    // `VectorField` / `Isoline` per-kind counters stay at zero because
    // Slice A does not execute any lane bodies. The taxonomy mirrors
    // GRAPHICS-077 Slice A's
    // `ExecutorReportsSkippedUnavailableWithoutPipelines` pin.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{781u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    // `BeginFrame()` resets the renderer's per-frame packet buffers, so
    // the snapshot submission must follow it for the recipe to observe
    // the visualization overlay packet this frame.
    SubmitOneVectorField(*renderer);

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 64, .Height = 64},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, NonOperationalDeviceSkipsNonOperational)
{
    // GRAPHICS-078 Slice A — when the device is not operational, the
    // executor still visits the new branch (the recipe declares the
    // pass because overlay packets exist), but the helper short-
    // circuits to `SkippedNonOperational` BEFORE the pipeline check,
    // so `MissingPipelineSkipCount` stays at zero. This mirrors the
    // present / debug-view / transient-debug symmetric shape.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{782u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    device.Operational = false;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneVectorField(*renderer);

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

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}
