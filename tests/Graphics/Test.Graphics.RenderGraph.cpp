#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>

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

TEST(GraphicsRenderGraph, UnusedTransientProducerIsCulled)
{
    RenderGraph graph;
    const auto scratch = graph.CreateTexture("Scratch", Extrinsic::RHI::TextureDesc{});
    graph.AddPass("UnusedProducer", [scratch](RenderGraphBuilder& builder) {
        builder.Write(scratch, TextureUsage::ColorAttachmentWrite);
    });

    const auto debug = graph.AddPass("Debug", [](RenderGraphBuilder& builder) { builder.SideEffect(); }, false);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
    EXPECT_EQ(compiled->CulledPassCount, 1u);
    ASSERT_EQ(compiled->TopologicalOrder.size(), 1u);
    EXPECT_EQ(compiled->TopologicalOrder.front(), debug.Index);
}

TEST(GraphicsRenderGraph, PresentChainKeepsProducerPasses)
{
    RenderGraph graph;
    const auto lighting = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{4u, 1u});

    const auto lightingPass = graph.AddPass("Lighting", [lighting](RenderGraphBuilder& builder) {
        builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    const auto tonemapPass = graph.AddPass("Tonemap", [lighting, backbuffer](RenderGraphBuilder& builder) {
        builder.Read(lighting, TextureUsage::ShaderRead);
        builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    });
    const auto presentPass = graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 3u);
    EXPECT_EQ(compiled->CulledPassCount, 0u);
    EXPECT_LT(FindOrder(*compiled, lightingPass.Index), FindOrder(*compiled, tonemapPass.Index));
    EXPECT_LT(FindOrder(*compiled, tonemapPass.Index), FindOrder(*compiled, presentPass.Index));
}

TEST(GraphicsRenderGraph, ImportedResourceWriterIsNotCulled)
{
    RenderGraph graph;
    const auto imported = graph.ImportBuffer("Readback", Extrinsic::RHI::BufferHandle{9u, 1u}, BufferState::TransferDst, BufferState::HostReadback);
    const auto writer = graph.AddPass("WriteReadback", [imported](RenderGraphBuilder& builder) {
        builder.Write(imported, BufferUsage::TransferDst);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
    EXPECT_EQ(compiled->CulledPassCount, 0u);
    ASSERT_EQ(compiled->TopologicalOrder.size(), 1u);
    EXPECT_EQ(compiled->TopologicalOrder.front(), writer.Index);
}

TEST(GraphicsRenderGraph, LifetimeFirstAndLastUseTracksPassIndices)
{
    RenderGraph graph;
    const auto history = graph.CreateTexture("History", Extrinsic::RHI::TextureDesc{});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{8u, 2u});

    graph.AddPass("HistoryWrite", [history](RenderGraphBuilder& builder) {
        builder.Write(history, TextureUsage::ColorAttachmentWrite);
    });
    graph.AddPass("HistoryRead", [history](RenderGraphBuilder& builder) {
        builder.Read(history, TextureUsage::ShaderRead);
    });
    graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_GE(compiled->TextureLifetimes.size(), 2u);
    const auto& historyLifetime = compiled->TextureLifetimes[history.Index];
    EXPECT_TRUE(historyLifetime.HasUse);
    EXPECT_EQ(historyLifetime.FirstUsePass, 0u);
    EXPECT_EQ(historyLifetime.LastUsePass, 1u);
    const auto& backbufferLifetime = compiled->TextureLifetimes[backbuffer.Index];
    EXPECT_TRUE(backbufferLifetime.HasUse);
    EXPECT_EQ(backbufferLifetime.FirstUsePass, 2u);
    EXPECT_EQ(backbufferLifetime.LastUsePass, 2u);
}

TEST(GraphicsRenderGraph, BarriersTransitionFromUndefinedToColorWriteThenShaderRead)
{
    RenderGraph graph;
    const auto lighting = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{12u, 1u});

    graph.AddPass("Lighting", [lighting](RenderGraphBuilder& builder) {
        builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    graph.AddPass("Post", [lighting, backbuffer](RenderGraphBuilder& builder) {
        builder.Read(lighting, TextureUsage::ShaderRead);
        builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    });
    graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_FALSE(compiled->BarrierPackets.empty());

    bool sawColorWriteBarrier = false;
    bool sawShaderReadBarrier = false;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex != lighting.Index)
            {
                continue;
            }
            if (barrier.Before == TextureBarrierState::Undefined &&
                barrier.After == TextureBarrierState::ColorAttachmentWrite)
            {
                sawColorWriteBarrier = true;
            }
            if (barrier.Before == TextureBarrierState::ColorAttachmentWrite &&
                barrier.After == TextureBarrierState::ShaderRead)
            {
                sawShaderReadBarrier = true;
            }
        }
    }

    EXPECT_TRUE(sawColorWriteBarrier);
    EXPECT_TRUE(sawShaderReadBarrier);
}

TEST(GraphicsRenderGraph, ImportedBufferUsesInitialStateForFirstBarrier)
{
    RenderGraph graph;
    const auto readback = graph.ImportBuffer("Readback",
        Extrinsic::RHI::BufferHandle{11u, 1u},
        BufferState::TransferDst,
        BufferState::HostReadback);
    graph.AddPass("MapReadback", [readback](RenderGraphBuilder& builder) {
        builder.Read(readback, BufferUsage::HostReadback);
    }, true);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_FALSE(compiled->BarrierPackets.empty());

    bool sawImportedInitialTransition = false;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.BufferBarriers)
        {
            if (barrier.BufferIndex == readback.Index &&
                barrier.Before == BufferBarrierState::TransferDst &&
                barrier.After == BufferBarrierState::HostReadback)
            {
                sawImportedInitialTransition = true;
            }
        }
    }

    EXPECT_TRUE(sawImportedInitialTransition);
}

TEST(GraphicsRenderGraph, ImportedIndirectBufferTransitionsFromShaderWriteToIndirectRead)
{
    RenderGraph graph;
    const auto drawArgs = graph.ImportBuffer("DrawArgs",
        Extrinsic::RHI::BufferHandle{21u, 1u},
        BufferState::ShaderWrite,
        BufferState::IndirectRead);

    graph.AddPass("CullWrite", [drawArgs](RenderGraphBuilder& builder) {
        builder.Write(drawArgs, BufferUsage::ShaderWrite);
    });
    graph.AddPass("DrawRead", [drawArgs](RenderGraphBuilder& builder) {
        builder.Read(drawArgs, BufferUsage::IndirectRead);
    }, true);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    bool sawWriteToIndirect = false;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.BufferBarriers)
        {
            if (barrier.BufferIndex == drawArgs.Index &&
                barrier.Before == BufferBarrierState::ShaderWrite &&
                barrier.After == BufferBarrierState::IndirectRead)
            {
                sawWriteToIndirect = true;
            }
        }
    }

    EXPECT_TRUE(sawWriteToIndirect);
}

TEST(GraphicsRenderGraph, ExecuteEmptyGraphSucceeds)
{
    RenderGraph graph;
    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    RenderGraphExecutor executor;
    auto result = executor.Execute(*compiled);
    EXPECT_TRUE(result.has_value());
}

TEST(GraphicsRenderGraph, ExecutePresentChainRecordsPassOrderAndBarriers)
{
    RenderGraph graph;
    const auto lighting = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{20u, 2u});
    const auto lightingPass = graph.AddPass("Lighting", [lighting](RenderGraphBuilder& builder) {
        builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    const auto postPass = graph.AddPass("Post", [lighting, backbuffer](RenderGraphBuilder& builder) {
        builder.Read(lighting, TextureUsage::ShaderRead);
        builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    });
    const auto presentPass = graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    std::vector<std::uint32_t> executed{};
    std::vector<std::uint32_t> barrierPasses{};
    RenderGraphExecutor executor;
    auto result = executor.Execute(
        *compiled,
        [&executed](const std::uint32_t passIndex) { executed.push_back(passIndex); },
        [&barrierPasses](const BarrierPacket& packet) { barrierPasses.push_back(packet.PassIndex); });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(executed, compiled->TopologicalOrder);
    EXPECT_EQ(executed.size(), 3u);
    EXPECT_EQ(executed[0], lightingPass.Index);
    EXPECT_EQ(executed[1], postPass.Index);
    EXPECT_EQ(executed[2], presentPass.Index);
    EXPECT_FALSE(barrierPasses.empty());
}

TEST(GraphicsRenderGraph, ExecuteFailsWhenBarrierReferencesInvalidResource)
{
    CompiledRenderGraph compiled{};
    compiled.TopologicalOrder = {0u};
    compiled.TextureHandles = {};
    compiled.BufferHandles = {};
    compiled.BarrierPackets = {
        BarrierPacket{
            .PassIndex = 0u,
            .TextureBarriers = {TextureBarrierPacket{.TextureIndex = 4u, .Before = TextureBarrierState::Undefined, .After = TextureBarrierState::ShaderRead}},
        }
    };

    RenderGraphExecutor executor;
    auto result = executor.Execute(compiled);
    EXPECT_FALSE(result.has_value());
}

TEST(GraphicsRenderGraph, DebugDumpContainsPassOrderAndResourceSections)
{
    RenderGraph graph;
    const auto texture = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto buffer = graph.CreateBuffer("Args", Extrinsic::RHI::BufferDesc{.SizeBytes = 64u});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{91u, 1u});

    graph.AddPass("Lighting", [texture, buffer](RenderGraphBuilder& builder) {
        builder.Write(texture, TextureUsage::ColorAttachmentWrite);
        builder.Write(buffer, BufferUsage::ShaderWrite);
    });
    graph.AddPass("Present", [texture, backbuffer, buffer](RenderGraphBuilder& builder) {
        builder.Read(texture, TextureUsage::ShaderRead);
        builder.Read(buffer, BufferUsage::IndirectRead);
        builder.Read(backbuffer, TextureUsage::Present);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::string dump = BuildRenderGraphDebugDump(*compiled);
    EXPECT_NE(dump.find("passes:"), std::string::npos);
    EXPECT_NE(dump.find("name=\"Lighting\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Present\""), std::string::npos);
    EXPECT_NE(dump.find("textures:"), std::string::npos);
    EXPECT_NE(dump.find("buffers:"), std::string::npos);
}
