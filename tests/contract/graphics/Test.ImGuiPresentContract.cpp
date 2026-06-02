#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.ImGuiUploadHelper;
import Extrinsic.Graphics.Pass.ImGui;
import Extrinsic.Graphics.Pass.Present;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

using namespace Extrinsic;

namespace
{
    enum class EventKind
    {
        BindPipeline,
        BindIndexBuffer,
        PushConstants,
        Draw,
        DrawIndexed,
    };

    struct Event
    {
        EventKind Kind{};
    };

    class RecordingCommandContext final : public RHI::ICommandContext
    {
    public:
        std::vector<Event> Events{};
        RHI::PipelineHandle LastPipeline{};
        RHI::BufferHandle LastIndexBuffer{};
        std::uint64_t LastIndexBufferOffset{0u};
        RHI::IndexType LastIndexType{RHI::IndexType::Uint32};
        std::uint32_t LastDrawVertexCount{0u};
        std::uint32_t LastDrawIndexedIndexCount{0u};
        std::vector<std::byte> LastPushConstants{};

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
        void BindIndexBuffer(RHI::BufferHandle buffer, std::uint64_t offset, RHI::IndexType type) override
        {
            Events.push_back({.Kind = EventKind::BindIndexBuffer});
            LastIndexBuffer = buffer;
            LastIndexBufferOffset = offset;
            LastIndexType = type;
        }
        void PushConstants(const void* data, std::uint32_t size, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::PushConstants});
            LastPushConstants.resize(size);
            if (data != nullptr && size > 0u)
            {
                std::memcpy(LastPushConstants.data(), data, size);
            }
        }
        void Draw(std::uint32_t vertexCount, std::uint32_t, std::uint32_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::Draw});
            LastDrawVertexCount = vertexCount;
        }
        void DrawIndexed(std::uint32_t indexCount, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override
        {
            Events.push_back({.Kind = EventKind::DrawIndexed});
            LastDrawIndexedIndexCount = indexCount;
        }
        void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
        void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
        void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
        void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
        void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
        void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
        void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
        void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
    };

    [[nodiscard]] Graphics::FrameRecipeImports MakeImports()
    {
        return Graphics::FrameRecipeImports{
            .Backbuffer = RHI::TextureHandle{1u, 1u},
            .SceneTable = RHI::BufferHandle{2u, 1u},
            .InstanceStatic = RHI::BufferHandle{3u, 1u},
            .InstanceDynamic = RHI::BufferHandle{4u, 1u},
            .EntityConfig = RHI::BufferHandle{5u, 1u},
            .GeometryRecords = RHI::BufferHandle{6u, 1u},
            .Bounds = RHI::BufferHandle{7u, 1u},
            .Lights = RHI::BufferHandle{8u, 1u},
            .MaterialBuffer = RHI::BufferHandle{9u, 1u},
            .SurfaceOpaqueIndexedArgs = RHI::BufferHandle{10u, 1u},
            .SurfaceOpaqueCount = RHI::BufferHandle{11u, 1u},
            .LinesIndexedArgs = RHI::BufferHandle{12u, 1u},
            .LinesCount = RHI::BufferHandle{13u, 1u},
            .PointsNonIndexedArgs = RHI::BufferHandle{14u, 1u},
            .PointsCount = RHI::BufferHandle{15u, 1u},
        };
    }

    [[nodiscard]] const Graphics::FrameRecipePassDeclaration* FindPass(const Graphics::FrameRecipeIntrospection& description,
                                                                       const Graphics::FrameRecipePassKind kind)
    {
        for (const Graphics::FrameRecipePassDeclaration& pass : description.Passes)
        {
            if (pass.Kind == kind)
            {
                return &pass;
            }
        }
        return nullptr;
    }

    [[nodiscard]] bool Contains(const std::vector<std::string_view>& values, const std::string_view value)
    {
        return std::ranges::find(values, value) != values.end();
    }
}

TEST(GraphicsImGuiPresentContract, FrameRecipeKeepsImGuiOffBackbufferAndPresentFinalizes)
{
    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{});

    const auto* imgui = FindPass(description, Graphics::FrameRecipePassKind::ImGui);
    ASSERT_NE(imgui, nullptr);
    EXPECT_TRUE(imgui->Enabled);
    EXPECT_FALSE(imgui->FinalizesBackbuffer);
    EXPECT_TRUE(Contains(imgui->Reads, "FrameRecipe.PresentSource"));
    EXPECT_TRUE(Contains(imgui->Writes, "FrameRecipe.PresentSource"));
    EXPECT_FALSE(Contains(imgui->Writes, "Backbuffer"));

    const auto* present = FindPass(description, Graphics::FrameRecipePassKind::Present);
    ASSERT_NE(present, nullptr);
    EXPECT_TRUE(present->FinalizesBackbuffer);
    EXPECT_TRUE(Contains(present->Reads, "FrameRecipe.PresentSource"));
    // GRAPHICS-076 Slice A follow-up — the default-recipe present pass
    // declares the imported `Backbuffer` as a color-attachment *write*
    // so the framegraph emits real render-pass attachments and the
    // present `BindPipeline + Draw(3, 1, 0, 0)` runs inside a render
    // pass on Vulkan. See `Graphics.FrameRecipe.cpp` for the matching
    // builder change.
    EXPECT_TRUE(Contains(present->Writes, "Backbuffer"));
}

TEST(GraphicsImGuiPresentContract, RenderGraphRejectsNonPresentBackbufferWrites)
{
    Graphics::RenderGraph graph;
    const Graphics::TextureRef backbuffer = graph.ImportBackbuffer("Backbuffer", RHI::TextureHandle{9u, 1u});
    const Graphics::PassRef badPass = graph.AddPass("BadOverlayBackbufferWrite", [=](Graphics::RenderGraphBuilder& builder) {
        builder.Write(backbuffer, Graphics::TextureUsage::ColorAttachmentWrite);
    });
    EXPECT_TRUE(badPass.IsValid());

    const auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());
    const auto& findings = graph.GetLastCompileValidationResult().Findings;
    ASSERT_FALSE(findings.empty());
    const bool reportedBadPass = std::ranges::any_of(findings, [](const Graphics::RenderGraphValidationFinding& finding) {
        return finding.PassName == "BadOverlayBackbufferWrite" ||
               finding.Message.find("BadOverlayBackbufferWrite") != std::string::npos;
    });
    EXPECT_TRUE(reportedBadPass);
}

TEST(GraphicsImGuiPresentContract, DefaultFrameRecipeBuildsPresentAsOnlyBackbufferUse)
{
    Graphics::RenderGraph graph;
    const Graphics::FrameRecipeBuildResult build = Graphics::BuildDefaultFrameRecipe(
        graph,
        Graphics::FrameRecipeFeatures{},
        MakeImports(),
        Graphics::FrameRecipeSizing{.Width = 800u, .Height = 600u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    const auto& compileResult = graph.GetLastCompileValidationResult();
    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    ASSERT_FALSE(compiled->PassNames.empty());
    EXPECT_EQ(compiled->PassNames.back(), "Present");
}

TEST(GraphicsImGuiPresentContract, ImGuiPassRecordsOverlayDrawDataOnlyWhenReady)
{
    Graphics::ImGuiOverlaySystem overlay;
    overlay.Initialize();
    overlay.SubmitFrame(Graphics::ImGuiOverlayFrame{
        .Enabled = true,
        .DisplayWidth = 128u,
        .DisplayHeight = 64u,
        .DrawLists = {Graphics::ImGuiOverlayDrawList{.CommandCount = 2u, .VertexCount = 10u, .IndexCount = 18u}},
    });

    Graphics::ImGuiPass pass{overlay};
    const RHI::PipelineHandle pipeline{55u, 1u};
    pass.SetPipeline(pipeline);

    RecordingCommandContext cmd;
    const RHI::BufferHandle vertexBuffer{101u, 1u};
    const RHI::BufferHandle indexBuffer{102u, 1u};
    const Graphics::ImGuiUploadResult upload{
        .DrawLists = {
            Graphics::ImGuiDrawListUploadResult{
                .VertexBuffer = vertexBuffer,
                .IndexBuffer = indexBuffer,
                .VertexBufferBDA = 0x12340000u,
                .IndexOffsetBytes = 64u,
                .FirstVertex = 7u,
                .VertexCount = 10u,
                .IndexCount = 18u,
                .Uploaded = true,
            },
        },
        .DrawListCount = 1u,
        .Uploaded = true,
    };
    pass.Execute(cmd, upload);

    ASSERT_EQ(cmd.Events.size(), 4u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::BindIndexBuffer);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[3].Kind, EventKind::DrawIndexed);
    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastIndexBuffer, indexBuffer);
    EXPECT_EQ(cmd.LastIndexBufferOffset, 64u);
    EXPECT_EQ(cmd.LastIndexType, RHI::IndexType::Uint32);
    EXPECT_EQ(cmd.LastDrawIndexedIndexCount, 18u);
    EXPECT_EQ(overlay.GetDiagnostics().DrawCalls, 1u);

    ASSERT_EQ(cmd.LastPushConstants.size(), sizeof(Graphics::ImGuiOverlayPushConstants));
    Graphics::ImGuiOverlayPushConstants pc{};
    std::memcpy(&pc, cmd.LastPushConstants.data(), sizeof(pc));
    EXPECT_EQ(pc.VertexBufferBDA, 0x12340000u);
    EXPECT_EQ(pc.FirstVertex, 7u);
    EXPECT_EQ(pc.IndexCount, 18u);
    EXPECT_EQ(pc.TextureBindlessIndex, RHI::kInvalidBindlessIndex);
    EXPECT_EQ(pc.Flags & Graphics::kImGuiOverlayPushFlagUserTexture, 0u);

    overlay.ClearFrame();
    RecordingCommandContext emptyCmd;
    pass.Execute(emptyCmd, Graphics::ImGuiUploadResult{});
    EXPECT_TRUE(emptyCmd.Events.empty());
}

TEST(GraphicsImGuiPresentContract, PresentPassRecordsFullscreenFinalizationOnlyWithPipeline)
{
    Graphics::PresentPass pass;
    RecordingCommandContext missingPipeline;
    pass.Execute(missingPipeline);
    EXPECT_TRUE(missingPipeline.Events.empty());

    const RHI::PipelineHandle pipeline{56u, 1u};
    pass.SetPipeline(pipeline);
    RecordingCommandContext cmd;
    pass.Execute(cmd);
    ASSERT_EQ(cmd.Events.size(), 2u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::Draw);
    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastDrawVertexCount, 3u);
}
