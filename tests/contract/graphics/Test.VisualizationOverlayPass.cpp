// CPU-mock contract for the canonical default-recipe `VisualizationOverlayPass`
// and its renderer-integrated `"VisualizationOverlayPass"` executor branch.
//
// Recipe declaration: the pass appears only when at least one overlay packet
// exists. Executor taxonomy: `SkippedNonOperational` on a non-operational
// device; `SkippedUnavailable` + `MissingPipelineSkipCount++` when pipelines
// are missing.
//
// Vector fields: one draw record per renderable packet is uploaded into the
// helper's per-frame-slot buffer, and each packet records
// `BindPipeline + PushConstants(24) + Draw(9, glyphCount, 0, 0)` — an
// instanced arrow per sampled live row, expanded on the GPU. Packets with
// unresolved buffer addresses are skipped. Isolines keep the fixture shape
// `Draw(2 * IsoValueCount, 1, 0, 0)`. Pixel output is owned by the opt-in
// `gpu;vulkan` smokes.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.VisualizationOverlay;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Graphics.VisualizationOverlayUploadHelper;
import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;

#include "MockRHI.hpp"
#include "GraphicsTestSupport.hpp"

using Extrinsic::Tests::GraphicsSupport::FindCommandPass;

using namespace Extrinsic;
using Tests::MockDevice;

static_assert(!std::is_polymorphic_v<Graphics::VisualizationOverlayUploadHelper>);

namespace
{
    // Stand-in resident buffer addresses: overlay packets are renderable only
    // once every source buffer has resolved to a device address.
    constexpr std::uint64_t kAnchorBDA = 0x10000u;
    constexpr std::uint64_t kVectorBDA = 0x20000u;

    void SubmitOneVectorField(Graphics::IRenderer& renderer, const bool depthTested = true)
    {
        // A single resolved vector-field packet flips
        // `features.EnableVisualizationOverlay`; `ElementCount = 1` records
        // one instanced arrow, `Draw(9, 1, 0, 0)`.
        static Graphics::VectorFieldOverlayPacket sPackets[1] = {
            Graphics::VectorFieldOverlayPacket{
                .Name         = "Test.VectorField",
                .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
                .ElementCount = 1u,
                .RowCount     = 1u,
                .PositionBufferBDA = kAnchorBDA,
                .VectorBufferBDA   = kVectorBDA,
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

    void SubmitOneIsoline(Graphics::IRenderer& renderer, const bool depthTested = true)
    {
        // A single non-empty isoline-overlay packet span flips
        // `features.EnableVisualizationOverlay` for the isoline lane.
        // `IsoValueCount = 1` produces a single placeholder line
        // segment = 2 packed vertices in the helper's host-visible
        // buffer; the pass records `Draw(2, 1, 0, 0)` per packet.
        static Graphics::IsolineOverlayPacket sPackets[1] = {
            Graphics::IsolineOverlayPacket{
                .SourceScalarName = "Test.Iso",
                .Domain           = Graphics::VisualizationAttributeDomain::Face,
                .IsoValueCount    = 1u,
                .RangeMin         = 0.0f,
                .RangeMax         = 1.0f,
                .LineWidth        = 1.0f,
                .Color            = glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
                .DepthTested      = true,
            },
        };
        sPackets[0].DepthTested = depthTested;

        renderer.SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .VisualizationIsolines =
                std::span<const Graphics::IsolineOverlayPacket>{sPackets, 1u},
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
            .RowCount     = 4u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
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
    // pipeline create (call #31, immediately after the GRAPHICS-077
    // point AlwaysOnTop at #30) yields
    // `"VisualizationOverlayPass" = SkippedUnavailable` while every
    // upstream pipeline lease (culling / depth / surface / line /
    // point / shadow / deferred / selection / postprocess / present /
    // debug-view / transient-debug) keeps the rest of the default
    // recipe recording. `MissingPipelineSkipCount` increments by
    // exactly 1 so the diagnostic counter distinguishes "feature on,
    // pipeline missing" from "feature off" (the latter does not reach
    // this branch).
    MockDevice device;
    device.FailPipelineCreateCall = 31;
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
    // A single resolved `VectorFieldOverlayPacket` (`ElementCount = 1`,
    // `DepthTested = true`) records the canonical per-packet
    // `BindPipeline(depth-tested) + PushConstants(24) +
    // Draw(9, glyphCount, 0, 0)` shape and increments
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

    // The packet's push block names the scene table and its draw record.
    static_assert(sizeof(Graphics::VisualizationVectorFieldPushConstants) == 24u);
    const auto& pushes = device.CommandContext.PushConstantPayloads;
    const auto push = std::find_if(pushes.begin(), pushes.end(), [](const auto& payload) {
        return payload.size() == sizeof(Graphics::VisualizationVectorFieldPushConstants);
    });
    ASSERT_NE(push, pushes.end());
    Graphics::VisualizationVectorFieldPushConstants pc{};
    std::memcpy(&pc, push->data(), sizeof(pc));
    EXPECT_NE(pc.SceneTableBDA, 0u);
    EXPECT_NE(pc.RecordBufferBDA, 0u);
    EXPECT_EQ(pc.RecordIndex, 0u);

    // One instanced arrow per live row: 9 vertices, ElementCount instances.
    const auto& draws = device.CommandContext.DrawRecords;
    EXPECT_EQ(std::count_if(draws.begin(), draws.end(), [](const auto& draw) {
                  return draw.VertexCount == Graphics::kVisualizationVectorFieldGlyphVertexCount &&
                         draw.InstanceCount == 1u && draw.FirstVertex == 0u &&
                         draw.FirstInstance == 0u;
              }),
              1);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldGlyphsRecorded, 1u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, SelectsVectorFieldAlwaysOnTopVariantPerPacket)
{
    // GRAPHICS-078 Slice B — mixed `DepthTested` flags across packets
    // cause the correct pipeline variant to bind per packet. Three
    // packets [depth-tested, always-on-top, depth-tested] flips the
    // variant twice, so the executor emits 3 BindPipelines + 3
    // PushConstants + 3 Draws under this pass. Counts the 24-byte
    // pushes attributable to the lane at >= 3.
    static const std::array<Graphics::VectorFieldOverlayPacket, 3> kVectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.A",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 2u,
            .RowCount     = 2u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested  = true,
        },
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.B",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 3u,
            .RowCount     = 3u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
            .Scale        = 1.0f,
            .Color        = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested  = false,
        },
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.C",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 1u,
            .RowCount     = 1u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
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

    // 3 packets ⇒ 3 vector-field pushes from this pass; counting `>= 3`
    // keeps the test stable if another pass uses the same push size.
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

TEST(VisualizationOverlayPassContract, UnresolvedVectorFieldBuffersSkipWithoutFalseRecorded)
{
    // A packet whose source buffers never resolved to device addresses must
    // not be drawn: the pass reports SkippedUnavailable rather than Recorded,
    // and the pipelines stay healthy.
    static const std::array<Graphics::VectorFieldOverlayPacket, 1> kVectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField.Unresolved",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 200000u,
            .RowCount     = 200000u,
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

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldPacketsSkipped, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);
    EXPECT_EQ(world.Visualization.OverlaySummary.VectorGlyphCount, 200000u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, VectorFieldRecordsCarryPacketStateAndStrideSampling)
{
    // The helper uploads one 128-byte record per renderable packet; the
    // record is the shader contract (transform, buffers, sampling, style).
    Graphics::VectorFieldOverlayPacket sampled{
        .Name = "Test.VectorField.Sampled",
        .RowBufferSourceKey = "rows",
        .Domain = Graphics::VisualizationAttributeDomain::Face,
        .ElementCount = 10u,
        .RowCount = 7u,
        .RowStride = 3u,
        .PositionBufferBDA = kAnchorBDA,
        .VectorBufferBDA = kVectorBDA,
        .RowBufferBDA = 0x30000u,
        .ObjectToWorld = glm::mat4{2.0f},
        .Scale = 0.25f,
        .NormalizeLength = false,
        .LineWidthPx = 3.0f,
        .Color = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
    };
    Graphics::VectorFieldOverlayPacket unresolved = sampled;
    unresolved.RowBufferBDA = 0u;
    EXPECT_EQ(Graphics::VectorFieldGlyphCount(sampled), 3u);
    EXPECT_TRUE(Graphics::IsRenderableVectorFieldPacket(sampled));
    EXPECT_FALSE(Graphics::IsRenderableVectorFieldPacket(unresolved));

    MockDevice device;
    device.FramesInFlight = 2u;
    RHI::BufferManager bufferManager{device};
    Graphics::VisualizationOverlayUploadHelper helper{device, bufferManager};
    helper.BeginFrame(0u, device.FramesInFlight);
    const std::array<Graphics::VectorFieldOverlayPacket, 2> packets{unresolved, sampled};
    const Graphics::VisualizationVectorFieldUploadResult upload = helper.UploadVectorFields(packets);
    ASSERT_TRUE(upload.Uploaded);
    EXPECT_EQ(upload.RecordCount, 1u);
    ASSERT_EQ(upload.RecordIndexForPacket.size(), 2u);
    EXPECT_EQ(upload.RecordIndexForPacket[0], Graphics::VisualizationVectorFieldUploadResult::kInvalidRecord);
    EXPECT_EQ(upload.RecordIndexForPacket[1], 0u);

    ASSERT_FALSE(device.BufferWrites.empty());
    const auto& write = device.BufferWrites.back();
    ASSERT_EQ(write.Data.size(), sizeof(Graphics::VisualizationVectorFieldDrawRecord));
    Graphics::VisualizationVectorFieldDrawRecord record{};
    std::memcpy(&record, write.Data.data(), sizeof(record));
    EXPECT_EQ(record.ObjectToWorld, glm::mat4{2.0f});
    EXPECT_EQ(record.PositionBufferBDA, kAnchorBDA);
    EXPECT_EQ(record.VectorBufferBDA, kVectorBDA);
    EXPECT_EQ(record.RowBufferBDA, 0x30000u);
    EXPECT_EQ(record.ElementCount, 10u);
    EXPECT_EQ(record.RowCount, 7u);
    EXPECT_EQ(record.RowStride, 3u);
    EXPECT_EQ(record.PackedColor, 0xFF0000FFu);
    EXPECT_FLOAT_EQ(record.Scale, 0.25f);
    EXPECT_FLOAT_EQ(record.LineWidthPx, 3.0f);
    EXPECT_EQ(record.Flags & Graphics::kVisualizationVectorFieldNormalizeFlag, 0u);
}

TEST(VisualizationOverlayPassContract, VectorFieldRecordOverflowSkipsUnavailable)
{
    // Records are capped per frame slot; exceeding the cap fails closed
    // before any draw is recorded.
    std::vector<Graphics::VectorFieldOverlayPacket> packets(
        (1u << 14) + 1u,
        Graphics::VectorFieldOverlayPacket{
            .Name = "Test.VectorField.Many",
            .ElementCount = 1u,
            .RowCount = 1u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA = kVectorBDA,
        });

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{788u, 1u};
    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields = std::span<const Graphics::VectorFieldOverlayPacket>{
            packets.data(), packets.size()},
    });
    const Graphics::RenderFrameInput input{.Viewport = {.Width = 64, .Height = 64}};
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedUnavailable);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 0u);
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
            .RowCount     = 4u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
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

TEST(VisualizationOverlayPassContract, UploadHelperPartitionsStorageByFrameSlot)
{
    static const Graphics::VectorFieldOverlayPacket kVectorFields[1] = {
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VectorField",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 2u,
            .RowCount     = 2u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f},
            .DepthTested  = true,
        },
    };
    static const Graphics::IsolineOverlayPacket kIsolines[1] = {
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 2u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{1.0f},
            .DepthTested      = true,
        },
    };

    MockDevice device;
    device.FramesInFlight = 2u;
    RHI::BufferManager bufferManager{device};
    Graphics::VisualizationOverlayUploadHelper helper{device, bufferManager};

    helper.BeginFrame(0u, device.FramesInFlight);
    const Graphics::VisualizationVectorFieldUploadResult vectorSlot0 =
        helper.UploadVectorFields(std::span<const Graphics::VectorFieldOverlayPacket>{
            kVectorFields, 1u});
    const Graphics::VisualizationIsolineUploadResult isolineSlot0 =
        helper.UploadIsolines(std::span<const Graphics::IsolineOverlayPacket>{
            kIsolines, 1u});
    ASSERT_TRUE(vectorSlot0.Uploaded);
    ASSERT_TRUE(isolineSlot0.Uploaded);

    helper.BeginFrame(1u, device.FramesInFlight);
    const Graphics::VisualizationVectorFieldUploadResult vectorSlot1 =
        helper.UploadVectorFields(std::span<const Graphics::VectorFieldOverlayPacket>{
            kVectorFields, 1u});
    const Graphics::VisualizationIsolineUploadResult isolineSlot1 =
        helper.UploadIsolines(std::span<const Graphics::IsolineOverlayPacket>{
            kIsolines, 1u});
    ASSERT_TRUE(vectorSlot1.Uploaded);
    ASSERT_TRUE(isolineSlot1.Uploaded);

    EXPECT_NE(vectorSlot0.RecordBuffer, vectorSlot1.RecordBuffer);
    EXPECT_NE(isolineSlot0.VertexBuffer, isolineSlot1.VertexBuffer);
    EXPECT_EQ(helper.GetBufferAllocationCount(), 4u);

    helper.BeginFrame(2u, device.FramesInFlight);
    const Graphics::VisualizationVectorFieldUploadResult vectorSlot0Again =
        helper.UploadVectorFields(std::span<const Graphics::VectorFieldOverlayPacket>{
            kVectorFields, 1u});
    const Graphics::VisualizationIsolineUploadResult isolineSlot0Again =
        helper.UploadIsolines(std::span<const Graphics::IsolineOverlayPacket>{
            kIsolines, 1u});
    ASSERT_TRUE(vectorSlot0Again.Uploaded);
    ASSERT_TRUE(isolineSlot0Again.Uploaded);
    EXPECT_EQ(vectorSlot0Again.RecordBuffer, vectorSlot0.RecordBuffer);
    EXPECT_EQ(isolineSlot0Again.VertexBuffer, isolineSlot0.VertexBuffer);
    EXPECT_EQ(helper.GetBufferAllocationCount(), 4u);
}

// -----------------------------------------------------------------------------
// Slice C — isoline lane executor + helper contract.
// -----------------------------------------------------------------------------

TEST(VisualizationOverlayPassContract, MissingIsolinePipelineLeaseSkipsUnavailable)
{
    // GRAPHICS-078 Slice C — failing the isoline DepthTested pipeline
    // create (call #33, immediately after vector-field AlwaysOnTop at
    // #32) yields `"VisualizationOverlayPass" = SkippedUnavailable`
    // for an isoline-only frame, with every upstream pipeline lease
    // keeping the rest of the default recipe recording.
    // `MissingPipelineSkipCount` increments by exactly 1 so the
    // diagnostic counter distinguishes "isoline lane on, pipeline
    // missing" from "feature off".
    MockDevice device;
    device.FailPipelineCreateCall = 33;
    device.BackbufferHandle = RHI::TextureHandle{790u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneIsoline(*renderer);

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

    // Isoline pipeline suppressed; the helper's upload path never runs
    // because the pipeline gate fails first. Submitted/recorded stay
    // at zero on the per-kind counters; only `MissingPipelineSkipCount`
    // ticks. Vector-field counters stay at zero too because this
    // frame submits no vector-field packets.
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 1u);

    // Upstream passes still record — verify a representative one.
    const auto* surfacePass = FindCommandPass(stats, "SurfacePass");
    ASSERT_NE(surfacePass, nullptr);
    EXPECT_EQ(surfacePass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, RecordsIsolineBindPipelineAndDraw)
{
    // GRAPHICS-078 Slice C — a single sanitized `IsolineOverlayPacket`
    // (`IsoValueCount = 1`, `DepthTested = true`) records the canonical
    // per-packet `BindPipeline(depth-tested) + PushConstants(16) +
    // Draw(2 * IsoValueCount, 1, 0, 0)` shape and increments
    // `IsolineRecordsSubmitted` + `IsolineRecordsRecorded` by exactly 1.
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{791u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneIsoline(*renderer, /*depthTested=*/true);

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

    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    // A 16-byte `VisualizationIsolinePushConstants` payload must reach
    // the device command context as part of the per-packet recording
    // sequence. The assertion is "at least one 16-byte push reached
    // the context" — sufficient to pin the contract without coupling
    // to the exact ordering of other 16-byte-push consumers (see the
    // matching note on `RecordsVectorFieldBindPipelineAndDraw`).
    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::VisualizationIsolinePushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 1u)
        << "expected at least one " << expectedPushSize
        << "-byte push for the visualization-overlay isoline packet";

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, SelectsIsolineAlwaysOnTopVariantPerPacket)
{
    // GRAPHICS-078 Slice C — mixed `DepthTested` flags across isoline
    // packets cause the correct pipeline variant to bind per packet.
    // Three packets [depth-tested, always-on-top, depth-tested] flips
    // the variant twice, so the executor emits 3 BindPipelines + 3
    // PushConstants + 3 Draws under this pass. Counts the 16-byte
    // pushes attributable to the lane at >= 3.
    static const std::array<Graphics::IsolineOverlayPacket, 3> kIsolines{{
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso.A",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 2u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested      = true,
        },
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso.B",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 3u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested      = false,
        },
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso.C",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 1u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{0.0f, 0.0f, 1.0f, 1.0f},
            .DepthTested      = true,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{792u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationIsolines = std::span<const Graphics::IsolineOverlayPacket>{
            kIsolines.data(), kIsolines.size()},
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
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 3u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 3u);

    const auto expectedPushSize =
        static_cast<std::uint32_t>(sizeof(Graphics::VisualizationIsolinePushConstants));
    std::size_t sizedPushCount = 0;
    for (const std::uint32_t size : device.CommandContext.PushConstantSizes)
    {
        if (size == expectedPushSize) { ++sizedPushCount; }
    }
    EXPECT_GE(sizedPushCount, 3u)
        << "expected at least 3 " << expectedPushSize
        << "-byte pushes for the three visualization-overlay isoline packets";

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, MixedLaneVectorFieldAndIsolineBothRecord)
{
    // GRAPHICS-078 Slice C — a frame that submits both vector-field and
    // isoline packets must record both lanes independently with valid
    // pipelines: the pass status is `Recorded`, each lane increments
    // its own submitted/recorded counters, and
    // `MissingPipelineSkipCount` stays at zero. This pins the per-lane
    // independence semantic that the GRAPHICS-077 transient-debug
    // pattern enforces.
    static const Graphics::VectorFieldOverlayPacket kVectorFields[1] = {
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VF",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 2u,
            .RowCount     = 2u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested  = true,
        },
    };
    static const Graphics::IsolineOverlayPacket kIsolines[1] = {
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 3u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested      = true,
        },
    };

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{793u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields =
            std::span<const Graphics::VectorFieldOverlayPacket>{kVectorFields, 1u},
        .VisualizationIsolines =
            std::span<const Graphics::IsolineOverlayPacket>{kIsolines, 1u},
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

    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, RetainedOverlayPacketLanesRecordTogether)
{
    static const std::array<Graphics::DebugTrianglePacket, 1> kTriangles{{
        Graphics::DebugTrianglePacket{
            .A = glm::vec3{0.0f, 0.4f, 0.0f},
            .B = glm::vec3{-0.4f, -0.4f, 0.0f},
            .C = glm::vec3{0.4f, -0.4f, 0.0f},
            .Color = glm::vec4{1.0f, 0.2f, 0.2f, 1.0f},
            .DepthTested = true,
        },
    }};
    static const std::array<Graphics::DebugLinePacket, 1> kLines{{
        Graphics::DebugLinePacket{
            .Start = glm::vec3{-0.5f, 0.0f, 0.0f},
            .End = glm::vec3{0.5f, 0.0f, 0.0f},
            .Color = glm::vec4{0.2f, 1.0f, 0.2f, 1.0f},
            .Width = 1.0f,
            .DepthTested = false,
        },
    }};
    static const std::array<Graphics::DebugPointPacket, 1> kPoints{{
        Graphics::DebugPointPacket{
            .Position = glm::vec3{0.0f, 0.0f, 0.0f},
            .Color = glm::vec4{0.2f, 0.2f, 1.0f, 1.0f},
            .Radius = 0.05f,
            .DepthTested = true,
        },
    }};
    static const std::array<Graphics::VectorFieldOverlayPacket, 1> kVectorFields{{
        Graphics::VectorFieldOverlayPacket{
            .Name = "GRAPHICS-085.VectorField",
            .Domain = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 1u,
            .RowCount     = 1u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
            .Scale = 1.0f,
            .Color = glm::vec4{1.0f},
            .DepthTested = true,
        },
    }};
    static const std::array<Graphics::IsolineOverlayPacket, 1> kIsolines{{
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "GRAPHICS-085.Isoline",
            .Domain = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount = 1u,
            .RangeMin = 0.0f,
            .RangeMax = 1.0f,
            .LineWidth = 1.0f,
            .Color = glm::vec4{1.0f},
            .DepthTested = false,
        },
    }};

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{798u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields = std::span<const Graphics::VectorFieldOverlayPacket>{
            kVectorFields.data(), kVectorFields.size()},
        .VisualizationIsolines = std::span<const Graphics::IsolineOverlayPacket>{
            kIsolines.data(), kIsolines.size()},
        .DebugLines = std::span<const Graphics::DebugLinePacket>{
            kLines.data(), kLines.size()},
        .DebugPoints = std::span<const Graphics::DebugPointPacket>{
            kPoints.data(), kPoints.size()},
        .DebugTriangles = std::span<const Graphics::DebugTrianglePacket>{
            kTriangles.data(), kTriangles.size()},
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

    const auto* transientPass = FindCommandPass(stats, "TransientDebugSurfacePass");
    ASSERT_NE(transientPass, nullptr);
    EXPECT_EQ(transientPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.TriangleRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.LineRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsSubmitted, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.PointRecordsRecorded, 1u);
    EXPECT_EQ(stats.TransientDebugUpload.MissingPipelineSkipCount, 0u);

    const auto* visualizationPass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(visualizationPass, nullptr);
    EXPECT_EQ(visualizationPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 0u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, VisualizationReadbackDefaultDisabledEvenWhenPassRecords)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{796u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    EXPECT_FALSE(renderer->GetVisualizationOverlayBackbufferReadbackBuffer().IsValid())
        << "Visualization-overlay readback must default to disabled after Initialize().";

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

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.VisualizationOverlayBackbufferReadbackCopyCount, 0u)
        << "Recorded visualization-overlay draws must not arm readback implicitly.";
    EXPECT_FALSE(device.HasBackbufferBarrier( RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc));

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, VisualizationReadbackRecordsOnlyWhenPassRecords)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{797u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    const RHI::BufferHandle readback{8797u, 1u};
    renderer->SetVisualizationOverlayBackbufferReadbackBuffer(readback);
    EXPECT_EQ(renderer->GetVisualizationOverlayBackbufferReadbackBuffer(), readback);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    SubmitOneVectorField(*renderer, /*depthTested=*/false);
    SubmitOneIsoline(*renderer, /*depthTested=*/false);

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
    EXPECT_EQ(stats.VisualizationOverlayBackbufferReadbackCopyCount, 1u)
        << "Visualization-overlay readback must record once for an armed operational frame "
           "whose overlay pass recorded.";
    EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 0u)
        << "Visualization-overlay readback must not reuse the canonical surface counter.";
    EXPECT_EQ(stats.TransientDebugBackbufferReadbackCopyCount, 0u)
        << "Visualization-overlay readback must not reuse the transient-debug counter.";
    EXPECT_TRUE(device.HasBackbufferBarrier( RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc));
    EXPECT_TRUE(device.HasBackbufferBarrier( RHI::TextureLayout::TransferSrc, RHI::TextureLayout::Present));

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, VisualizationReadbackSkipsWhenPassOmitted)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{798u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetVisualizationOverlayBackbufferReadbackBuffer(RHI::BufferHandle{8798u, 1u});

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 128, .Height = 128},
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_EQ(FindCommandPass(stats, "VisualizationOverlayPass"), nullptr);
    EXPECT_EQ(stats.VisualizationOverlayBackbufferReadbackCopyCount, 0u)
        << "An armed visualization-overlay readback buffer must not copy clean default frames.";
    EXPECT_FALSE(device.HasBackbufferBarrier( RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc));

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, VisualizationReadbackSkipsWhenDeviceNonOperational)
{
    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{799u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);
    renderer->SetVisualizationOverlayBackbufferReadbackBuffer(RHI::BufferHandle{8799u, 1u});

    device.Operational = false;

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
    EXPECT_FALSE(stats.Execute.DeviceOperational);

    const auto* pass = FindCommandPass(stats, "VisualizationOverlayPass");
    ASSERT_NE(pass, nullptr);
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::SkippedNonOperational);
    EXPECT_EQ(stats.VisualizationOverlayBackbufferReadbackCopyCount, 0u);
    EXPECT_FALSE(device.HasBackbufferBarrier( RHI::TextureLayout::Present, RHI::TextureLayout::TransferSrc));

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, PerLanePartialSkipKeepsSiblingLaneRecording)
{
    // GRAPHICS-078 Slice C — when one lane's pipelines are missing but
    // the other lane's pipelines are healthy AND both lanes have
    // packets, the missing lane increments `MissingPipelineSkipCount`
    // while the sibling lane still records. The pass status is
    // `Recorded` because at least one lane succeeded. Mirrors
    // GRAPHICS-077 Slice C's per-lane independence pin.
    //
    // FailPipelineCreateCall = 33 suppresses isoline DepthTested only;
    // vector-field pipelines (#31 + #32) are healthy.
    static const Graphics::VectorFieldOverlayPacket kVectorFields[1] = {
        Graphics::VectorFieldOverlayPacket{
            .Name         = "Test.VF",
            .Domain       = Graphics::VisualizationAttributeDomain::Vertex,
            .ElementCount = 1u,
            .RowCount     = 1u,
            .PositionBufferBDA = kAnchorBDA,
            .VectorBufferBDA   = kVectorBDA,
            .Scale        = 1.0f,
            .Color        = glm::vec4{1.0f, 0.0f, 0.0f, 1.0f},
            .DepthTested  = true,
        },
    };
    static const Graphics::IsolineOverlayPacket kIsolines[1] = {
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 2u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f},
            .DepthTested      = true,
        },
    };

    MockDevice device;
    device.FailPipelineCreateCall = 33;
    device.BackbufferHandle = RHI::TextureHandle{794u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
        .VisualizationVectorFields =
            std::span<const Graphics::VectorFieldOverlayPacket>{kVectorFields, 1u},
        .VisualizationIsolines =
            std::span<const Graphics::IsolineOverlayPacket>{kIsolines, 1u},
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
    // Vector-field lane recorded; isoline lane skipped → status is
    // `Recorded` (per-lane independence).
    EXPECT_EQ(pass->Status, Graphics::RenderCommandPassStatus::Recorded);

    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.VectorFieldRecordsRecorded, 1u);
    // Isoline lane's pipeline gate failed first, so neither submitted
    // nor recorded counters tick for that lane — only
    // `MissingPipelineSkipCount` does.
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.MissingPipelineSkipCount, 1u);

    renderer->Shutdown();
}

TEST(VisualizationOverlayPassContract, PerFrameBufferRecyclingDoesNotLeakIsoline)
{
    // GRAPHICS-078 Slice C — across N frames with a constant isoline
    // payload the upload helper's underlying host-visible vertex
    // buffer must not leak. Mirrors the vector-field recycling test:
    // after the first operational frame the helper's allocation is
    // in-flight, and subsequent frames must not increase the buffer-
    // create count attributable to the visualization-overlay pass.
    static const Graphics::IsolineOverlayPacket kIsolines[1] = {
        Graphics::IsolineOverlayPacket{
            .SourceScalarName = "Test.Iso",
            .Domain           = Graphics::VisualizationAttributeDomain::Face,
            .IsoValueCount    = 4u,
            .RangeMin         = 0.0f,
            .RangeMax         = 1.0f,
            .LineWidth        = 1.0f,
            .Color            = glm::vec4{1.0f},
            .DepthTested      = true,
        },
    };

    MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{795u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    int createBufferCountAfterFrame1 = 0;
    for (int frameIndex = 1; frameIndex <= 5; ++frameIndex)
    {
        RHI::FrameHandle frame{};
        ASSERT_TRUE(renderer->BeginFrame(frame));
        renderer->SubmitRuntimeSnapshots(Graphics::RuntimeRenderSnapshotBatch{
            .VisualizationIsolines = std::span<const Graphics::IsolineOverlayPacket>{
                kIsolines, 1u},
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
            EXPECT_EQ(device.CreateBufferCount, createBufferCountAfterFrame1)
                << "frame " << frameIndex;
        }
    }

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_EQ(stats.VisualizationOverlayUpload.UploadOverflowCount, 0u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsSubmitted, 1u);
    EXPECT_EQ(stats.VisualizationOverlayUpload.IsolineRecordsRecorded, 1u);

    renderer->Shutdown();
}
