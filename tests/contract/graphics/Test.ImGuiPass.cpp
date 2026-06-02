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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.ImGuiUploadHelper;
import Extrinsic.Graphics.Pass.ImGui;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.BufferManager;
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

    [[nodiscard]] Graphics::ImGuiOverlayDrawList MakeOverlayDrawList(
        const std::uint32_t commandCount,
        const std::uint32_t vertexCount,
        const std::vector<std::uint32_t>& indices)
    {
        Graphics::ImGuiOverlayDrawList drawList{};
        drawList.CommandCount = commandCount;
        drawList.VertexCount = vertexCount;
        drawList.IndexCount = static_cast<std::uint32_t>(indices.size());
        drawList.Vertices.reserve(vertexCount);
        for (std::uint32_t index = 0u; index < vertexCount; ++index)
        {
            drawList.Vertices.push_back(Graphics::ImGuiOverlayVertex{
                .Position = {
                    static_cast<float>(index),
                    static_cast<float>(index + 1u),
                },
                .UV = {0.0f, 1.0f},
                .Color = 0xff00ffffu,
            });
        }
        drawList.Indices = indices;
        return drawList;
    }

    [[nodiscard]] Graphics::ImGuiOverlayFrame MakePayloadOverlayFrame()
    {
        Graphics::ImGuiOverlayFrame frame{};
        frame.Enabled = true;
        frame.DisplayWidth = 256u;
        frame.DisplayHeight = 144u;
        frame.DrawLists.push_back(MakeOverlayDrawList(1u, 3u, {0u, 1u, 2u}));
        frame.DrawLists.push_back(MakeOverlayDrawList(2u, 4u, {0u, 1u, 2u, 2u, 3u, 0u}));
        return frame;
    }

    [[nodiscard]] std::size_t CountTextureUploadsFor(const Tests::MockDevice& device,
                                                     const RHI::TextureHandle texture)
    {
        std::size_t count = 0u;
        for (const auto& upload : device.TransferQueue.TextureUploads)
        {
            if (upload.Texture == texture)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<std::byte> LastTextureUploadBytesFor(
        const Tests::MockDevice& device,
        const RHI::TextureHandle texture)
    {
        std::vector<std::byte> bytes{};
        for (const auto& upload : device.TransferQueue.TextureUploads)
        {
            if (upload.Texture == texture)
            {
                bytes = upload.Data;
            }
        }
        return bytes;
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

TEST(ImGuiPassContract, UploadHelperPacksTwoDrawListsAndPassRecordsPerList)
{
    Tests::MockDevice device;
    RHI::BufferManager bufferManager{device};

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakePayloadOverlayFrame());
    ASSERT_TRUE(overlay.HasOverlayWork());

    const Graphics::ImGuiOverlayFrame* frame = overlay.GetCurrentFrame();
    ASSERT_NE(frame, nullptr);

    Graphics::ImGuiUploadResult upload{};
    const RHI::PipelineHandle pipeline{600u, 1u};
    {
        Graphics::ImGuiUploadHelper helper{device, bufferManager};
        upload = helper.UploadFrame(*frame);

        ASSERT_TRUE(upload.Uploaded);
        ASSERT_FALSE(upload.Overflow);
        ASSERT_EQ(upload.DrawListCount, 2u);
        ASSERT_EQ(upload.DrawLists.size(), 2u);
        EXPECT_EQ(upload.DrawLists[0].FirstVertex, 0u);
        EXPECT_EQ(upload.DrawLists[0].IndexOffsetBytes, 0u);
        EXPECT_EQ(upload.DrawLists[0].IndexCount, 3u);
        EXPECT_EQ(upload.DrawLists[1].FirstVertex, 3u);
        EXPECT_EQ(upload.DrawLists[1].IndexOffsetBytes, 3u * sizeof(std::uint32_t));
        EXPECT_EQ(upload.DrawLists[1].IndexCount, 6u);
        EXPECT_EQ(helper.GetBufferAllocationCount(), 2u);

        ASSERT_EQ(device.BufferWrites.size(), 2u);
        EXPECT_EQ(device.BufferWrites[0].Data.size(), 7u * sizeof(Graphics::ImGuiOverlayVertex));
        EXPECT_EQ(device.BufferWrites[1].Data.size(), 9u * sizeof(std::uint32_t));

        Graphics::ImGuiPass pass{overlay};
        pass.SetPipeline(pipeline);
        pass.Execute(device.CommandContext, upload);
    }

    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 1);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 2);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndexedCalls, 2);
    EXPECT_EQ(device.CommandContext.LastBoundPipeline, pipeline);
    EXPECT_EQ(device.CommandContext.LastIndexType, RHI::IndexType::Uint32);
    EXPECT_EQ(device.CommandContext.LastDrawIndexed.IndexCount, 6u);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCalls, 2u);

    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 2u);
    Graphics::ImGuiOverlayPushConstants firstPush{};
    std::memcpy(&firstPush,
                device.CommandContext.PushConstantPayloads[0].data(),
                sizeof(firstPush));
    EXPECT_EQ(firstPush.FirstVertex, 0u);
    EXPECT_EQ(firstPush.IndexCount, 3u);

    Graphics::ImGuiOverlayPushConstants secondPush{};
    std::memcpy(&secondPush,
                device.CommandContext.PushConstantPayloads[1].data(),
                sizeof(secondPush));
    EXPECT_EQ(secondPush.FirstVertex, 3u);
    EXPECT_EQ(secondPush.IndexCount, 6u);
}

TEST(ImGuiPassContract, FontAtlasUploadSurvivesOperationalRebuildByteIdentical)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{508u, 1u};

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    Graphics::ImGuiOverlayFrame frame = MakePayloadOverlayFrame();
    frame.FontAtlas = Graphics::ImGuiOverlayFontAtlas{
        .Valid = true,
        .Width = 2u,
        .Height = 2u,
        .BytesPerPixel = 1u,
        .UseColors = false,
        .Pixels = {
            std::byte{0x10},
            std::byte{0x20},
            std::byte{0x30},
            std::byte{0x40},
        },
    };
    overlay.SubmitFrame(frame);

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->SetImGuiOverlaySystem(&overlay);
    renderer->Initialize(device);

    overlay.UploadPendingFontAtlas();
    const Graphics::ImGuiOverlayDiagnostics before = overlay.GetDiagnostics();
    EXPECT_TRUE(before.FontAtlasAvailable);
    EXPECT_TRUE(before.FontAtlasGpuAllocated);
    EXPECT_TRUE(before.FontAtlasTexture.IsValid());
    EXPECT_NE(before.FontAtlasBindlessIndex, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(before.FontAtlasUploadCount, 1u);
    EXPECT_EQ(before.FontAtlasAllocationCount, 1u);
    EXPECT_EQ(CountTextureUploadsFor(device, before.FontAtlasTexture), 1u);
    const std::vector<std::byte> firstUpload =
        LastTextureUploadBytesFor(device, before.FontAtlasTexture);
    EXPECT_EQ(firstUpload, frame.FontAtlas.Pixels);

    ASSERT_TRUE(renderer->RebuildOperationalResources(device));
    overlay.UploadPendingFontAtlas();

    const Graphics::ImGuiOverlayDiagnostics after = overlay.GetDiagnostics();
    EXPECT_EQ(after.FontAtlasTexture, before.FontAtlasTexture);
    EXPECT_EQ(after.FontAtlasBindlessIndex, before.FontAtlasBindlessIndex);
    EXPECT_EQ(after.FontAtlasUploadCount, 1u);
    EXPECT_EQ(after.FontAtlasAllocationCount, 1u);
    EXPECT_EQ(CountTextureUploadsFor(device, before.FontAtlasTexture), 1u);
    EXPECT_EQ(LastTextureUploadBytesFor(device, before.FontAtlasTexture), firstUpload);

    renderer->Shutdown();
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
    (void)DriveDefaultFrame(*renderer, frame);

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
    (void)DriveDefaultFrame(*renderer, frame);

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
    (void)DriveDefaultFrame(*renderer, frame);

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
    (void)DriveDefaultFrame(*renderer, frame);

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
    (void)DriveDefaultFrame(*renderer, frame);

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
    (void)DriveDefaultFrame(*renderer, frame);

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
    (void)DriveDefaultFrame(*renderer, frame);

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
