#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

using namespace Extrinsic::Graphics;
namespace RHI = Extrinsic::RHI;

TEST(RenderGraphDebugDump, GoldenSmallRenderPassGraphIncludesAttachmentsAndResourceMaps)
{
    RenderGraph graph;

    RHI::TextureDesc depthDesc{};
    depthDesc.Fmt = RHI::Format::D32_FLOAT;
    const TextureRef depth = graph.CreateTexture("SceneDepth", depthDesc);

    RHI::TextureDesc sceneDesc{};
    sceneDesc.Fmt = RHI::Format::RGBA16_FLOAT;
    const TextureRef sceneColor = graph.CreateTexture("SceneColorHDR", sceneDesc);

    const TextureRef backbuffer = graph.ImportTexture(
        "Backbuffer",
        RHI::TextureHandle{99u, 1u},
        TextureState::Undefined,
        TextureState::Present);

    (void)graph.AddPass("DepthPrepass", [depth](RenderGraphBuilder& builder) {
        (void)builder.Write(depth, TextureUsage::DepthWrite);
        builder.SetRenderPass(RHI::RenderPassDesc{
            .ColorTargets = std::span<const RHI::ColorAttachment>{},
            .Depth = RHI::DepthAttachment{.Target = RHI::TextureHandle{1u, 1u}, .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
        });
    });

    std::array<RHI::ColorAttachment, 1u> surfaceColor{
        RHI::ColorAttachment{.Target = RHI::TextureHandle{2u, 1u}, .Load = RHI::LoadOp::Clear, .Store = RHI::StoreOp::Store},
    };
    (void)graph.AddPass("SurfacePass", [depth, sceneColor, &surfaceColor](RenderGraphBuilder& builder) {
        (void)builder.Read(depth, TextureUsage::DepthRead);
        (void)builder.Write(sceneColor, TextureUsage::ColorAttachmentWrite);
        builder.SetRenderPass(RHI::RenderPassDesc{
            .ColorTargets = std::span<const RHI::ColorAttachment>{surfaceColor},
            .Depth = RHI::DepthAttachment{.Target = RHI::TextureHandle{1u, 1u}, .Load = RHI::LoadOp::Load, .Store = RHI::StoreOp::Store},
        });
    });

    std::array<RHI::ColorAttachment, 1u> presentColor{
        RHI::ColorAttachment{.Target = RHI::TextureHandle{3u, 1u}, .Load = RHI::LoadOp::DontCare, .Store = RHI::StoreOp::Store},
    };

    (void)graph.AddPass("Present", [sceneColor, backbuffer, &presentColor](RenderGraphBuilder& builder) {
        (void)builder.Read(sceneColor, TextureUsage::ShaderRead);
        (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
        builder.SetRenderPass(RHI::RenderPassDesc{
            .ColorTargets = std::span<const RHI::ColorAttachment>{presentColor},
            .Depth = RHI::DepthAttachment{},
        });
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const std::string expected =
        "RenderGraph\n"
        "  pass_count=3 culled_pass_count=0 resource_count=3 edge_count=2 queue_handoff_edges=0 cross_queue_timeline_edges=0 cross_queue_ownership_transfers=0 barrier_packet_count=4 transient_naive_memory_bytes=512 transient_placed_peak_memory_bytes=512\n"
        "  passes:\n"
        "    [0] pass=0 name=\"DepthPrepass\" layer=0 queue=graphics side_effect=false\n"
        "      explicit_dependencies: none\n"
        "      color_targets: none\n"
        "      depth_target: texture=0 name=\"SceneDepth\" load=clear store=store format=D32_FLOAT\n"
        "    [1] pass=1 name=\"SurfacePass\" layer=1 queue=graphics side_effect=false\n"
        "      explicit_dependencies: none\n"
        "      color_targets:\n"
        "        [0] texture=1 name=\"SceneColorHDR\" load=clear store=store format=RGBA16_FLOAT\n"
        "      depth_target: texture=0 name=\"SceneDepth\" load=load store=store format=D32_FLOAT\n"
        "    [2] pass=2 name=\"Present\" layer=2 queue=graphics side_effect=true\n"
        "      explicit_dependencies: none\n"
        "      color_targets:\n"
        "        [0] texture=2 name=\"Backbuffer\" load=dont_care store=store format=RGBA8_UNORM\n"
        "      depth_target: none\n"
        "  textures:\n"
        "    texture[0] name=\"SceneDepth\" used=true imported=false sharing=exclusive final_state=Undefined first_write_pass=0 last_read_pass=1 producer_count=1 consumer_count=1 first_use_pass=0 last_use_pass=1\n"
        "    texture[1] name=\"SceneColorHDR\" used=true imported=false sharing=exclusive final_state=Undefined first_write_pass=1 last_read_pass=2 producer_count=1 consumer_count=1 first_use_pass=1 last_use_pass=2\n"
        "    texture[2] name=\"Backbuffer\" used=true imported=true sharing=exclusive final_state=Present first_write_pass=2 last_read_pass=none producer_count=1 consumer_count=0 first_use_pass=2 last_use_pass=2\n"
        "  buffers:\n";

    EXPECT_EQ(BuildRenderGraphDebugDump(*compiled), expected);
}

TEST(RenderGraphDebugDump, ResourceOnlyGraphRetainsImportedStateMetadata)
{
    RenderGraph graph;
    (void)graph.ImportTexture(
        "HistoryColor",
        RHI::TextureHandle{7u, 1u},
        TextureState::ShaderRead,
        TextureState::Present);

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const std::string dump = BuildRenderGraphDebugDump(*compiled);
    EXPECT_NE(dump.find("pass_count=0"), std::string::npos);
    EXPECT_NE(dump.find("texture[0] name=\"HistoryColor\" used=false imported=true sharing=exclusive final_state=Present"),
              std::string::npos);
}

