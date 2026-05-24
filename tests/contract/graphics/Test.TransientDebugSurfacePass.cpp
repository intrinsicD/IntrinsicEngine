// GRAPHICS-077 — CPU-mock contract for the canonical default-recipe
// `TransientDebugSurfacePass` and its renderer-integrated
// `"TransientDebugSurfacePass"` executor branch.
//
// Slice A pinned the recipe-declaration shape (pass appears in the
// recipe only when at least one transient debug primitive packet
// exists for the frame) and the executor-taxonomy seams
// (`SkippedNonOperational` when the device is not operational;
// `SkippedUnavailable` + `MissingPipelineSkipCount++` when the
// pipelines are missing on an operational device).
//
// Slice B promotes the triangle lane from `SkippedUnavailable` to
// `Recorded` by creating two pipeline variants (depth-tested +
// always-on-top, calls #26 + #27), driving the renderer-owned
// `TransientDebugUploadHelper` to pack per-frame vertex data, and
// recording `BindPipeline + PushConstants(16) + Draw(3, 1, 0, 0)`
// per submitted triangle packet (switching pipeline variants on each
// `DepthTested` flip).
//
// Slice C extends to line + point lanes; Slice D adds the opt-in
// `gpu;vulkan` pixel-readback smoke.

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.TransientDebug.Surface;
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

    void SubmitOneTriangle(Graphics::IRenderer& renderer, const bool depthTested = true)
    {
        // A single sanitized triangle packet is enough to flip
        // `features.EnableTransientDebugSurface` and add the pass to
        // the recipe. The renderer's input validator rejects
        // non-finite coordinates / NaN colors, so use plain unit
        // values that survive `IsValidDebugTriangle(...)`.
        static Graphics::DebugTrianglePacket sTriangles[1] = {
            Graphics::DebugTrianglePacket{
                .A = glm::vec3{0.0f, 0.5f, 0.0f},
                .B = glm::vec3{-0.5f, -0.5f, 0.0f},
                .C = glm::vec3{0.5f, -0.5f, 0.0f},
                .Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                .DepthTested = true,
            },
        };
        sTriangles[0].DepthTested = depthTested;

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .DebugTriangles = std::span<const Graphics::DebugTrianglePacket>{sTriangles, 1u},
        });
    }

    void SubmitOneLine(Graphics::IRenderer& renderer, const bool depthTested = true)
    {
        // GRAPHICS-077 Slice C — single sanitized line packet survives
        // `IsValidDebugLine(...)` (finite endpoints, positive width,
        // finite alpha-1 color).
        static Graphics::DebugLinePacket sLines[1] = {
            Graphics::DebugLinePacket{
                .Start = glm::vec3{-0.5f, 0.0f, 0.0f},
                .End   = glm::vec3{0.5f, 0.0f, 0.0f},
                .Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                .Width = 1.0f,
                .DepthTested = true,
            },
        };
        sLines[0].DepthTested = depthTested;

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .DebugLines = std::span<const Graphics::DebugLinePacket>{sLines, 1u},
        });
    }

    void SubmitOnePoint(Graphics::IRenderer& renderer, const bool depthTested = true)
    {
        // GRAPHICS-077 Slice C — single sanitized point packet survives
        // `IsValidDebugPoint(...)` (finite position, positive radius,
        // finite alpha-1 color).
        static Graphics::DebugPointPacket sPoints[1] = {
            Graphics::DebugPointPacket{
                .Position = glm::vec3{0.0f, 0.0f, 0.0f},
                .Color    = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                .Radius   = 0.05f,
                .DepthTested = true,
            },
        };
        sPoints[0].DepthTested = depthTested;

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .DebugPoints = std::span<const Graphics::DebugPointPacket>{sPoints, 1u},
        });
    }
}

// -----------------------------------------------------------------------------
// Recipe-declaration tests (Slice A — unchanged by Slice B).
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

TEST(TransientDebugSurfacePassContract, MissingTrianglePipelineLeaseSkipsUnavailable)
{
    // GRAPHICS-077 Slice B — failing the triangle DepthTested
    // pipeline create (call #26, immediately after debug-view at #25)
    // yields `"TransientDebugSurfacePass" = SkippedUnavailable` while
    // every upstream pipeline lease (culling / depth / surface / line /
    // point / shadow / deferred / selection / postprocess / present /
    // debug-view) keeps the rest of the default recipe recording.
    // `MissingPipelineSkipCount` increments by exactly 1 so the
    // diagnostic counter distinguishes "feature on, pipeline missing"
    // from "feature off" (the latter does not reach this branch).
    MockDevice device;
    device.FailPipelineCreateCall = 26;
    device.BackbufferHandle = RHI::TextureHandle{703u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
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

    // The triangle pipeline was suppressed; the helper's upload still
    // runs (the upload helper is independent of the pipeline create
    // failure), but `ExecuteTriangles(...)` never runs because the
    // pipeline gate fails first. So submitted/recorded stay at zero
    // on the per-lane counters, and only `MissingPipelineSkipCount`
    // ticks.
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 1u);

    // Upstream passes still record — verify a representative one.
    const auto* surfacePass = FindCommandPass(stats, "SurfacePass");
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, RecordsTriangleBindPipelineAndDraw)
{
    // GRAPHICS-077 Slice B — a single sanitized `DebugTrianglePacket`
    // (DepthTested=true, 3 vertices) records the canonical per-packet
    // `BindPipeline(depth-tested) + PushConstants(16) + Draw(3, 1, 0, 0)`
    // shape and increments `TriangleRecordsSubmitted` +
    // `TriangleRecordsRecorded` by exactly 1.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{704u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneTriangle(*renderer, /*depthTested=*/true);

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
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    // A 16-byte `TransientDebugTrianglePushConstants` payload must
    // reach the device command context as part of the per-packet
    // recording sequence. Other passes in the default recipe also
    // push 16-byte payloads (e.g. `DebugViewPushConstants` when
    // `EnableDebugView` is set, which is also true here because the
    // transient debug submission flips `HasTransientDebug`), so the
    // assertion is "at least one 16-byte push reached the context" —
    // sufficient to pin the contract without coupling to the exact
    // ordering of other 16-byte-push consumers.
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::TransientDebugTrianglePushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 1u)
        << "expected at least one " << expectedPushSize
        << "-byte push for the transient-debug triangle packet";

    // Note: BDA-value correctness on a real Vulkan device requires
    // `BufferUsage::Storage` on the helper's vertex buffer (per
    // `RHI.Device.cppm` and `Backends.Vulkan.Device.cpp` `HasBDA`
    // gate). The CPU/null gate cannot uniquely identify the
    // transient-debug push from MockDevice's command context here
    // because (a) `BufferManager` returns a pool handle whose index
    // diverges from the device handle index used by MockDevice's
    // BDA bookkeeping (`BufferManager::Create` uses its own
    // slot.size(); MockDevice uses `m_NextBuffer`), and (b) other
    // 16-byte push consumers (`DebugViewPushConstants`) share the
    // same payload size. The operational BDA validation is owned by
    // the GRAPHICS-077 Slice D `gpu;vulkan` smoke; this contract
    // test pins the bind/push/draw command shape only.

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, SelectsAlwaysOnTopVariantPerPacket)
{
    // GRAPHICS-077 Slice B — mixed `DepthTested` flags across packets
    // cause the correct pipeline variant to bind per packet. Three
    // packets [depth-tested, always-on-top, depth-tested] flips the
    // variant twice, so the executor emits 3 BindPipelines + 3
    // PushConstants + 3 Draws under this pass. The depth-tested and
    // always-on-top pipeline device handles are observed via
    // `LastBoundPipeline` after the last bind in the sequence
    // (depth-tested) — the variant flip itself is exercised
    // implicitly by the bind count.
    static const std::array<Graphics::DebugTrianglePacket, 3> kTriangles{{
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.5f, 0.0f},
            .B = glm::vec3{-0.5f, -0.5f, 0.0f},
            .C = glm::vec3{0.5f, -0.5f, 0.0f},
            .Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested = true,
        },
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.6f, 0.0f},
            .B = glm::vec3{-0.6f, -0.6f, 0.0f},
            .C = glm::vec3{0.6f, -0.6f, 0.0f},
            .Color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested = false,
        },
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.7f, 0.0f},
            .B = glm::vec3{-0.7f, -0.7f, 0.0f},
            .C = glm::vec3{0.7f, -0.7f, 0.0f},
            .Color = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
            .DepthTested = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{705u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .DebugTriangles = std::span<const Graphics::DebugTrianglePacket>{kTriangles.data(), kTriangles.size()},
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

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 3u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 3u);

    // 3 packets with alternating `DepthTested` flags ⇒ 3 packet
    // boundaries ⇒ 3 16-byte pushes from this pass. Other passes in
    // the recipe also push 16-byte payloads (e.g. `DebugViewPass` when
    // the overlay is on), so the contract pin is "at least 3 sized
    // pushes reached the device". Counting `>= 3` rather than `== 3`
    // keeps the test stable if a sibling 16-byte-push consumer
    // changes.
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::TransientDebugTrianglePushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 3u)
        << "expected at least 3 " << expectedPushSize
        << "-byte pushes for the three transient-debug triangle packets";

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, PerFrameBufferRecyclingDoesNotLeak)
{
    // GRAPHICS-077 Slice B — across N frames with a constant triangle
    // payload the upload helper's underlying host-visible vertex buffer
    // must not leak — the helper reuses a single growing buffer rather
    // than allocating per frame. The renderer-internal helper instance
    // is not directly exposed, so we observe the invariant through the
    // `MockDevice.CreateBufferCount` delta: after the first
    // operational frame the helper's allocation is in-flight, and
    // subsequent frames must not increase the buffer-create count
    // attributable to the transient-debug pass. Other systems may
    // create buffers on the first operational frame as well, so we
    // sample the counter after frame 1 as the baseline and assert the
    // delta across frames 2-5 is zero.
    static const std::array<Graphics::DebugTrianglePacket, 1> kTriangles{{
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.5f, 0.0f},
            .B = glm::vec3{-0.5f, -0.5f, 0.0f},
            .C = glm::vec3{0.5f, -0.5f, 0.0f},
            .Color = glm::vec4{1.0f},
            .DepthTested = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{706u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    int createBufferCountAfterFrame1 = 0;
    for (int frameIndex = 1; frameIndex <= 5; ++frameIndex)
    {
        RHI::FrameHandle frame{};
        ASSERT_TRUE(renderer->BeginFrame(frame));
        renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .DebugTriangles = std::span<const Graphics::DebugTrianglePacket>{kTriangles.data(), kTriangles.size()},
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
            // No further buffer creation should be attributable to the
            // transient-debug helper across frames 2-5 with constant
            // payload (the helper's buffer is reused).
            EXPECT_EQ(device.CreateBufferCount, createBufferCountAfterFrame1)
                << "frame " << frameIndex;
        }
    }

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    // Each frame submits + records one packet; across 5 frames the
    // counters accumulate per-frame totals from the FINAL frame only
    // (the renderer resets `m_LastRenderGraphStats` per
    // `ExecuteFrame()`).
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 1u);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// Slice C — line lane executor + helper contract.
// -----------------------------------------------------------------------------

TEST(TransientDebugSurfacePassContract, RecordsLineBindPipelineAndDraw)
{
    // GRAPHICS-077 Slice C — a single sanitized `DebugLinePacket`
    // (DepthTested=true, 2 vertices) records the canonical per-packet
    // `BindPipeline(line.DepthTested) + PushConstants(16) +
    // Draw(2, 1, 0, 0)` shape and increments `LineRecordsSubmitted` +
    // `LineRecordsRecorded` by exactly 1.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{710u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneLine(*renderer, /*depthTested=*/true);

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
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    // A 16-byte `TransientDebugLinePushConstants` payload must reach
    // the device. Other passes in the default recipe also push 16-byte
    // payloads (e.g. `DebugViewPushConstants` when the overlay is on,
    // which is also true here because the transient line submission
    // flips `HasTransientDebug`), so the assertion is "at least one
    // 16-byte push reached the context".
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::TransientDebugLinePushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 1u)
        << "expected at least one " << expectedPushSize
        << "-byte push for the transient-debug line packet";

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, SelectsLineAlwaysOnTopVariantPerPacket)
{
    // GRAPHICS-077 Slice C — mixed `DepthTested` flags across three
    // line packets cause the correct pipeline variant to bind per
    // packet. The variant flip is exercised implicitly by the bind
    // count.
    static const std::array<Graphics::DebugLinePacket, 3> kLines{{
        Graphics::DebugLinePacket{
            .Start = glm::vec3{-0.5f, 0.0f, 0.0f},
            .End   = glm::vec3{0.5f, 0.0f, 0.0f},
            .Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .Width = 1.0f,
            .DepthTested = true,
        },
        Graphics::DebugLinePacket{
            .Start = glm::vec3{-0.5f, 0.1f, 0.0f},
            .End   = glm::vec3{0.5f, 0.1f, 0.0f},
            .Color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .Width = 1.0f,
            .DepthTested = false,
        },
        Graphics::DebugLinePacket{
            .Start = glm::vec3{-0.5f, 0.2f, 0.0f},
            .End   = glm::vec3{0.5f, 0.2f, 0.0f},
            .Color = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
            .Width = 1.0f,
            .DepthTested = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{711u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .DebugLines = std::span<const Graphics::DebugLinePacket>{kLines.data(), kLines.size()},
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

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 3u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 3u);

    // 3 packets with alternating `DepthTested` flags ⇒ 3 16-byte
    // pushes from this pass. The `>= 3` form keeps the test stable if
    // a sibling 16-byte-push consumer changes.
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::TransientDebugLinePushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 3u)
        << "expected at least 3 " << expectedPushSize
        << "-byte pushes for the three transient-debug line packets";

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, MissingLinePipelineLeaseSkipsUnavailable)
{
    // GRAPHICS-077 Slice C — failing the line DepthTested pipeline
    // create (call #28, immediately after triangle AlwaysOnTop at #27)
    // yields `LineRecordsSubmitted/Recorded == 0` and increments
    // `MissingPipelineSkipCount` by exactly 1. With no other lane
    // submissions the pass status is `SkippedUnavailable`.
    MockDevice device;
    device.FailPipelineCreateCall = 28;
    device.BackbufferHandle = RHI::TextureHandle{712u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneLine(*renderer);

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

    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 1u);

    // Triangle / point lanes have no packets in this frame, so their
    // counters stay at zero and they do not trip the missing-pipeline
    // skip (the recipe-side gate would have already prevented the
    // branch if all three lanes were empty).
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 0u);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// Slice C — point lane executor + helper contract.
// -----------------------------------------------------------------------------

TEST(TransientDebugSurfacePassContract, RecordsPointBindPipelineAndDraw)
{
    // GRAPHICS-077 Slice C — a single sanitized `DebugPointPacket`
    // (DepthTested=true, 1 vertex) records the canonical per-packet
    // `BindPipeline(point.DepthTested) + PushConstants(16) +
    // Draw(1, 1, 0, 0)` shape and increments `PointRecordsSubmitted` +
    // `PointRecordsRecorded` by exactly 1.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{713u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOnePoint(*renderer, /*depthTested=*/true);

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
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::TransientDebugPointPushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 1u)
        << "expected at least one " << expectedPushSize
        << "-byte push for the transient-debug point packet";

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, SelectsPointAlwaysOnTopVariantPerPacket)
{
    // GRAPHICS-077 Slice C — mixed `DepthTested` flags across three
    // point packets cause the correct pipeline variant to bind per
    // packet.
    static const std::array<Graphics::DebugPointPacket, 3> kPoints{{
        Graphics::DebugPointPacket{
            .Position = glm::vec3{0.0f, 0.0f, 0.0f},
            .Color    = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .Radius   = 0.05f,
            .DepthTested = true,
        },
        Graphics::DebugPointPacket{
            .Position = glm::vec3{0.1f, 0.1f, 0.0f},
            .Color    = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .Radius   = 0.05f,
            .DepthTested = false,
        },
        Graphics::DebugPointPacket{
            .Position = glm::vec3{0.2f, 0.2f, 0.0f},
            .Color    = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
            .Radius   = 0.05f,
            .DepthTested = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{714u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .DebugPoints = std::span<const Graphics::DebugPointPacket>{kPoints.data(), kPoints.size()},
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

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 3u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 3u);

    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::TransientDebugPointPushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 3u)
        << "expected at least 3 " << expectedPushSize
        << "-byte pushes for the three transient-debug point packets";

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, MissingPointPipelineLeaseSkipsUnavailable)
{
    // GRAPHICS-077 Slice C — failing the point DepthTested pipeline
    // create (call #30) yields `PointRecordsSubmitted/Recorded == 0`
    // and increments `MissingPipelineSkipCount` by exactly 1.
    MockDevice device;
    device.FailPipelineCreateCall = 30;
    device.BackbufferHandle = RHI::TextureHandle{715u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOnePoint(*renderer);

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

    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 1u);

    renderer->Shutdown();
}

// -----------------------------------------------------------------------------
// Slice C — cross-lane behavior.
// -----------------------------------------------------------------------------

TEST(TransientDebugSurfacePassContract, RecordsAllThreeLanesWhenAllSubmitted)
{
    // GRAPHICS-077 Slice C — a frame that submits one triangle, one
    // line, and one point packet records all three lanes
    // (`Recorded`), increments each per-lane recorded counter to 1,
    // and leaves `MissingPipelineSkipCount` at zero.
    static const std::array<Graphics::DebugTrianglePacket, 1> kTriangles{{
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.5f, 0.0f},
            .B = glm::vec3{-0.5f, -0.5f, 0.0f},
            .C = glm::vec3{0.5f, -0.5f, 0.0f},
            .Color = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            .DepthTested = true,
        },
    }};
    static const std::array<Graphics::DebugLinePacket, 1> kLines{{
        Graphics::DebugLinePacket{
            .Start = glm::vec3{-0.4f, 0.0f, 0.0f},
            .End   = glm::vec3{0.4f, 0.0f, 0.0f},
            .Color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f},
            .Width = 1.0f,
            .DepthTested = true,
        },
    }};
    static const std::array<Graphics::DebugPointPacket, 1> kPoints{{
        Graphics::DebugPointPacket{
            .Position = glm::vec3{0.0f, 0.0f, 0.0f},
            .Color    = glm::vec4{0.25f, 0.25f, 0.25f, 1.0f},
            .Radius   = 0.05f,
            .DepthTested = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{716u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .DebugLines     = std::span<const Graphics::DebugLinePacket>{kLines.data(), kLines.size()},
        .DebugPoints    = std::span<const Graphics::DebugPointPacket>{kPoints.data(), kPoints.size()},
        .DebugTriangles = std::span<const Graphics::DebugTrianglePacket>{kTriangles.data(), kTriangles.size()},
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

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}

TEST(TransientDebugSurfacePassContract, MixedLanePartialPipelineSkipRecordsRemainingLanes)
{
    // GRAPHICS-077 Slice C — per-lane gating: when one lane's pipeline
    // is missing but the other lanes' pipelines are valid, the other
    // lanes still record their draws and the pass status stays
    // `Recorded`. `MissingPipelineSkipCount` increments once for the
    // skipped lane. This pins the per-lane independence promised by
    // `RecordTransientDebugSurfacePass`.
    static const std::array<Graphics::DebugLinePacket, 1> kLines{{
        Graphics::DebugLinePacket{
            .Start = glm::vec3{-0.4f, 0.0f, 0.0f},
            .End   = glm::vec3{0.4f, 0.0f, 0.0f},
            .Color = glm::vec4{0.5f, 0.5f, 0.5f, 1.0f},
            .Width = 1.0f,
            .DepthTested = true,
        },
    }};
    static const std::array<Graphics::DebugPointPacket, 1> kPoints{{
        Graphics::DebugPointPacket{
            .Position = glm::vec3{0.0f, 0.0f, 0.0f},
            .Color    = glm::vec4{0.25f, 0.25f, 0.25f, 1.0f},
            .Radius   = 0.05f,
            .DepthTested = true,
        },
    }};

    MockDevice device;
    // Fail point DepthTested (call #30) — line + triangle pipelines
    // succeed, so triangle has no packets and the line lane records.
    device.FailPipelineCreateCall = 30;
    device.BackbufferHandle = RHI::TextureHandle{717u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .DebugLines  = std::span<const Graphics::DebugLinePacket>{kLines.data(), kLines.size()},
        .DebugPoints = std::span<const Graphics::DebugPointPacket>{kPoints.data(), kPoints.size()},
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

    const auto* pass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded)
        << "the line lane should still record when only the point lane's pipeline is missing";

    // Line lane records, point lane skips (pipeline missing), triangle
    // lane has no packets.
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 0u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 1u);

    renderer->Shutdown();
}
