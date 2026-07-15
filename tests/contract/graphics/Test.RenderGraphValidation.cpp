#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <sstream>
#include <string>
#include <vector>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Core.Error;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    inline constexpr std::array<RHI::ColorAttachment, 1u> kCompilerScratchColorAttachments{
        RHI::ColorAttachment{.Load = RHI::LoadOp::Load, .Store = RHI::StoreOp::Store},
    };

    [[nodiscard]] CompiledRenderGraph MakeCompiled(std::uint32_t passCount,
                                                   std::uint32_t textureCount,
                                                   std::uint32_t bufferCount)
    {
        CompiledRenderGraph compiled{};
        compiled.PassCount = passCount;
        compiled.ResourceCount = textureCount + bufferCount;
        compiled.PassNames.resize(passCount);
        compiled.PassSideEffects.resize(passCount, false);
        compiled.PassDeclarations.resize(passCount);
        for (std::uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            compiled.TopologicalOrder.push_back(passIndex);
            compiled.PassDeclarations[passIndex].PassIndex = passIndex;
        }

        compiled.TextureNames.resize(textureCount);
        compiled.TextureLifetimes.resize(textureCount);
        compiled.TextureInitialStates.resize(textureCount, TextureState::Undefined);
        compiled.TextureFinalStates.resize(textureCount, TextureState::Undefined);
        compiled.TextureImported.resize(textureCount, false);
        compiled.TextureImportedWriteAllowed.resize(textureCount, false);
        compiled.TextureIsBackbuffer.resize(textureCount, false);

        compiled.BufferNames.resize(bufferCount);
        compiled.BufferLifetimes.resize(bufferCount);
        compiled.BufferInitialStates.resize(bufferCount, BufferState::Undefined);
        compiled.BufferFinalStates.resize(bufferCount, BufferState::Undefined);
        compiled.BufferImported.resize(bufferCount, false);
        return compiled;
    }

    [[nodiscard]] std::vector<RenderGraphValidationFinding> FindingsByCode(
        const RenderGraphValidationResult& result,
        RenderGraphValidationCode code)
    {
        std::vector<RenderGraphValidationFinding> matches{};
        for (const RenderGraphValidationFinding& finding : result.Findings)
        {
            if (finding.Code == code)
            {
                matches.push_back(finding);
            }
        }
        return matches;
    }

    [[nodiscard]] constexpr std::uint64_t AlignUpForTransientPlacement(
        const std::uint64_t value,
        const std::uint64_t alignment) noexcept
    {
        const std::uint64_t remainder = value % alignment;
        return remainder == 0u ? value : value + (alignment - remainder);
    }

    void DeclareCompilerScratchFixture(RenderGraph& graph)
    {
        RHI::TextureDesc textureDesc{};
        textureDesc.Width = 128u;
        textureDesc.Height = 64u;
        textureDesc.Usage = RHI::TextureUsage::Sampled |
                            RHI::TextureUsage::Storage |
                            RHI::TextureUsage::ColorTarget;

        RHI::BufferDesc bufferDesc{};
        bufferDesc.SizeBytes = 4096u;
        bufferDesc.Usage = RHI::BufferUsage::Storage |
                           RHI::BufferUsage::TransferSrc |
                           RHI::BufferUsage::TransferDst;

        const TextureRef scene = graph.CreateTexture("Scene", textureDesc);
        const TextureRef intermediate = graph.CreateTexture("Intermediate", textureDesc);
        const BufferRef scratch = graph.CreateBuffer("Scratch", bufferDesc);

        const PassRef seed = graph.AddPass("Seed", [scene, scratch](RenderGraphBuilder& builder) {
            (void)builder.Write(scene, TextureUsage::ShaderWrite);
            (void)builder.Write(scratch, BufferUsage::TransferDst);
            builder.SideEffect();
        });

        const PassRef async = graph.AddPass("AsyncFilter", [seed, scene, intermediate](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::AsyncCompute);
            builder.DependsOn(seed);
            (void)builder.Read(scene, TextureUsage::ShaderRead);
            (void)builder.Write(intermediate, TextureUsage::ShaderWrite);
        });

        const PassRef transfer = graph.AddPass("TransferReadback", [seed, scratch](RenderGraphBuilder& builder) {
            builder.SetQueue(RenderQueue::Transfer);
            builder.DependsOn(seed);
            (void)builder.Read(scratch, BufferUsage::TransferSrc);
            builder.SideEffect();
        });

        (void)graph.AddPass("Composite", [async, transfer, scene, intermediate](RenderGraphBuilder& builder) {
            builder.DependsOn(async);
            builder.DependsOn(transfer);
            (void)builder.Read(intermediate, TextureUsage::ShaderRead);
            (void)builder.Write(scene, TextureUsage::ColorAttachmentWrite);
            builder.SideEffect();

            RHI::RenderPassDesc desc{};
            desc.ColorTargets = kCompilerScratchColorAttachments;
            builder.SetRenderPass(desc);
        });
    }

    [[nodiscard]] std::string CompiledGraphSignature(const CompiledRenderGraph& compiled)
    {
        std::ostringstream out;
        out << "counts " << compiled.PassCount << ' ' << compiled.CulledPassCount << ' '
            << compiled.ResourceCount << ' ' << compiled.EdgeCount << ' '
            << compiled.QueueHandoffEdgeCount << ' ' << compiled.CrossQueueTimelineEdgeCount << ' '
            << compiled.CrossQueueOwnershipTransferCount << '\n';

        for (std::size_t i = 0u; i < compiled.TopologicalOrder.size(); ++i)
        {
            out << "order " << i << ' ' << compiled.TopologicalOrder[i] << '\n';
        }
        for (std::size_t i = 0u; i < compiled.TopologicalLayerByPass.size(); ++i)
        {
            out << "layer " << i << ' ' << compiled.TopologicalLayerByPass[i] << '\n';
        }
        for (std::size_t i = 0u; i < compiled.PassNames.size(); ++i)
        {
            out << "pass " << i << ' ' << compiled.PassNames[i] << ' '
                << static_cast<int>(compiled.PassQueues[i]) << ' '
                << compiled.PassSideEffects[i] << '\n';
        }
        for (const CompiledPassDeclarations& declaration : compiled.PassDeclarations)
        {
            out << "decl " << declaration.PassIndex;
            for (const std::uint32_t pass : declaration.ExplicitDependencyPasses)
            {
                out << " dep=" << pass;
            }
            for (const std::uint32_t texture : declaration.ReadTextures)
            {
                out << " rt=" << texture;
            }
            for (const std::uint32_t texture : declaration.WriteTextures)
            {
                out << " wt=" << texture;
            }
            for (const std::uint32_t buffer : declaration.ReadBuffers)
            {
                out << " rb=" << buffer;
            }
            for (const std::uint32_t buffer : declaration.WriteBuffers)
            {
                out << " wb=" << buffer;
            }
            out << '\n';
        }
        for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
        {
            out << "attachment " << attachment.PassIndex << ' ' << attachment.ResourceIndex << ' '
                << attachment.AttachmentIndex << ' ' << attachment.IsDepthAttachment << ' '
                << static_cast<int>(attachment.Load) << ' ' << static_cast<int>(attachment.Store) << ' '
                << static_cast<int>(attachment.Format) << '\n';
        }
        for (const BarrierPacket& packet : compiled.BarrierPackets)
        {
            out << "packet " << static_cast<int>(packet.Kind) << ' ' << packet.PassIndex << ' '
                << static_cast<int>(packet.Stage) << '\n';
            for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
            {
                out << " tb " << barrier.TextureIndex << ' ' << static_cast<int>(barrier.Before) << ' '
                    << static_cast<int>(barrier.After) << ' ' << static_cast<int>(barrier.SharingMode) << ' '
                    << static_cast<int>(barrier.OwnershipTransfer.Kind) << ' '
                    << static_cast<int>(barrier.OwnershipTransfer.SourceQueue) << ' '
                    << static_cast<int>(barrier.OwnershipTransfer.DestinationQueue) << '\n';
            }
            for (const BufferBarrierPacket& barrier : packet.BufferBarriers)
            {
                out << " bb " << barrier.BufferIndex << ' ' << static_cast<int>(barrier.Before) << ' '
                    << static_cast<int>(barrier.After) << ' ' << static_cast<int>(barrier.SharingMode) << ' '
                    << static_cast<int>(barrier.OwnershipTransfer.Kind) << ' '
                    << static_cast<int>(barrier.OwnershipTransfer.SourceQueue) << ' '
                    << static_cast<int>(barrier.OwnershipTransfer.DestinationQueue) << '\n';
            }
        }
        for (const CrossQueueTimelineEdge& edge : compiled.CrossQueueTimelineEdges)
        {
            out << "timeline " << edge.SignalPassIndex << ' ' << edge.WaitPassIndex << ' '
                << static_cast<int>(edge.SignalQueue) << ' ' << static_cast<int>(edge.WaitQueue) << ' '
                << edge.Value << '\n';
        }
        return out.str();
    }
}

TEST(RenderGraphValidation, EmptyGraphHasNoFindings)
{
    const CompiledRenderGraph compiled{};

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    EXPECT_TRUE(result.Findings.empty());
    EXPECT_FALSE(result.HasErrors());
    EXPECT_FALSE(result.HasWarnings());
    EXPECT_EQ(result.CountBySeverity(RenderGraphValidationSeverity::Info), 0u);
}

TEST(RenderGraphValidation, LoadWithoutEarlierWriterReportsWarning)
{
    CompiledRenderGraph compiled = MakeCompiled(1u, 1u, 0u);
    compiled.PassNames[0] = "LoadColor";
    compiled.TextureNames[0] = "SceneColor";
    compiled.TextureLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 0u};
    compiled.PassDeclarations[0].WriteTextures = {0u};
    compiled.RenderPassAttachments = {
        CompiledRenderPassAttachment{.PassIndex = 0u, .ResourceIndex = 0u, .IsTextureResource = true, .Load = Extrinsic::RHI::LoadOp::Load},
    };

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    ASSERT_EQ(result.Findings.size(), 1u);
    const RenderGraphValidationFinding& finding = result.Findings.front();
    EXPECT_EQ(finding.Severity, RenderGraphValidationSeverity::Warning);
    EXPECT_EQ(finding.Code, RenderGraphValidationCode::LoadWithoutGuaranteedWriter);
    EXPECT_EQ(finding.PassIndex, 0u);
    EXPECT_EQ(finding.PassName, "LoadColor");
    EXPECT_EQ(finding.ResourceIndex, 0u);
    EXPECT_TRUE(finding.IsTextureResource);
    EXPECT_EQ(finding.ResourceName, "SceneColor");
    EXPECT_FALSE(result.HasErrors());
    EXPECT_TRUE(result.HasWarnings());
}

TEST(RenderGraphValidation, TransientBufferReadWithoutProducerReportsError)
{
    CompiledRenderGraph compiled = MakeCompiled(1u, 0u, 1u);
    compiled.PassNames[0] = "ReadArgs";
    compiled.BufferNames[0] = "TransientArgs";
    compiled.BufferLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 0u};
    compiled.PassDeclarations[0].ReadBuffers = {0u};

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    ASSERT_EQ(result.Findings.size(), 1u);
    const RenderGraphValidationFinding& finding = result.Findings.front();
    EXPECT_EQ(finding.Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(finding.Code, RenderGraphValidationCode::TransientBufferWithoutProducer);
    EXPECT_EQ(finding.ResourceIndex, 0u);
    EXPECT_FALSE(finding.IsTextureResource);
    EXPECT_EQ(finding.ResourceName, "TransientArgs");
    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.CountBySeverity(RenderGraphValidationSeverity::Error), 1u);
}

TEST(RenderGraphValidation, TextureReadBeforeProducerReportsMissingProducer)
{
    CompiledRenderGraph compiled = MakeCompiled(2u, 1u, 0u);
    compiled.PassNames[0] = "SampleBeforeWrite";
    compiled.PassNames[1] = "LateProducer";
    compiled.TextureNames[0] = "History";
    compiled.TextureLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 1u};
    compiled.PassDeclarations[0].ReadTextures = {0u};
    compiled.PassDeclarations[1].WriteTextures = {0u};

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    ASSERT_EQ(result.Findings.size(), 1u);
    const RenderGraphValidationFinding& finding = result.Findings.front();
    EXPECT_EQ(finding.Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(finding.Code, RenderGraphValidationCode::MissingTextureProducer);
    EXPECT_EQ(finding.PassIndex, 0u);
    EXPECT_EQ(finding.PassName, "SampleBeforeWrite");
    EXPECT_EQ(finding.ResourceIndex, 0u);
    EXPECT_EQ(finding.ResourceName, "History");
}

TEST(RenderGraphValidation, BufferReadBeforeProducerReportsMissingProducer)
{
    CompiledRenderGraph compiled = MakeCompiled(2u, 0u, 1u);
    compiled.PassNames[0] = "ReadBeforeWrite";
    compiled.PassNames[1] = "WriteArgs";
    compiled.BufferNames[0] = "DrawArgs";
    compiled.BufferLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 1u};
    compiled.PassDeclarations[0].ReadBuffers = {0u};
    compiled.PassDeclarations[1].WriteBuffers = {0u};

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::MissingBufferProducer);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(findings.front().PassIndex, 0u);
    EXPECT_EQ(findings.front().ResourceIndex, 0u);
    EXPECT_FALSE(findings.front().IsTextureResource);
    EXPECT_EQ(findings.front().ResourceName, "DrawArgs");
}

TEST(RenderGraphValidation, TransientTextureReadWithoutProducerReportsError)
{
    CompiledRenderGraph compiled = MakeCompiled(1u, 1u, 0u);
    compiled.PassNames[0] = "SampleUnwritten";
    compiled.TextureNames[0] = "UnwrittenTexture";
    compiled.TextureLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 0u};
    compiled.PassDeclarations[0].ReadTextures = {0u};

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::TransientTextureWithoutProducer);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(findings.front().ResourceIndex, 0u);
    EXPECT_TRUE(findings.front().IsTextureResource);
    EXPECT_EQ(findings.front().ResourceName, "UnwrittenTexture");
}

TEST(RenderGraphValidation, ImportedBackbufferNonFinalizerWriteReportsError)
{
    CompiledRenderGraph compiled = MakeCompiled(2u, 1u, 0u);
    compiled.PassNames[0] = "EarlyComposite";
    compiled.PassNames[1] = "Present";
    compiled.PassSideEffects[1] = true;
    compiled.TextureNames[0] = "Backbuffer";
    compiled.TextureImported[0] = true;
    compiled.TextureIsBackbuffer[0] = true;
    compiled.TextureFinalStates[0] = TextureState::Present;
    compiled.TextureLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 1u};
    compiled.PassDeclarations[0].WriteTextures = {0u};
    compiled.PassDeclarations[1].WriteTextures = {0u};

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    ASSERT_EQ(result.Findings.size(), 1u);
    const RenderGraphValidationFinding& finding = result.Findings.front();
    EXPECT_EQ(finding.Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(finding.Code, RenderGraphValidationCode::BackbufferWrittenByNonFinalizer);
    EXPECT_EQ(finding.PassIndex, 0u);
    EXPECT_EQ(finding.PassName, "EarlyComposite");
    EXPECT_EQ(finding.ResourceIndex, 0u);
    EXPECT_EQ(finding.ResourceName, "Backbuffer");
}

TEST(RenderGraphValidation, ImportedTextureAuthorizationListControlsWriters)
{
    CompiledRenderGraph allowed = MakeCompiled(1u, 1u, 0u);
    allowed.PassNames[0] = "AllowedPass";
    allowed.TextureNames[0] = "History";
    allowed.TextureImported[0] = true;
    allowed.PassDeclarations[0].WriteTextures = {0u};
    const ImportedResourceAuthorization allowHistory{
        .ResourceIndex = 0u,
        .IsTexture = true,
        .Policy = ImportedResourceWritePolicy::AllowFinalizerOnly,
        .AuthorizedWriterPassNames = {"AllowedPass"},
    };

    EXPECT_TRUE(ValidateCompiledGraph(allowed, std::span<const ImportedResourceAuthorization>(&allowHistory, 1u)).Findings.empty());

    CompiledRenderGraph denied = allowed;
    denied.PassNames[0] = "DeniedPass";

    const RenderGraphValidationResult deniedResult = ValidateCompiledGraph(
        denied,
        std::span<const ImportedResourceAuthorization>(&allowHistory, 1u));

    ASSERT_EQ(deniedResult.Findings.size(), 1u);
    const RenderGraphValidationFinding& finding = deniedResult.Findings.front();
    EXPECT_EQ(finding.Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(finding.Code, RenderGraphValidationCode::UnauthorizedImportedTextureWrite);
    EXPECT_EQ(finding.PassIndex, 0u);
    EXPECT_EQ(finding.PassName, "DeniedPass");
    EXPECT_EQ(finding.ResourceIndex, 0u);
    EXPECT_EQ(finding.ResourceName, "History");
}

TEST(RenderGraphValidation, ShaderReadBoundaryImportRejectsWriteWithoutAuthorization)
{
    RenderGraph graph;
    const TextureRef imported = graph.ImportTexture(
        "ReadOnlyHistory",
        RHI::TextureHandle{3u, 1u},
        TextureState::ShaderRead,
        TextureState::ShaderRead);
    const TextureResourceDesc* desc = graph.GetTextureDesc(imported);
    ASSERT_NE(desc, nullptr);
    EXPECT_FALSE(desc->ImportedWriteAllowed);

    (void)graph.AddPass("WriteImported", [imported](RenderGraphBuilder& builder) {
        EXPECT_FALSE(builder.Write(imported, TextureUsage::ColorAttachmentWrite).IsValid());
    });

    EXPECT_FALSE(graph.Compile().has_value());
}

TEST(RenderGraphValidation, AuthorizedImportedWritePreservesShaderReadBoundaryStates)
{
    RenderGraph graph;
    const TextureRef imported = graph.ImportTexture(
        "RetainedTarget",
        RHI::TextureHandle{4u, 1u},
        TextureState::ShaderRead,
        TextureState::ShaderRead,
        true);
    const TextureResourceDesc* desc = graph.GetTextureDesc(imported);
    ASSERT_NE(desc, nullptr);
    EXPECT_TRUE(desc->ImportedWriteAllowed);

    (void)graph.AddPass("WriteRetainedTarget", [imported](RenderGraphBuilder& builder) {
        EXPECT_TRUE(builder.Write(imported, TextureUsage::ColorAttachmentWrite).IsValid());
    });
    (void)graph.AddPass("SampleRetainedTarget", [imported](RenderGraphBuilder& builder) {
        EXPECT_TRUE(builder.Read(imported, TextureUsage::ShaderRead).IsValid());
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_TRUE(compiled->ValidationFindings.empty());
    ASSERT_LT(imported.Index, compiled->TextureImportedWriteAllowed.size());
    EXPECT_TRUE(compiled->TextureImportedWriteAllowed[imported.Index]);
    ASSERT_LT(imported.Index, compiled->TextureInitialStates.size());
    ASSERT_LT(imported.Index, compiled->TextureFinalStates.size());
    EXPECT_EQ(compiled->TextureInitialStates[imported.Index], TextureState::ShaderRead);
    EXPECT_EQ(compiled->TextureFinalStates[imported.Index], TextureState::ShaderRead);

    bool sawWriteTransition = false;
    bool sawReadTransition = false;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex != imported.Index)
            {
                continue;
            }
            sawWriteTransition = sawWriteTransition ||
                (barrier.Before == TextureBarrierState::ShaderRead &&
                 barrier.After == TextureBarrierState::ColorAttachmentWrite);
            sawReadTransition = sawReadTransition ||
                (barrier.Before == TextureBarrierState::ColorAttachmentWrite &&
                 barrier.After == TextureBarrierState::ShaderRead);
        }
    }
    EXPECT_TRUE(sawWriteTransition);
    EXPECT_TRUE(sawReadTransition);
}

TEST(RenderGraphValidation, ImportedBufferAuthorizationListControlsWriters)
{
    CompiledRenderGraph compiled = MakeCompiled(1u, 0u, 1u);
    compiled.PassNames[0] = "UnauthorizedCull";
    compiled.BufferNames[0] = "ImportedArgs";
    compiled.BufferImported[0] = true;
    compiled.PassDeclarations[0].WriteBuffers = {0u};

    const ImportedResourceAuthorization disallowArgs{
        .ResourceIndex = 0u,
        .IsTexture = false,
        .Policy = ImportedResourceWritePolicy::Disallow,
    };

    const RenderGraphValidationResult result = ValidateCompiledGraph(
        compiled,
        std::span<const ImportedResourceAuthorization>(&disallowArgs, 1u));

    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::UnauthorizedImportedBufferWrite);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(findings.front().PassIndex, 0u);
    EXPECT_EQ(findings.front().ResourceIndex, 0u);
    EXPECT_FALSE(findings.front().IsTextureResource);
    EXPECT_EQ(findings.front().ResourceName, "ImportedArgs");
}

TEST(RenderGraphValidation, ImportedBackbufferFinalStateMismatchReportsError)
{
    CompiledRenderGraph compiled = MakeCompiled(0u, 1u, 0u);
    compiled.TextureNames[0] = "Backbuffer";
    compiled.TextureImported[0] = true;
    compiled.TextureIsBackbuffer[0] = true;
    compiled.TextureFinalStates[0] = TextureState::ShaderRead;

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::ImportedTextureFinalStateMismatch);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(findings.front().ResourceIndex, 0u);
    EXPECT_EQ(findings.front().ResourceName, "Backbuffer");
}

TEST(RenderGraphValidation, CompileInvalidExplicitDependencyReportsStructuredFinding)
{
    std::vector<RenderPassRecord> passes{
        RenderPassRecord{.Name = "InvalidDependency", .SideEffect = true, .ExplicitDependencies = {PassRef{.Index = 42u, .Generation = 1u}}},
    };

    RenderGraphValidationResult result{};
    const auto compiled = RenderGraphCompiler::Compile(passes, {}, {}, &result);

    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::InvalidExplicitDependency);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(findings.front().PassIndex, 0u);
    EXPECT_EQ(findings.front().PassName, "InvalidDependency");
    EXPECT_NE(findings.front().Message.find("InvalidDependency"), std::string::npos);
}

TEST(RenderGraphValidation, CompileInvalidResourceAccessReportsStructuredFindings)
{
    {
        std::vector<RenderPassRecord> passes{
            RenderPassRecord{
                .Name = "InvalidTexture",
                .SideEffect = true,
                .TextureAccesses = {TextureAccess{.Ref = TextureRef{.Index = 7u, .Generation = 1u}, .Usage = TextureUsage::ShaderRead}},
            },
        };

        RenderGraphValidationResult result{};
        const auto compiled = RenderGraphCompiler::Compile(passes, {}, {}, &result);

        ASSERT_FALSE(compiled.has_value());
        const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::InvalidTextureAccess);
        ASSERT_EQ(findings.size(), 1u);
        EXPECT_EQ(findings.front().PassName, "InvalidTexture");
        EXPECT_EQ(findings.front().ResourceIndex, 7u);
        EXPECT_TRUE(findings.front().IsTextureResource);
    }

    {
        std::vector<RenderPassRecord> passes{
            RenderPassRecord{
                .Name = "InvalidBuffer",
                .SideEffect = true,
                .BufferAccesses = {BufferAccess{.Ref = BufferRef{.Index = 9u, .Generation = 1u}, .Usage = BufferUsage::ShaderRead}},
            },
        };

        RenderGraphValidationResult result{};
        const auto compiled = RenderGraphCompiler::Compile(passes, {}, {}, &result);

        ASSERT_FALSE(compiled.has_value());
        const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::InvalidBufferAccess);
        ASSERT_EQ(findings.size(), 1u);
        EXPECT_EQ(findings.front().PassName, "InvalidBuffer");
        EXPECT_EQ(findings.front().ResourceIndex, 9u);
        EXPECT_FALSE(findings.front().IsTextureResource);
    }
}

TEST(RenderGraphValidation, CompileRenderPassAttachmentMismatchesReportStructuredFindings)
{
    RHI::TextureDesc desc{};
    std::vector<TextureResourceDesc> textures(1u);
    textures[0].Name = "SceneTarget";
    textures[0].Desc = desc;

    {
        const std::array colorTargets{RHI::ColorAttachment{.Target = RHI::TextureHandle{1u, 1u}}};
        RHI::RenderPassDesc renderPass{};
        renderPass.ColorTargets = colorTargets;
        std::vector<RenderPassRecord> passes{
            RenderPassRecord{.Name = "ColorMissingWrite", .SideEffect = true, .HasRenderPassDesc = true, .RenderPass = renderPass},
        };

        RenderGraphValidationResult result{};
        const auto compiled = RenderGraphCompiler::Compile(passes, textures, {}, &result);

        ASSERT_FALSE(compiled.has_value());
        const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::RenderPassColorWriteMissing);
        ASSERT_EQ(findings.size(), 1u);
        EXPECT_EQ(findings.front().PassName, "ColorMissingWrite");
    }

    {
        RHI::RenderPassDesc renderPass{};
        renderPass.Depth.Target = RHI::TextureHandle{2u, 1u};
        std::vector<RenderPassRecord> passes{
            RenderPassRecord{.Name = "DepthMissingUsage", .SideEffect = true, .HasRenderPassDesc = true, .RenderPass = renderPass},
        };

        RenderGraphValidationResult result{};
        const auto compiled = RenderGraphCompiler::Compile(passes, textures, {}, &result);

        ASSERT_FALSE(compiled.has_value());
        const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::RenderPassDepthAccessMissing);
        ASSERT_EQ(findings.size(), 1u);
        EXPECT_EQ(findings.front().PassName, "DepthMissingUsage");
    }
}

TEST(RenderGraphValidation, CompileCycleReportsStructuredFinding)
{
    std::vector<RenderPassRecord> passes{
        RenderPassRecord{.Name = "CycleA", .SideEffect = true, .ExplicitDependencies = {PassRef{.Index = 1u, .Generation = 1u}}},
        RenderPassRecord{.Name = "CycleB", .ExplicitDependencies = {PassRef{.Index = 0u, .Generation = 1u}}},
    };

    RenderGraphValidationResult result{};
    const auto compiled = RenderGraphCompiler::Compile(passes, {}, {}, &result);

    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error(), Extrinsic::Core::ErrorCode::InvalidState);
    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::CycleDetected);
    ASSERT_EQ(findings.size(), 2u);
    EXPECT_EQ(findings[0].PassName, "CycleA");
    EXPECT_EQ(findings[1].PassName, "CycleB");
    EXPECT_TRUE(result.HasErrors());
    EXPECT_NE(findings[0].Message.find("cycle"), std::string::npos);
}

TEST(RenderGraphValidation, CompileBackbufferWrittenByNonFinalizerReportsStructuredFinding)
{
    // GRAPHICS-076 Slice C — a non-present write to the imported `Backbuffer`
    // must surface a `RenderGraphValidationResult` finding through the
    // full `RenderGraphCompiler::Compile(...)` path, not just the
    // hand-built `ValidateCompiledGraph` shape pinned by
    // `ImportedBackbufferNonFinalizerWriteReportsError`.
    RHI::TextureDesc backbufferDesc{};
    std::vector<TextureResourceDesc> textures(1u);
    textures[0].Name = "Backbuffer";
    textures[0].Imported = true;
    textures[0].IsBackbuffer = true;
    textures[0].FinalState = TextureState::Present;
    textures[0].ImportedHandle = RHI::TextureHandle{1u, 1u};
    textures[0].Desc = backbufferDesc;

    std::vector<RenderPassRecord> passes{
        RenderPassRecord{
            .Name = "EarlyComposite",
            .SideEffect = false,
            .TextureAccesses = {TextureAccess{.Ref = TextureRef{.Index = 0u, .Generation = 1u},
                                              .Usage = TextureUsage::ColorAttachmentWrite,
                                              .Write = true}},
        },
        RenderPassRecord{
            .Name = "Present",
            .SideEffect = true,
            .TextureAccesses = {TextureAccess{.Ref = TextureRef{.Index = 0u, .Generation = 1u},
                                              .Usage = TextureUsage::ColorAttachmentWrite,
                                              .Write = true}},
        },
    };

    RenderGraphValidationResult compileResult{};
    const auto compiled = RenderGraphCompiler::Compile(passes, textures, {}, &compileResult);

    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);

    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(
        RenderGraphValidationResult{.Findings = compiled->ValidationFindings},
        RenderGraphValidationCode::BackbufferWrittenByNonFinalizer);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(findings.front().PassName, "EarlyComposite");
    EXPECT_EQ(findings.front().ResourceName, "Backbuffer");
    EXPECT_TRUE(findings.front().IsTextureResource);

    // The finalizer write through "Present" is allowed and must not surface
    // an additional `BackbufferWrittenByNonFinalizer` finding.
    for (const RenderGraphValidationFinding& finding : findings)
    {
        EXPECT_NE(finding.PassName, "Present");
    }

    // The explicit compile validation out-param mirrors the compiled-graph
    // findings so renderer/runtime diagnostics observe the same payload as
    // direct validator callers (parity with `SuccessfulCompileStoresValidationFindings`).
    EXPECT_EQ(compileResult.Findings.size(), compiled->ValidationFindings.size());
}

TEST(RenderGraphValidation, SuccessfulCompileStoresValidationFindings)
{
    RHI::TextureDesc desc{};
    std::vector<TextureResourceDesc> textures(1u);
    textures[0].Name = "LoadedTarget";
    textures[0].Desc = desc;

    const std::array colorTargets{RHI::ColorAttachment{.Target = RHI::TextureHandle{1u, 1u}, .Load = RHI::LoadOp::Load}};
    RHI::RenderPassDesc renderPass{};
    renderPass.ColorTargets = colorTargets;
    std::vector<RenderPassRecord> passes{
        RenderPassRecord{
            .Name = "LoadWithoutWriter",
            .SideEffect = true,
            .TextureAccesses = {TextureAccess{.Ref = TextureRef{.Index = 0u, .Generation = 1u}, .Usage = TextureUsage::ColorAttachmentWrite, .Write = true}},
            .HasRenderPassDesc = true,
            .RenderPass = renderPass,
        },
    };

    RenderGraphValidationResult compileResult{};
    const auto compiled = RenderGraphCompiler::Compile(passes, textures, {}, &compileResult);

    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(
        RenderGraphValidationResult{.Findings = compiled->ValidationFindings},
        RenderGraphValidationCode::LoadWithoutGuaranteedWriter);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Warning);
    EXPECT_EQ(findings.front().PassName, "LoadWithoutWriter");
    EXPECT_EQ(compileResult.Findings.size(), compiled->ValidationFindings.size());
}

TEST(RenderGraphValidation, FindingsUseDeterministicSeverityCodePassResourceOrdering)
{
    CompiledRenderGraph compiled = MakeCompiled(1u, 1u, 1u);
    compiled.PassNames[0] = "MixedDiagnostics";
    compiled.TextureNames[0] = "LoadedColor";
    compiled.BufferNames[0] = "ReadOnlyScratch";
    compiled.PassDeclarations[0].WriteTextures = {0u};
    compiled.PassDeclarations[0].ReadBuffers = {0u};
    compiled.TextureLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 0u};
    compiled.BufferLifetimes[0] = ResourceLifetime{.HasUse = true, .FirstUsePass = 0u, .LastUsePass = 0u};
    compiled.RenderPassAttachments = {
        CompiledRenderPassAttachment{.PassIndex = 0u, .ResourceIndex = 0u, .IsTextureResource = true, .Load = Extrinsic::RHI::LoadOp::Load},
    };

    const RenderGraphValidationResult result = ValidateCompiledGraph(compiled);

    ASSERT_EQ(result.Findings.size(), 2u);
    EXPECT_EQ(result.Findings[0].Severity, RenderGraphValidationSeverity::Warning);
    EXPECT_EQ(result.Findings[0].Code, RenderGraphValidationCode::LoadWithoutGuaranteedWriter);
    EXPECT_EQ(result.Findings[1].Severity, RenderGraphValidationSeverity::Error);
    EXPECT_EQ(result.Findings[1].Code, RenderGraphValidationCode::TransientBufferWithoutProducer);

    const std::vector<RenderGraphValidationFinding> loadFindings = FindingsByCode(
        result,
        RenderGraphValidationCode::LoadWithoutGuaranteedWriter);
    ASSERT_EQ(loadFindings.size(), 1u);
    EXPECT_EQ(loadFindings.front().ResourceName, "LoadedColor");
}

TEST(RenderGraphValidation, ColorAttachmentReadUsesReadStateAndSkipsConsecutiveReadBarrier)
{
    RenderGraph graph;
    const TextureRef lighting = graph.CreateTexture("Lighting", RHI::TextureDesc{});

    (void)graph.AddPass("LightingWrite", [lighting](RenderGraphBuilder& builder) {
        (void)builder.Write(lighting, TextureUsage::ColorAttachmentWrite);
    });
    (void)graph.AddPass("LightingRead0", [lighting](RenderGraphBuilder& builder) {
        (void)builder.Read(lighting, TextureUsage::ColorAttachmentRead);
        builder.SideEffect();
    });
    (void)graph.AddPass("LightingRead1", [lighting](RenderGraphBuilder& builder) {
        (void)builder.Read(lighting, TextureUsage::ColorAttachmentRead);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_FALSE(compiled->BarrierPackets.empty());

    std::uint32_t writeToReadBarriers = 0u;
    std::uint32_t readToReadBarriers = 0u;
    std::uint32_t readPassWriteBarriers = 0u;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            if (barrier.TextureIndex != lighting.Index)
            {
                continue;
            }
            if (barrier.Before == TextureBarrierState::ColorAttachmentWrite &&
                barrier.After == TextureBarrierState::ColorAttachmentRead)
            {
                ++writeToReadBarriers;
            }
            if (barrier.Before == TextureBarrierState::ColorAttachmentRead &&
                barrier.After == TextureBarrierState::ColorAttachmentRead)
            {
                ++readToReadBarriers;
            }
            if (barrier.Before == TextureBarrierState::ColorAttachmentRead &&
                barrier.After == TextureBarrierState::ColorAttachmentWrite)
            {
                ++readPassWriteBarriers;
            }
        }
    }

    EXPECT_EQ(writeToReadBarriers, 1u);
    EXPECT_EQ(readToReadBarriers, 0u);
    EXPECT_EQ(readPassWriteBarriers, 0u);
}

TEST(RenderGraphValidation, TransientMemoryEstimateUsesRhiBlockCompressedStorageSize)
{
    RenderGraph graph;
    RHI::TextureDesc compressedDesc{};
    compressedDesc.Width = 65u;
    compressedDesc.Height = 65u;
    compressedDesc.Fmt = RHI::Format::BC1_UNORM;
    compressedDesc.Usage = RHI::TextureUsage::Storage;

    const TextureRef compressed = graph.CreateTexture("CompressedScratch", compressedDesc);
    (void)graph.AddPass("WriteCompressed", [compressed](RenderGraphBuilder& builder) {
        (void)builder.Write(compressed, TextureUsage::ShaderWrite);
        builder.SideEffect();
    });

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::uint64_t expectedBytes =
        AlignUpForTransientPlacement(RHI::EstimateTextureStorageBytes(compressedDesc), 256u);
    EXPECT_EQ(expectedBytes, 2560u)
        << "BC1 65x65 storage should use ceil(65 / 4)^2 * 8 bytes before transient alignment";
    EXPECT_EQ(compiled->TransientNaiveMemoryEstimateBytes, expectedBytes);
    EXPECT_EQ(compiled->TransientPlacedPeakMemoryEstimateBytes, expectedBytes);
    EXPECT_EQ(compiled->TransientMemoryEstimateBytes, expectedBytes);

    ASSERT_EQ(compiled->TextureTransientPlacements.size(), 1u);
    EXPECT_EQ(compiled->TextureTransientPlacements.front().ResourceIndex, compressed.Index);
    EXPECT_EQ(compiled->TextureTransientPlacements.front().SizeBytes, expectedBytes);
}

TEST(RenderGraphValidation, ResetReusedPassRecordClearsStaleDeclarations)
{
    RenderGraph graph;
    const TextureRef history = graph.CreateTexture("History", RHI::TextureDesc{});
    const BufferRef scratch = graph.CreateBuffer("Scratch", RHI::BufferDesc{.SizeBytes = 4096u, .Usage = RHI::BufferUsage::Storage});

    (void)graph.AddPass("PopulateScratch", [history, scratch](RenderGraphBuilder& builder) {
        (void)builder.Write(history, TextureUsage::ShaderWrite);
        (void)builder.Write(scratch, BufferUsage::ShaderWrite);
        builder.SideEffect();
    });

    graph.Reset();
    (void)graph.AddPass("StandaloneSideEffect", true);

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    ASSERT_EQ(compiled->PassDeclarations.size(), 1u);

    EXPECT_EQ(compiled->PassCount, 1u);
    EXPECT_EQ(compiled->ResourceCount, 0u);
    EXPECT_TRUE(compiled->PassDeclarations.front().ReadTextures.empty());
    EXPECT_TRUE(compiled->PassDeclarations.front().WriteTextures.empty());
    EXPECT_TRUE(compiled->PassDeclarations.front().ReadBuffers.empty());
    EXPECT_TRUE(compiled->PassDeclarations.front().WriteBuffers.empty());
    EXPECT_TRUE(compiled->RenderPassAttachments.empty());
    EXPECT_TRUE(compiled->BarrierPackets.empty());
}

TEST(RenderGraphValidation, ResetReusedCompilerScratchKeepsCompiledOutputStable)
{
    RenderGraph fresh;
    DeclareCompilerScratchFixture(fresh);
    const auto freshCompiled = fresh.Compile();
    ASSERT_TRUE(freshCompiled.has_value());

    RenderGraph reused;
    DeclareCompilerScratchFixture(reused);
    const auto firstReusedCompiled = reused.Compile();
    ASSERT_TRUE(firstReusedCompiled.has_value());

    reused.Reset();
    DeclareCompilerScratchFixture(reused);
    const auto secondReusedCompiled = reused.Compile();
    ASSERT_TRUE(secondReusedCompiled.has_value());

    const std::string freshSignature = CompiledGraphSignature(*freshCompiled);
    EXPECT_EQ(CompiledGraphSignature(*firstReusedCompiled), freshSignature);
    EXPECT_EQ(CompiledGraphSignature(*secondReusedCompiled), freshSignature);
    EXPECT_TRUE(reused.GetLastCompileValidationResult().Findings.empty());
}
