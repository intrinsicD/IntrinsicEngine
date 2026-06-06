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
// pass. Slice D.1 promotes the default recipe's `"ImGuiPass"` node to read and
// write `FrameRecipe.PresentSource` through a load/store color attachment, so
// an operational device with an attached payload records the route under the
// CPU/null render-pass scope. Missing overlay state, missing work, detached
// overlays, failed uploads, and non-operational devices still report the
// fail-closed taxonomy instead of falling through to the executor soft-skip
// default.
//
// The recipe-declaration shape (ImGui reads `FrameRecipe.PresentSource`, does
// not write `Backbuffer`) and the render-graph rejection of a non-present
// `Backbuffer` write are covered by `Test.ImGuiPresentContract.cpp`.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
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
import Extrinsic.RHI.Bindless;
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

    // One accepted overlay draw list with matching POD vertex/index payload so
    // `ImGuiOverlaySystem::HasOverlayWork()` becomes true and the Slice C
    // upload helper can pack it when Slice D.1 gives the pass a render target.
    [[nodiscard]] Graphics::ImGuiOverlayFrame MakeOverlayFrameWithWork()
    {
        Graphics::ImGuiOverlayFrame frame{};
        frame.Enabled = true;
        frame.DisplayWidth = 256u;
        frame.DisplayHeight = 144u;
        Graphics::ImGuiOverlayDrawList drawList{};
        drawList.CommandCount = 1u;
        drawList.VertexCount = 3u;
        drawList.IndexCount = 3u;
        drawList.UsesUserTexture = false;
        drawList.Vertices = {
            Graphics::ImGuiOverlayVertex{.Position = {0.0f, 0.0f}, .UV = {0.0f, 0.0f}, .Color = 0xffffffffu},
            Graphics::ImGuiOverlayVertex{.Position = {1.0f, 0.0f}, .UV = {1.0f, 0.0f}, .Color = 0xffffffffu},
            Graphics::ImGuiOverlayVertex{.Position = {0.0f, 1.0f}, .UV = {0.0f, 1.0f}, .Color = 0xffffffffu},
        };
        drawList.Indices = {0u, 1u, 2u};
        frame.DrawLists.push_back(std::move(drawList));
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

    [[nodiscard]] Graphics::ImGuiOverlayFrame MakeUserTextureCommandFrame()
    {
        Graphics::ImGuiOverlayFrame frame{};
        frame.Enabled = true;
        frame.DisplayWidth = 320u;
        frame.DisplayHeight = 180u;
        Graphics::ImGuiOverlayDrawList drawList =
            MakeOverlayDrawList(2u, 4u, {0u, 1u, 2u, 1u, 2u, 3u});
        drawList.UsesUserTexture = true;
        drawList.Commands = {
            Graphics::ImGuiOverlayDrawCommand{
                .IndexOffset = 0u,
                .VertexOffset = 0u,
                .IndexCount = 3u,
                .TextureBindlessIndex = 77u,
                .UsesUserTexture = true,
            },
            Graphics::ImGuiOverlayDrawCommand{
                .IndexOffset = 3u,
                .VertexOffset = 1u,
                .IndexCount = 3u,
                .TextureBindlessIndex = RHI::kInvalidBindlessIndex,
                .UsesUserTexture = false,
            },
        };
        frame.DrawLists.push_back(std::move(drawList));
        return frame;
    }

    [[nodiscard]] std::size_t CountTextureWritesFor(const Tests::MockDevice& device,
                                                    const RHI::TextureHandle texture)
    {
        std::size_t count = 0u;
        for (const auto& write : device.TextureWrites)
        {
            if (write.Handle == texture)
            {
                ++count;
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<std::byte> LastTextureWriteBytesFor(
        const Tests::MockDevice& device,
        const RHI::TextureHandle texture)
    {
        std::vector<std::byte> bytes{};
        for (const auto& write : device.TextureWrites)
        {
            if (write.Handle == texture)
            {
                bytes = write.Data;
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
    EXPECT_EQ(secondPush.TextureBindlessIndex, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(secondPush.Flags & Graphics::kImGuiOverlayPushFlagUserTexture, 0u);
}

TEST(ImGuiPassContract, UploadHelperPreservesPerCommandTextureBindlessIndices)
{
    Tests::MockDevice device;
    RHI::BufferManager bufferManager{device};

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeUserTextureCommandFrame());
    ASSERT_TRUE(overlay.HasOverlayWork());
    EXPECT_TRUE(overlay.GetDiagnostics().HasUserTextures);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCommandCount, 2u);

    const Graphics::ImGuiOverlayFrame* frame = overlay.GetCurrentFrame();
    ASSERT_NE(frame, nullptr);

    Graphics::ImGuiUploadHelper helper{device, bufferManager};
    const Graphics::ImGuiUploadResult upload = helper.UploadFrame(*frame);
    ASSERT_TRUE(upload.Uploaded);
    ASSERT_EQ(upload.DrawLists.size(), 1u);
    ASSERT_EQ(upload.DrawLists[0].Commands.size(), 2u);
    EXPECT_EQ(upload.DrawLists[0].Commands[0].IndexOffset, 0u);
    EXPECT_EQ(upload.DrawLists[0].Commands[0].IndexCount, 3u);
    EXPECT_EQ(upload.DrawLists[0].Commands[0].TextureBindlessIndex, 77u);
    EXPECT_TRUE(upload.DrawLists[0].Commands[0].UsesUserTexture);
    EXPECT_EQ(upload.DrawLists[0].Commands[1].IndexOffset, 3u);
    EXPECT_EQ(upload.DrawLists[0].Commands[1].VertexOffset, 1u);
    EXPECT_EQ(upload.DrawLists[0].Commands[1].TextureBindlessIndex, RHI::kInvalidBindlessIndex);
    EXPECT_FALSE(upload.DrawLists[0].Commands[1].UsesUserTexture);

    Graphics::ImGuiPass pass{overlay};
    pass.SetPipeline(RHI::PipelineHandle{601u, 1u});
    pass.Execute(device.CommandContext, upload);

    EXPECT_EQ(device.CommandContext.BindPipelineCalls, 1);
    EXPECT_EQ(device.CommandContext.BindIndexBufferCalls, 1);
    EXPECT_EQ(device.CommandContext.PushConstantsCalls, 2);
    EXPECT_EQ(device.CommandContext.DrawIndexedCalls, 2);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCalls, 2u);

    ASSERT_EQ(device.CommandContext.PushConstantPayloads.size(), 2u);
    Graphics::ImGuiOverlayPushConstants firstPush{};
    std::memcpy(&firstPush,
                device.CommandContext.PushConstantPayloads[0].data(),
                sizeof(firstPush));
    EXPECT_EQ(firstPush.FirstVertex, 0u);
    EXPECT_EQ(firstPush.IndexCount, 3u);
    EXPECT_EQ(firstPush.TextureBindlessIndex, 77u);
    EXPECT_EQ(firstPush.Flags & Graphics::kImGuiOverlayPushFlagUserTexture,
              Graphics::kImGuiOverlayPushFlagUserTexture);

    Graphics::ImGuiOverlayPushConstants secondPush{};
    std::memcpy(&secondPush,
                device.CommandContext.PushConstantPayloads[1].data(),
                sizeof(secondPush));
    EXPECT_EQ(secondPush.FirstVertex, 1u);
    EXPECT_EQ(secondPush.IndexCount, 3u);
    EXPECT_EQ(secondPush.TextureBindlessIndex, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(secondPush.Flags & Graphics::kImGuiOverlayPushFlagUserTexture, 0u);
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
    EXPECT_EQ(CountTextureWritesFor(device, before.FontAtlasTexture), 1u);
    ASSERT_FALSE(device.Bindless.AllocatedSamplers.empty());
    const RHI::SamplerHandle allocatedSampler = device.Bindless.AllocatedSamplers.back();
    EXPECT_NE(std::find(device.CreatedSamplerHandles.begin(),
                        device.CreatedSamplerHandles.end(),
                        allocatedSampler),
              device.CreatedSamplerHandles.end())
        << "Font atlas bindless allocation must receive a device-created sampler handle, "
           "not a SamplerManager-local pool handle.";
    const std::vector<std::byte> firstUpload =
        LastTextureWriteBytesFor(device, before.FontAtlasTexture);
    EXPECT_EQ(firstUpload, frame.FontAtlas.Pixels);

    ASSERT_TRUE(renderer->RebuildOperationalResources(device));
    overlay.UploadPendingFontAtlas();

    const Graphics::ImGuiOverlayDiagnostics after = overlay.GetDiagnostics();
    EXPECT_EQ(after.FontAtlasTexture, before.FontAtlasTexture);
    EXPECT_EQ(after.FontAtlasBindlessIndex, before.FontAtlasBindlessIndex);
    EXPECT_EQ(after.FontAtlasUploadCount, 1u);
    EXPECT_EQ(after.FontAtlasAllocationCount, 1u);
    EXPECT_EQ(CountTextureWritesFor(device, before.FontAtlasTexture), 1u);
    EXPECT_EQ(LastTextureWriteBytesFor(device, before.FontAtlasTexture), firstUpload);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, FontAtlasAllocatesAfterColdStartOperationalRebuild)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{509u, 1u};
    device.Operational = false;

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
            std::byte{0x11},
            std::byte{0x22},
            std::byte{0x33},
            std::byte{0x44},
        },
    };
    overlay.SubmitFrame(frame);

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->SetImGuiOverlaySystem(&overlay);
    renderer->Initialize(device);

    const Graphics::ImGuiOverlayDiagnostics coldStart = overlay.GetDiagnostics();
    EXPECT_TRUE(coldStart.FontAtlasAvailable);
    EXPECT_FALSE(coldStart.FontAtlasGpuAllocated);
    EXPECT_EQ(coldStart.FontAtlasAllocationCount, 0u);
    EXPECT_EQ(coldStart.FontAtlasUploadCount, 0u);

    device.Operational = true;
    ASSERT_TRUE(renderer->RebuildOperationalResources(device));
    overlay.UploadPendingFontAtlas();

    const Graphics::ImGuiOverlayDiagnostics promoted = overlay.GetDiagnostics();
    EXPECT_TRUE(promoted.FontAtlasGpuAllocated);
    EXPECT_TRUE(promoted.FontAtlasTexture.IsValid());
    EXPECT_NE(promoted.FontAtlasBindlessIndex, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(promoted.FontAtlasAllocationCount, 1u);
    EXPECT_EQ(promoted.FontAtlasUploadCount, 1u);
    EXPECT_EQ(CountTextureWritesFor(device, promoted.FontAtlasTexture), 1u);
    EXPECT_EQ(LastTextureWriteBytesFor(device, promoted.FontAtlasTexture),
              frame.FontAtlas.Pixels);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, AttachedOverlayWithWorkRecordsAfterInitialize)
{
    // Slice D.1 gives `"ImGuiPass"` a load/store `FrameRecipe.PresentSource`
    // color attachment, so an operational device with an attached overlay and
    // uploadable payload records the route instead of soft-skipping.
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{501u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    // Engine-owned overlay system handed in AFTER Initialize(): the pipeline
    // lease already exists, so `SetImGuiOverlaySystem` binds it to the freshly
    // constructed pass.
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
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCalls, 1u);
    EXPECT_GE(device.CommandContext.BindIndexBufferCalls, 1);
    EXPECT_GE(device.CommandContext.DrawIndexedCalls, 1);

    // The canonical present pass still finalizes the backbuffer; ImGui never
    // owns the imported backbuffer.
    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(stats.CommandRecords.SkippedUnavailable, 0u);
    EXPECT_EQ(stats.CommandRecords.SkippedNonOperational, 0u);
    for (const Graphics::RenderGraphCommandPassStats& pass : stats.CommandRecords.Passes)
    {
        EXPECT_NE(pass.Status, Graphics::RenderCommandPassStatus::SkippedUnavailable)
            << pass.Name;
        EXPECT_NE(pass.Status, Graphics::RenderCommandPassStatus::SkippedNonOperational)
            << pass.Name;
    }

    renderer->Shutdown();
}

TEST(ImGuiPassContract, PipelineMatchesLegacyDearImGuiStraightAlphaBlend)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{510u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    const RHI::PipelineDesc* imguiPipeline = nullptr;
    for (const RHI::PipelineDesc& desc : device.CreatedPipelineDescs)
    {
        if (std::string_view(desc.DebugName) == "Renderer.ImGui")
        {
            imguiPipeline = &desc;
            break;
        }
    }
    ASSERT_NE(imguiPipeline, nullptr);

    const RHI::ColorBlendDesc& blend = imguiPipeline->ColorBlend[0];
    EXPECT_TRUE(blend.Enable);
    EXPECT_EQ(blend.SrcColorFactor, RHI::BlendFactor::SrcAlpha);
    EXPECT_EQ(blend.DstColorFactor, RHI::BlendFactor::OneMinusSrcAlpha);
    EXPECT_EQ(blend.ColorOp, RHI::BlendOp::Add);
    EXPECT_EQ(blend.SrcAlphaFactor, RHI::BlendFactor::One);
    EXPECT_EQ(blend.DstAlphaFactor, RHI::BlendFactor::OneMinusSrcAlpha);
    EXPECT_EQ(blend.AlphaOp, RHI::BlendOp::Add);

    renderer->Shutdown();
}

TEST(ImGuiPassContract, DebugViewRgba8PresentSourceBindsRgba8PipelineVariant)
{
    Tests::MockDevice device;
    device.BackbufferFormat = RHI::Format::BGRA8_UNORM;
    device.BackbufferHandle = RHI::TextureHandle{512u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    RHI::PipelineHandle backbufferImGuiPipeline{};
    RHI::PipelineHandle rgba8ImGuiPipeline{};
    ASSERT_EQ(device.CreatedPipelineDescs.size(), device.CreatedPipelineHandles.size());
    for (std::size_t i = 0; i < device.CreatedPipelineDescs.size(); ++i)
    {
        const RHI::PipelineDesc& desc = device.CreatedPipelineDescs[i];
        if (std::string_view(desc.DebugName) != "Renderer.ImGui")
        {
            continue;
        }
        if (desc.ColorTargetFormats[0] == RHI::Format::BGRA8_UNORM)
        {
            backbufferImGuiPipeline = device.CreatedPipelineHandles[i];
        }
        else if (desc.ColorTargetFormats[0] == RHI::Format::RGBA8_UNORM)
        {
            rgba8ImGuiPipeline = device.CreatedPipelineHandles[i];
        }
    }
    ASSERT_TRUE(backbufferImGuiPipeline.IsValid());
    ASSERT_TRUE(rgba8ImGuiPipeline.IsValid());
    ASSERT_NE(backbufferImGuiPipeline, rgba8ImGuiPipeline);

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(MakeOverlayFrameWithWork());
    renderer->SetImGuiOverlaySystem(&overlay);

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));

    const Graphics::RenderFrameInput input{
        .Viewport = {.Width = 256, .Height = 144},
        .DebugOverlayEnabled = true,
    };
    Graphics::RenderWorld world = renderer->ExtractRenderWorld(input);
    renderer->PrepareFrame(world);
    renderer->ExecuteFrame(frame, world);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* debugViewPass = FindCommandPass(stats, "DebugViewPass");
    ASSERT_NE(debugViewPass, nullptr);
    EXPECT_EQ(debugViewPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    const auto& bound = device.CommandContext.BoundPipelines;
    EXPECT_NE(std::find(bound.begin(), bound.end(), rgba8ImGuiPipeline), bound.end());
    EXPECT_EQ(std::find(bound.begin(), bound.end(), backbufferImGuiPipeline), bound.end())
        << "ImGui over DebugViewRGBA must not bind the swapchain-format pipeline.";

    renderer->Shutdown();
}

TEST(ImGuiPassContract, FontAtlasUploadFlushesBindlessHeapBeforeDraw)
{
    Tests::MockDevice device;
    device.BackbufferHandle = RHI::TextureHandle{513u, 1u};

    std::unique_ptr<Graphics::IRenderer> renderer = Graphics::CreateRenderer();
    renderer->Initialize(device);

    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    Graphics::ImGuiOverlayFrame frameWithAtlas = MakeOverlayFrameWithWork();
    frameWithAtlas.FontAtlas = Graphics::ImGuiOverlayFontAtlas{
        .Valid = true,
        .Width = 2u,
        .Height = 2u,
        .BytesPerPixel = 1u,
        .UseColors = false,
        .Pixels = {
            std::byte{0x00},
            std::byte{0x80},
            std::byte{0xc0},
            std::byte{0xff},
        },
    };
    overlay.SubmitFrame(frameWithAtlas);
    renderer->SetImGuiOverlaySystem(&overlay);

    const int flushCallsBeforeFrame = device.Bindless.FlushCalls;

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    (void)DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_GT(device.Bindless.FlushCalls, flushCallsBeforeFrame);

    const Graphics::ImGuiOverlayDiagnostics diag = overlay.GetDiagnostics();
    EXPECT_TRUE(diag.FontAtlasGpuAllocated);
    EXPECT_EQ(diag.FontAtlasUploadCount, 1u);
    EXPECT_NE(diag.FontAtlasBindlessIndex, RHI::kInvalidBindlessIndex);

    bool foundImGuiPushConstants = false;
    for (const std::vector<std::byte>& payload : device.CommandContext.PushConstantPayloads)
    {
        if (payload.size() != sizeof(Graphics::ImGuiOverlayPushConstants))
        {
            continue;
        }

        Graphics::ImGuiOverlayPushConstants pc{};
        std::memcpy(&pc, payload.data(), sizeof(pc));
        if (pc.FontAtlasBindlessIndex == diag.FontAtlasBindlessIndex &&
            pc.TextureBindlessIndex == diag.FontAtlasBindlessIndex &&
            pc.FirstVertex == 0u &&
            pc.IndexCount == frameWithAtlas.DrawLists.front().IndexCount)
        {
            foundImGuiPushConstants = true;
            break;
        }
    }
    EXPECT_TRUE(foundImGuiPushConstants)
        << "ImGuiPass must draw with the uploaded font atlas bindless slot after "
           "flushing the queued descriptor write.";

    renderer->Shutdown();
}

TEST(ImGuiPassContract, AttachedOverlayWithWorkRecordsWhenAttachedBeforeInitialize)
{
    // Reverse attach order: the runtime may hand the overlay in before the
    // renderer initializes. The pass is emplaced without a pipeline, and
    // `InitializeOperationalPassResources` binds the lease to it once created.
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
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCalls, 1u);

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

TEST(ImGuiPassContract, RouteRecordsAfterOperationalRebuild)
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
    // re-binds the consumer pass. With Slice D.1's render target topology, the
    // route records after the rebuild.
    ASSERT_TRUE(renderer->RebuildOperationalResources(device));

    RHI::FrameHandle frame{};
    ASSERT_TRUE(renderer->BeginFrame(frame));
    (void)DriveDefaultFrame(*renderer, frame);

    const Graphics::RenderGraphFrameStats& stats = renderer->GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;

    const auto* imguiPass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(imguiPass, nullptr);
    EXPECT_EQ(imguiPass->Status, Graphics::RenderCommandPassStatus::Recorded);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCalls, 1u);

    const auto* presentPass = FindCommandPass(stats, "Present");
    ASSERT_NE(presentPass, nullptr);
    EXPECT_EQ(presentPass->Status, Graphics::RenderCommandPassStatus::Recorded);

    renderer->Shutdown();
}
