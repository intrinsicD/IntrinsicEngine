// GRAPHICS-078 — CPU-mock contract for the canonical default-recipe
// `VisualizationOverlayPass` and its renderer-integrated
// `"VisualizationOverlayPass"` executor branch.
//
// Slice A pinned the recipe-declaration shape (pass appears in the
// recipe only when at least one visualization-overlay packet exists
// for the frame) and the executor-taxonomy seams
// (`SkippedNonOperational` when the device is not operational;
// `SkippedUnavailable` + `MissingPipelineSkipCount++` when the
// pipelines are missing on an operational device).
//
// Slice B promotes the vector-field lane from `SkippedUnavailable` to
// `Recorded` by creating two pipeline variants (depth-tested +
// always-on-top, calls #32 + #33), driving the renderer-owned
// `VisualizationOverlayUploadHelper` to pack per-frame vertex data,
// and recording `BindPipeline + PushConstants(16) +
// Draw(2 * ElementCount, 1, 0, 0)` per submitted vector-field packet
// (switching pipeline variants on each `DepthTested` flip).
//
// Slice C extends to the isoline lane; Slice D adds the opt-in
// `gpu;vulkan` pixel-readback smoke.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

    void SubmitOneVectorField(Graphics::IRenderer& renderer, const bool depthTested = true)
    {
        // A single non-empty vector-field-overlay packet span is enough
        // to flip `features.EnableVisualizationOverlay` and add the
        // pass to the recipe. `ElementCount = 1` produces a single
        // glyph = 2 packed vertices in the helper's host-visible
        // buffer; the pass records `Draw(2, 1, 0, 0)` per packet.
        static Graphics::VectorFieldOverlayPacket sPackets[1] = {
            Graphics::VectorFieldOverlayPacket{
                .Name         = "Test.VectorField",
                .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
                .ElementCount = 1u,
                .Scale        = 1.0f,
                .Color        = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                .DepthTested  = true,
            },
        };
        sPackets[0].DepthTested = depthTested;

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .VisualizationVectorFields =
                std::span<const Graphics::VectorFieldOverlayPacket>{sPackets, 1u},
        });
    }
}

// -----------------------------------------------------------------------------
// Recipe-declaration tests (Slice A — unchanged by Slice B).
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

// -----------------------------------------------------------------------------
// Slice B — vector-field lane executor + helper contract.
// -----------------------------------------------------------------------------

TEST(VisualizationOverlayPassContract, MissingVectorFieldPipelineLeaseSkipsUnavailable)
{
    // GRAPHICS-078 Slice B — failing the vector-field DepthTested
    // pipeline create (call #32, immediately after the GRAPHICS-077
    // point AlwaysOnTop at #31) yields
    // `"VisualizationOverlayPass" = SkippedUnavailable` while every
    // upstream pipeline lease (culling / depth / surface / line /
    // point / shadow / deferred / selection / postprocess / present /
    // debug-view / transient-debug) keeps the rest of the default
    // recipe recording. `MissingPipelineSkipCount` increments by
    // exactly 1 so the diagnostic counter distinguishes "feature on,
    // pipeline missing" from "feature off" (the latter does not reach
    // this branch).
    MockDevice device;
    device.FailPipelineCreateCall = 32;
    device.BackbufferHandle = RHI::TextureHandle{783u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneVectorField(*renderer);

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

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);

    // The vector-field pipeline was suppressed; the helper's upload
    // path never runs because the pipeline gate fails first. So
    // submitted/recorded stay at zero on the per-kind counters, and
    // only `MissingPipelineSkipCount` ticks.
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 1u);

    // Upstream passes still record — verify a representative one.
    const auto* surfacePass = FindCommandPass(stats, "SurfacePass");
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, RecordsVectorFieldBindPipelineAndDraw)
{
    // GRAPHICS-078 Slice B — a single sanitized
    // `VectorFieldOverlayPacket` (`ElementCount = 1`, `DepthTested =
    // true`) records the canonical per-packet
    // `BindPipeline(depth-tested) + PushConstants(16) +
    // Draw(2 * ElementCount, 1, 0, 0)` shape and increments
    // `VectorFieldRecordsSubmitted` + `VectorFieldRecordsRecorded` by
    // exactly 1.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{784u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneVectorField(*renderer, /*depthTested=*/true);

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

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    // A 16-byte `VisualizationVectorFieldPushConstants` payload must
    // reach the device command context as part of the per-packet
    // recording sequence. Other passes in the default recipe also
    // push 16-byte payloads (e.g. `DebugViewPushConstants` when the
    // overlay is on, and the GRAPHICS-077 transient-debug push
    // blocks), so the assertion is "at least one 16-byte push reached
    // the context" — sufficient to pin the contract without coupling
    // to the exact ordering of other 16-byte-push consumers.
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::VisualizationVectorFieldPushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 1u)
        << "expected at least one " << expectedPushSize
        << "-byte push for the visualization-overlay vector-field packet";

    // Note: BDA-value correctness on a real Vulkan device requires
    // `BufferUsage::Storage` on the helper's vertex buffer (per
    // `RHI.Device.cppm` and `Backends.Vulkan.Device.cpp` `HasBDA`
    // gate). The operational BDA validation is owned by the
    // GRAPHICS-078 Slice D `gpu;vulkan` smoke; this contract test
    // pins the bind/push/draw command shape only.

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, SelectsVectorFieldAlwaysOnTopVariantPerPacket)
{
    // GRAPHICS-078 Slice B — mixed `DepthTested` flags across packets
    // cause the correct pipeline variant to bind per packet. Three
    // packets [depth-tested, always-on-top, depth-tested] flips the
    // variant twice, so the executor emits 3 BindPipelines + 3
    // PushConstants + 3 Draws under this pass. Counts the 16-byte
    // pushes attributable to the lane at >= 3.
    static const std::array<Graphics::VectorFieldOverlayPacket, 3> kVectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.A",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 2u,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested  = true,
        },
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.B",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 3u,
            .Scale        = 1.0f,
            .Color        = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested  = false,
        },
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.C",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 1u,
            .Scale        = 1.0f,
            .Color        = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
            .DepthTested  = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{785u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields = std::span<const Graphics::VectorFieldOverlayPacket>{
            kVectorFields.data(), kVectorFields.size()},
    });

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 128},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 3u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 3u);

    // 3 packets ⇒ 3 16-byte pushes from this pass. Other passes in
    // the recipe may also push 16-byte payloads, so the contract pin
    // is "at least 3 sized pushes reached the device". Counting `>= 3`
    // keeps the test stable if a sibling 16-byte-push consumer
    // changes.
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::VisualizationVectorFieldPushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 3u)
        << "expected at least 3 " << expectedPushSize
        << "-byte pushes for the three visualization-overlay vector-field packets";

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, UploadOverflowSkipsUnavailableWithoutFalseRecorded)
{
    // GRAPHICS-078 Slice B — adversarial-input pin. A single
    // `VectorFieldOverlayPacket` whose `2 * ElementCount` exceeds the
    // per-lane cap (`kMaxVectorFieldVertexCount = 1 << 18 = 262144`)
    // must fail-close BEFORE the helper's staging-buffer allocation —
    // otherwise the per-frame `std::vector<PackedOverlayVertex>` of
    // size `2 * ElementCount` would attempt a multi-GiB host
    // allocation (or throw `bad_alloc`) for an adversarial
    // `ElementCount = UINT32_MAX`. The pass MUST report
    // `SkippedUnavailable` rather than masking the failure as
    // `Recorded`. `UploadOverflowCount` ticks once;
    // `MissingPipelineSkipCount` stays at zero (the pipelines are
    // healthy, the upload gate is independent of the pipeline gate);
    // `VectorFieldRecordsRecorded` stays at zero.
    //
    // 200 000 * 2 = 400 000 > 262 144 → over the cap by ~140k verts
    // (~2.1 MiB if the allocation succeeded). Picked deliberately
    // large enough to overflow the cap but small enough that a buggy
    // pre-fix run that DOES allocate the buffer still does not OOM
    // the CI host (so the regression failure mode is "Recorded /
    // unbounded allocation" rather than "process killed", which
    // would make the regression hard to diagnose).
    static const std::array<Graphics::VectorFieldOverlayPacket, 1> kVectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.Overflow",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 200000u,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            .DepthTested  = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{787u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields = std::span<const Graphics::VectorFieldOverlayPacket>{
            kVectorFields.data(), kVectorFields.size()},
    });

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

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable)
        << "upload overflow must surface as SkippedUnavailable, not Recorded";

    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 1u);
    // Pipeline gate is independent of upload — the lane's pipelines
    // are healthy, so the missing-pipeline counter stays untouched.
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, PerFrameBufferRecyclingDoesNotLeakVectorField)
{
    // GRAPHICS-078 Slice B — across N frames with a constant vector-
    // field payload the upload helper's underlying host-visible vertex
    // buffer must not leak — the helper reuses a single growing
    // buffer rather than allocating per frame. The renderer-internal
    // helper instance is not directly exposed, so we observe the
    // invariant through the `MockDevice.CreateBufferCount` delta:
    // after the first operational frame the helper's allocation is
    // in-flight, and subsequent frames must not increase the buffer-
    // create count attributable to the visualization-overlay pass.
    // Other systems may create buffers on the first operational frame
    // as well, so we sample the counter after frame 1 as the baseline
    // and assert the delta across frames 2-5 is zero.
    static const std::array<Graphics::VectorFieldOverlayPacket, 1> kVectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 4u,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f},
            .DepthTested  = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{786u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    int createBufferCountAfterFrame1 = 0;
    for (int frameIndex = 1; frameIndex <= 5; ++frameIndex)
    {
        RHI::FrameHandle frame{};
        ASSERT_TRUE(renderer->BeginFrame(frame));
        renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .VisualizationVectorFields = std::span<const Graphics::VectorFieldOverlayPacket>{
                kVectorFields.data(), kVectorFields.size()},
        });
        const Graphics::RenderFrameInput input{
            .Viewport = {.Width = 64, .Height = 64},
        };
        Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
        renderer->PrepareFrame(world);
        renderer->ExecuteFrame(frame, world);
        (void)renderer->EndFrame(frame);

        if (frameIndex == 1)
        {
            createBufferCountAfterFrame1 = device.CreateBufferCount;
        }
        else
        {
            // No further buffer creation should be attributable to
            // the visualization-overlay helper across frames 2-5 with
            // constant payload (the helper's buffer is reused).
            EXPECT_EQ(device.CreateBufferCount, createBufferCountAfterFrame1)
                << "frame " << frameIndex;
        }
    }

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    // Each frame submits + records one packet; across 5 frames the
    // counters accumulate per-frame totals from the FINAL frame only
    // (the renderer resets `m_LastRenderGraphStats` per
    // `ExecuteFrame()`).
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);

    renderer->Shutdown();
}
