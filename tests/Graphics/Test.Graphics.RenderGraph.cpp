#include <gtest/gtest.h>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;

using namespace Extrinsic::Graphics;

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
