#include <gtest/gtest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;

namespace
{
    using namespace Extrinsic::Graphics;

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

TEST(RenderGraphValidation, ImportedBackbufferNonFinalizerWriteReportsError)
{
    CompiledRenderGraph compiled = MakeCompiled(2u, 1u, 0u);
    compiled.PassNames[0] = "EarlyComposite";
    compiled.PassNames[1] = "Present";
    compiled.PassSideEffects[1] = true;
    compiled.TextureNames[0] = "Backbuffer";
    compiled.TextureImported[0] = true;
    compiled.TextureIsBackbuffer[0] = true;
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
