#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Core.Error;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.TextureUpload;

using namespace Extrinsic::Graphics;

namespace
{
    namespace RHI = Extrinsic::RHI;

    inline constexpr std::uint64_t kExpectedPlacementAlignmentBytes = 256u;

    std::size_t FindOrder(const CompiledRenderGraph& compiled, const std::uint32_t passIndex)
    {
        auto it = std::find(compiled.TopologicalOrder.begin(), compiled.TopologicalOrder.end(), passIndex);
        return static_cast<std::size_t>(std::distance(compiled.TopologicalOrder.begin(), it));
    }

    [[nodiscard]] constexpr std::uint64_t AlignUpForPlacementTest(const std::uint64_t value,
                                                                  const std::uint64_t alignment) noexcept
    {
        const std::uint64_t remainder = value % alignment;
        return remainder == 0u ? value : value + (alignment - remainder);
    }

    [[nodiscard]] std::uint64_t ExpectedTexturePlacementBytes(const RHI::TextureDesc& desc) noexcept
    {
        return AlignUpForPlacementTest(RHI::EstimateTextureStorageBytes(desc), kExpectedPlacementAlignmentBytes);
    }

    [[nodiscard]] std::uint64_t ExpectedBufferPlacementBytes(const std::uint64_t sizeBytes) noexcept
    {
        return AlignUpForPlacementTest(sizeBytes, kExpectedPlacementAlignmentBytes);
    }

    [[nodiscard]] const TransientResourcePlacement* FindTexturePlacement(const CompiledRenderGraph& compiled,
                                                                        const std::uint32_t resourceIndex)
    {
        const auto it = std::find_if(compiled.TextureTransientPlacements.begin(),
                                     compiled.TextureTransientPlacements.end(),
                                     [resourceIndex](const TransientResourcePlacement& placement) {
                                         return placement.ResourceIndex == resourceIndex;
                                     });
        return it == compiled.TextureTransientPlacements.end() ? nullptr : &*it;
    }

    [[nodiscard]] const TransientResourcePlacement* FindBufferPlacement(const CompiledRenderGraph& compiled,
                                                                       const std::uint32_t resourceIndex)
    {
        const auto it = std::find_if(compiled.BufferTransientPlacements.begin(),
                                     compiled.BufferTransientPlacements.end(),
                                     [resourceIndex](const TransientResourcePlacement& placement) {
                                         return placement.ResourceIndex == resourceIndex;
                                     });
        return it == compiled.BufferTransientPlacements.end() ? nullptr : &*it;
    }

    [[nodiscard]] std::uint32_t CountTextureAliasReuseHazards(const CompiledRenderGraph& compiled)
    {
        std::uint32_t count = 0u;
        for (const BarrierPacket& packet : compiled.BarrierPackets)
        {
            count += static_cast<std::uint32_t>(packet.TextureAliasReuseBarriers.size());
        }
        return count;
    }

    [[nodiscard]] std::uint32_t CountBufferAliasReuseHazards(const CompiledRenderGraph& compiled)
    {
        std::uint32_t count = 0u;
        for (const BarrierPacket& packet : compiled.BarrierPackets)
        {
            count += static_cast<std::uint32_t>(packet.BufferAliasReuseBarriers.size());
        }
        return count;
    }

    [[nodiscard]] std::uint32_t CountRegularBarrierTransitions(const CompiledRenderGraph& compiled)
    {
        std::uint32_t count = 0u;
        for (const BarrierPacket& packet : compiled.BarrierPackets)
        {
            count += static_cast<std::uint32_t>(packet.TextureBarriers.size());
            count += static_cast<std::uint32_t>(packet.BufferBarriers.size());
        }
        return count;
    }

    [[nodiscard]] constexpr bool LifetimesOverlap(const ResourceLifetime& lhs,
                                                  const ResourceLifetime& rhs) noexcept
    {
        return lhs.HasUse && rhs.HasUse && lhs.FirstUsePass <= rhs.LastUsePass && rhs.FirstUsePass <= lhs.LastUsePass;
    }

    [[nodiscard]] constexpr bool ByteRangesOverlap(const TransientResourcePlacement& lhs,
                                                   const TransientResourcePlacement& rhs) noexcept
    {
        const std::uint64_t lhsEnd = lhs.OffsetBytes + lhs.SizeBytes;
        const std::uint64_t rhsEnd = rhs.OffsetBytes + rhs.SizeBytes;
        return lhs.OffsetBytes < rhsEnd && rhs.OffsetBytes < lhsEnd;
    }

    void ExpectPlacementVectorsEqual(const std::vector<TransientResourcePlacement>& lhs,
                                     const std::vector<TransientResourcePlacement>& rhs)
    {
        ASSERT_EQ(lhs.size(), rhs.size());
        for (std::size_t i = 0u; i < lhs.size(); ++i)
        {
            EXPECT_EQ(lhs[i].ResourceIndex, rhs[i].ResourceIndex);
            EXPECT_EQ(lhs[i].BlockIndex, rhs[i].BlockIndex);
            EXPECT_EQ(lhs[i].OffsetBytes, rhs[i].OffsetBytes);
            EXPECT_EQ(lhs[i].SizeBytes, rhs[i].SizeBytes);
            EXPECT_EQ(lhs[i].AlignmentBytes, rhs[i].AlignmentBytes);
            EXPECT_EQ(lhs[i].FirstUsePass, rhs[i].FirstUsePass);
            EXPECT_EQ(lhs[i].LastUsePass, rhs[i].LastUsePass);
        }
    }

    void ExpectAliasHazardsEqual(const CompiledRenderGraph& lhs,
                                 const CompiledRenderGraph& rhs)
    {
        ASSERT_EQ(lhs.BarrierPackets.size(), rhs.BarrierPackets.size());
        for (std::size_t packetIndex = 0u; packetIndex < lhs.BarrierPackets.size(); ++packetIndex)
        {
            const BarrierPacket& leftPacket = lhs.BarrierPackets[packetIndex];
            const BarrierPacket& rightPacket = rhs.BarrierPackets[packetIndex];
            ASSERT_EQ(leftPacket.TextureAliasReuseBarriers.size(), rightPacket.TextureAliasReuseBarriers.size());
            ASSERT_EQ(leftPacket.BufferAliasReuseBarriers.size(), rightPacket.BufferAliasReuseBarriers.size());
            for (std::size_t i = 0u; i < leftPacket.TextureAliasReuseBarriers.size(); ++i)
            {
                EXPECT_EQ(leftPacket.TextureAliasReuseBarriers[i].PreviousTextureIndex,
                          rightPacket.TextureAliasReuseBarriers[i].PreviousTextureIndex);
                EXPECT_EQ(leftPacket.TextureAliasReuseBarriers[i].TextureIndex,
                          rightPacket.TextureAliasReuseBarriers[i].TextureIndex);
                EXPECT_EQ(leftPacket.TextureAliasReuseBarriers[i].BlockIndex,
                          rightPacket.TextureAliasReuseBarriers[i].BlockIndex);
                EXPECT_EQ(leftPacket.TextureAliasReuseBarriers[i].OffsetBytes,
                          rightPacket.TextureAliasReuseBarriers[i].OffsetBytes);
                EXPECT_EQ(leftPacket.TextureAliasReuseBarriers[i].SizeBytes,
                          rightPacket.TextureAliasReuseBarriers[i].SizeBytes);
            }
            for (std::size_t i = 0u; i < leftPacket.BufferAliasReuseBarriers.size(); ++i)
            {
                EXPECT_EQ(leftPacket.BufferAliasReuseBarriers[i].PreviousBufferIndex,
                          rightPacket.BufferAliasReuseBarriers[i].PreviousBufferIndex);
                EXPECT_EQ(leftPacket.BufferAliasReuseBarriers[i].BufferIndex,
                          rightPacket.BufferAliasReuseBarriers[i].BufferIndex);
                EXPECT_EQ(leftPacket.BufferAliasReuseBarriers[i].BlockIndex,
                          rightPacket.BufferAliasReuseBarriers[i].BlockIndex);
                EXPECT_EQ(leftPacket.BufferAliasReuseBarriers[i].OffsetBytes,
                          rightPacket.BufferAliasReuseBarriers[i].OffsetBytes);
                EXPECT_EQ(leftPacket.BufferAliasReuseBarriers[i].SizeBytes,
                          rightPacket.BufferAliasReuseBarriers[i].SizeBytes);
            }
        }
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

    (void)graph.AddPass("ReadPass",
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

    (void)graph.AddPass("WritePass",
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

    (void)graph.AddPass("InvalidRef",
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

    (void)graph.AddPass("WriteImported",
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
        (void)builder.Write(tex, TextureUsage::ColorAttachmentWrite);
    });
    const auto reader = graph.AddPass("Reader", [tex](RenderGraphBuilder& builder) {
        (void)builder.Read(tex, TextureUsage::ShaderRead);
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
        (void)builder.Read(tex, TextureUsage::DepthRead);
    });
    const auto writer = graph.AddPass("Writer", [tex](RenderGraphBuilder& builder) {
        (void)builder.Write(tex, TextureUsage::DepthWrite);
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
        (void)builder.Write(buf, BufferUsage::ShaderWrite);
    });
    const auto second = graph.AddPass("Second", [buf](RenderGraphBuilder& builder) {
        (void)builder.Write(buf, BufferUsage::TransferDst);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_LT(FindOrder(*compiled, first.Index), FindOrder(*compiled, second.Index));
}

TEST(GraphicsRenderGraph, CrossQueueReadReadAddsQueueHandoffPlaceholderEdge)
{
    RenderGraph graph;
    const auto texture = graph.CreateTexture("SharedRead", Extrinsic::RHI::TextureDesc{});

    const auto graphicsReader = graph.AddPass("GraphicsRead", [texture](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::Graphics);
        (void)builder.Read(texture, TextureUsage::ShaderRead);
        builder.SideEffect();
    });
    const auto computeReader = graph.AddPass("ComputeRead", [texture](RenderGraphBuilder& builder) {
        builder.SetQueue(RenderQueue::AsyncCompute);
        (void)builder.Read(texture, TextureUsage::ShaderRead);
        builder.SideEffect();
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->QueueHandoffEdgeCount, 1u);
    EXPECT_EQ(compiled->EdgeCount, 1u);
    EXPECT_LT(FindOrder(*compiled, graphicsReader.Index), FindOrder(*compiled, computeReader.Index));
}

TEST(GraphicsRenderGraph, ExplicitDependencyOrdersIndependentPasses)
{
    RenderGraph graph;
    const auto producer = graph.AddPass("Producer",
                                        [](RenderGraphBuilder& builder) {
                                            builder.SideEffect();
                                        },
                                        false);
    const auto consumer = graph.AddPass("Consumer",
                                        [producer](RenderGraphBuilder& builder) {
                                            builder.DependsOn(producer);
                                            builder.SideEffect();
                                        },
                                        false);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_LT(FindOrder(*compiled, producer.Index), FindOrder(*compiled, consumer.Index));
    EXPECT_EQ(compiled->EdgeCount, 1u);
}

TEST(GraphicsRenderGraph, IndependentSideEffectPassesCompileInDeterministicOrder)
{
    RenderGraph graph;
    const auto first = graph.AddPass("First", [](RenderGraphBuilder& builder) { builder.SideEffect(); }, false);
    const auto second = graph.AddPass("Second", [](RenderGraphBuilder& builder) { builder.SideEffect(); }, false);

    auto compiledA = graph.Compile();
    ASSERT_TRUE(compiledA.has_value());
    ASSERT_EQ(compiledA->TopologicalOrder.size(), 2u);
    EXPECT_EQ(compiledA->TopologicalOrder[0], first.Index);
    EXPECT_EQ(compiledA->TopologicalOrder[1], second.Index);

    auto compiledB = graph.Compile();
    ASSERT_TRUE(compiledB.has_value());
    EXPECT_EQ(compiledA->TopologicalOrder, compiledB->TopologicalOrder);
}

TEST(GraphicsRenderGraph, InvalidExplicitDependencyFailsCompile)
{
    RenderGraph graph;
    constexpr PassRef missingDependency{.Index = 42u, .Generation = 1u};
    (void)graph.AddPass("Consumer",
                  [](RenderGraphBuilder& builder) {
                      builder.SideEffect();
                  },
                  false);
    (void)graph.AddPass("Dependent",
                  [missingDependency](RenderGraphBuilder& builder) {
                      builder.DependsOn(missingDependency);
                      builder.SideEffect();
                  },
                  false);

    auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());
    const auto& findings = graph.GetLastCompileValidationResult().Findings;
    ASSERT_FALSE(findings.empty());
    EXPECT_NE(findings.front().Message.find("Dependent"), std::string::npos);
}

TEST(GraphicsRenderGraph, DependencyCycleReportsPassNamesInDiagnostic)
{
    std::vector<RenderPassRecord> passes{};
    RenderPassRecord passA{};
    passA.Name = "PassA";
    passA.SideEffect = true;
    passA.ExplicitDependencies.push_back(PassRef{.Index = 1u, .Generation = 1u});
    passes.push_back(passA);

    RenderPassRecord passB{};
    passB.Name = "PassB";
    passB.SideEffect = true;
    passB.ExplicitDependencies.push_back(PassRef{.Index = 0u, .Generation = 1u});
    passes.push_back(passB);

    RenderGraphValidationResult validation{};
    auto compiled = RenderGraphCompiler::Compile(passes, {}, {}, &validation);
    ASSERT_FALSE(compiled.has_value());
    const auto& findings = validation.Findings;
    ASSERT_FALSE(findings.empty());
    EXPECT_FALSE(findings.front().Message.empty());
    EXPECT_NE(findings.front().Message.find("PassA"), std::string::npos);
    EXPECT_NE(findings.front().Message.find("PassB"), std::string::npos);
}

TEST(GraphicsRenderGraph, TransientLifetimesUseTopologicalExecutionOrder)
{
    std::vector<TextureResourceDesc> textures{};
    RHI::TextureDesc desc{};
    desc.Width = 16u;
    desc.Height = 16u;
    desc.Fmt = RHI::Format::RGBA8_UNORM;
    textures.push_back(TextureResourceDesc{
        .Name = "LongLivedA",
        .Desc = desc,
        .Generation = 1u,
    });
    textures.push_back(TextureResourceDesc{
        .Name = "MiddleB",
        .Desc = desc,
        .Generation = 1u,
    });

    std::vector<RenderPassRecord> passes{};
    RenderPassRecord writeB{};
    writeB.Name = "WriteB";
    writeB.SideEffect = true;
    writeB.TextureAccesses.push_back(TextureAccess{
        .Ref = TextureRef{.Index = 1u, .Generation = 1u},
        .Usage = TextureUsage::ColorAttachmentWrite,
        .Write = true,
    });
    writeB.ExplicitDependencies.push_back(PassRef{.Index = 1u, .Generation = 1u});
    passes.push_back(std::move(writeB));

    RenderPassRecord writeA{};
    writeA.Name = "WriteA";
    writeA.TextureAccesses.push_back(TextureAccess{
        .Ref = TextureRef{.Index = 0u, .Generation = 1u},
        .Usage = TextureUsage::ColorAttachmentWrite,
        .Write = true,
    });
    passes.push_back(std::move(writeA));

    RenderPassRecord readA{};
    readA.Name = "ReadA";
    readA.SideEffect = true;
    readA.TextureAccesses.push_back(TextureAccess{
        .Ref = TextureRef{.Index = 0u, .Generation = 1u},
        .Usage = TextureUsage::ShaderRead,
        .Write = false,
    });
    passes.push_back(std::move(readA));

    auto compiled = RenderGraphCompiler::Compile(passes, textures, {});
    ASSERT_TRUE(compiled.has_value());
    ASSERT_EQ(compiled->TopologicalOrder, (std::vector<std::uint32_t>{1u, 0u, 2u}));
    ASSERT_EQ(compiled->TextureLifetimes.size(), 2u);

    EXPECT_TRUE(compiled->TextureLifetimes[0u].HasUse);
    EXPECT_EQ(compiled->TextureLifetimes[0u].FirstUsePass, 0u);
    EXPECT_EQ(compiled->TextureLifetimes[0u].LastUsePass, 2u);
    EXPECT_TRUE(compiled->TextureLifetimes[1u].HasUse);
    EXPECT_EQ(compiled->TextureLifetimes[1u].FirstUsePass, 1u);
    EXPECT_EQ(compiled->TextureLifetimes[1u].LastUsePass, 1u);
    EXPECT_TRUE(LifetimesOverlap(compiled->TextureLifetimes[0u],
                                 compiled->TextureLifetimes[1u]));
}

TEST(GraphicsRenderGraph, AliasReuseBarriersUseTopologicalPassIndex)
{
    RenderGraph graph;
    RHI::TextureDesc desc{};
    desc.Width = 16u;
    desc.Height = 16u;
    desc.Fmt = RHI::Format::RGBA8_UNORM;

    const auto first = graph.CreateTexture("First", desc);
    const auto second = graph.CreateTexture("Second", desc);

    const PassRef writeSecond = graph.AddPass(
        "WriteSecond",
        [second](RenderGraphBuilder& builder) {
            (void)builder.Write(second, TextureUsage::ColorAttachmentWrite);
            builder.DependsOn(PassRef{.Index = 1u, .Generation = 1u});
        },
        true);

    const PassRef writeFirst = graph.AddPass(
        "WriteFirst",
        [first](RenderGraphBuilder& builder) {
            (void)builder.Write(first, TextureUsage::ColorAttachmentWrite);
        },
        true);

    ASSERT_EQ(writeSecond.Index, 0u);
    ASSERT_EQ(writeFirst.Index, 1u);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_EQ(compiled->TopologicalOrder, (std::vector<std::uint32_t>{1u, 0u}));

    const TransientResourcePlacement* firstPlacement = FindTexturePlacement(*compiled, first.Index);
    const TransientResourcePlacement* secondPlacement = FindTexturePlacement(*compiled, second.Index);
    ASSERT_NE(firstPlacement, nullptr);
    ASSERT_NE(secondPlacement, nullptr);
    EXPECT_EQ(firstPlacement->OffsetBytes, secondPlacement->OffsetBytes);
    EXPECT_EQ(firstPlacement->FirstUsePass, 0u);
    EXPECT_EQ(secondPlacement->FirstUsePass, 1u);

    const BarrierPacket* aliasPacket = nullptr;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        if (!packet.TextureAliasReuseBarriers.empty())
        {
            aliasPacket = &packet;
            break;
        }
    }
    ASSERT_NE(aliasPacket, nullptr);
    EXPECT_EQ(aliasPacket->PassIndex, writeSecond.Index);
    EXPECT_EQ(aliasPacket->Stage, BarrierPacketStage::BeforePass);
    ASSERT_EQ(aliasPacket->TextureAliasReuseBarriers.size(), 1u);
    EXPECT_EQ(aliasPacket->TextureAliasReuseBarriers.front().PreviousTextureIndex, first.Index);
    EXPECT_EQ(aliasPacket->TextureAliasReuseBarriers.front().TextureIndex, second.Index);
}

TEST(GraphicsRenderGraph, InvalidPresentTargetFailsValidation)
{
    RenderGraph graph;
    const auto transient = graph.CreateTexture("NotBackbuffer", Extrinsic::RHI::TextureDesc{});
    (void)graph.AddPass("Present", [transient](RenderGraphBuilder& builder) {
        (void)builder.Read(transient, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());
    const auto& findings = graph.GetLastCompileValidationResult().Findings;
    ASSERT_FALSE(findings.empty());
    EXPECT_NE(findings.front().Message.find("Present"), std::string::npos);
}

TEST(GraphicsRenderGraph, PresentOnImportedBackbufferCompiles)
{
    RenderGraph graph;
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{2u, 1u});
    (void)graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
}

TEST(GraphicsRenderGraph, UnusedTransientProducerIsCulled)
{
    RenderGraph graph;
    const auto scratch = graph.CreateTexture("Scratch", Extrinsic::RHI::TextureDesc{});
    (void)graph.AddPass("UnusedProducer", [scratch](RenderGraphBuilder& builder) {
        (void)builder.Write(scratch, TextureUsage::ColorAttachmentWrite);
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
        (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    const auto tonemapPass = graph.AddPass("Tonemap", [lighting, backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(lighting, TextureUsage::ShaderRead);
        (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    }, true);
    const auto presentPass = graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 3u);
    EXPECT_EQ(compiled->CulledPassCount, 0u);
    EXPECT_LT(FindOrder(*compiled, lightingPass.Index), FindOrder(*compiled, tonemapPass.Index));
    EXPECT_LT(FindOrder(*compiled, tonemapPass.Index), FindOrder(*compiled, presentPass.Index));
}

TEST(GraphicsRenderGraph, SelectionOutlineReadDependenciesOrderAfterDepthAndGBuffer)
{
    RenderGraph graph;
    const auto depth = graph.CreateTexture("Depth", Extrinsic::RHI::TextureDesc{});
    const auto entityId = graph.CreateTexture("EntityId", Extrinsic::RHI::TextureDesc{});
    const auto post = graph.CreateTexture("Post", Extrinsic::RHI::TextureDesc{});

    const auto depthPass = graph.AddPass("DepthPrepass", [depth](RenderGraphBuilder& builder) {
        (void)builder.Write(depth, TextureUsage::DepthWrite);
    });
    const auto gbufferPass = graph.AddPass("GBuffer", [entityId, depth](RenderGraphBuilder& builder) {
        (void)builder.Write(entityId, TextureUsage::ColorAttachmentWrite);
        (void)builder.Read(depth, TextureUsage::DepthRead);
    });
    const auto selectionPass = graph.AddPass("SelectionOutline", [entityId, depth, post](RenderGraphBuilder& builder) {
        (void)builder.Read(entityId, TextureUsage::ShaderRead);
        (void)builder.Read(depth, TextureUsage::DepthRead);
        (void)builder.Write(post, TextureUsage::ColorAttachmentWrite);
    }, true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_LT(FindOrder(*compiled, depthPass.Index), FindOrder(*compiled, gbufferPass.Index));
    EXPECT_LT(FindOrder(*compiled, gbufferPass.Index), FindOrder(*compiled, selectionPass.Index));
}

TEST(GraphicsRenderGraph, ImportedResourceWriterIsNotCulled)
{
    RenderGraph graph;
    const auto imported = graph.ImportBuffer("Readback", Extrinsic::RHI::BufferHandle{9u, 1u}, BufferState::TransferDst, BufferState::HostReadback);
    const auto writer = graph.AddPass("WriteReadback", [imported](RenderGraphBuilder& builder) {
        (void)builder.Write(imported, BufferUsage::TransferDst);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->PassCount, 1u);
    EXPECT_EQ(compiled->CulledPassCount, 0u);
    ASSERT_EQ(compiled->TopologicalOrder.size(), 1u);
    EXPECT_EQ(compiled->TopologicalOrder.front(), writer.Index);
}

TEST(GraphicsRenderGraph, ImportedBufferHandleIsPreservedAcrossCompileAndReset)
{
    RenderGraph graph;
    constexpr Extrinsic::RHI::BufferHandle importedHandle{9001u, 77u};
    const auto imported = graph.ImportBuffer("PersistentImported",
        importedHandle,
        BufferState::TransferDst,
        BufferState::HostReadback);

    (void)graph.AddPass("ReadImported", [imported](RenderGraphBuilder& builder) {
        (void)builder.Read(imported, BufferUsage::HostReadback);
        builder.SideEffect();
    }, true);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(imported.Index, compiled->BufferHandles.size());
    EXPECT_EQ(compiled->BufferHandles[imported.Index], importedHandle);

    graph.Reset();

    const auto importedAgain = graph.ImportBuffer("PersistentImported",
        importedHandle,
        BufferState::TransferDst,
        BufferState::HostReadback);
    (void)graph.AddPass("ReadImportedAgain", [importedAgain](RenderGraphBuilder& builder) {
        (void)builder.Read(importedAgain, BufferUsage::HostReadback);
        builder.SideEffect();
    }, true);

    compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(importedAgain.Index, compiled->BufferHandles.size());
    EXPECT_EQ(compiled->BufferHandles[importedAgain.Index], importedHandle);
}

TEST(GraphicsRenderGraph, ImportedBuffersNeverAliasTransientBuffers)
{
    RenderGraph graph;
    constexpr Extrinsic::RHI::BufferHandle importedHandle{123456u, 5u};
    const auto imported = graph.ImportBuffer("ImportedIndirect",
        importedHandle,
        BufferState::ShaderWrite,
        BufferState::IndirectRead);
    const auto transient = graph.CreateBuffer("TransientScratch", Extrinsic::RHI::BufferDesc{.SizeBytes = 256u});

    (void)graph.AddPass("Producer", [transient](RenderGraphBuilder& builder) {
        (void)builder.Write(transient, BufferUsage::TransferDst);
    });
    (void)graph.AddPass("Consumer", [transient, imported](RenderGraphBuilder& builder) {
        (void)builder.Read(transient, BufferUsage::ShaderRead);
        (void)builder.Read(imported, BufferUsage::IndirectRead);
        builder.SideEffect();
    }, true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_LT(imported.Index, compiled->BufferHandles.size());
    ASSERT_LT(transient.Index, compiled->BufferHandles.size());
    EXPECT_EQ(compiled->BufferHandles[imported.Index], importedHandle);
    EXPECT_NE(compiled->BufferHandles[transient.Index], importedHandle);
}

TEST(GraphicsRenderGraph, LifetimeFirstAndLastUseTracksPassIndices)
{
    RenderGraph graph;
    const auto history = graph.CreateTexture("History", Extrinsic::RHI::TextureDesc{});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{8u, 2u});

    (void)graph.AddPass("HistoryWrite", [history](RenderGraphBuilder& builder) {
        (void)builder.Write(history, TextureUsage::ColorAttachmentWrite);
    });
    (void)graph.AddPass("HistoryRead", [history](RenderGraphBuilder& builder) {
        (void)builder.Read(history, TextureUsage::ShaderRead);
    });
    (void)graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(backbuffer, TextureUsage::Present);
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

    (void)graph.AddPass("Lighting", [lighting](RenderGraphBuilder& builder) {
        (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    (void)graph.AddPass("Post", [lighting, backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(lighting, TextureUsage::ShaderRead);
        (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    }, true);
    (void)graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(backbuffer, TextureUsage::Present);
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
    (void)graph.AddPass("MapReadback", [readback](RenderGraphBuilder& builder) {
        (void)builder.Read(readback, BufferUsage::HostReadback);
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

    (void)graph.AddPass("CullWrite", [drawArgs](RenderGraphBuilder& builder) {
        (void)builder.Write(drawArgs, BufferUsage::ShaderWrite);
    });
    (void)graph.AddPass("DrawRead", [drawArgs](RenderGraphBuilder& builder) {
        (void)builder.Read(drawArgs, BufferUsage::IndirectRead);
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

TEST(GraphicsRenderGraph, DepthWriteThenDepthReadEmitsTransition)
{
    RenderGraph graph;
    const auto depth = graph.CreateTexture("Depth", Extrinsic::RHI::TextureDesc{});

    (void)graph.AddPass("DepthPrepass", [depth](RenderGraphBuilder& builder) {
        (void)builder.Write(depth, TextureUsage::DepthWrite);
    });
    (void)graph.AddPass("Shading", [depth](RenderGraphBuilder& builder) {
        (void)builder.Read(depth, TextureUsage::DepthRead);
    }, true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    bool sawDepthTransition = false;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex == depth.Index &&
                barrier.Before == TextureBarrierState::DepthWrite &&
                barrier.After == TextureBarrierState::DepthRead)
            {
                sawDepthTransition = true;
            }
        }
    }

    EXPECT_TRUE(sawDepthTransition);
}

TEST(GraphicsRenderGraph, TransferDstThenShaderReadEmitsTransition)
{
    RenderGraph graph;
    const auto texture = graph.CreateTexture("Upload", Extrinsic::RHI::TextureDesc{});

    (void)graph.AddPass("Upload", [texture](RenderGraphBuilder& builder) {
        (void)builder.Write(texture, TextureUsage::TransferDst);
    });
    (void)graph.AddPass("Sample", [texture](RenderGraphBuilder& builder) {
        (void)builder.Read(texture, TextureUsage::ShaderRead);
    }, true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    bool sawTransition = false;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex == texture.Index &&
                barrier.Before == TextureBarrierState::TransferDst &&
                barrier.After == TextureBarrierState::ShaderRead)
            {
                sawTransition = true;
            }
        }
    }

    EXPECT_TRUE(sawTransition);
}

TEST(GraphicsRenderGraph, ImportedBackbufferFinalStateTransitionsToPresent)
{
    RenderGraph graph;
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{33u, 2u});

    (void)graph.AddPass("ToneMap", [backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    }, true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    bool sawPresentTransition = false;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex == backbuffer.Index &&
                barrier.Before == TextureBarrierState::ColorAttachmentWrite &&
                barrier.After == TextureBarrierState::Present)
            {
                sawPresentTransition = true;
            }
        }
    }

    EXPECT_TRUE(sawPresentTransition);
}

TEST(GraphicsRenderGraph, RepeatedShaderReadsDoNotEmitRedundantBarrier)
{
    RenderGraph graph;
    const auto texture = graph.CreateTexture("History", Extrinsic::RHI::TextureDesc{});

    (void)graph.AddPass("ReadA", [texture](RenderGraphBuilder& builder) {
        (void)builder.Read(texture, TextureUsage::ShaderRead);
    }, true);
    (void)graph.AddPass("ReadB", [texture](RenderGraphBuilder& builder) {
        (void)builder.Read(texture, TextureUsage::ShaderRead);
    }, true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    std::uint32_t shaderReadTransitions = 0;
    for (const auto& packet : compiled->BarrierPackets)
    {
        for (const auto& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex == texture.Index &&
                barrier.After == TextureBarrierState::ShaderRead)
            {
                ++shaderReadTransitions;
            }
        }
    }

    EXPECT_EQ(shaderReadTransitions, 1u);
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
        (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    const auto postPass = graph.AddPass("Post", [lighting, backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(lighting, TextureUsage::ShaderRead);
        (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    }, true);
    const auto presentPass = graph.AddPass("Present", [backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(backbuffer, TextureUsage::Present);
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

TEST(GraphicsRenderGraph, ExecuteInterleavesBarriersWithPassesAndEmitsFinalBarrierLast)
{
    RenderGraph graph;
    const auto lighting = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{21u, 3u});
    const auto lightingPass = graph.AddPass("Lighting", [lighting](RenderGraphBuilder& builder) {
        (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    const auto compositePass = graph.AddPass("Composite", [lighting, backbuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(lighting, TextureUsage::ShaderRead);
        (void)builder.Write(backbuffer, TextureUsage::ColorAttachmentWrite);
    }, true);

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    std::vector<std::string> events{};
    RenderGraphExecutor executor;
    auto result = executor.Execute(
        *compiled,
        [&events](const std::uint32_t passIndex) { events.push_back("pass(" + std::to_string(passIndex) + ")"); },
        [&events](const BarrierPacket& packet) { events.push_back("barrier(" + std::to_string(packet.PassIndex) + ")"); });

    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(compiled->TopologicalOrder.size(), 2u);
    EXPECT_EQ(lightingPass.Index, 0u);
    EXPECT_EQ(compositePass.Index, 1u);

    const std::vector<std::string> expected{
        "barrier(0)",
        "pass(0)",
        "barrier(1)",
        "pass(1)",
        "barrier(2)",
    };
    EXPECT_EQ(events, expected);
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

TEST(GraphicsRenderGraph, ExecuteFailsWhenResolveRequestsUndeclaredTexture)
{
    RenderGraph graph;
    const auto declared = graph.CreateTexture("Declared", Extrinsic::RHI::TextureDesc{});
    const auto undeclared = graph.CreateTexture("Undeclared", Extrinsic::RHI::TextureDesc{});
    (void)graph.AddPass("DeclaredOnly", [declared](RenderGraphBuilder& builder) {
        (void)builder.Read(declared, TextureUsage::ShaderRead);
        builder.SideEffect();
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    RenderGraphExecutor executor;
    const auto result = executor.Execute(
        *compiled,
        [undeclared](const CompiledPassDeclarations& declarations) { return declarations.RequireTextureRead(undeclared); },
        {},
        {});

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
}

TEST(GraphicsRenderGraph, DebugDumpContainsPassOrderAndResourceSections)
{
    RenderGraph graph;
    const auto texture = graph.CreateTexture("Lighting", Extrinsic::RHI::TextureDesc{});
    const auto buffer = graph.CreateBuffer("Args", Extrinsic::RHI::BufferDesc{.SizeBytes = 64u});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{91u, 1u});

    (void)graph.AddPass("Lighting", [texture, buffer](RenderGraphBuilder& builder) {
        (void)builder.Write(texture, TextureUsage::ColorAttachmentWrite);
        (void)builder.Write(buffer, BufferUsage::ShaderWrite);
    });
    (void)graph.AddPass("Present", [texture, backbuffer, buffer](RenderGraphBuilder& builder) {
        (void)builder.Read(texture, TextureUsage::ShaderRead);
        (void)builder.Read(buffer, BufferUsage::IndirectRead);
        (void)builder.Read(backbuffer, TextureUsage::Present);
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

TEST(GraphicsRenderGraph, TransientResourcesAllocateHandlesForUsedVirtualResources)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc textureDesc{};
    textureDesc.Width = 320;
    textureDesc.Height = 180;
    textureDesc.Fmt = Extrinsic::RHI::Format::RGBA8_UNORM;
    const auto texture = graph.CreateTexture("TransientColor", textureDesc);
    const auto buffer = graph.CreateBuffer("TransientArgs", Extrinsic::RHI::BufferDesc{.SizeBytes = 256u});
    const auto backbuffer = graph.ImportBackbuffer("Backbuffer", Extrinsic::RHI::TextureHandle{91u, 1u});

    (void)graph.AddPass("WriteTransient", [texture, buffer](RenderGraphBuilder& builder) {
        (void)builder.Write(texture, TextureUsage::ColorAttachmentWrite);
        (void)builder.Write(buffer, BufferUsage::ShaderWrite);
    });
    (void)graph.AddPass("Present", [texture, backbuffer, buffer](RenderGraphBuilder& builder) {
        (void)builder.Read(texture, TextureUsage::ShaderRead);
        (void)builder.Read(buffer, BufferUsage::IndirectRead);
        (void)builder.Read(backbuffer, TextureUsage::Present);
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_TRUE(compiled->TextureHandles[texture.Index].IsValid());
    EXPECT_TRUE(compiled->BufferHandles[buffer.Index].IsValid());
    EXPECT_EQ(compiled->TransientTextureCount, 1u);
    EXPECT_EQ(compiled->TransientBufferCount, 1u);
    EXPECT_GT(compiled->TransientMemoryEstimateBytes, 0u);
}

TEST(GraphicsRenderGraph, TransientPlacementReportsNaivePeakAndAliasHazards)
{
    RenderGraph graph;
    RHI::TextureDesc textureDesc{};
    textureDesc.Width = 16u;
    textureDesc.Height = 16u;
    textureDesc.Fmt = RHI::Format::RGBA8_UNORM;
    const auto firstTexture = graph.CreateTexture("FirstTexture", textureDesc);
    const auto secondTexture = graph.CreateTexture("SecondTexture", textureDesc);
    const auto firstBuffer = graph.CreateBuffer("FirstBuffer", RHI::BufferDesc{.SizeBytes = 384u});
    const auto secondBuffer = graph.CreateBuffer("SecondBuffer", RHI::BufferDesc{.SizeBytes = 384u});

    (void)graph.AddPass("UseFirst",
                        [firstTexture, firstBuffer](RenderGraphBuilder& builder) {
                            (void)builder.Write(firstTexture, TextureUsage::ColorAttachmentWrite);
                            (void)builder.Write(firstBuffer, BufferUsage::ShaderWrite);
                        },
                        true);
    (void)graph.AddPass("UseSecond",
                        [secondTexture, secondBuffer](RenderGraphBuilder& builder) {
                            (void)builder.Write(secondTexture, TextureUsage::ColorAttachmentWrite);
                            (void)builder.Write(secondBuffer, BufferUsage::ShaderWrite);
                        },
                        true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::uint64_t textureBytes = ExpectedTexturePlacementBytes(textureDesc);
    const std::uint64_t bufferBytes = ExpectedBufferPlacementBytes(384u);
    EXPECT_EQ(compiled->TransientNaiveMemoryEstimateBytes, (textureBytes + bufferBytes) * 2u);
    EXPECT_EQ(compiled->TransientPlacedPeakMemoryEstimateBytes, textureBytes + bufferBytes);
    EXPECT_EQ(compiled->TransientMemoryEstimateBytes, compiled->TransientPlacedPeakMemoryEstimateBytes);

    const TransientResourcePlacement* firstTexturePlacement = FindTexturePlacement(*compiled, firstTexture.Index);
    const TransientResourcePlacement* secondTexturePlacement = FindTexturePlacement(*compiled, secondTexture.Index);
    const TransientResourcePlacement* firstBufferPlacement = FindBufferPlacement(*compiled, firstBuffer.Index);
    const TransientResourcePlacement* secondBufferPlacement = FindBufferPlacement(*compiled, secondBuffer.Index);
    ASSERT_NE(firstTexturePlacement, nullptr);
    ASSERT_NE(secondTexturePlacement, nullptr);
    ASSERT_NE(firstBufferPlacement, nullptr);
    ASSERT_NE(secondBufferPlacement, nullptr);

    EXPECT_EQ(firstTexturePlacement->BlockIndex, secondTexturePlacement->BlockIndex);
    EXPECT_EQ(firstTexturePlacement->OffsetBytes, secondTexturePlacement->OffsetBytes);
    EXPECT_EQ(firstTexturePlacement->SizeBytes, textureBytes);
    EXPECT_EQ(firstTexturePlacement->AlignmentBytes, kExpectedPlacementAlignmentBytes);
    EXPECT_EQ(firstBufferPlacement->BlockIndex, secondBufferPlacement->BlockIndex);
    EXPECT_EQ(firstBufferPlacement->OffsetBytes, secondBufferPlacement->OffsetBytes);
    EXPECT_EQ(firstBufferPlacement->SizeBytes, bufferBytes);
    EXPECT_EQ(firstBufferPlacement->AlignmentBytes, kExpectedPlacementAlignmentBytes);

    EXPECT_EQ(CountTextureAliasReuseHazards(*compiled), 1u);
    EXPECT_EQ(CountBufferAliasReuseHazards(*compiled), 1u);

    const TextureAliasReuseBarrierPacket* textureHazard = nullptr;
    const BufferAliasReuseBarrierPacket* bufferHazard = nullptr;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        if (!packet.TextureAliasReuseBarriers.empty())
        {
            textureHazard = &packet.TextureAliasReuseBarriers.front();
            EXPECT_EQ(packet.PassIndex, 1u);
            EXPECT_EQ(packet.Stage, BarrierPacketStage::BeforePass);
        }
        if (!packet.BufferAliasReuseBarriers.empty())
        {
            bufferHazard = &packet.BufferAliasReuseBarriers.front();
            EXPECT_EQ(packet.PassIndex, 1u);
            EXPECT_EQ(packet.Stage, BarrierPacketStage::BeforePass);
        }
    }
    ASSERT_NE(textureHazard, nullptr);
    ASSERT_NE(bufferHazard, nullptr);
    EXPECT_EQ(textureHazard->PreviousTextureIndex, firstTexture.Index);
    EXPECT_EQ(textureHazard->TextureIndex, secondTexture.Index);
    EXPECT_EQ(textureHazard->BlockIndex, secondTexturePlacement->BlockIndex);
    EXPECT_EQ(textureHazard->OffsetBytes, secondTexturePlacement->OffsetBytes);
    EXPECT_EQ(textureHazard->SizeBytes, secondTexturePlacement->SizeBytes);
    EXPECT_EQ(bufferHazard->PreviousBufferIndex, firstBuffer.Index);
    EXPECT_EQ(bufferHazard->BufferIndex, secondBuffer.Index);
    EXPECT_EQ(bufferHazard->BlockIndex, secondBufferPlacement->BlockIndex);
    EXPECT_EQ(bufferHazard->OffsetBytes, secondBufferPlacement->OffsetBytes);
    EXPECT_EQ(bufferHazard->SizeBytes, secondBufferPlacement->SizeBytes);
}

TEST(GraphicsRenderGraph, TransientPlacementAliasingDisabledEqualsNaiveAndOmitsHazards)
{
    RenderGraph graph;
    graph.SetTransientAliasingEnabled(false);
    RHI::TextureDesc desc{};
    desc.Width = 16u;
    desc.Height = 16u;
    const auto first = graph.CreateTexture("First", desc);
    const auto second = graph.CreateTexture("Second", desc);

    (void)graph.AddPass("UseFirst",
                        [first](RenderGraphBuilder& builder) { (void)builder.Write(first, TextureUsage::ColorAttachmentWrite); },
                        true);
    (void)graph.AddPass("UseSecond",
                        [second](RenderGraphBuilder& builder) { (void)builder.Write(second, TextureUsage::ColorAttachmentWrite); },
                        true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->TransientPlacedPeakMemoryEstimateBytes, compiled->TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(compiled->TransientMemoryEstimateBytes, compiled->TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(CountTextureAliasReuseHazards(*compiled), 0u);
    EXPECT_EQ(CountBufferAliasReuseHazards(*compiled), 0u);

    const TransientResourcePlacement* firstPlacement = FindTexturePlacement(*compiled, first.Index);
    const TransientResourcePlacement* secondPlacement = FindTexturePlacement(*compiled, second.Index);
    ASSERT_NE(firstPlacement, nullptr);
    ASSERT_NE(secondPlacement, nullptr);
    EXPECT_EQ(firstPlacement->BlockIndex, secondPlacement->BlockIndex);
    EXPECT_FALSE(ByteRangesOverlap(*firstPlacement, *secondPlacement));
}

TEST(GraphicsRenderGraph, TransientPlacementDoesNotOverlapLiveRanges)
{
    struct Interval
    {
        TextureRef Ref{};
        std::uint32_t FirstUse = 0u;
        std::uint32_t LastUse = 0u;
    };

    for (std::uint32_t seed = 0u; seed < 16u; ++seed)
    {
        RenderGraph graph;
        RHI::TextureDesc desc{};
        desc.Width = 32u + seed;
        desc.Height = 16u;
        std::array<Interval, 8u> intervals{};
        for (std::uint32_t i = 0u; i < intervals.size(); ++i)
        {
            const std::uint32_t firstUse = (seed * 5u + i * 3u) % 7u;
            const std::uint32_t span = (seed + i * 2u) % 4u;
            intervals[i] = Interval{
                .Ref = graph.CreateTexture("Transient", desc),
                .FirstUse = firstUse,
                .LastUse = std::min<std::uint32_t>(7u, firstUse + span),
            };
        }

        for (std::uint32_t passIndex = 0u; passIndex < 8u; ++passIndex)
        {
            (void)graph.AddPass("PlacementPass" + std::to_string(passIndex),
                                [intervals, passIndex](RenderGraphBuilder& builder) {
                                    for (const Interval& interval : intervals)
                                    {
                                        if (interval.FirstUse == passIndex)
                                        {
                                            (void)builder.Write(interval.Ref, TextureUsage::ColorAttachmentWrite);
                                        }
                                        else if (interval.LastUse == passIndex)
                                        {
                                            (void)builder.Read(interval.Ref, TextureUsage::ShaderRead);
                                        }
                                    }
                                    builder.SideEffect();
                                });
        }

        const auto compiled = graph.Compile();
        ASSERT_TRUE(compiled.has_value()) << seed;
        EXPECT_LE(compiled->TransientPlacedPeakMemoryEstimateBytes,
                  compiled->TransientNaiveMemoryEstimateBytes) << seed;
        for (std::size_t lhsIndex = 0u; lhsIndex < compiled->TextureTransientPlacements.size(); ++lhsIndex)
        {
            for (std::size_t rhsIndex = lhsIndex + 1u; rhsIndex < compiled->TextureTransientPlacements.size(); ++rhsIndex)
            {
                const TransientResourcePlacement& lhs = compiled->TextureTransientPlacements[lhsIndex];
                const TransientResourcePlacement& rhs = compiled->TextureTransientPlacements[rhsIndex];
                if (lhs.BlockIndex != rhs.BlockIndex || !ByteRangesOverlap(lhs, rhs))
                {
                    continue;
                }
                ASSERT_LT(lhs.ResourceIndex, compiled->TextureLifetimes.size());
                ASSERT_LT(rhs.ResourceIndex, compiled->TextureLifetimes.size());
                EXPECT_FALSE(LifetimesOverlap(compiled->TextureLifetimes[lhs.ResourceIndex],
                                              compiled->TextureLifetimes[rhs.ResourceIndex]))
                    << "seed=" << seed << " lhs=" << lhs.ResourceIndex << " rhs=" << rhs.ResourceIndex;
            }
        }
    }
}

TEST(GraphicsRenderGraph, TransientPlacementIsDeterministicForFixedGraph)
{
    auto buildGraph = [] {
        RenderGraph graph;
        RHI::TextureDesc desc{};
        desc.Width = 48u;
        desc.Height = 32u;
        const auto a = graph.CreateTexture("A", desc);
        const auto b = graph.CreateTexture("B", desc);
        const auto c = graph.CreateTexture("C", desc);
        const auto d = graph.CreateTexture("D", desc);
        const auto firstBuffer = graph.CreateBuffer("FirstBuffer", RHI::BufferDesc{.SizeBytes = 1024u});
        const auto secondBuffer = graph.CreateBuffer("SecondBuffer", RHI::BufferDesc{.SizeBytes = 512u});

        (void)graph.AddPass("WriteA",
                            [a, firstBuffer](RenderGraphBuilder& builder) {
                                (void)builder.Write(a, TextureUsage::ColorAttachmentWrite);
                                (void)builder.Write(firstBuffer, BufferUsage::ShaderWrite);
                            },
                            true);
        (void)graph.AddPass("WriteB",
                            [b](RenderGraphBuilder& builder) {
                                (void)builder.Write(b, TextureUsage::ColorAttachmentWrite);
                            },
                            true);
        (void)graph.AddPass("ReadAWriteC",
                            [a, c](RenderGraphBuilder& builder) {
                                (void)builder.Read(a, TextureUsage::ShaderRead);
                                (void)builder.Write(c, TextureUsage::ColorAttachmentWrite);
                            },
                            true);
        (void)graph.AddPass("WriteDAndSecondBuffer",
                            [d, secondBuffer](RenderGraphBuilder& builder) {
                                (void)builder.Write(d, TextureUsage::ColorAttachmentWrite);
                                (void)builder.Write(secondBuffer, BufferUsage::ShaderWrite);
                            },
                            true);
        return graph.Compile();
    };

    const auto first = buildGraph();
    const auto second = buildGraph();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->TransientNaiveMemoryEstimateBytes, second->TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(first->TransientPlacedPeakMemoryEstimateBytes, second->TransientPlacedPeakMemoryEstimateBytes);
    ExpectPlacementVectorsEqual(first->TextureTransientPlacements, second->TextureTransientPlacements);
    ExpectPlacementVectorsEqual(first->BufferTransientPlacements, second->BufferTransientPlacements);
    ExpectAliasHazardsEqual(*first, *second);
}

TEST(GraphicsRenderGraph, TransientAllocatorReusesCompatibleHandlesAcrossFrames)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc desc{};
    desc.Width = 128;
    desc.Height = 128;
    const auto a = graph.CreateTexture("A", desc);
    (void)graph.AddPass("UseA", [a](RenderGraphBuilder& builder) { (void)builder.Write(a, TextureUsage::ColorAttachmentWrite); }, true);

    auto first = graph.Compile();
    ASSERT_TRUE(first.has_value());
    const auto firstHandle = first->TextureHandles[a.Index];
    ASSERT_TRUE(firstHandle.IsValid());

    graph.Reset();
    const auto b = graph.CreateTexture("B", desc);
    (void)graph.AddPass("UseB", [b](RenderGraphBuilder& builder) { (void)builder.Write(b, TextureUsage::ColorAttachmentWrite); }, true);
    auto second = graph.Compile();
    ASSERT_TRUE(second.has_value());
    const auto secondHandle = second->TextureHandles[b.Index];
    EXPECT_EQ(firstHandle.Index, secondHandle.Index);
    EXPECT_EQ(firstHandle.Generation, secondHandle.Generation);
}

TEST(GraphicsRenderGraph, ImportedResourcesNeverCountAsTransientAllocations)
{
    RenderGraph graph;
    const auto importedTexture = graph.ImportTexture(
        "History", Extrinsic::RHI::TextureHandle{5u, 1u}, TextureState::ShaderRead, TextureState::ShaderRead);
    const auto importedBuffer =
        graph.ImportBuffer("Readback", Extrinsic::RHI::BufferHandle{7u, 1u}, BufferState::TransferDst, BufferState::HostReadback);
    (void)graph.AddPass("UseImported", [importedTexture, importedBuffer](RenderGraphBuilder& builder) {
        (void)builder.Read(importedTexture, TextureUsage::ShaderRead);
        (void)builder.Read(importedBuffer, BufferUsage::HostReadback);
        builder.SideEffect();
    });

    auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_EQ(compiled->TransientTextureCount, 0u);
    EXPECT_EQ(compiled->TransientBufferCount, 0u);
}

TEST(GraphicsRenderGraph, IncompatibleTransientDescriptorAllocatesNewHandle)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc small{};
    small.Width = 64;
    small.Height = 64;
    const auto firstTexture = graph.CreateTexture("Small", small);
    (void)graph.AddPass("UseSmall",
                  [firstTexture](RenderGraphBuilder& builder) { (void)builder.Write(firstTexture, TextureUsage::ColorAttachmentWrite); },
                  true);
    auto first = graph.Compile();
    ASSERT_TRUE(first.has_value());
    const auto firstHandle = first->TextureHandles[firstTexture.Index];
    ASSERT_TRUE(firstHandle.IsValid());

    graph.Reset();
    Extrinsic::RHI::TextureDesc large{};
    large.Width = 1920;
    large.Height = 1080;
    const auto secondTexture = graph.CreateTexture("Large", large);
    (void)graph.AddPass("UseLarge",
                  [secondTexture](RenderGraphBuilder& builder) { (void)builder.Write(secondTexture, TextureUsage::ColorAttachmentWrite); },
                  true);
    auto second = graph.Compile();
    ASSERT_TRUE(second.has_value());
    const auto secondHandle = second->TextureHandles[secondTexture.Index];
    ASSERT_TRUE(secondHandle.IsValid());
    EXPECT_NE(firstHandle.Index, secondHandle.Index);
}

TEST(GraphicsRenderGraph, NonOverlappingCompatibleTransientTexturesAliasWithinFrame)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc desc{};
    desc.Width = 256;
    desc.Height = 256;
    const auto first = graph.CreateTexture("First", desc);
    const auto second = graph.CreateTexture("Second", desc);
    (void)graph.AddPass("WriteFirst",
                  [first](RenderGraphBuilder& builder) { (void)builder.Write(first, TextureUsage::ColorAttachmentWrite); },
                  true);
    (void)graph.AddPass("WriteSecond",
                  [second](RenderGraphBuilder& builder) { (void)builder.Write(second, TextureUsage::ColorAttachmentWrite); },
                  true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    const auto firstHandle = compiled->TextureHandles[first.Index];
    const auto secondHandle = compiled->TextureHandles[second.Index];
    ASSERT_TRUE(firstHandle.IsValid());
    ASSERT_TRUE(secondHandle.IsValid());
    EXPECT_EQ(firstHandle.Index, secondHandle.Index);
}

TEST(GraphicsRenderGraph, OverlappingCompatibleTransientTexturesDoNotAlias)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc desc{};
    desc.Width = 256;
    desc.Height = 256;
    const auto producer = graph.CreateTexture("Producer", desc);
    const auto consumer = graph.CreateTexture("Consumer", desc);
    (void)graph.AddPass("WriteProducer",
                  [producer](RenderGraphBuilder& builder) { (void)builder.Write(producer, TextureUsage::ColorAttachmentWrite); },
                  false);
    (void)graph.AddPass("UseBoth", [producer, consumer](RenderGraphBuilder& builder) {
        (void)builder.Read(producer, TextureUsage::ShaderRead);
        (void)builder.Write(consumer, TextureUsage::ColorAttachmentWrite);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    const auto producerHandle = compiled->TextureHandles[producer.Index];
    const auto consumerHandle = compiled->TextureHandles[consumer.Index];
    ASSERT_TRUE(producerHandle.IsValid());
    ASSERT_TRUE(consumerHandle.IsValid());
    EXPECT_NE(producerHandle.Index, consumerHandle.Index);
}

TEST(GraphicsRenderGraph, NonCompatibleTransientTexturesDoNotAliasEvenWhenNonOverlapping)
{
    RenderGraph graph;
    Extrinsic::RHI::TextureDesc small{};
    small.Width = 128;
    small.Height = 128;
    Extrinsic::RHI::TextureDesc large{};
    large.Width = 1920;
    large.Height = 1080;
    const auto first = graph.CreateTexture("Small", small);
    const auto second = graph.CreateTexture("Large", large);
    (void)graph.AddPass("WriteSmall",
                  [first](RenderGraphBuilder& builder) { (void)builder.Write(first, TextureUsage::ColorAttachmentWrite); },
                  true);
    (void)graph.AddPass("WriteLarge",
                  [second](RenderGraphBuilder& builder) { (void)builder.Write(second, TextureUsage::ColorAttachmentWrite); },
                  true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    const auto firstHandle = compiled->TextureHandles[first.Index];
    const auto secondHandle = compiled->TextureHandles[second.Index];
    ASSERT_TRUE(firstHandle.IsValid());
    ASSERT_TRUE(secondHandle.IsValid());
    EXPECT_NE(firstHandle.Index, secondHandle.Index);
}

TEST(GraphicsRenderGraph, ImportedTextureNeverAliasesTransientTexture)
{
    RenderGraph graph;
    const auto imported = graph.ImportTexture(
        "History", Extrinsic::RHI::TextureHandle{41u, 3u}, TextureState::ShaderRead, TextureState::ShaderRead);
    Extrinsic::RHI::TextureDesc transientDesc{};
    transientDesc.Width = 64;
    transientDesc.Height = 64;
    const auto transient = graph.CreateTexture("Transient", transientDesc);
    (void)graph.AddPass("UseImported", [imported](RenderGraphBuilder& builder) {
        (void)builder.Read(imported, TextureUsage::ShaderRead);
        builder.SideEffect();
    });
    (void)graph.AddPass("UseTransient", [transient](RenderGraphBuilder& builder) {
        (void)builder.Write(transient, TextureUsage::ColorAttachmentWrite);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    const auto importedHandle = compiled->TextureHandles[imported.Index];
    const auto transientHandle = compiled->TextureHandles[transient.Index];
    ASSERT_TRUE(importedHandle.IsValid());
    ASSERT_TRUE(transientHandle.IsValid());
    EXPECT_NE(importedHandle.Index, transientHandle.Index);
}

TEST(GraphicsRenderGraph, TransientAliasingTogglePreservesLogicalPassOrderAndBarriers)
{
    auto buildGraph = [](const bool aliasingEnabled) {
        RenderGraph graph;
        graph.SetTransientAliasingEnabled(aliasingEnabled);
        Extrinsic::RHI::TextureDesc desc{};
        desc.Width = 320;
        desc.Height = 180;
        const auto a = graph.CreateTexture("A", desc);
        const auto b = graph.CreateTexture("B", desc);
        (void)graph.AddPass("WriteA", [a](RenderGraphBuilder& builder) {
            (void)builder.Write(a, TextureUsage::ColorAttachmentWrite);
            builder.SideEffect();
        });
        (void)graph.AddPass("ReadAWriteB", [a, b](RenderGraphBuilder& builder) {
            (void)builder.Read(a, TextureUsage::ShaderRead);
            (void)builder.Write(b, TextureUsage::ColorAttachmentWrite);
            builder.SideEffect();
        });
        return graph.Compile();
    };

    const auto withAliasing = buildGraph(true);
    const auto withoutAliasing = buildGraph(false);
    ASSERT_TRUE(withAliasing.has_value());
    ASSERT_TRUE(withoutAliasing.has_value());
    EXPECT_EQ(withAliasing->TopologicalOrder, withoutAliasing->TopologicalOrder);
    EXPECT_EQ(CountRegularBarrierTransitions(*withAliasing), CountRegularBarrierTransitions(*withoutAliasing));
}
