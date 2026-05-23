// GRAPHICS-077 Slice A — CPU-mock contract for the canonical
// default-recipe `TransientDebugSurfacePass`. Slice A is scaffold-only:
// the recipe declares the pass when at least one transient debug
// primitive packet exists for the frame, the renderer's
// `m_TransientDebugSurfacePass` holds no pipeline yet, and the
// executor's new `"TransientDebugSurfacePass"` branch routes through
// `RecordTransientDebugSurfacePass(...)` which returns
// `SkippedUnavailable` on the operational path and
// `SkippedNonOperational` otherwise. Slice B starts populating the
// triangle-lane counters on `TransientDebugUploadDiagnostics`; Slice C
// extends to line + point lanes; Slice D adds the opt-in `gpu;vulkan`
// pixel-readback smoke. This file pins the four invariants the slice
// must preserve.

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
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

    void SubmitOneTriangle(Graphics::IRenderer& renderer)
    {
        // A single sanitized triangle packet is enough to flip
        // `features.EnableTransientDebugSurface` and add the pass to
        // the recipe. The renderer's input validator rejects
        // non-finite coordinates / NaN colors, so use plain unit
        // values that survive `IsValidDebugTriangle(...)`.
        static const std::array<Graphics::DebugTrianglePacket, 1> kTriangles{{
            Graphics::DebugTrianglePacket{
                .A = glm::vec3{0.0f, 0.5f, 0.0f},
                .B = glm::vec3{-0.5f, -0.5f, 0.0f},
                .C = glm::vec3{0.5f, -0.5f, 0.0f},
                .Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                .DepthTested = true,
            },
        }};

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .DebugTriangles = kTriangles,
        });
    }
}

// -----------------------------------------------------------------------------
// Recipe-declaration tests
// -----------------------------------------------------------------------------

TEST(TransientDebugSurfacePassContract, RecipeDeclaresPassWhenTransientPrimitivesExist)
{
    // GRAPHICS-077 Slice A — derived `features.EnableTransientDebugSurface`
    // must flip true when at least one lane's packet span is non-empty,
    // and `DescribeDefaultFrameRecipe(...)` must include the pass exactly
    // once with the canonical reads/writes.
    Graphics::RenderWorld world{};
    const std::array<Graphics::DebugTrianglePacket, 1> triangles{{
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.5f, 0.0f},
            .B = glm::vec3{-0.5f, -0.5f, 0.0f},
            .C = glm::vec3{0.5f, -0.5f, 0.0f},
            .Color = glm::vec4{1.0f},
            .DepthTested = true,
        },
    }};
    world.DebugPrimitives.Triangles = triangles;
    world.DebugPrimitives.TriangleCount = 1u;
    world.DebugPrimitives.HasTransientDebug = true;

    const Graphics::FrameRecipeFeatures features = Graphics::DeriveDefaultFrameRecipeFeatures(world);
    EXPECT_TRUE(features.EnableTransientDebugSurface);

    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);

    std::uint32_t occurrences = 0u;
    const Graphics::FrameRecipePassDeclaration* match = nullptr;
    for (const auto& pass : description.Passes)
    {
        if (pass.Kind == Graphics::FrameRecipePassKind::TransientDebugSurface)
        {
            ++occurrences;
            match = &pass;
        }
    }
    ASSERT_EQ(occurrences, 1u);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->Name, std::string_view{"TransientDebugSurfacePass"});
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

TEST(TransientDebugSurfacePassContract, RecipeOmitsPassWhenNoTransientPrimitives)
{
    // GRAPHICS-077 Slice A — empty spans across all three lanes must
    // keep `features.EnableTransientDebugSurface` false, and the
    // recipe must surface the pass as `Enabled=false` (declared but
    // dropped) so the executor never sees the branch. The compiled
    // graph in turn must omit the pass entirely from
    // `CommandRecords.Passes` when the renderer runs a clean default
    // frame.
    const Graphics::RenderWorld world{};
    const Graphics::FrameRecipeFeatures features = Graphics::DeriveDefaultFrameRecipeFeatures(world);
    EXPECT_FALSE(features.EnableTransientDebugSurface);

    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);
    const Graphics::FrameRecipePassDeclaration* match = nullptr;
    for (const auto& pass : description.Passes)
    {
        if (pass.Kind == Graphics::FrameRecipePassKind::TransientDebugSurface)
        {
            match = &pass;
            break;
        }
    }
    ASSERT_NE(match, nullptr);
    EXPECT_FALSE(match->Enabled);

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{700u, 1u};
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
    EXPECT_EQ(FindCommandPass(stats, "TransientDebugSurfacePass"), nullptr);
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// Renderer-integrated executor-branch tests
// -----------------------------------------------------------------------------

TEST(TransientDebugSurfacePassContract, ExecutorReportsSkippedUnavailableWithoutPipelines)
{
    // GRAPHICS-077 Slice A — when the world submits a transient debug
    // primitive, the recipe declares the pass, the executor reaches the
    // new branch, and the helper returns `SkippedUnavailable` (no
    // pipelines exist yet in Slice A). All `TransientDebugUpload`
    // counters stay at zero except `MissingPipelineSkipCount`, which
    // increments by one to distinguish "feature on but pipeline
    // missing" from "feature off".
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{701u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    // `BeginFrame()` resets the renderer's per-frame packet buffers, so
    // the snapshot submission must follow it for the recipe to observe
    // the transient triangle this frame.
    SubmitOneTriangle(*renderer);

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 128},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    // Slice A pins zero records — no helper, no per-lane recording.
    // `MissingPipelineSkipCount` is the operational-scaffold signal.
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 1u);

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, NonOperationalDeviceSkipsNonOperational)
{
    // GRAPHICS-077 Slice A — when the device is not operational, the
    // executor still visits the new branch (the recipe declares the
    // pass because primitives exist), but the helper short-circuits to
    // `SkippedNonOperational` BEFORE the pipeline check, so
    // `MissingPipelineSkipCount` stays at zero. This mirrors the
    // present / debug-view symmetric shape from GRAPHICS-076.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{702u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    device.Operational = false;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    // `BeginFrame()` resets the renderer's per-frame packet buffers, so
    // the snapshot submission must follow it for the recipe to observe
    // the transient triangle this frame.
    SubmitOneTriangle(*renderer);

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

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}
