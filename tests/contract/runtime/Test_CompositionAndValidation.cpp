// tests/Test_CompositionAndValidation.cpp
//
// Tests for render graph validation (RenderGraphValidationResult,
// imported-resource write policies). CPU-side contract tests — no Vulkan
// device required.

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

#include "RHI.Vulkan.hpp"

import Graphics;
import Graphics.RenderDriver;
import Core;

using namespace Graphics;
using namespace Core::Hash;

// =========================================================================
// Render Graph Validation Tests
// =========================================================================

// Helper: build a minimal RenderGraphDebugImage for a known resource.
static RenderGraphDebugImage MakeDebugImage(RenderResource resource,
                                             bool imported = false,
                                             uint32_t firstWrite = ~0u)
{
    const auto def = GetRenderResourceDefinition(resource);
    RenderGraphDebugImage img{};
    img.Name = def.Name;
    img.Resource = static_cast<ResourceID>(resource);
    img.IsImported = imported;
    img.FirstWritePass = firstWrite;
    img.Format = def.FixedFormat;
    return img;
}

static RenderGraphDebugImage MakeDebugImageRaw(StringID name,
                                                ResourceID id,
                                                bool imported = false,
                                                uint32_t firstWrite = ~0u)
{
    RenderGraphDebugImage img{};
    img.Name = name;
    img.Resource = id;
    img.IsImported = imported;
    img.FirstWritePass = firstWrite;
    return img;
}

// Helper: build a minimal RenderGraphDebugPass with one attachment.
static RenderGraphDebugPass MakePassWithAttachment(
    const char* passName,
    uint32_t passIndex,
    StringID resourceName,
    ResourceID resourceId,
    bool imported,
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR)
{
    RenderGraphDebugPass pass{};
    pass.Name = passName;
    pass.PassIndex = passIndex;

    RenderGraphDebugPass::Attachment att{};
    att.ResourceName = resourceName;
    att.Resource = resourceId;
    att.IsImported = imported;
    att.LoadOp = loadOp;
    pass.Attachments.push_back(att);

    return pass;
}

static std::string DescribeValidationResult(const RenderGraphValidationResult& result)
{
    std::ostringstream oss;
    for (const auto& diag : result.Diagnostics)
        oss << (diag.Severity == RenderGraphValidationSeverity::Error ? "error" : "warning") << ": " << diag.Message << '\n';
    return oss.str();
}

TEST(RenderGraphValidation, EmptyGraph_NoRecipe_NoDiagnostics)
{
    FrameRecipe recipe{};
    std::vector<RenderGraphDebugPass> passes;
    std::vector<RenderGraphDebugImage> images;

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors()) << DescribeValidationResult(result);
    EXPECT_EQ(result.ErrorCount(), 0u);
    EXPECT_EQ(result.WarningCount(), 0u);
}

TEST(RenderGraphValidation, MissingRequiredResource_IsError)
{
    FrameRecipe recipe{};
    recipe.Depth = true; // requires SceneDepth
    recipe.LightingPath = FrameLightingPath::Forward; // requires SceneColorHDR

    // Provide SceneDepth but NOT SceneColorHDR.
    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImage(RenderResource::SceneDepth, true, 0),
    };
    std::vector<RenderGraphDebugPass> passes;

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_TRUE(result.HasErrors());

    // SceneColorHDR is missing — should be an error.
    bool foundHdrError = false;
    for (const auto& d : result.Diagnostics)
    {
        if (d.Severity == RenderGraphValidationSeverity::Error &&
            d.Message.find("missing") != std::string::npos)
        {
            foundHdrError = true;
        }
    }
    EXPECT_TRUE(foundHdrError);
}

TEST(RenderGraphValidation, TransientWithoutProducer_IsError)
{
    FrameRecipe recipe{};
    recipe.EntityId = true; // requires EntityId (transient)

    // EntityId exists but has no producer (FirstWritePass == ~0u).
    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImage(RenderResource::EntityId, false, ~0u),
    };
    std::vector<RenderGraphDebugPass> passes;

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_TRUE(result.HasErrors());

    bool foundProducerError = false;
    for (const auto& d : result.Diagnostics)
    {
        if (d.Severity == RenderGraphValidationSeverity::Error &&
            d.Message.find("no producer") != std::string::npos)
        {
            foundProducerError = true;
        }
    }
    EXPECT_TRUE(foundProducerError);
}

TEST(RenderGraphValidation, BackbufferWrittenByUnauthorizedPass_IsError)
{
    FrameRecipe recipe{};
    const StringID bbName = StringID{"Backbuffer"};
    constexpr ResourceID bbId = 100;

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImageRaw(bbName, bbId, true, 0),
    };

    // A rogue pass writing to the Backbuffer (not Present.LDR).
    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("RoguePass", 0, bbName, bbId, true),
    };

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_TRUE(result.HasErrors());

    bool foundBackbufferError = false;
    for (const auto& d : result.Diagnostics)
    {
        if (d.Severity == RenderGraphValidationSeverity::Error &&
            d.Message.find("authorized") != std::string::npos)
        {
            foundBackbufferError = true;
        }
    }
    EXPECT_TRUE(foundBackbufferError);
}

TEST(RenderGraphValidation, BackbufferWrittenByPresent_NoError)
{
    FrameRecipe recipe{};
    const StringID bbName = StringID{"Backbuffer"};
    constexpr ResourceID bbId = 100;

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImageRaw(bbName, bbId, true, 0),
    };

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("Present.LDR", 0, bbName, bbId, true),
    };

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors()) << DescribeValidationResult(result);
}

TEST(RenderGraphValidation, CustomWritePolicy_UnauthorizedWriter_IsError)
{
    FrameRecipe recipe{};
    const StringID resName = StringID{"CustomImported"};
    constexpr ResourceID resId = 200;

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImageRaw(resName, resId, true, 0),
    };

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("WrongPass", 0, resName, resId, true),
    };

    // Custom policy: only "CorrectPass" may write to CustomImported.
    std::vector<ImportedResourceWritePolicy> policies = {
        {resName, "CorrectPass"},
    };

    auto result = ValidateCompiledGraph(recipe, passes, images, policies);
    EXPECT_TRUE(result.HasErrors());
    EXPECT_EQ(result.ErrorCount(), 1u);
}

TEST(RenderGraphValidation, CustomWritePolicy_AuthorizedWriter_NoError)
{
    FrameRecipe recipe{};
    const StringID resName = StringID{"CustomImported"};
    constexpr ResourceID resId = 200;

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImageRaw(resName, resId, true, 0),
    };

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("CorrectPass", 0, resName, resId, true),
    };

    std::vector<ImportedResourceWritePolicy> policies = {
        {resName, "CorrectPass"},
    };

    auto result = ValidateCompiledGraph(recipe, passes, images, policies);
    EXPECT_FALSE(result.HasErrors()) << DescribeValidationResult(result);
}

TEST(RenderGraphValidation, MultipleReInitialization_IsWarning)
{
    FrameRecipe recipe{};
    const StringID resName = GetRenderResourceName(RenderResource::SceneColorHDR);
    constexpr ResourceID resId = 50;

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImageRaw(resName, resId, false, 0),
    };

    // Two passes both clear (initialize) the same transient resource.
    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("PassA", 0, resName, resId, false, VK_ATTACHMENT_LOAD_OP_CLEAR),
        MakePassWithAttachment("PassB", 1, resName, resId, false, VK_ATTACHMENT_LOAD_OP_CLEAR),
    };

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors());  // re-initialization is a warning, not error
    EXPECT_GE(result.WarningCount(), 1u);
}

TEST(RenderGraphValidation, ValidForwardPipeline_NoDiagnostics)
{
    FrameRecipe recipe{};
    recipe.Depth = true;
    recipe.LightingPath = FrameLightingPath::Forward;
    recipe.Post = true;
    recipe.SceneColorLDR = true;
    recipe.EntityId = true;

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImage(RenderResource::SceneDepth, true, 0),
        MakeDebugImage(RenderResource::EntityId, false, 0),
        MakeDebugImage(RenderResource::SceneColorHDR, false, 1),
        MakeDebugImage(RenderResource::SceneColorLDR, false, 4),
    };

    // Present.LDR writes to Backbuffer.
    const StringID bbName = StringID{"Backbuffer"};
    constexpr ResourceID bbId = 99;
    images.push_back(MakeDebugImageRaw(bbName, bbId, true, 5));

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("Present.LDR", 5, bbName, bbId, true),
    };

    auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.WarningCount(), 0u);
}


TEST(RenderGraphValidation, DefaultForwardRecipe_BaselineContractsValidateCleanly)
{
    DefaultPipelineRecipeInputs inputs{};
    inputs.SurfacePassEnabled = true;
    inputs.PostProcessPassEnabled = true;
    inputs.ImGuiPassEnabled = true;

    const FrameRecipe recipe = BuildDefaultPipelineRecipe(inputs);

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImage(RenderResource::SceneDepth, true, 0),
        MakeDebugImage(RenderResource::EntityId, false, 0),
        MakeDebugImage(RenderResource::PrimitiveId, false, 0),
        MakeDebugImage(RenderResource::SceneColorHDR, false, 0),
        MakeDebugImage(RenderResource::SceneColorLDR, false, 1),
        MakeDebugImageRaw(StringID{"Backbuffer"}, 99, true, 2),
    };

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("Present.LDR", 2, StringID{"Backbuffer"}, 99, true),
    };

    const auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.WarningCount(), 0u);
}

TEST(RenderGraphValidation, DefaultDeferredRecipe_BaselineContractsValidateCleanly)
{
    DefaultPipelineRecipeInputs inputs{};
    inputs.SurfacePassEnabled = true;
    inputs.CompositionPassEnabled = true;
    inputs.PostProcessPassEnabled = true;
    inputs.RequestedLightingPath = FrameLightingPath::Deferred;

    const FrameRecipe recipe = BuildDefaultPipelineRecipe(inputs);
    ASSERT_EQ(recipe.LightingPath, FrameLightingPath::Deferred);

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImage(RenderResource::SceneDepth, true, 0),
        MakeDebugImage(RenderResource::EntityId, false, 0),
        MakeDebugImage(RenderResource::PrimitiveId, false, 0),
        MakeDebugImage(RenderResource::SceneNormal, false, 0),
        MakeDebugImage(RenderResource::Albedo, false, 0),
        MakeDebugImage(RenderResource::Material0, false, 0),
        MakeDebugImage(RenderResource::SceneColorHDR, false, 1),
        MakeDebugImage(RenderResource::SceneColorLDR, false, 2),
        MakeDebugImageRaw(StringID{"Backbuffer"}, 100, true, 3),
    };

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("Present.LDR", 3, StringID{"Backbuffer"}, 100, true),
    };

    const auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.WarningCount(), 0u);
}

TEST(RenderGraphValidation, DefaultHybridRecipe_BaselineContractsValidateCleanly)
{
    DefaultPipelineRecipeInputs inputs{};
    inputs.SurfacePassEnabled = true;
    inputs.CompositionPassEnabled = true;
    inputs.PostProcessPassEnabled = true;
    inputs.RequestedLightingPath = FrameLightingPath::Hybrid;

    const FrameRecipe recipe = BuildDefaultPipelineRecipe(inputs);
    ASSERT_EQ(recipe.LightingPath, FrameLightingPath::Hybrid);
    ASSERT_TRUE(UsesDeferredComposition(recipe.LightingPath));

    std::vector<RenderGraphDebugImage> images = {
        MakeDebugImage(RenderResource::SceneDepth, true, 0),
        MakeDebugImage(RenderResource::EntityId, false, 0),
        MakeDebugImage(RenderResource::PrimitiveId, false, 0),
        MakeDebugImage(RenderResource::SceneNormal, false, 0),
        MakeDebugImage(RenderResource::Albedo, false, 0),
        MakeDebugImage(RenderResource::Material0, false, 0),
        MakeDebugImage(RenderResource::SceneColorHDR, false, 1),
        MakeDebugImage(RenderResource::SceneColorLDR, false, 2),
        MakeDebugImageRaw(StringID{"Backbuffer"}, 101, true, 3),
    };

    std::vector<RenderGraphDebugPass> passes = {
        MakePassWithAttachment("Present.LDR", 3, StringID{"Backbuffer"}, 101, true),
    };

    const auto result = ValidateCompiledGraph(recipe, passes, images);
    EXPECT_FALSE(result.HasErrors());
    EXPECT_EQ(result.WarningCount(), 0u);
}

TEST(RenderGraphValidation, DefaultImportedWritePolicies_ContainsBackbuffer)
{
    const auto policies = GetDefaultImportedWritePolicies();
    ASSERT_FALSE(policies.empty());

    bool foundBackbuffer = false;
    for (const auto& p : policies)
    {
        if (p.ResourceName == StringID{"Backbuffer"})
        {
            EXPECT_EQ(p.AuthorizedWriter, "Present.LDR");
            foundBackbuffer = true;
        }
    }
    EXPECT_TRUE(foundBackbuffer);
}
