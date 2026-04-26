#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <iterator>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

using namespace Extrinsic::Graphics;

namespace
{
    std::size_t FindOrder(const CompiledRenderGraph& compiled, const std::uint32_t passIndex)
    {
        auto it = std::find(compiled.TopologicalOrder.begin(), compiled.TopologicalOrder.end(), passIndex);
        return static_cast<std::size_t>(std::distance(compiled.TopologicalOrder.begin(), it));
    }
}

TEST(GraphicsRenderGraph, EmptyGraphCompiles)
{
    RenderGraph graph;
    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 0u);
    EXPECT_EQ(compiled->ResourceCount, 0u);
}

TEST(GraphicsRenderGraph, ResetClearsPasses)
{
    RenderGraph graph;
    auto ref = graph.AddPass("DepthPrepass", false);
    EXPECT_TRUE(ref.IsValid());
    EXPECT_EQ(graph.GetPassCount(), 1u);

    graph.Reset();
    EXPECT_EQ(graph.GetPassCount(), 0u);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 0u);
}

TEST(GraphicsRenderGraph, ImportBackbufferReturnsValidTextureRef)
{
    RenderGraph graph;
    const auto handle = Extrinsic::RHI::TextureHandle{7u, 2u};
    const auto ref = graph.ImportBackbuffer("Backbuffer", handle);
    EXPECT_TRUE(ref.IsValid());
    const auto* desc = graph.GetTextureDesc(ref);
    ASSERT_NE(desc, nullptr);
    EXPECT_TRUE(desc->Imported);
    EXPECT_FALSE(desc->AliasEligible);
    EXPECT_EQ(desc->FinalState, TextureState::Present);
}

TEST(GraphicsRenderGraph, ImportedTextureIsNotAliasEligible)
{
    RenderGraph graph;
    const auto handle = Extrinsic::RHI::TextureHandle{1u, 1u};
    const auto ref = graph.ImportTexture("History", handle, TextureState::ShaderRead, TextureState::ShaderRead);
    const auto* desc = graph.GetTextureDesc(ref);
    ASSERT_NE(desc, nullptr);
    EXPECT_TRUE(desc->Imported);
    EXPECT_FALSE(desc->AliasEligible);
}

TEST(GraphicsRenderGraph, TransientTextureStartsUndefined)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc textureDesc{};
    textureDesc.Width = 1280;
    textureDesc.Height = 720;
    const auto ref = graph.CreateTexture("LightingOut", textureDesc);
    const auto* desc = graph.GetTextureDesc(ref);
    ASSERT_NE(desc, nullptr);
    EXPECT_FALSE(desc->Imported);
    EXPECT_TRUE(desc->AliasEligible);
    EXPECT_EQ(desc->InitialState, TextureState::Undefined);
    EXPECT_EQ(desc->FinalState, TextureState::Undefined);
}

TEST(GraphicsRenderGraph, InvalidGenerationFailsValidation)
{
    RenderGraph graph;
    const auto ref = graph.CreateBuffer("Scratch", Extrinsic::RHI::BufferDesc{.SizeBytes = 64u});
    BufferRef stale = ref;
    graph.Reset();
    auto result = graph.ValidateBufferRef(stale);
    EXPECT_FALSE(result.has_value());
}

TEST(GraphicsRenderGraph, DuplicateDebugNamesAreAllowed)
{
    RenderGraph graph;
    const auto a = graph.CreateBuffer("SharedName", Extrinsic::RHI::BufferDesc{.SizeBytes = 16u});
    const auto b = graph.CreateBuffer("SharedName", Extrinsic::RHI::BufferDesc{.SizeBytes = 32u});
    EXPECT_TRUE(graph.ValidateBufferRef(a).has_value());
    EXPECT_TRUE(graph.ValidateBufferRef(b).has_value());
    EXPECT_NE(a.Index, b.Index);
}

TEST(GraphicsRenderGraph, PassWithDeclaredReadCompiles)
{
    RenderGraph graph;
    const auto tex = graph.CreateTexture("GBufferA", Extrinsic::RHI::TextureDesc{});

    graph.AddPass("ReadPass",
                  [tex](RenderGraphBuilder& builder) {
                      const auto read = builder.Read(tex, TextureUsage::ShaderRead);
                      EXPECT_TRUE(read.IsValid());
                  });

    auto compiled = graph.Compile();
    EXPECT_TRUE(compiled.has_value());
}

TEST(GraphicsRenderGraph, PassWithDeclaredWriteCompiles)
{
    RenderGraph graph;
    const auto tex = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});

    graph.AddPass("WritePass",
                  [tex](RenderGraphBuilder& builder) {
                      const auto written = builder.Write(tex, TextureUsage::ColorAttachmentWrite);
                      EXPECT_TRUE(written.IsValid());
                  });

    auto compiled = graph.Compile();
    EXPECT_TRUE(compiled.has_value());
}

TEST(GraphicsRenderGraph, InvalidResourceRefFailsCompile)
{
    RenderGraph graph;
    TextureRef invalid{.Index = 404u, .Generation = 7u};

    graph.AddPass("InvalidRef",
                  [invalid](RenderGraphBuilder& builder) {
                      const auto result = builder.Read(invalid, TextureUsage::ShaderRead);
                      EXPECT_FALSE(result.IsValid());
                  });

    auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());
}

TEST(GraphicsRenderGraph, CannotWriteReadOnlyImportedTexture)
{
    RenderGraph graph;
    const auto imported = graph.ImportTexture(
        "ReadOnlyHistory", Extrinsic::RHI::TextureHandle{3u, 1u}, TextureState::ShaderRead, TextureState::ShaderRead);

    graph.AddPass("WriteImported",
                  [imported](RenderGraphBuilder& builder) {
                      const auto result = builder.Write(imported, TextureUsage::ShaderWrite);
                      EXPECT_FALSE(result.IsValid());
                  });

    auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());
}

TEST(GraphicsRenderGraph, SideEffectPassIsNotCulled)
{
    RenderGraph graph;
    const auto pass = graph.AddPass("DebugBlit", [](RenderGraphBuilder& builder) { builder.SideEffect(); }, false);

    ASSERT_TRUE(pass.IsValid());
    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
}

TEST(GraphicsRenderGraph, WriteThenReadCreatesDependency)
{
    RenderGraph graph;
    const auto tex = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto writer = graph.AddPass("Writer", [tex](RenderGraphBuilder& builder) {
        builder.Write(tex, TextureUsage::ColorAttachmentWrite);
    });
    const auto reader = graph.AddPass("Reader", [tex](RenderGraphBuilder& builder) {
        builder.Read(tex, TextureUsage::ShaderRead);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_LT(FindOrder(*compiled, writer.Index), FindOrder(*compiled, reader.Index));
}

TEST(GraphicsRenderGraph, ReadThenWriteCreatesDependency)
{
    RenderGraph graph;
    const auto tex = graph.CreateTexture("Depth", Extrinsic::RHI::TextureDesc{});
    const auto reader = graph.AddPass("Reader", [tex](RenderGraphBuilder& builder) {
        builder.Read(tex, TextureUsage::DepthRead);
    });
    const auto writer = graph.AddPass("Writer", [tex](RenderGraphBuilder& builder) {
        builder.Write(tex, TextureUsage::DepthWrite);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_LT(FindOrder(*compiled, reader.Index), FindOrder(*compiled, writer.Index));
}

TEST(GraphicsRenderGraph, WriteThenWriteCreatesDependency)
{
    RenderGraph graph;
    const auto buf = graph.CreateBuffer("Indirect", Extrinsic::RHI::BufferDesc{.SizeBytes = 128u});
    const auto first = graph.AddPass("First", [buf](RenderGraphBuilder& builder) {
        builder.Write(buf, BufferUsage::ShaderWrite);
    });
    const auto second = graph.AddPass("Second", [buf](RenderGraphBuilder& builder) {
        builder.Write(buf, BufferUsage::TransferDst);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_LT(FindOrder(*compiled, first.Index), FindOrder(*compiled, second.Index));
}

TEST(GraphicsRenderGraph, InvalidPresentTargetFailsValidation)
{
    RenderGraph graph;
    const auto transient = graph.CreateTexture("NotBackbuffer", Extrinsic::RHI::TextureDesc{});
    graph.AddPass("Present", [transient](RenderGraphBuilder& builder) {
        builder.Read(transient, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());
}

TEST(GraphicsRenderGraph, PresentOnImportedBackbufferCompiles)
{
    RenderGraph graph;
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{2u, 1u});
    graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
}
