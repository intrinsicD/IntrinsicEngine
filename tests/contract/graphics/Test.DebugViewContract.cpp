#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Graphics.DebugViewSystem;
import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.Pass.DebugView;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Types;

using namespace Extrinsic;

namespace
{
    enum class EventKind
    {
        BindPipeline,
        PushConstants,
        Draw,
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
        std::uint32_t LastDrawVertexCount{0u};
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
        void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
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
        void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
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

    [[nodiscard]] std::vector<std::string> OrderedPassNames(const Graphics::CompiledRenderGraph& compiled)
    {
        std::vector<std::string> names{};
        for (const std::uint32_t index : compiled.TopologicalOrder)
        {
            names.push_back(compiled.PassNames[index]);
        }
        return names;
    }
}

TEST(GraphicsDebugViewContract, FrameRecipeDeclaresDebugViewPreviewStage)
{
    Graphics::FrameRecipeFeatures features{};
    features.EnableDebugView = true;
    const Graphics::FrameRecipeIntrospection description = Graphics::DescribeDefaultFrameRecipe(features);

    const auto* debugView = FindPass(description, Graphics::FrameRecipePassKind::DebugView);
    ASSERT_NE(debugView, nullptr);
    EXPECT_TRUE(debugView->Enabled);
    EXPECT_TRUE(Contains(debugView->Reads, "FrameRecipe.PresentSource"));
    EXPECT_TRUE(Contains(debugView->Writes, "DebugViewRGBA"));

    Graphics::RenderGraph graph;
    const Graphics::FrameRecipeBuildResult build = Graphics::BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        Graphics::FrameRecipeSizing{.Width = 640u, .Height = 360u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value()) << graph.GetLastCompileDiagnostic();
    const std::vector<std::string> order = OrderedPassNames(*compiled);
    const auto post = std::ranges::find(order, "PostProcessPass");
    const auto debug = std::ranges::find(order, "DebugViewPass");
    const auto imgui = std::ranges::find(order, "ImGuiPass");
    ASSERT_NE(debug, order.end());
    ASSERT_NE(post, order.end());
    ASSERT_NE(imgui, order.end());
    EXPECT_LT(post, debug);
    EXPECT_LT(debug, imgui);
}

TEST(GraphicsDebugViewContract, DebugViewPassRecordsFullscreenPreviewForResolvedSelection)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();
    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = true, .RequestedResourceName = "SceneColorHDR"});
    const Graphics::FrameRecipeIntrospection recipe = Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{});
    const Graphics::DebugViewResolvedSelection selection = debug.ResolveSelection(recipe);
    ASSERT_TRUE(selection.Enabled);

    Graphics::DebugViewPass pass{debug};
    const RHI::PipelineHandle pipeline{42u, 1u};
    pass.SetPipeline(pipeline);

    RHI::CameraUBO camera{};
    RecordingCommandContext cmd;
    pass.Execute(cmd, camera);

    ASSERT_EQ(cmd.Events.size(), 3u);
    EXPECT_EQ(cmd.Events[0].Kind, EventKind::BindPipeline);
    EXPECT_EQ(cmd.Events[1].Kind, EventKind::PushConstants);
    EXPECT_EQ(cmd.Events[2].Kind, EventKind::Draw);
    EXPECT_EQ(cmd.LastPipeline, pipeline);
    EXPECT_EQ(cmd.LastDrawVertexCount, 3u);

    ASSERT_EQ(cmd.LastPushConstants.size(), sizeof(Graphics::DebugViewPushConstants));
    Graphics::DebugViewPushConstants pc{};
    std::memcpy(&pc, cmd.LastPushConstants.data(), sizeof(pc));
    EXPECT_EQ(pc.ResourceClass, static_cast<std::uint32_t>(Graphics::DebugViewResourceClass::Texture));
    EXPECT_EQ(pc.UsedFallback, 0u);
}

TEST(GraphicsDebugViewContract, DebugViewPassSkipsDisabledSelectionAndMissingPipeline)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();
    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = false, .RequestedResourceName = "SceneColorHDR"});
    const Graphics::DebugViewResolvedSelection disabledSelection =
        debug.ResolveSelection(Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{}));
    EXPECT_FALSE(disabledSelection.Enabled);

    RHI::CameraUBO camera{};
    Graphics::DebugViewPass disabledPass{debug};
    disabledPass.SetPipeline(RHI::PipelineHandle{43u, 1u});
    RecordingCommandContext disabledCmd;
    disabledPass.Execute(disabledCmd, camera);
    EXPECT_TRUE(disabledCmd.Events.empty());

    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = true, .RequestedResourceName = "SceneColorHDR"});
    const Graphics::DebugViewResolvedSelection enabledSelection =
        debug.ResolveSelection(Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{}));
    EXPECT_TRUE(enabledSelection.Enabled);
    Graphics::DebugViewPass missingPipeline{debug};
    RecordingCommandContext missingPipelineCmd;
    missingPipeline.Execute(missingPipelineCmd, camera);
    EXPECT_TRUE(missingPipelineCmd.Events.empty());
}


