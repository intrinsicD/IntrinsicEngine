#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.Core.Error;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

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

    const auto compiled = RenderGraphCompiler::Compile(passes, {}, {});

    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error(), Extrinsic::Core::ErrorCode::InvalidArgument);
    const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
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

        const auto compiled = RenderGraphCompiler::Compile(passes, {}, {});

        ASSERT_FALSE(compiled.has_value());
        const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
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

        const auto compiled = RenderGraphCompiler::Compile(passes, {}, {});

        ASSERT_FALSE(compiled.has_value());
        const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
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

        const auto compiled = RenderGraphCompiler::Compile(passes, textures, {});

        ASSERT_FALSE(compiled.has_value());
        const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
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

        const auto compiled = RenderGraphCompiler::Compile(passes, textures, {});

        ASSERT_FALSE(compiled.has_value());
        const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
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

    const auto compiled = RenderGraphCompiler::Compile(passes, {}, {});

    ASSERT_FALSE(compiled.has_value());
    EXPECT_EQ(compiled.error(), Extrinsic::Core::ErrorCode::InvalidState);
    const RenderGraphValidationResult& result = RenderGraphCompiler::GetLastCompileValidationResult();
    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(result, RenderGraphValidationCode::CycleDetected);
    ASSERT_EQ(findings.size(), 2u);
    EXPECT_EQ(findings[0].PassName, "CycleA");
    EXPECT_EQ(findings[1].PassName, "CycleB");
    EXPECT_TRUE(result.HasErrors());
    EXPECT_NE(findings[0].Message.find("cycle"), std::string::npos);
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

    const auto compiled = RenderGraphCompiler::Compile(passes, textures, {});

    const auto& compileResult = RenderGraphCompiler::GetLastCompileValidationResult();
    ASSERT_TRUE(compiled.has_value())
        << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    const std::vector<RenderGraphValidationFinding> findings = FindingsByCode(
        RenderGraphValidationResult{.Findings = compiled->ValidationFindings},
        RenderGraphValidationCode::LoadWithoutGuaranteedWriter);
    ASSERT_EQ(findings.size(), 1u);
    EXPECT_EQ(findings.front().Severity, RenderGraphValidationSeverity::Warning);
    EXPECT_EQ(findings.front().PassName, "LoadWithoutWriter");
    EXPECT_EQ(RenderGraphCompiler::GetLastCompileValidationResult().Findings.size(), compiled->ValidationFindings.size());
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
