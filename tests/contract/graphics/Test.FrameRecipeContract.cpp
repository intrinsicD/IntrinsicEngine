#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;

namespace
{
    using namespace Extrinsic::Graphics;
    namespace RHI = Extrinsic::RHI;

    [[nodiscard]] FrameRecipeImports MakeImports()
    {
        return FrameRecipeImports{
            .Backbuffer = Extrinsic::RHI::TextureHandle{1u, 1u},
            .SceneTable = Extrinsic::RHI::BufferHandle{2u, 1u},
            .InstanceStatic = Extrinsic::RHI::BufferHandle{3u, 1u},
            .InstanceDynamic = Extrinsic::RHI::BufferHandle{4u, 1u},
            .EntityConfig = Extrinsic::RHI::BufferHandle{5u, 1u},
            .GeometryRecords = Extrinsic::RHI::BufferHandle{6u, 1u},
            .Bounds = Extrinsic::RHI::BufferHandle{7u, 1u},
            .Lights = Extrinsic::RHI::BufferHandle{8u, 1u},
            .MaterialBuffer = Extrinsic::RHI::BufferHandle{9u, 1u},
            .SurfaceOpaqueIndexedArgs = Extrinsic::RHI::BufferHandle{10u, 1u},
            .SurfaceOpaqueCount = Extrinsic::RHI::BufferHandle{11u, 1u},
            .LinesIndexedArgs = Extrinsic::RHI::BufferHandle{12u, 1u},
            .LinesCount = Extrinsic::RHI::BufferHandle{13u, 1u},
            .LineQuadsNonIndexedArgs = Extrinsic::RHI::BufferHandle{14u, 1u},
            .LineQuadsCount = Extrinsic::RHI::BufferHandle{15u, 1u},
            .PointsNonIndexedArgs = Extrinsic::RHI::BufferHandle{16u, 1u},
            .PointsCount = Extrinsic::RHI::BufferHandle{17u, 1u},
            // GRAPHICS-074 Slice D.2 — renderer-owned host-visible
            // `Picking.Readback` buffer is now imported by the recipe rather
            // than transient; supply a valid handle so picking-enabled
            // contract tests keep their compile path. Tests that disable
            // picking ignore this import (recipe drops the resource).
            .PickingReadback = Extrinsic::RHI::BufferHandle{18u, 1u},
            // GRAPHICS-075 Slice E.2 — renderer-owned host-visible
            // `Histogram.Readback` buffer; same import-or-skip pattern as
            // picking above. Tests that disable postprocess ignore this
            // import (the recipe drops the histogram pass altogether).
            .HistogramReadback = Extrinsic::RHI::BufferHandle{19u, 1u},
            // GRAPHICS-038B — renderer-owned retained HZB.Current texture.
            // Tests opt into `EnableHZBBuild`; default recipe tests leave the
            // feature off so this import is ignored.
            .HZBCurrent = Extrinsic::RHI::TextureHandle{20u, 1u},
            // GRAPHICS-039A — renderer-owned cluster AABB storage buffer.
            // Tests opt into `EnableClusterGridBuild`; default recipe tests
            // leave the feature off so this import is ignored.
            .ClusterGridAABBs = Extrinsic::RHI::BufferHandle{19u, 1u},
            // GRAPHICS-039B — renderer-owned clustered-light assignment
            // outputs. Tests opt into `EnableClusterLightAssignment`; default
            // recipe tests leave the feature off so these imports are ignored.
            .ClusterLightHeaders = Extrinsic::RHI::BufferHandle{20u, 1u},
            .ClusterLightIndices = Extrinsic::RHI::BufferHandle{21u, 1u},
            .ClusterLightCounter = Extrinsic::RHI::BufferHandle{22u, 1u},
        };
    }

    [[nodiscard]] std::vector<std::string> OrderedPassNames(const CompiledRenderGraph& compiled)
    {
        std::vector<std::string> names{};
        names.reserve(compiled.TopologicalOrder.size());
        for (const std::uint32_t passIndex : compiled.TopologicalOrder)
        {
            names.push_back(compiled.PassNames[passIndex]);
        }
        return names;
    }

    [[nodiscard]] std::vector<std::uint32_t> RankByPass(const CompiledRenderGraph& compiled)
    {
        std::vector<std::uint32_t> rank(
            compiled.PassNames.size(),
            std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t orderIndex = 0u; orderIndex < compiled.TopologicalOrder.size(); ++orderIndex)
        {
            const std::uint32_t passIndex = compiled.TopologicalOrder[orderIndex];
            if (passIndex < rank.size())
            {
                rank[passIndex] = orderIndex;
            }
        }
        return rank;
    }

    [[nodiscard]] std::uint32_t RankByName(const CompiledRenderGraph& compiled,
                                           const std::string_view passName)
    {
        std::uint32_t passIndex = static_cast<std::uint32_t>(compiled.PassNames.size());
        for (std::uint32_t i = 0u; i < compiled.PassNames.size(); ++i)
        {
            if (compiled.PassNames[i] == passName)
            {
                passIndex = i;
                break;
            }
        }
        const std::vector<std::uint32_t> ranks = RankByPass(compiled);
        return passIndex < ranks.size() ? ranks[passIndex] : std::numeric_limits<std::uint32_t>::max();
    }

    void ExpectPassBefore(const CompiledRenderGraph& compiled,
                          const std::string_view before,
                          const std::string_view after)
    {
        const std::uint32_t beforeRank = RankByName(compiled, before);
        const std::uint32_t afterRank = RankByName(compiled, after);
        ASSERT_NE(beforeRank, std::numeric_limits<std::uint32_t>::max()) << before;
        ASSERT_NE(afterRank, std::numeric_limits<std::uint32_t>::max()) << after;
        EXPECT_LT(beforeRank, afterRank) << before << " should run before " << after;
    }

    [[nodiscard]] std::size_t ExplicitDependencyCount(const CompiledRenderGraph& compiled)
    {
        std::size_t count = 0;
        for (const CompiledPassDeclarations& declarations : compiled.PassDeclarations)
        {
            count += declarations.ExplicitDependencyPasses.size();
        }
        return count;
    }

    [[nodiscard]] std::uint32_t PassIndexByName(const CompiledRenderGraph& compiled,
                                                const std::string_view passName)
    {
        for (std::uint32_t i = 0u; i < compiled.PassNames.size(); ++i)
        {
            if (compiled.PassNames[i] == passName)
            {
                return i;
            }
        }
        return compiled.PassCount;
    }

    [[nodiscard]] bool HasPassName(const CompiledRenderGraph& compiled,
                                   const std::string_view passName)
    {
        return PassIndexByName(compiled, passName) < compiled.PassNames.size();
    }

    [[nodiscard]] bool HasEnabledResource(const FrameRecipeIntrospection& description,
                                          const FrameRecipeResourceKind kind)
    {
        return std::ranges::any_of(description.Resources, [kind](const FrameRecipeResourceDeclaration& resource) {
            return resource.Kind == kind && resource.Enabled;
        });
    }

    [[nodiscard]] const FrameRecipePassDeclaration* FindPass(const FrameRecipeIntrospection& description,
                                                             const FrameRecipePassKind kind)
    {
        const auto it = std::ranges::find_if(description.Passes, [kind](const FrameRecipePassDeclaration& pass) {
            return pass.Kind == kind;
        });
        return it == description.Passes.end() ? nullptr : &(*it);
    }

    [[nodiscard]] const FrameRecipeResourceDeclaration* FindResource(const FrameRecipeIntrospection& description,
                                                                     const FrameRecipeResourceKind kind)
    {
        const auto it = std::ranges::find_if(description.Resources, [kind](const FrameRecipeResourceDeclaration& resource) {
            return resource.Kind == kind;
        });
        return it == description.Resources.end() ? nullptr : &(*it);
    }

    [[nodiscard]] bool Contains(const std::vector<std::string_view>& values, const std::string_view value)
    {
        return std::ranges::find(values, value) != values.end();
    }

    [[nodiscard]] bool HasContributionDiagnostic(
        const FrameRecipeContributionValidationResult& result,
        const FrameRecipeContributionDiagnosticCode code)
    {
        return std::ranges::any_of(result.Diagnostics, [code](const FrameRecipeContributionDiagnostic& diagnostic) {
            return diagnostic.Code == code;
        });
    }

    [[nodiscard]] bool HasTextureUsage(const RHI::TextureUsage flags,
                                       const RHI::TextureUsage bit) noexcept
    {
        return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(bit)) != 0u;
    }

    [[nodiscard]] std::uint32_t TextureIndexByName(const CompiledRenderGraph& compiled,
                                                   const std::string_view textureName)
    {
        for (std::uint32_t i = 0u; i < compiled.TextureNames.size(); ++i)
        {
            if (compiled.TextureNames[i] == textureName)
            {
                return i;
            }
        }
        return static_cast<std::uint32_t>(compiled.TextureNames.size());
    }

    [[nodiscard]] bool IsDepthBarrierState(const TextureBarrierState state) noexcept
    {
        return state == TextureBarrierState::DepthRead || state == TextureBarrierState::DepthWrite;
    }

    [[nodiscard]] bool TextureBarrierStateCompatibleWithUsage(const TextureBarrierState state,
                                                              const TextureResourceDesc& desc) noexcept
    {
        switch (state)
        {
        case TextureBarrierState::Undefined:
            return true;
        case TextureBarrierState::ColorAttachmentRead:
        case TextureBarrierState::ColorAttachmentWrite:
            return desc.IsBackbuffer || HasTextureUsage(desc.Desc.Usage, RHI::TextureUsage::ColorTarget);
        case TextureBarrierState::DepthRead:
        case TextureBarrierState::DepthWrite:
            return !desc.IsBackbuffer && HasTextureUsage(desc.Desc.Usage, RHI::TextureUsage::DepthTarget);
        case TextureBarrierState::ShaderRead:
            return !desc.IsBackbuffer && HasTextureUsage(desc.Desc.Usage, RHI::TextureUsage::Sampled);
        case TextureBarrierState::ShaderWrite:
            return !desc.IsBackbuffer && HasTextureUsage(desc.Desc.Usage, RHI::TextureUsage::Storage);
        case TextureBarrierState::TransferSrc:
            return desc.IsBackbuffer || HasTextureUsage(desc.Desc.Usage, RHI::TextureUsage::TransferSrc);
        case TextureBarrierState::TransferDst:
            return desc.IsBackbuffer || HasTextureUsage(desc.Desc.Usage, RHI::TextureUsage::TransferDst);
        case TextureBarrierState::Present:
            return desc.IsBackbuffer;
        }
        return false;
    }

    [[nodiscard]] bool HasCompiledRenderPassAttachment(const CompiledRenderGraph& compiled,
                                                       const std::string_view passName)
    {
        for (const CompiledRenderPassAttachment& attachment : compiled.RenderPassAttachments)
        {
            if (attachment.PassIndex < compiled.PassNames.size() && compiled.PassNames[attachment.PassIndex] == passName)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] bool PassReadsTexture(const CompiledRenderGraph& compiled,
                                        const std::string_view passName,
                                        const std::string_view textureName)
    {
        for (const CompiledPassDeclarations& declaration : compiled.PassDeclarations)
        {
            if (declaration.PassIndex >= compiled.PassNames.size() ||
                compiled.PassNames[declaration.PassIndex] != passName)
            {
                continue;
            }

            for (const std::uint32_t textureIndex : declaration.ReadTextures)
            {
                if (textureIndex < compiled.TextureNames.size() &&
                    compiled.TextureNames[textureIndex] == textureName)
                {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] bool PassWritesTexture(const CompiledRenderGraph& compiled,
                                         const std::string_view passName,
                                         const std::string_view textureName)
    {
        for (const CompiledPassDeclarations& declaration : compiled.PassDeclarations)
        {
            if (declaration.PassIndex >= compiled.PassNames.size() ||
                compiled.PassNames[declaration.PassIndex] != passName)
            {
                continue;
            }

            for (const std::uint32_t textureIndex : declaration.WriteTextures)
            {
                if (textureIndex < compiled.TextureNames.size() &&
                    compiled.TextureNames[textureIndex] == textureName)
                {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] const TextureResourceDesc* TextureDescByName(RenderGraph& graph,
                                                              const CompiledRenderGraph& compiled,
                                                              const std::string_view textureName)
    {
        const std::uint32_t textureIndex = TextureIndexByName(compiled, textureName);
        if (textureIndex >= compiled.TextureNames.size())
        {
            return nullptr;
        }
        return graph.GetTextureDescByIndex(textureIndex);
    }
}

TEST(FrameRecipeContract, DefaultRecipeBuildsCanonicalPassOrder)
{
    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        MakeImports(),
        FrameRecipeSizing{.Width = 1280u, .Height = 720u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const std::vector<std::string> expected{
        "CullingPass",
        "DepthPrepass",
        "SurfacePass",
        "CompositionPass",
        "LinePass",
        "PointPass",
        // GRAPHICS-075 Slice E.1 — the histogram compute dispatch lives
        // in its own ordered graph pass before `"PostProcessPass"`.
        // Vulkan rejects `vkCmdDispatch` inside an active render-pass
        // scope and `"PostProcessPass"` is a render-pass-scope pass
        // (bloom + tonemap write color attachments), so collapsing the
        // histogram dispatch back into the umbrella would reintroduce
        // the dispatch-inside-render-pass hazard.
        "PostProcessHistogramPass",
        "PostProcessPass",
        // GRAPHICS-040C — AA passes are no longer default structural
        // no-ops. The recipe selector instantiates only the passes required
        // by the selected AA mode; NoAA keeps the default chain lean.
        "ImGuiPass",
        "Present",
    };
    EXPECT_EQ(OrderedPassNames(*compiled), expected);
    EXPECT_EQ(build.DeclaredPassCount, expected.size());
}

TEST(FrameRecipeContract, DefaultRecipePropagatesTypedPassAndResourceIds)
{
    const FrameRecipeIntrospection recipe = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        MakeImports(),
        FrameRecipeSizing{.Width = 1280u, .Height = 720u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::optional<std::uint32_t> surfaceRecipeIndex =
        FindFrameRecipePassIndexById(recipe, ToFramePassId(FrameRecipePassKind::Surface));
    ASSERT_TRUE(surfaceRecipeIndex.has_value());
    EXPECT_EQ(recipe.Passes[*surfaceRecipeIndex].Id, ToFramePassId(FrameRecipePassKind::Surface));
    EXPECT_EQ(FrameRecipePassIdName(recipe.Passes[*surfaceRecipeIndex].Id), std::string_view{"SurfacePass"});

    const std::optional<std::uint32_t> surfaceIndex =
        FindCompiledPassIndexForRecipeId(recipe, *compiled, ToFramePassId(FrameRecipePassKind::Surface));
    ASSERT_TRUE(surfaceIndex.has_value());
    ASSERT_LT(*surfaceIndex, compiled->PassNames.size());
    ASSERT_LT(*surfaceIndex, compiled->PassIds.size());
    EXPECT_EQ(compiled->PassIds[*surfaceIndex], ToFramePassId(FrameRecipePassKind::Surface));
    EXPECT_EQ(compiled->PassNames[*surfaceIndex], "SurfacePass");

    const std::optional<std::uint32_t> presentIndex =
        FindCompiledPassIndexForRecipeId(recipe, *compiled, ToFramePassId(FrameRecipePassKind::Present));
    ASSERT_TRUE(presentIndex.has_value());
    ASSERT_LT(*presentIndex, compiled->PassNames.size());
    ASSERT_LT(*presentIndex, compiled->PassIds.size());
    EXPECT_EQ(compiled->PassIds[*presentIndex], ToFramePassId(FrameRecipePassKind::Present));
    EXPECT_EQ(compiled->PassNames[*presentIndex], "Present");

    const std::optional<std::uint32_t> hdrRecipeIndex =
        FindFrameRecipeResourceIndexById(recipe, ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR));
    ASSERT_TRUE(hdrRecipeIndex.has_value());
    EXPECT_EQ(recipe.Resources[*hdrRecipeIndex].Id, ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR));
    EXPECT_EQ(FrameRecipeResourceIdName(recipe.Resources[*hdrRecipeIndex].Id), std::string_view{"SceneColorHDR"});

    const std::optional<std::uint32_t> hdrIndex =
        FindCompiledTextureIndexForRecipeId(recipe, *compiled, ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR));
    ASSERT_TRUE(hdrIndex.has_value());
    ASSERT_LT(*hdrIndex, compiled->TextureNames.size());
    EXPECT_EQ(compiled->TextureNames[*hdrIndex], "SceneColorHDR");

    const std::optional<std::uint32_t> sceneTableIndex =
        FindCompiledBufferIndexForRecipeId(recipe, *compiled, ToFrameResourceId(FrameRecipeResourceKind::SceneTable));
    ASSERT_TRUE(sceneTableIndex.has_value());
    ASSERT_LT(*sceneTableIndex, compiled->BufferNames.size());
    EXPECT_EQ(compiled->BufferNames[*sceneTableIndex], "GpuWorld.SceneTable");
}

TEST(FrameRecipeContract, CompiledPassLookupUsesTypedIdWhenDebugNameChanges)
{
    const FrameRecipeIntrospection recipe = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});
    RenderGraph graph;
    const PassRef renamedSurface = graph.AddPass("RenamedSurfacePass", true);
    ASSERT_TRUE(graph.SetPassId(renamedSurface, ToFramePassId(FrameRecipePassKind::Surface)).has_value());

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::optional<std::uint32_t> surfaceIndex =
        FindCompiledPassIndexForRecipeId(recipe, *compiled, ToFramePassId(FrameRecipePassKind::Surface));
    ASSERT_TRUE(surfaceIndex.has_value());
    ASSERT_LT(*surfaceIndex, compiled->PassIds.size());
    ASSERT_LT(*surfaceIndex, compiled->PassNames.size());
    EXPECT_EQ(compiled->PassIds[*surfaceIndex], ToFramePassId(FrameRecipePassKind::Surface));
    EXPECT_EQ(compiled->PassNames[*surfaceIndex], "RenamedSurfacePass");
}

TEST(FrameRecipeContract, CompiledResourceLookupUsesTypedIdWhenDebugNameChanges)
{
    const FrameRecipeIntrospection recipe = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});
    RenderGraph graph;
    const TextureRef renamedHdr = graph.CreateTexture("RenamedHdrTarget", RHI::TextureDesc{
        .Width = 32u,
        .Height = 32u,
        .Fmt = RHI::Format::RGBA16_FLOAT,
        .Usage = RHI::TextureUsage::ColorTarget | RHI::TextureUsage::Sampled,
        .DebugName = "RenamedHdrTarget",
    });
    ASSERT_TRUE(graph.SetTextureResourceId(
        renamedHdr,
        ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR)).has_value());
    const BufferRef renamedSceneTable = graph.CreateBuffer("RenamedSceneTable", RHI::BufferDesc{
        .SizeBytes = 64u,
        .Usage = RHI::BufferUsage::Storage,
        .DebugName = "RenamedSceneTable",
    });
    ASSERT_TRUE(graph.SetBufferResourceId(
        renamedSceneTable,
        ToFrameResourceId(FrameRecipeResourceKind::SceneTable)).has_value());

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::optional<std::uint32_t> hdrIndex =
        FindCompiledTextureIndexForRecipeId(
            recipe,
            *compiled,
            ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR));
    ASSERT_TRUE(hdrIndex.has_value());
    ASSERT_LT(*hdrIndex, compiled->TextureNames.size());
    ASSERT_LT(*hdrIndex, compiled->TextureResourceIds.size());
    EXPECT_EQ(compiled->TextureNames[*hdrIndex], "RenamedHdrTarget");
    EXPECT_EQ(compiled->TextureResourceIds[*hdrIndex],
              ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR));

    const std::optional<std::uint32_t> sceneTableIndex =
        FindCompiledBufferIndexForRecipeId(
            recipe,
            *compiled,
            ToFrameResourceId(FrameRecipeResourceKind::SceneTable));
    ASSERT_TRUE(sceneTableIndex.has_value());
    ASSERT_LT(*sceneTableIndex, compiled->BufferNames.size());
    ASSERT_LT(*sceneTableIndex, compiled->BufferResourceIds.size());
    EXPECT_EQ(compiled->BufferNames[*sceneTableIndex], "RenamedSceneTable");
    EXPECT_EQ(compiled->BufferResourceIds[*sceneTableIndex],
              ToFrameResourceId(FrameRecipeResourceKind::SceneTable));
}

TEST(FrameRecipeContract, TypedIdentityDoesNotChangeDebugNames)
{
    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        MakeImports(),
        FrameRecipeSizing{.Width = 320u, .Height = 180u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::string dump = BuildRenderGraphDebugDump(*compiled);
    EXPECT_NE(dump.find("name=\"CullingPass\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"SceneColorHDR\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"GpuWorld.SceneTable\""), std::string::npos);
    EXPECT_EQ(FrameRecipePassKindName(FrameRecipePassKind::PostProcess), std::string_view{"PostProcessPass"});
    EXPECT_EQ(FrameRecipeResourceKindName(FrameRecipeResourceKind::PostProcessHistogram),
              std::string_view{"PostProcess.Histogram"});
}

TEST(FrameRecipeContract, DuplicateTypedPassIdsAreDiagnosed)
{
    RenderGraph graph;
    const PassRef first = graph.AddPass("FirstTypedPass", true);
    const PassRef duplicate = graph.AddPass("RenamedDebugPass", true);
    ASSERT_TRUE(graph.SetPassId(first, FramePassId{77u}).has_value());
    ASSERT_TRUE(graph.SetPassId(duplicate, FramePassId{77u}).has_value());

    const auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());

    const RenderGraphValidationResult& result = graph.GetLastCompileValidationResult();
    ASSERT_EQ(result.Findings.size(), 1u);
    EXPECT_EQ(result.Findings[0].Code, RenderGraphValidationCode::DuplicatePassId);
    EXPECT_EQ(result.Findings[0].PassName, "RenamedDebugPass");
    EXPECT_NE(result.Findings[0].Message.find("FirstTypedPass"), std::string::npos);
}

TEST(FrameRecipeContract, DuplicateTypedResourceIdsAreDiagnosed)
{
    RenderGraph graph;
    const RHI::TextureDesc desc{
        .Width = 1u,
        .Height = 1u,
        .Fmt = RHI::Format::RGBA8_UNORM,
        .Usage = RHI::TextureUsage::ColorTarget,
        .DebugName = "DuplicateResource",
    };
    const TextureRef first = graph.CreateTexture("FirstTypedResource", desc);
    const TextureRef duplicate = graph.CreateTexture("RenamedDebugResource", desc);
    ASSERT_TRUE(graph.SetTextureResourceId(first, FrameResourceId{91u}).has_value());
    ASSERT_TRUE(graph.SetTextureResourceId(duplicate, FrameResourceId{91u}).has_value());

    const auto compiled = graph.Compile();
    EXPECT_FALSE(compiled.has_value());

    const RenderGraphValidationResult& result = graph.GetLastCompileValidationResult();
    ASSERT_EQ(result.Findings.size(), 1u);
    EXPECT_EQ(result.Findings[0].Code, RenderGraphValidationCode::DuplicateResourceId);
    EXPECT_EQ(result.Findings[0].ResourceName, "RenamedDebugResource");
    EXPECT_NE(result.Findings[0].Message.find("FirstTypedResource"), std::string::npos);
}

TEST(FrameRecipeContract, DefaultRecipeDoesNotDepthTransitionColorResources)
{
    RenderGraph graph;
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableShadows = true;
    features.EnableSelectionOutline = true;
    features.EnablePostProcess = true;
    features.EnableAntiAliasing = true;
    features.EnableTransientDebugSurface = true;
    features.EnableVisualizationOverlay = true;

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 256u, .Height = 256u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            if (!IsDepthBarrierState(barrier.Before) && !IsDepthBarrierState(barrier.After))
            {
                continue;
            }

            ASSERT_LT(barrier.TextureIndex, compiled->TextureNames.size());
            const std::string& name = compiled->TextureNames[barrier.TextureIndex];
            EXPECT_TRUE(name == "SceneDepth" || name == "ShadowAtlas")
                << "Texture \"" << name << "\" received a depth barrier in pass index "
                << packet.PassIndex << ": before=" << static_cast<int>(barrier.Before)
                << " after=" << static_cast<int>(barrier.After);
        }
    }
}

TEST(FrameRecipeContract, DefaultRecipeBarriersRespectTextureUsageCapabilities)
{
    RenderGraph graph;
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableShadows = true;
    features.EnableSelectionOutline = true;
    features.EnableDebugView = true;
    features.EnableImGui = true;
    features.EnablePostProcess = true;
    features.EnableAntiAliasing = true;
    features.EnableTransientDebugSurface = true;
    features.EnableVisualizationOverlay = true;
    const FrameRecipeAAOptions aaOptions{.Mode = FrameRecipeAAMode::SMAA};

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 256u, .Height = 256u},
        aaOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            ASSERT_LT(barrier.TextureIndex, compiled->TextureNames.size());
            const TextureResourceDesc* desc = graph.GetTextureDescByIndex(barrier.TextureIndex);
            ASSERT_NE(desc, nullptr);
            const std::string& name = compiled->TextureNames[barrier.TextureIndex];
            EXPECT_TRUE(TextureBarrierStateCompatibleWithUsage(barrier.Before, *desc))
                << "Texture \"" << name << "\" has incompatible source barrier state in pass index "
                << packet.PassIndex << ": state=" << static_cast<int>(barrier.Before);
            EXPECT_TRUE(TextureBarrierStateCompatibleWithUsage(barrier.After, *desc))
                << "Texture \"" << name << "\" has incompatible destination barrier state in pass index "
                << packet.PassIndex << ": state=" << static_cast<int>(barrier.After);
        }
    }
}

TEST(FrameRecipeContract, DefaultRecipeCompiledGraphHasNoValidationFindings)
{
    RenderGraph graph;
    const FrameRecipeFeatures features{};
    const FrameRecipeIntrospection recipe = DescribeDefaultFrameRecipe(features);
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 1280u, .Height = 720u});

    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(recipe, *compiled);
    EXPECT_FALSE(validation.HasErrors());
    EXPECT_FALSE(validation.HasWarnings());
    EXPECT_EQ(validation.CountBySeverity(RenderGraphValidationSeverity::Error), 0u);
    EXPECT_EQ(validation.CountBySeverity(RenderGraphValidationSeverity::Warning), 0u);
    EXPECT_TRUE(validation.Findings.empty());
}

TEST(FrameRecipeContract, ContributionDescriptorProjectsTypedResourcesIntoRecipeIntrospection)
{
    const FrameRecipeIntrospection baseRecipe = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});

    FrameRecipePassContributionRegistry registry{};
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = FramePassId{9000u},
            .Name = "RegisteredOverlayPass",
            .Enabled = true,
            .Queue = RenderQueue::AsyncCompute,
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = ToFramePassId(FrameRecipePassKind::Surface),
                .Placement = FrameRecipeContributionAnchorPlacement::After,
            },
            .Reads = {
                ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR),
                ToFrameResourceId(FrameRecipeResourceKind::SceneDepth),
            },
            .Writes = {
                ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR),
            },
        });

    const FrameRecipeContributionDescriptionResult result =
        DescribeFrameRecipeWithContributions(baseRecipe, registry.Passes);

    ASSERT_TRUE(result.Succeeded);
    EXPECT_FALSE(result.Validation.HasErrors());
    ASSERT_EQ(result.Recipe.Resources.size(), baseRecipe.Resources.size());
    ASSERT_EQ(result.Recipe.Passes.size(), baseRecipe.Passes.size() + 1u);

    const std::optional<std::uint32_t> surfaceIndex =
        FindFrameRecipePassIndexById(result.Recipe, ToFramePassId(FrameRecipePassKind::Surface));
    ASSERT_TRUE(surfaceIndex.has_value());
    ASSERT_LT(*surfaceIndex + 1u, result.Recipe.Passes.size());

    const FrameRecipePassDeclaration& contribution = result.Recipe.Passes[*surfaceIndex + 1u];
    EXPECT_EQ(contribution.Id, FramePassId{9000u});
    EXPECT_EQ(contribution.Name, std::string_view{"RegisteredOverlayPass"});
    EXPECT_TRUE(contribution.Enabled);
    EXPECT_TRUE(contribution.Contributed);
    EXPECT_EQ(contribution.Queue, RenderQueue::AsyncCompute);
    EXPECT_TRUE(Contains(contribution.Reads, "SceneColorHDR"));
    EXPECT_TRUE(Contains(contribution.Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(contribution.Writes, "SceneColorHDR"));

    ClearFrameRecipePassContributions(registry);
    EXPECT_TRUE(registry.Passes.empty());
}

TEST(FrameRecipeContract, DisabledContributionDescriptorIsFeatureGatedNoOp)
{
    const FrameRecipeIntrospection baseRecipe = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});

    FrameRecipePassContributionRegistry registry{};
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = {},
            .Name = {},
            .Enabled = false,
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = FramePassId{7777u},
                .Placement = FrameRecipeContributionAnchorPlacement::Before,
            },
            .Reads = {FrameResourceId{9999u}},
            .Writes = {FrameResourceId{9998u}},
        });

    const FrameRecipeContributionDescriptionResult result =
        DescribeFrameRecipeWithContributions(baseRecipe, registry.Passes);

    EXPECT_TRUE(result.Succeeded);
    EXPECT_FALSE(result.Validation.HasErrors());
    EXPECT_EQ(result.Recipe.Passes.size(), baseRecipe.Passes.size());
    EXPECT_EQ(result.Recipe.Resources.size(), baseRecipe.Resources.size());
}

TEST(FrameRecipeContract, ContributionValidationFailsClosedForInvalidDescriptors)
{
    const FrameRecipeIntrospection baseRecipe = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});

    FrameRecipePassContributionRegistry registry{};
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Surface,
            .Id = ToFramePassId(FrameRecipePassKind::Surface),
            .Name = "SurfaceReplacement",
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = ToFramePassId(FrameRecipePassKind::Present),
            },
            .Reads = {ToFrameResourceId(FrameRecipeResourceKind::SceneDepth)},
        });
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = FramePassId{9001u},
            .Name = "DuplicateContributionA",
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = ToFramePassId(FrameRecipePassKind::Present),
            },
            .Reads = {ToFrameResourceId(FrameRecipeResourceKind::SceneDepth)},
        });
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = FramePassId{9001u},
            .Name = "DuplicateContributionB",
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = ToFramePassId(FrameRecipePassKind::Present),
            },
            .Writes = {ToFrameResourceId(FrameRecipeResourceKind::SceneColorHDR)},
        });
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = FramePassId{9002u},
            .Name = "UnknownResourceContribution",
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = ToFramePassId(FrameRecipePassKind::Present),
            },
            .Reads = {FrameResourceId{9999u}},
        });
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = FramePassId{9003u},
            .Name = "InvalidAnchorContribution",
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = FramePassId{8888u},
                .Placement = FrameRecipeContributionAnchorPlacement::Before,
            },
            .Reads = {ToFrameResourceId(FrameRecipeResourceKind::SceneDepth)},
        });
    RegisterFrameRecipePassContribution(
        registry,
        FrameRecipePassContribution{
            .Kind = FrameRecipePassKind::Culling,
            .Id = {},
            .Name = {},
            .Anchor = FrameRecipeContributionAnchor{
                .PassId = ToFramePassId(FrameRecipePassKind::Present),
            },
        });

    const FrameRecipeContributionValidationResult validation =
        ValidateFrameRecipePassContributions(baseRecipe, registry.Passes);
    EXPECT_TRUE(validation.HasErrors());
    EXPECT_TRUE(HasContributionDiagnostic(validation, FrameRecipeContributionDiagnosticCode::FixedCorePassConflict));
    EXPECT_TRUE(HasContributionDiagnostic(validation, FrameRecipeContributionDiagnosticCode::DuplicatePassId));
    EXPECT_TRUE(HasContributionDiagnostic(validation, FrameRecipeContributionDiagnosticCode::UnknownResource));
    EXPECT_TRUE(HasContributionDiagnostic(validation, FrameRecipeContributionDiagnosticCode::InvalidAnchor));
    EXPECT_TRUE(HasContributionDiagnostic(validation, FrameRecipeContributionDiagnosticCode::InvalidPassId));
    EXPECT_TRUE(HasContributionDiagnostic(validation, FrameRecipeContributionDiagnosticCode::EmptyName));

    const FrameRecipeContributionDescriptionResult projected =
        DescribeFrameRecipeWithContributions(baseRecipe, registry.Passes);
    EXPECT_FALSE(projected.Succeeded);
    EXPECT_TRUE(projected.Validation.HasErrors());
    EXPECT_EQ(projected.Recipe.Passes.size(), baseRecipe.Passes.size())
        << "Invalid contributions must not partially mutate the recipe surface.";
    EXPECT_EQ(projected.Recipe.Resources.size(), baseRecipe.Resources.size());
}

TEST(FrameRecipeContract, DefaultOverlayContributionsProjectLegacyOverlayShape)
{
    FrameRecipeFeatures features{};
    features.EnableSelectionOutline = true;
    features.EnableDebugView = true;
    features.EnableTransientDebugSurface = true;
    features.EnableVisualizationOverlay = true;
    features.EnableAntiAliasing = true;
    const FrameRecipeAAOptions aaOptions{.Mode = FrameRecipeAAMode::SMAA};

    FrameRecipePassContributionRegistry registry{};
    RegisterDefaultFrameRecipeOverlayContributions(
        registry,
        features,
        aaOptions,
        FrameRecipeTemporalOptions{});
    ASSERT_EQ(registry.Passes.size(), 4u);

    const FrameRecipeContributionDescriptionResult described =
        DescribeDefaultFrameRecipeWithContributions(
            features,
            aaOptions,
            FrameRecipeTemporalOptions{},
            registry.Passes);
    ASSERT_TRUE(described.Succeeded);
    EXPECT_FALSE(described.Validation.HasErrors());

    const auto* visualization = FindPass(described.Recipe, FrameRecipePassKind::VisualizationOverlay);
    ASSERT_NE(visualization, nullptr);
    EXPECT_TRUE(visualization->Enabled);
    EXPECT_TRUE(visualization->Contributed);
    EXPECT_TRUE(Contains(visualization->Reads, "SceneColorHDR"));
    EXPECT_TRUE(Contains(visualization->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(visualization->Writes, "SceneColorHDR"));

    const auto* outline = FindPass(described.Recipe, FrameRecipePassKind::SelectionOutline);
    ASSERT_NE(outline, nullptr);
    EXPECT_TRUE(outline->Enabled);
    EXPECT_TRUE(outline->Contributed);
    EXPECT_TRUE(Contains(outline->Reads, "EntityId"));
    EXPECT_TRUE(Contains(outline->Reads, "FrameRecipe.PresentSource"));
    EXPECT_TRUE(Contains(outline->Writes, "FrameRecipe.PresentSource"));

    const auto* debug = FindPass(described.Recipe, FrameRecipePassKind::DebugView);
    ASSERT_NE(debug, nullptr);
    EXPECT_TRUE(debug->Enabled);
    EXPECT_TRUE(debug->Contributed);
    EXPECT_TRUE(Contains(debug->Reads, "FrameRecipe.PresentSource"));
    EXPECT_TRUE(Contains(debug->Writes, "DebugViewRGBA"));

    const auto* imgui = FindPass(described.Recipe, FrameRecipePassKind::ImGui);
    ASSERT_NE(imgui, nullptr);
    EXPECT_TRUE(imgui->Enabled);
    EXPECT_TRUE(imgui->Contributed);
    EXPECT_TRUE(Contains(imgui->Reads, "FrameRecipe.PresentSource"));
    EXPECT_TRUE(Contains(imgui->Writes, "FrameRecipe.PresentSource"));
}

TEST(FrameRecipeContract, ExplicitDefaultOverlayContributionsMatchDefaultBuildShape)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableSelectionOutline = true;
    features.EnableDebugView = true;
    features.EnableTransientDebugSurface = true;
    features.EnableVisualizationOverlay = true;
    features.EnableAntiAliasing = true;
    const FrameRecipeAAOptions aaOptions{.Mode = FrameRecipeAAMode::SMAA};

    RenderGraph defaultGraph;
    const FrameRecipeBuildResult defaultBuild = BuildDefaultFrameRecipe(
        defaultGraph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        aaOptions);
    ASSERT_TRUE(defaultBuild.Succeeded) << defaultBuild.Diagnostic;
    const auto defaultCompiled = defaultGraph.Compile();
    ASSERT_TRUE(defaultCompiled.has_value());

    FrameRecipePassContributionRegistry registry{};
    RegisterDefaultFrameRecipeOverlayContributions(
        registry,
        features,
        aaOptions,
        FrameRecipeTemporalOptions{});

    RenderGraph explicitGraph;
    const FrameRecipeBuildResult explicitBuild =
        BuildDefaultFrameRecipeWithContributions(
            explicitGraph,
            features,
            MakeImports(),
            FrameRecipeSizing{.Width = 640u, .Height = 360u},
            aaOptions,
            FrameRecipeShadowSizing{},
            FrameRecipeTemporalOptions{},
            registry.Passes);
    ASSERT_TRUE(explicitBuild.Succeeded) << explicitBuild.Diagnostic;
    const auto explicitCompiled = explicitGraph.Compile();
    ASSERT_TRUE(explicitCompiled.has_value());

    EXPECT_EQ(explicitBuild.DeclaredPassCount, defaultBuild.DeclaredPassCount);
    EXPECT_EQ(explicitBuild.DeclaredResourceCount, defaultBuild.DeclaredResourceCount);
    EXPECT_EQ(OrderedPassNames(*explicitCompiled), OrderedPassNames(*defaultCompiled));
    EXPECT_EQ(BuildRenderGraphDebugDump(*explicitCompiled), BuildRenderGraphDebugDump(*defaultCompiled));
}

TEST(FrameRecipeContract, EmptyContributionRegistryCompilesOverlayAbsentCoreRecipe)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = false;
    features.EnableSelectionOutline = true;
    features.EnableDebugView = true;
    features.EnableImGui = true;
    features.EnableVisualizationOverlay = true;

    const std::vector<FrameRecipePassContribution> noContributions{};
    const FrameRecipeContributionDescriptionResult described =
        DescribeDefaultFrameRecipeWithContributions(features, noContributions);
    ASSERT_TRUE(described.Succeeded);
    EXPECT_EQ(FindPass(described.Recipe, FrameRecipePassKind::SelectionOutline), nullptr);
    EXPECT_EQ(FindPass(described.Recipe, FrameRecipePassKind::DebugView), nullptr);
    EXPECT_EQ(FindPass(described.Recipe, FrameRecipePassKind::ImGui), nullptr);
    EXPECT_EQ(FindPass(described.Recipe, FrameRecipePassKind::VisualizationOverlay), nullptr);

    RenderGraph graph;
    const FrameRecipeBuildResult build =
        BuildDefaultFrameRecipeWithContributions(
            graph,
            features,
            MakeImports(),
            FrameRecipeSizing{.Width = 640u, .Height = 480u},
            FrameRecipeAAOptions{},
            FrameRecipeShadowSizing{},
            FrameRecipeTemporalOptions{},
            noContributions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(described.Recipe, *compiled);
    EXPECT_FALSE(validation.HasErrors());
    EXPECT_FALSE(validation.HasWarnings());

    std::vector<std::uint32_t> executedPasses{};
    RenderGraphExecutor executor;
    const auto result = executor.Execute(
        *compiled,
        [&executedPasses](const std::uint32_t passIndex) { executedPasses.push_back(passIndex); },
        {});
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(executedPasses, compiled->TopologicalOrder);

    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    EXPECT_EQ(std::ranges::find(passNames, "SelectionOutlinePass"), passNames.end());
    EXPECT_EQ(std::ranges::find(passNames, "DebugViewPass"), passNames.end());
    EXPECT_EQ(std::ranges::find(passNames, "ImGuiPass"), passNames.end());
    EXPECT_EQ(std::ranges::find(passNames, "VisualizationOverlayPass"), passNames.end());
    EXPECT_EQ(std::ranges::find(passNames, "PickingPass"), passNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "EntityId"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "DebugViewRGBA"), compiled->TextureNames.end());
}

TEST(FrameRecipeContract, DefaultRecipeUsesResourceDependenciesWithoutLinearChain)
{
    RenderGraph graph;
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableShadows = true;
    features.EnableSelectionOutline = true;
    features.EnableDebugView = true;
    features.EnableAntiAliasing = true;
    features.EnableTransientDebugSurface = true;
    features.EnableVisualizationOverlay = true;
    features.EnableHZBBuild = true;
    features.EnableClusterGridBuild = true;
    features.EnableClusterLightAssignment = true;
    const FrameRecipeAAOptions aaOptions{.Mode = FrameRecipeAAMode::SMAA};

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        aaOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    EXPECT_EQ(ExplicitDependencyCount(*compiled), 0u)
        << "Default recipe should not serialize every pass through explicit previous-pass dependencies.";
    EXPECT_GT(compiled->EdgeCount, 0u);

    ExpectPassBefore(*compiled, "DepthPrepass", "HZBBuildPass");
    ExpectPassBefore(*compiled, "DepthPrepass", "PickingPass");
    ExpectPassBefore(*compiled, "ClusterGridBuildPass", "LightClusterAssignmentPass");
    ExpectPassBefore(*compiled, "LightClusterAssignmentPass", "CompositionPass");
    ExpectPassBefore(*compiled, "ShadowPass", "CompositionPass");
    ExpectPassBefore(*compiled, "CompositionPass", "LinePass");
    ExpectPassBefore(*compiled, "LinePass", "PointPass");
    ExpectPassBefore(*compiled, "PointPass", "TransientDebugSurfacePass");
    ExpectPassBefore(*compiled, "TransientDebugSurfacePass", "VisualizationOverlayPass");
    ExpectPassBefore(*compiled, "VisualizationOverlayPass", "PostProcessPass");
    ExpectPassBefore(*compiled, "PostProcessPass", "PostProcessAAEdgePass");
    ExpectPassBefore(*compiled, "PostProcessAAEdgePass", "PostProcessAABlendPass");
    ExpectPassBefore(*compiled, "PostProcessAABlendPass", "PostProcessAAResolvePass");
    ExpectPassBefore(*compiled, "PostProcessAAResolvePass", "SelectionOutlinePass");
    ExpectPassBefore(*compiled, "SelectionOutlinePass", "DebugViewPass");
    ExpectPassBefore(*compiled, "DebugViewPass", "ImGuiPass");
    ExpectPassBefore(*compiled, "ImGuiPass", "Present");
}

TEST(FrameRecipeContract, DependencyDrivenDefaultRecipeKeepsBarrierPacketsTopological)
{
    RenderGraph graph;
    FrameRecipeFeatures features{};
    features.EnableAntiAliasing = true;
    const FrameRecipeAAOptions aaOptions{.Mode = FrameRecipeAAMode::SMAA};

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        aaOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    const std::vector<std::uint32_t> ranks = RankByPass(*compiled);

    std::uint32_t previousRank = 0u;
    BarrierPacketStage previousStage = BarrierPacketStage::BeforePass;
    bool firstPacket = true;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        const bool terminalPacket = packet.PassIndex == compiled->PassCount;
        ASSERT_TRUE(packet.PassIndex < ranks.size() || terminalPacket);
        const std::uint32_t packetRank = terminalPacket
            ? std::numeric_limits<std::uint32_t>::max()
            : ranks[packet.PassIndex];
        if (!terminalPacket)
        {
            ASSERT_NE(packetRank, std::numeric_limits<std::uint32_t>::max());
        }
        if (!firstPacket)
        {
            EXPECT_TRUE(packetRank > previousRank ||
                        (packetRank == previousRank &&
                         static_cast<std::uint8_t>(packet.Stage) >= static_cast<std::uint8_t>(previousStage)))
                << "Barrier packet order regressed after dependency-driven recipe build.";
        }
        firstPacket = false;
        previousRank = packetRank;
        previousStage = packet.Stage;
    }

    const std::uint32_t edgePass = PassIndexByName(*compiled, "PostProcessAAEdgePass");
    const std::uint32_t ldrTexture = TextureIndexByName(*compiled, "SceneColorLDR");
    ASSERT_LT(edgePass, compiled->PassNames.size());
    ASSERT_LT(ldrTexture, compiled->TextureNames.size());

    bool foundLdrShaderReadBarrier = false;
    for (const BarrierPacket& packet : compiled->BarrierPackets)
    {
        if (packet.PassIndex != edgePass || packet.Stage != BarrierPacketStage::BeforePass)
        {
            continue;
        }
        for (const TextureBarrierPacket& barrier : packet.TextureBarriers)
        {
            foundLdrShaderReadBarrier = foundLdrShaderReadBarrier ||
                (barrier.TextureIndex == ldrTexture &&
                 barrier.Before == TextureBarrierState::ColorAttachmentWrite &&
                 barrier.After == TextureBarrierState::ShaderRead);
        }
    }
    EXPECT_TRUE(foundLdrShaderReadBarrier)
        << "AA edge pass should still receive SceneColorLDR ColorAttachmentWrite -> ShaderRead barrier.";
}

TEST(FrameRecipeContract, RenderGraphDebugDumpReportsExplicitDependencyState)
{
    RenderGraph graph;
    const PassRef producer = graph.AddPass("Producer", [](RenderGraphBuilder& builder) {
        builder.SideEffect();
    }, true);
    const PassRef consumer = graph.AddPass("Consumer", [producer](RenderGraphBuilder& builder) {
        builder.DependsOn(producer);
        builder.SideEffect();
    }, true);
    (void)consumer;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::string dump = BuildRenderGraphDebugDump(*compiled);
    EXPECT_NE(dump.find("name=\"Producer\""), std::string::npos);
    EXPECT_NE(dump.find("name=\"Consumer\""), std::string::npos);
    EXPECT_NE(dump.find("explicit_dependencies: none"), std::string::npos);
    EXPECT_NE(dump.find("explicit_dependencies: 0(\"Producer\")"), std::string::npos);
}

TEST(FrameRecipeContract, DefaultRecipeDrawPassesDeclareRenderPassAttachments)
{
    RenderGraph graph;
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableShadows = true;
    features.EnableSelectionOutline = true;
    features.EnableDebugView = true;
    features.EnableAntiAliasing = true;
    features.EnableTransientDebugSurface = true;
    features.EnableVisualizationOverlay = true;

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 1280u, .Height = 720u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::vector<std::string_view> drawPasses{
        "DepthPrepass",
        "PickingPass",
        "ShadowPass",
        "SurfacePass",
        "LinePass",
        "PointPass",
        "TransientDebugSurfacePass",
        "VisualizationOverlayPass",
        "PostProcessPass",
        "SelectionOutlinePass",
        "DebugViewPass",
        "Present",
    };

    for (const std::string_view passName : drawPasses)
    {
        EXPECT_TRUE(HasCompiledRenderPassAttachment(*compiled, passName))
            << "Default-recipe graphics pass \"" << passName
            << "\" compiled without a render-pass attachment; Vulkan draw commands would record outside dynamic rendering.";
    }
}

TEST(FrameRecipeContract, SpatialAARecipeDrawPassesDeclareRenderPassAttachments)
{
    RenderGraph graph;
    FrameRecipeFeatures features{};
    features.EnableAntiAliasing = true;
    const FrameRecipeAAOptions aaOptions{.Mode = FrameRecipeAAMode::SMAA};

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 1280u, .Height = 720u},
        aaOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    for (const std::string_view passName : {
             std::string_view{"PostProcessAAEdgePass"},
             std::string_view{"PostProcessAABlendPass"},
             std::string_view{"PostProcessAAResolvePass"},
         })
    {
        EXPECT_TRUE(HasCompiledRenderPassAttachment(*compiled, passName))
            << "Spatial-AA graph pass \"" << passName
            << "\" compiled without a render-pass attachment.";
    }
}

TEST(FrameRecipeContract, OptionalResourcesAreGatedByFeatures)
{
    const FrameRecipeIntrospection defaults = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::EntityId));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ShadowAtlas));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::SelectionOutline));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::DebugViewRGBA));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::HZBCurrent));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ClusterGridAABBs));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ClusterLightHeaders));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ClusterLightIndices));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ClusterLightCounter));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::MotionVectors));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ReconstructionHistoryPrevious));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ReconstructionHistoryCurrent));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ReconstructionResolvedHDR));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::PostProcessAATempEdges));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::PostProcessAATempWeights));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::PostProcessAATempResolved));
    EXPECT_TRUE(HasEnabledResource(defaults, FrameRecipeResourceKind::SceneColorLDR));

    FrameRecipeFeatures allFeatures{};
    allFeatures.EnablePicking = true;
    allFeatures.EnableShadows = true;
    allFeatures.EnableSelectionOutline = true;
    allFeatures.EnableDebugView = true;
    allFeatures.EnableHZBBuild = true;
    allFeatures.EnableClusterGridBuild = true;
    allFeatures.EnableClusterLightAssignment = true;

    const FrameRecipeIntrospection full = DescribeDefaultFrameRecipe(
        allFeatures,
        FrameRecipeTemporalOptions{.EnableMotionVectors = true});
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::EntityId));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::ShadowAtlas));
    EXPECT_FALSE(HasEnabledResource(full, FrameRecipeResourceKind::SelectionOutline));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::DebugViewRGBA));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::HZBCurrent));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::ClusterGridAABBs));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::ClusterLightHeaders));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::ClusterLightIndices));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::ClusterLightCounter));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::MotionVectors));

    const FrameRecipeIntrospection smaa = DescribeDefaultFrameRecipe(
        FrameRecipeFeatures{},
        FrameRecipeAAOptions{.Mode = FrameRecipeAAMode::SMAA});
    EXPECT_TRUE(HasEnabledResource(smaa, FrameRecipeResourceKind::PostProcessAATempEdges));
    EXPECT_TRUE(HasEnabledResource(smaa, FrameRecipeResourceKind::PostProcessAATempWeights));
    EXPECT_TRUE(HasEnabledResource(smaa, FrameRecipeResourceKind::PostProcessAATempResolved));

    const FrameRecipeAAOptions temporalAA{
        .Mode = FrameRecipeAAMode::TAA,
        .ReconstructionHistoryPrevious = RHI::TextureHandle{30u, 1u},
        .ReconstructionHistoryCurrent = RHI::TextureHandle{31u, 1u},
    };
    const FrameRecipeIntrospection temporal = DescribeDefaultFrameRecipe(FrameRecipeFeatures{}, temporalAA);
    EXPECT_TRUE(HasEnabledResource(temporal, FrameRecipeResourceKind::MotionVectors));
    EXPECT_TRUE(HasEnabledResource(temporal, FrameRecipeResourceKind::ReconstructionHistoryPrevious));
    EXPECT_TRUE(HasEnabledResource(temporal, FrameRecipeResourceKind::ReconstructionHistoryCurrent));
    EXPECT_TRUE(HasEnabledResource(temporal, FrameRecipeResourceKind::ReconstructionResolvedHDR));
    EXPECT_FALSE(HasEnabledResource(temporal, FrameRecipeResourceKind::PostProcessAATempEdges));
    EXPECT_FALSE(HasEnabledResource(temporal, FrameRecipeResourceKind::PostProcessAATempWeights));
    EXPECT_FALSE(HasEnabledResource(temporal, FrameRecipeResourceKind::PostProcessAATempResolved));
}

TEST(FrameRecipeContract, AAModeSelectorCompilesExpectedPassSets)
{
    auto buildFor = [](RenderGraph& graph, const FrameRecipeAAMode mode) {
        FrameRecipeAAOptions aaOptions{
            .Mode = mode,
            .ReconstructionHistoryPrevious = RHI::TextureHandle{30u, 1u},
            .ReconstructionHistoryCurrent = RHI::TextureHandle{31u, 1u},
        };
        return BuildDefaultFrameRecipe(
            graph,
            FrameRecipeFeatures{},
            MakeImports(),
            FrameRecipeSizing{.Width = 320u, .Height = 180u},
            aaOptions);
    };

    {
        RenderGraph graph;
        const FrameRecipeBuildResult build = buildFor(graph, FrameRecipeAAMode::NoAA);
        ASSERT_TRUE(build.Succeeded);
        const auto compiled = graph.Compile();
        ASSERT_TRUE(compiled.has_value());
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAAEdgePass"));
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAABlendPass"));
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAAResolvePass"));
        EXPECT_FALSE(HasPassName(*compiled, "ReconstructionPass"));
    }

    {
        RenderGraph graph;
        const FrameRecipeBuildResult build = buildFor(graph, FrameRecipeAAMode::FXAA);
        ASSERT_TRUE(build.Succeeded);
        const auto compiled = graph.Compile();
        ASSERT_TRUE(compiled.has_value());
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAAEdgePass"));
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAABlendPass"));
        EXPECT_TRUE(HasPassName(*compiled, "PostProcessAAResolvePass"));
        EXPECT_FALSE(HasPassName(*compiled, "ReconstructionPass"));
        EXPECT_TRUE(PassReadsTexture(*compiled, "PostProcessAAResolvePass", "SceneColorLDR"));
        EXPECT_FALSE(PassReadsTexture(*compiled, "PostProcessAAResolvePass", "PostProcess.AATemp.Weights"));
    }

    {
        RenderGraph graph;
        const FrameRecipeBuildResult build = buildFor(graph, FrameRecipeAAMode::SMAA);
        ASSERT_TRUE(build.Succeeded);
        const auto compiled = graph.Compile();
        ASSERT_TRUE(compiled.has_value());
        EXPECT_TRUE(HasPassName(*compiled, "PostProcessAAEdgePass"));
        EXPECT_TRUE(HasPassName(*compiled, "PostProcessAABlendPass"));
        EXPECT_TRUE(HasPassName(*compiled, "PostProcessAAResolvePass"));
        EXPECT_FALSE(HasPassName(*compiled, "ReconstructionPass"));
        EXPECT_TRUE(PassReadsTexture(*compiled, "PostProcessAAResolvePass", "PostProcess.AATemp.Weights"));
    }

    for (const FrameRecipeAAMode mode : {
             FrameRecipeAAMode::TAA,
             FrameRecipeAAMode::ExternalReconstructor,
         })
    {
        RenderGraph graph;
        const FrameRecipeBuildResult build = buildFor(graph, mode);
        ASSERT_TRUE(build.Succeeded);
        const auto compiled = graph.Compile();
        ASSERT_TRUE(compiled.has_value());
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAAEdgePass"));
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAABlendPass"));
        EXPECT_FALSE(HasPassName(*compiled, "PostProcessAAResolvePass"));
        EXPECT_TRUE(HasPassName(*compiled, "ReconstructionPass"));
        EXPECT_TRUE(PassReadsTexture(*compiled, "ReconstructionPass", "MotionVectors"));
        EXPECT_TRUE(PassWritesTexture(*compiled, "ReconstructionPass", "Reconstruction.ResolvedHDR"));
        EXPECT_TRUE(PassReadsTexture(*compiled, "PostProcessPass", "Reconstruction.ResolvedHDR"));
    }
}

TEST(FrameRecipeContract, TemporalReconstructionUsesInputAndOutputExtents)
{
    RenderGraph graph;
    const FrameRecipeAAOptions aaOptions{
        .Mode = FrameRecipeAAMode::TAA,
        .InputWidth = 1280u,
        .InputHeight = 720u,
        .ReconstructionHistoryPrevious = RHI::TextureHandle{30u, 1u},
        .ReconstructionHistoryCurrent = RHI::TextureHandle{31u, 1u},
    };
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        MakeImports(),
        FrameRecipeSizing{.Width = 1920u, .Height = 1080u},
        aaOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const TextureResourceDesc* hdr = TextureDescByName(graph, *compiled, "SceneColorHDR");
    const TextureResourceDesc* depth = TextureDescByName(graph, *compiled, "SceneDepth");
    const TextureResourceDesc* motion = TextureDescByName(graph, *compiled, "MotionVectors");
    const TextureResourceDesc* reconstruction = TextureDescByName(graph, *compiled, "Reconstruction.ResolvedHDR");
    const TextureResourceDesc* ldr = TextureDescByName(graph, *compiled, "SceneColorLDR");
    ASSERT_NE(hdr, nullptr);
    ASSERT_NE(depth, nullptr);
    ASSERT_NE(motion, nullptr);
    ASSERT_NE(reconstruction, nullptr);
    ASSERT_NE(ldr, nullptr);

    EXPECT_EQ(hdr->Desc.Width, 1280u);
    EXPECT_EQ(hdr->Desc.Height, 720u);
    EXPECT_EQ(depth->Desc.Width, 1280u);
    EXPECT_EQ(depth->Desc.Height, 720u);
    EXPECT_EQ(motion->Desc.Width, 1280u);
    EXPECT_EQ(motion->Desc.Height, 720u);
    EXPECT_EQ(reconstruction->Desc.Width, 1920u);
    EXPECT_EQ(reconstruction->Desc.Height, 1080u);
    EXPECT_EQ(reconstruction->Desc.Fmt, RHI::Format::RGBA16_FLOAT);
    EXPECT_TRUE(HasTextureUsage(reconstruction->Desc.Usage, RHI::TextureUsage::Storage));
    EXPECT_TRUE(HasTextureUsage(reconstruction->Desc.Usage, RHI::TextureUsage::Sampled));
    EXPECT_EQ(ldr->Desc.Width, 1920u);
    EXPECT_EQ(ldr->Desc.Height, 1080u);

    const std::vector<std::string> order = OrderedPassNames(*compiled);
    const auto pointIt = std::ranges::find(order, "PointPass");
    const auto reconstructionIt = std::ranges::find(order, "ReconstructionPass");
    const auto histogramIt = std::ranges::find(order, "PostProcessHistogramPass");
    const auto postIt = std::ranges::find(order, "PostProcessPass");
    ASSERT_NE(pointIt, order.end());
    ASSERT_NE(reconstructionIt, order.end());
    ASSERT_NE(histogramIt, order.end());
    ASSERT_NE(postIt, order.end());
    EXPECT_LT(pointIt, reconstructionIt);
    EXPECT_LT(reconstructionIt, histogramIt);
    EXPECT_LT(histogramIt, postIt);
}

TEST(FrameRecipeContract, NoJitterNoHistorySuppressesTemporalReconstruction)
{
    RenderGraph graph;
    const FrameRecipeAAOptions aaOptions{
        .Mode = FrameRecipeAAMode::TAA,
        .ReconstructionHistoryPrevious = RHI::TextureHandle{30u, 1u},
        .ReconstructionHistoryCurrent = RHI::TextureHandle{31u, 1u},
    };
    constexpr FrameRecipeTemporalOptions temporalOptions{.NoJitterNoHistory = true};

    const FrameRecipeIntrospection description =
        DescribeDefaultFrameRecipe(FrameRecipeFeatures{}, aaOptions, temporalOptions);
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::MotionVectors));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::ReconstructionHistoryPrevious));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::ReconstructionHistoryCurrent));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::ReconstructionResolvedHDR));
    const auto* reconstruction = FindPass(description, FrameRecipePassKind::Reconstruction);
    ASSERT_NE(reconstruction, nullptr);
    EXPECT_FALSE(reconstruction->Enabled);

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        aaOptions,
        FrameRecipeShadowSizing{},
        temporalOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());
    EXPECT_FALSE(HasPassName(*compiled, "ReconstructionPass"));
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "MotionVectors"),
              compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "Reconstruction.ResolvedHDR"),
              compiled->TextureNames.end());
}

TEST(FrameRecipeContract, TemporalReconstructionRequiresHistoryImports)
{
    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        FrameRecipeAAOptions{.Mode = FrameRecipeAAMode::TAA});

    EXPECT_FALSE(build.Succeeded);
    EXPECT_NE(build.Diagnostic.find("Reconstruction history"), std::string::npos);
}

TEST(FrameRecipeContract, LightingPathControlsGBufferResourcesAndComposition)
{
    FrameRecipeFeatures forwardFeatures{};
    forwardFeatures.LightingPath = FrameRecipeLightingPath::Forward;
    const FrameRecipeIntrospection forward = DescribeDefaultFrameRecipe(forwardFeatures);
    EXPECT_FALSE(HasEnabledResource(forward, FrameRecipeResourceKind::SceneNormal));
    EXPECT_FALSE(HasEnabledResource(forward, FrameRecipeResourceKind::Albedo));
    EXPECT_FALSE(HasEnabledResource(forward, FrameRecipeResourceKind::Material0));
    const auto* forwardComposition = FindPass(forward, FrameRecipePassKind::Composition);
    ASSERT_NE(forwardComposition, nullptr);
    EXPECT_FALSE(forwardComposition->Enabled);
    const auto* forwardSurface = FindPass(forward, FrameRecipePassKind::Surface);
    ASSERT_NE(forwardSurface, nullptr);
    EXPECT_TRUE(Contains(forwardSurface->Writes, "SceneColorHDR"));
    EXPECT_TRUE(Contains(forwardSurface->Writes, "SceneDepth"));

    for (const FrameRecipeLightingPath path : {FrameRecipeLightingPath::Deferred, FrameRecipeLightingPath::Hybrid})
    {
        FrameRecipeFeatures features{};
        features.LightingPath = path;
        const FrameRecipeIntrospection deferredLike = DescribeDefaultFrameRecipe(features);
        EXPECT_TRUE(HasEnabledResource(deferredLike, FrameRecipeResourceKind::SceneNormal));
        EXPECT_TRUE(HasEnabledResource(deferredLike, FrameRecipeResourceKind::Albedo));
        EXPECT_TRUE(HasEnabledResource(deferredLike, FrameRecipeResourceKind::Material0));
        const auto* composition = FindPass(deferredLike, FrameRecipePassKind::Composition);
        ASSERT_NE(composition, nullptr);
        EXPECT_TRUE(composition->Enabled);
        const auto* surface = FindPass(deferredLike, FrameRecipePassKind::Surface);
        ASSERT_NE(surface, nullptr);
        EXPECT_TRUE(Contains(surface->Writes, "SceneNormal"));
        EXPECT_TRUE(Contains(surface->Writes, "Albedo"));
        EXPECT_TRUE(Contains(surface->Writes, "Material0"));
    }
}

TEST(FrameRecipeContract, MotionVectorTargetIsOptInRg16SurfaceOutput)
{
    FrameRecipeFeatures features{};
    features.LightingPath = FrameRecipeLightingPath::Forward;
    constexpr FrameRecipeTemporalOptions temporalOptions{.EnableMotionVectors = true};

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features, temporalOptions);
    const auto* motion = FindResource(description, FrameRecipeResourceKind::MotionVectors);
    ASSERT_NE(motion, nullptr);
    EXPECT_TRUE(motion->Enabled);
    EXPECT_TRUE(motion->Optional);
    EXPECT_FALSE(motion->Imported);
    EXPECT_EQ(motion->Name, std::string_view{"MotionVectors"});

    const auto* surface = FindPass(description, FrameRecipePassKind::Surface);
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(Contains(surface->Writes, "SceneColorHDR"));
    EXPECT_TRUE(Contains(surface->Writes, "MotionVectors"));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        FrameRecipeShadowSizing{},
        temporalOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const std::uint32_t textureIndex = TextureIndexByName(*compiled, "MotionVectors");
    ASSERT_LT(textureIndex, compiled->TextureNames.size());
    const TextureResourceDesc* textureDesc = graph.GetTextureDescByIndex(textureIndex);
    ASSERT_NE(textureDesc, nullptr);
    EXPECT_EQ(textureDesc->Desc.Width, 640u);
    EXPECT_EQ(textureDesc->Desc.Height, 360u);
    EXPECT_EQ(textureDesc->Desc.Fmt, RHI::Format::RG16_FLOAT);
    EXPECT_TRUE(HasTextureUsage(textureDesc->Desc.Usage, RHI::TextureUsage::ColorTarget));
    EXPECT_TRUE(HasTextureUsage(textureDesc->Desc.Usage, RHI::TextureUsage::Sampled));

    const std::uint32_t surfaceIndex = PassIndexByName(*compiled, "SurfacePass");
    ASSERT_LT(surfaceIndex, compiled->PassNames.size());
    bool foundMotionAttachment = false;
    for (const CompiledRenderPassAttachment& attachment : compiled->RenderPassAttachments)
    {
        if (attachment.PassIndex == surfaceIndex &&
            !attachment.IsDepthAttachment &&
            attachment.ResourceIndex == textureIndex)
        {
            foundMotionAttachment = true;
            EXPECT_EQ(attachment.Format, RHI::Format::RG16_FLOAT);
        }
    }
    EXPECT_TRUE(foundMotionAttachment);
}

TEST(FrameRecipeContract, NoJitterNoHistorySuppressesMotionVectorTarget)
{
    FrameRecipeFeatures features{};
    constexpr FrameRecipeTemporalOptions temporalOptions{
        .NoJitterNoHistory = true,
        .EnableMotionVectors = true,
    };

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features, temporalOptions);
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::MotionVectors));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 360u},
        FrameRecipeShadowSizing{},
        temporalOptions);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "MotionVectors"),
              compiled->TextureNames.end());
}

TEST(FrameRecipeContract, DepthPrepassFeatureGatesPassAndSurfaceDepthOwnership)
{
    FrameRecipeFeatures features{};
    features.EnableDepthPrepass = false;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* depth = FindPass(description, FrameRecipePassKind::DepthPrepass);
    ASSERT_NE(depth, nullptr);
    EXPECT_FALSE(depth->Enabled);

    const auto* surface = FindPass(description, FrameRecipePassKind::Surface);
    ASSERT_NE(surface, nullptr);
    EXPECT_TRUE(Contains(surface->Writes, "SceneDepth"));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    EXPECT_EQ(std::ranges::find(passNames, "DepthPrepass"), passNames.end());
    EXPECT_NE(std::ranges::find(passNames, "SurfacePass"), passNames.end());
}

TEST(FrameRecipeContract, HZBBuildRequiresDepthPrepassAndImportedTarget)
{
    FrameRecipeFeatures features{};
    features.EnableHZBBuild = true;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* hzbPass = FindPass(description, FrameRecipePassKind::HZBBuild);
    ASSERT_NE(hzbPass, nullptr);
    EXPECT_TRUE(hzbPass->Enabled);
    EXPECT_TRUE(Contains(hzbPass->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(hzbPass->Writes, "HZB.Current"));

    const auto* hzbResource = FindResource(description, FrameRecipeResourceKind::HZBCurrent);
    ASSERT_NE(hzbResource, nullptr);
    EXPECT_TRUE(hzbResource->Enabled);
    EXPECT_TRUE(hzbResource->Imported);
    EXPECT_TRUE(hzbResource->Optional);
    EXPECT_TRUE(hzbResource->ImportedWriteAllowed);

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    const auto depthIt = std::ranges::find(passNames, "DepthPrepass");
    const auto hzbIt = std::ranges::find(passNames, "HZBBuildPass");
    const auto surfaceIt = std::ranges::find(passNames, "SurfacePass");
    ASSERT_NE(depthIt, passNames.end());
    ASSERT_NE(hzbIt, passNames.end());
    ASSERT_NE(surfaceIt, passNames.end());
    EXPECT_LT(depthIt, hzbIt);
    EXPECT_LT(hzbIt, surfaceIt);
    EXPECT_NE(std::ranges::find(compiled->TextureNames, "HZB.Current"), compiled->TextureNames.end());

    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(description, *compiled);
    EXPECT_FALSE(validation.HasErrors())
        << (validation.Findings.empty() ? "<no findings>" : validation.Findings.front().Message);

    FrameRecipeImports missingImport = MakeImports();
    missingImport.HZBCurrent = {};
    RenderGraph missingImportGraph;
    const FrameRecipeBuildResult missingImportBuild = BuildDefaultFrameRecipe(
        missingImportGraph,
        features,
        missingImport,
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(missingImportBuild.Succeeded) << missingImportBuild.Diagnostic;
    const auto missingImportCompiled = missingImportGraph.Compile();
    ASSERT_TRUE(missingImportCompiled.has_value());
    const std::vector<std::string> missingImportPassNames = OrderedPassNames(*missingImportCompiled);
    EXPECT_EQ(std::ranges::find(missingImportPassNames, "HZBBuildPass"), missingImportPassNames.end());

    features.EnableDepthPrepass = false;
    const FrameRecipeIntrospection depthDisabled = DescribeDefaultFrameRecipe(features);
    const auto* depthDisabledHZB = FindPass(depthDisabled, FrameRecipePassKind::HZBBuild);
    ASSERT_NE(depthDisabledHZB, nullptr);
    EXPECT_FALSE(depthDisabledHZB->Enabled);
    EXPECT_FALSE(HasEnabledResource(depthDisabled, FrameRecipeResourceKind::HZBCurrent));

    RenderGraph depthDisabledGraph;
    const FrameRecipeBuildResult depthDisabledBuild = BuildDefaultFrameRecipe(
        depthDisabledGraph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(depthDisabledBuild.Succeeded) << depthDisabledBuild.Diagnostic;
    const auto depthDisabledCompiled = depthDisabledGraph.Compile();
    ASSERT_TRUE(depthDisabledCompiled.has_value());
    const std::vector<std::string> depthDisabledPassNames = OrderedPassNames(*depthDisabledCompiled);
    EXPECT_EQ(std::ranges::find(depthDisabledPassNames, "HZBBuildPass"), depthDisabledPassNames.end());
}

TEST(FrameRecipeContract, ClusterGridBuildRequiresDepthPrepassAndImportedTarget)
{
    FrameRecipeFeatures features{};
    features.EnableClusterGridBuild = true;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* clusterPass = FindPass(description, FrameRecipePassKind::ClusterGridBuild);
    ASSERT_NE(clusterPass, nullptr);
    EXPECT_TRUE(clusterPass->Enabled);
    EXPECT_TRUE(clusterPass->Reads.empty());
    EXPECT_TRUE(Contains(clusterPass->Writes, "ClusterGrid.AABBs"));

    const auto* clusterResource = FindResource(description, FrameRecipeResourceKind::ClusterGridAABBs);
    ASSERT_NE(clusterResource, nullptr);
    EXPECT_TRUE(clusterResource->Enabled);
    EXPECT_TRUE(clusterResource->Imported);
    EXPECT_TRUE(clusterResource->Optional);
    EXPECT_TRUE(clusterResource->ImportedWriteAllowed);

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    EXPECT_TRUE(HasPassName(*compiled, "DepthPrepass"));
    EXPECT_TRUE(HasPassName(*compiled, "ClusterGridBuildPass"));
    EXPECT_TRUE(HasPassName(*compiled, "SurfacePass"));
    EXPECT_NE(std::ranges::find(compiled->BufferNames, "ClusterGrid.AABBs"), compiled->BufferNames.end());

    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(description, *compiled);
    EXPECT_FALSE(validation.HasErrors())
        << (validation.Findings.empty() ? "<no findings>" : validation.Findings.front().Message);

    FrameRecipeImports missingImport = MakeImports();
    missingImport.ClusterGridAABBs = {};
    RenderGraph missingImportGraph;
    const FrameRecipeBuildResult missingImportBuild = BuildDefaultFrameRecipe(
        missingImportGraph,
        features,
        missingImport,
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(missingImportBuild.Succeeded) << missingImportBuild.Diagnostic;
    const auto missingImportCompiled = missingImportGraph.Compile();
    ASSERT_TRUE(missingImportCompiled.has_value());
    const std::vector<std::string> missingImportPassNames = OrderedPassNames(*missingImportCompiled);
    EXPECT_EQ(std::ranges::find(missingImportPassNames, "ClusterGridBuildPass"), missingImportPassNames.end());

    features.EnableDepthPrepass = false;
    const FrameRecipeIntrospection depthDisabled = DescribeDefaultFrameRecipe(features);
    const auto* depthDisabledCluster = FindPass(depthDisabled, FrameRecipePassKind::ClusterGridBuild);
    ASSERT_NE(depthDisabledCluster, nullptr);
    EXPECT_FALSE(depthDisabledCluster->Enabled);
    EXPECT_FALSE(HasEnabledResource(depthDisabled, FrameRecipeResourceKind::ClusterGridAABBs));

    RenderGraph depthDisabledGraph;
    const FrameRecipeBuildResult depthDisabledBuild = BuildDefaultFrameRecipe(
        depthDisabledGraph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(depthDisabledBuild.Succeeded) << depthDisabledBuild.Diagnostic;
    const auto depthDisabledCompiled = depthDisabledGraph.Compile();
    ASSERT_TRUE(depthDisabledCompiled.has_value());
    const std::vector<std::string> depthDisabledPassNames = OrderedPassNames(*depthDisabledCompiled);
    EXPECT_EQ(std::ranges::find(depthDisabledPassNames, "ClusterGridBuildPass"), depthDisabledPassNames.end());
}

TEST(FrameRecipeContract, LightClusterAssignmentRequiresGridBuildAndImportedOutputs)
{
    FrameRecipeFeatures features{};
    features.EnableClusterGridBuild = true;
    features.EnableClusterLightAssignment = true;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* assignmentPass = FindPass(description, FrameRecipePassKind::LightClusterAssignment);
    ASSERT_NE(assignmentPass, nullptr);
    EXPECT_TRUE(assignmentPass->Enabled);
    EXPECT_TRUE(Contains(assignmentPass->Reads, "ClusterGrid.AABBs"));
    EXPECT_TRUE(Contains(assignmentPass->Reads, "GpuWorld.Lights"));
    EXPECT_TRUE(Contains(assignmentPass->Writes, "ClusterLights.Headers"));
    EXPECT_TRUE(Contains(assignmentPass->Writes, "ClusterLights.Indices"));
    EXPECT_TRUE(Contains(assignmentPass->Writes, "ClusterLights.Counter"));
    const auto* compositionPass = FindPass(description, FrameRecipePassKind::Composition);
    ASSERT_NE(compositionPass, nullptr);
    EXPECT_TRUE(Contains(compositionPass->Reads, "ClusterLights.Headers"));
    EXPECT_TRUE(Contains(compositionPass->Reads, "ClusterLights.Indices"));
    FrameRecipeFeatures forwardFeatures = features;
    forwardFeatures.LightingPath = FrameRecipeLightingPath::Forward;
    const FrameRecipeIntrospection forwardDescription = DescribeDefaultFrameRecipe(forwardFeatures);
    const auto* forwardSurface = FindPass(forwardDescription, FrameRecipePassKind::Surface);
    ASSERT_NE(forwardSurface, nullptr);
    EXPECT_TRUE(Contains(forwardSurface->Reads, "ClusterLights.Headers"));
    EXPECT_TRUE(Contains(forwardSurface->Reads, "ClusterLights.Indices"));

    const auto* headerResource = FindResource(description, FrameRecipeResourceKind::ClusterLightHeaders);
    ASSERT_NE(headerResource, nullptr);
    EXPECT_TRUE(headerResource->Enabled);
    EXPECT_TRUE(headerResource->Imported);
    EXPECT_TRUE(headerResource->Optional);
    EXPECT_TRUE(headerResource->ImportedWriteAllowed);
    const auto* indexResource = FindResource(description, FrameRecipeResourceKind::ClusterLightIndices);
    ASSERT_NE(indexResource, nullptr);
    EXPECT_TRUE(indexResource->Enabled);
    EXPECT_TRUE(indexResource->Imported);
    EXPECT_TRUE(indexResource->Optional);
    EXPECT_TRUE(indexResource->ImportedWriteAllowed);
    const auto* counterResource = FindResource(description, FrameRecipeResourceKind::ClusterLightCounter);
    ASSERT_NE(counterResource, nullptr);
    EXPECT_TRUE(counterResource->Enabled);
    EXPECT_TRUE(counterResource->Imported);
    EXPECT_TRUE(counterResource->Optional);
    EXPECT_TRUE(counterResource->ImportedWriteAllowed);

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    EXPECT_TRUE(HasPassName(*compiled, "DepthPrepass"));
    EXPECT_TRUE(HasPassName(*compiled, "SurfacePass"));
    ExpectPassBefore(*compiled, "ClusterGridBuildPass", "LightClusterAssignmentPass");
    ExpectPassBefore(*compiled, "LightClusterAssignmentPass", "CompositionPass");
    EXPECT_NE(std::ranges::find(compiled->BufferNames, "ClusterLights.Headers"), compiled->BufferNames.end());
    EXPECT_NE(std::ranges::find(compiled->BufferNames, "ClusterLights.Indices"), compiled->BufferNames.end());
    EXPECT_NE(std::ranges::find(compiled->BufferNames, "ClusterLights.Counter"), compiled->BufferNames.end());

    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(description, *compiled);
    EXPECT_FALSE(validation.HasErrors())
        << (validation.Findings.empty() ? "<no findings>" : validation.Findings.front().Message);

    FrameRecipeImports missingHeaders = MakeImports();
    missingHeaders.ClusterLightHeaders = {};
    RenderGraph missingHeadersGraph;
    const FrameRecipeBuildResult missingHeadersBuild = BuildDefaultFrameRecipe(
        missingHeadersGraph,
        features,
        missingHeaders,
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(missingHeadersBuild.Succeeded) << missingHeadersBuild.Diagnostic;
    const auto missingHeadersCompiled = missingHeadersGraph.Compile();
    ASSERT_TRUE(missingHeadersCompiled.has_value());
    const std::vector<std::string> missingHeaderPassNames = OrderedPassNames(*missingHeadersCompiled);
    EXPECT_EQ(std::ranges::find(missingHeaderPassNames, "LightClusterAssignmentPass"), missingHeaderPassNames.end());

    FrameRecipeImports missingCounter = MakeImports();
    missingCounter.ClusterLightCounter = {};
    RenderGraph missingCounterGraph;
    const FrameRecipeBuildResult missingCounterBuild = BuildDefaultFrameRecipe(
        missingCounterGraph,
        features,
        missingCounter,
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(missingCounterBuild.Succeeded) << missingCounterBuild.Diagnostic;
    const auto missingCounterCompiled = missingCounterGraph.Compile();
    ASSERT_TRUE(missingCounterCompiled.has_value());
    const std::vector<std::string> missingCounterPassNames = OrderedPassNames(*missingCounterCompiled);
    EXPECT_EQ(std::ranges::find(missingCounterPassNames, "LightClusterAssignmentPass"), missingCounterPassNames.end());

    FrameRecipeFeatures missingGridFeature{};
    missingGridFeature.EnableClusterLightAssignment = true;
    const FrameRecipeIntrospection missingGrid = DescribeDefaultFrameRecipe(missingGridFeature);
    const auto* missingGridAssignment = FindPass(missingGrid, FrameRecipePassKind::LightClusterAssignment);
    ASSERT_NE(missingGridAssignment, nullptr);
    EXPECT_FALSE(missingGridAssignment->Enabled);
    EXPECT_FALSE(HasEnabledResource(missingGrid, FrameRecipeResourceKind::ClusterLightHeaders));
    EXPECT_FALSE(HasEnabledResource(missingGrid, FrameRecipeResourceKind::ClusterLightIndices));
    EXPECT_FALSE(HasEnabledResource(missingGrid, FrameRecipeResourceKind::ClusterLightCounter));
}

TEST(FrameRecipeContract, ClusterPassesRequestAsyncComputeAndDemoteToGraphics)
{
    FrameRecipeFeatures features{};
    features.EnableClusterGridBuild = true;
    features.EnableClusterLightAssignment = true;
    // Isolate this contract from the existing postprocess histogram async pass.
    features.EnablePostProcess = false;

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const std::uint32_t clusterGridPass =
        PassIndexByName(*compiled, "ClusterGridBuildPass");
    const std::uint32_t assignmentPass =
        PassIndexByName(*compiled, "LightClusterAssignmentPass");
    ASSERT_LT(clusterGridPass, compiled->PassQueues.size());
    ASSERT_LT(assignmentPass, compiled->PassQueues.size());
    EXPECT_EQ(compiled->PassQueues[clusterGridPass], RenderQueue::AsyncCompute);
    EXPECT_EQ(compiled->PassQueues[assignmentPass], RenderQueue::AsyncCompute);

    const QueuePartition singleQueuePartition =
        PartitionPassesByQueue(*compiled, RHI::QueueCapabilityProfile{});
    EXPECT_TRUE(singleQueuePartition.AsyncCompute.empty());
    EXPECT_EQ(singleQueuePartition.QueueAffinityDemotedCount, 2u);
    const auto isDemotedClusterPass = [clusterGridPass, assignmentPass](const QueuePartitionedPass& pass) {
        return (pass.PassIndex == clusterGridPass || pass.PassIndex == assignmentPass) &&
               pass.Requested == RenderQueue::AsyncCompute &&
               pass.Resolved == RenderQueue::Graphics &&
               pass.Demoted;
    };
    EXPECT_EQ(std::ranges::count_if(singleQueuePartition.Graphics, isDemotedClusterPass), 2);

    const QueueSubmitPlan singleQueuePlan =
        BuildQueueSubmitPlan(*compiled, RHI::QueueCapabilityProfile{});
    ASSERT_EQ(singleQueuePlan.Batches.size(), 1u);
    EXPECT_EQ(singleQueuePlan.Batches[0].Queue, RenderQueue::Graphics);
    EXPECT_EQ(singleQueuePlan.QueueAffinityDemotedCount, 2u);
    EXPECT_NE(std::ranges::find(singleQueuePlan.Batches[0].PassIndices, clusterGridPass),
              singleQueuePlan.Batches[0].PassIndices.end());
    EXPECT_NE(std::ranges::find(singleQueuePlan.Batches[0].PassIndices, assignmentPass),
              singleQueuePlan.Batches[0].PassIndices.end());

    const QueueSubmitPlan asyncPlan = BuildQueueSubmitPlan(
        *compiled,
        RHI::QueueCapabilityProfile{.SupportsAsyncCompute = true});
    EXPECT_EQ(asyncPlan.QueueAffinityDemotedCount, 0u);
    const auto asyncBatchHasClusterGrid = [clusterGridPass](const QueueSubmitBatch& batch) {
        return batch.Queue == RenderQueue::AsyncCompute &&
               std::ranges::find(batch.PassIndices, clusterGridPass) != batch.PassIndices.end();
    };
    const auto asyncBatchHasAssignment = [assignmentPass](const QueueSubmitBatch& batch) {
        return batch.Queue == RenderQueue::AsyncCompute &&
               std::ranges::find(batch.PassIndices, assignmentPass) != batch.PassIndices.end();
    };
    EXPECT_TRUE(std::ranges::any_of(asyncPlan.Batches, asyncBatchHasClusterGrid));
    EXPECT_TRUE(std::ranges::any_of(asyncPlan.Batches, asyncBatchHasAssignment));
    ExpectPassBefore(*compiled, "ClusterGridBuildPass", "LightClusterAssignmentPass");
}

TEST(FrameRecipeContract, BackbufferIsFinalizedOnlyByPresentDeclaration)
{
    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});

    std::uint32_t finalizerCount = 0u;
    for (const FrameRecipePassDeclaration& pass : description.Passes)
    {
        if (!pass.FinalizesBackbuffer)
        {
            continue;
        }

        ++finalizerCount;
        EXPECT_EQ(pass.Kind, FrameRecipePassKind::Present);
        EXPECT_EQ(pass.Name, std::string_view{"Present"});
        EXPECT_TRUE(pass.Enabled);
    }

    EXPECT_EQ(finalizerCount, 1u);

    std::uint32_t backbufferResourceCount = 0u;
    for (const FrameRecipeResourceDeclaration& resource : description.Resources)
    {
        if (!resource.Backbuffer)
        {
            continue;
        }

        ++backbufferResourceCount;
        EXPECT_EQ(resource.Kind, FrameRecipeResourceKind::Backbuffer);
        EXPECT_TRUE(resource.Imported);
        EXPECT_TRUE(resource.Enabled);
    }

    EXPECT_EQ(backbufferResourceCount, 1u);

    const auto* backbuffer = FindResource(description, FrameRecipeResourceKind::Backbuffer);
    ASSERT_NE(backbuffer, nullptr);
    EXPECT_FALSE(backbuffer->ImportedWriteAllowed);

    const auto* drawArgs = FindResource(description, FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs);
    ASSERT_NE(drawArgs, nullptr);
    EXPECT_TRUE(drawArgs->ImportedWriteAllowed);
}

TEST(FrameRecipeContract, IntrospectionReportsPassResourceReadsAndWrites)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableSelectionOutline = true;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);

    const auto* culling = FindPass(description, FrameRecipePassKind::Culling);
    ASSERT_NE(culling, nullptr);
    EXPECT_TRUE(Contains(culling->Reads, "GpuWorld.SceneTable"));
    EXPECT_TRUE(Contains(culling->Writes, "Cull.SurfaceOpaque.IndexedArgs"));
    EXPECT_TRUE(Contains(culling->Writes, "Cull.Lines.IndexedArgs"));
    EXPECT_TRUE(Contains(culling->Writes, "Cull.LineQuads.NonIndexedArgs"));
    EXPECT_TRUE(Contains(culling->Writes, "Cull.LineQuads.Count"));
    EXPECT_TRUE(Contains(culling->Writes, "Cull.Points.NonIndexedArgs"));

    const auto* line = FindPass(description, FrameRecipePassKind::Line);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(Contains(line->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(line->Reads, "Cull.LineQuads.NonIndexedArgs"));
    EXPECT_TRUE(Contains(line->Reads, "Cull.LineQuads.Count"));
    EXPECT_TRUE(Contains(line->Writes, "SceneColorHDR"));

    const auto* point = FindPass(description, FrameRecipePassKind::Point);
    ASSERT_NE(point, nullptr);
    EXPECT_TRUE(Contains(point->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(point->Reads, "Cull.Points.NonIndexedArgs"));
    EXPECT_TRUE(Contains(point->Reads, "Cull.Points.Count"));
    EXPECT_TRUE(Contains(point->Writes, "SceneColorHDR"));

    const auto* picking = FindPass(description, FrameRecipePassKind::Picking);
    ASSERT_NE(picking, nullptr);
    EXPECT_TRUE(picking->Enabled);
    EXPECT_TRUE(Contains(picking->Writes, "EntityId"));
    EXPECT_TRUE(Contains(picking->Writes, "PrimitiveId"));
    EXPECT_TRUE(Contains(picking->Writes, "Picking.Readback"));
    // GRAPHICS-074 recipe-side follow-up — picking samples the
    // depth-prepass-populated SceneDepth so the depth-equal pipeline picks
    // the nearest-surface fragment per pixel instead of last-fragment-winning.
    EXPECT_TRUE(Contains(picking->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(picking->Reads, "Cull.SurfaceOpaque.IndexedArgs"));
    EXPECT_TRUE(Contains(picking->Reads, "Cull.Lines.IndexedArgs"));
    EXPECT_TRUE(Contains(picking->Reads, "Cull.Points.NonIndexedArgs"));

    const auto* outline = FindPass(description, FrameRecipePassKind::SelectionOutline);
    ASSERT_NE(outline, nullptr);
    EXPECT_TRUE(outline->Enabled);
    EXPECT_TRUE(Contains(outline->Reads, "EntityId"));
    EXPECT_TRUE(Contains(outline->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(outline->Reads, "FrameRecipe.PresentSource"));
    EXPECT_TRUE(Contains(outline->Writes, "FrameRecipe.PresentSource"));
    EXPECT_FALSE(Contains(outline->Writes, "SelectionOutline"));

    const auto* present = FindPass(description, FrameRecipePassKind::Present);
    ASSERT_NE(present, nullptr);
    // GRAPHICS-076 Slice A follow-up — the default-recipe present pass
    // declares the imported `Backbuffer` as a color-attachment *write*
    // so the framegraph compiler emits real render-pass attachments and the
    // executor's `BindPipeline + Draw(3, 1, 0, 0)` runs inside a
    // `BeginRenderPass/EndRenderPass` scope. Pre-fixup the introspection
    // listed `Backbuffer` under `Reads` because the recipe only
    // consumed it for the `ColorAttachment → Present` end-of-graph
    // transition.
    EXPECT_TRUE(Contains(present->Writes, "Backbuffer"));
    EXPECT_TRUE(present->FinalizesBackbuffer);
}

// GRAPHICS-074 recipe-side follow-up — picking and its picking-only
// resources are gated on `EnablePicking && EnableDepthPrepass`. Without
// the depth prepass the recipe cannot produce a populated `SceneDepth`
// and the depth-equal selection-ID pipelines would be render-pass-
// incompatible with a depth-less PickingPass, so the introspection drops
// the pass and its `PrimitiveId` / `Picking.Readback` resources entirely
// and the compiled graph contains no PickingPass node. `EntityId` follows
// the same gate when `EnableSelectionOutline` is also false (its only
// other consumer). The matching builder gate in `BuildDefaultFrameRecipe`
// keeps the declared and built resource/pass sets aligned.
TEST(FrameRecipeContract, PickingRequiresDepthPrepass)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableDepthPrepass = false;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* picking = FindPass(description, FrameRecipePassKind::Picking);
    ASSERT_NE(picking, nullptr);
    EXPECT_FALSE(picking->Enabled);
    // Picking-only resources must be dropped along with the pass; otherwise
    // the recipe would allocate dead full-resolution R32_UINT targets and
    // a host-visible readback buffer that no pass writes or reads.
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::EntityId));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::PickingReadback));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    EXPECT_EQ(std::ranges::find(passNames, "PickingPass"), passNames.end());
    EXPECT_EQ(std::ranges::find(passNames, "DepthPrepass"), passNames.end());
    // The picking-only resources must not appear in the compiled graph either.
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "PrimitiveId"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->BufferNames, "Picking.Readback"), compiled->BufferNames.end());
    // EntityId is shared with SelectionOutlinePass — disabled here too — so
    // it must also be dropped.
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "EntityId"), compiled->TextureNames.end());
}

// BUG-018 — without a depth prepass, the depth-equal selection-ID pipelines
// cannot produce valid IDs for either readback or outline. Keep that case
// fail-closed: no PickingPass, no SelectionOutlinePass, and no ID targets.
TEST(FrameRecipeContract, SelectionOutlineRequiresSelectionIdDepthPrepass)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableDepthPrepass = false;
    features.EnableSelectionOutline = true;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* picking = FindPass(description, FrameRecipePassKind::Picking);
    ASSERT_NE(picking, nullptr);
    EXPECT_FALSE(picking->Enabled);
    const auto* outline = FindPass(description, FrameRecipePassKind::SelectionOutline);
    ASSERT_NE(outline, nullptr);
    EXPECT_FALSE(outline->Enabled);
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::EntityId));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::PickingReadback));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "EntityId"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "SelectionOutline"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "PrimitiveId"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->BufferNames, "Picking.Readback"), compiled->BufferNames.end());
}

// GRAPHICS-113 — hierarchy selection enables SelectionOutline without a
// pending mouse pick. The recipe must still run the selection-ID producer so
// SelectionOutlinePass has a written EntityId texture, while dropping
// PrimitiveId, the host readback buffer, and TransferSrc-only texture usage.
TEST(FrameRecipeContract, SelectionOutlineWithoutPendingPickProducesIdsWithoutReadback)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = false;
    features.EnableSelectionOutline = true;
    // EnableDepthPrepass defaults to true.

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
    const auto* picking = FindPass(description, FrameRecipePassKind::Picking);
    ASSERT_NE(picking, nullptr);
    EXPECT_TRUE(picking->Enabled);
    EXPECT_TRUE(Contains(picking->Writes, "EntityId"));
    EXPECT_FALSE(Contains(picking->Writes, "PrimitiveId"));
    EXPECT_FALSE(Contains(picking->Writes, "Picking.Readback"));

    const auto* outline = FindPass(description, FrameRecipePassKind::SelectionOutline);
    ASSERT_NE(outline, nullptr);
    EXPECT_TRUE(outline->Enabled);
    EXPECT_TRUE(Contains(outline->Reads, "EntityId"));
    EXPECT_TRUE(Contains(outline->Writes, "FrameRecipe.PresentSource"));

    EXPECT_TRUE(HasEnabledResource(description, FrameRecipeResourceKind::EntityId));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::PickingReadback));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    EXPECT_NE(std::ranges::find(compiled->TextureNames, "EntityId"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "PrimitiveId"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->TextureNames, "SelectionOutline"), compiled->TextureNames.end());
    EXPECT_EQ(std::ranges::find(compiled->BufferNames, "Picking.Readback"), compiled->BufferNames.end());
    ExpectPassBefore(*compiled, "PickingPass", "SelectionOutlinePass");

    const TextureResourceDesc* entityId = TextureDescByName(graph, *compiled, "EntityId");
    ASSERT_NE(entityId, nullptr);
    EXPECT_TRUE(HasTextureUsage(entityId->Desc.Usage, RHI::TextureUsage::ColorTarget));
    EXPECT_TRUE(HasTextureUsage(entityId->Desc.Usage, RHI::TextureUsage::Sampled));
    EXPECT_FALSE(HasTextureUsage(entityId->Desc.Usage, RHI::TextureUsage::TransferSrc));

    const std::uint32_t pickingIndex = PassIndexByName(*compiled, "PickingPass");
    std::size_t colorAttachments = 0u;
    for (const CompiledRenderPassAttachment& attachment : compiled->RenderPassAttachments)
    {
        if (attachment.PassIndex != pickingIndex || attachment.IsDepthAttachment)
        {
            continue;
        }
        ++colorAttachments;
        EXPECT_EQ(attachment.Format, RHI::Format::R32_UINT);
        EXPECT_EQ(attachment.ClearR, 0.0f);
        EXPECT_EQ(attachment.ClearG, 0.0f);
        EXPECT_EQ(attachment.ClearB, 0.0f);
        EXPECT_EQ(attachment.ClearA, 0.0f);
    }
    EXPECT_EQ(colorAttachments, 1u)
        << "Outline-only PickingPass must declare exactly the EntityId color target.";

    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(description, *compiled);
    EXPECT_FALSE(validation.HasErrors())
        << (validation.Findings.empty() ? "<no findings>" : validation.Findings.front().Message);
}

// GRAPHICS-074 recipe-side follow-up — when picking *and* the depth prepass
// are both enabled, the compiled graph places PickingPass after DepthPrepass
// so the selection-ID pipelines depth-equal-test against the populated
// SceneDepth instead of last-fragment-winning into the EntityId/PrimitiveId
// targets.
TEST(FrameRecipeContract, PickingPassRunsAfterDepthPrepass)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    // EnableDepthPrepass defaults to true.

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    const auto depthIt = std::ranges::find(passNames, "DepthPrepass");
    const auto pickingIt = std::ranges::find(passNames, "PickingPass");
    ASSERT_NE(depthIt, passNames.end());
    ASSERT_NE(pickingIt, passNames.end());
    EXPECT_LT(depthIt, pickingIt);
}

// GRAPHICS-074 Slice D.2 — when the recipe is built with a valid
// `imports.PickingReadback` handle (the renderer's host-visible buffer from
// Slice D.1) and picking is active, the compiled graph must mark
// `Picking.Readback` as imported, route the renderer's handle through, and
// carry the `(InitialState = TransferDst, FinalState = HostReadback)` pair
// required by the framegraph's imported-write contract for the PickingPass
// `Write(..., BufferUsage::TransferDst)` plus Slice D.3's `BeginFrame()`
// host-mapped drain.
TEST(FrameRecipeContract, PickingReadbackImportedFromRenderer)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    // EnableDepthPrepass defaults to true.

    const Extrinsic::RHI::BufferHandle rendererPickingBuffer{0xAB12u, 3u};
    FrameRecipeImports imports = MakeImports();
    imports.PickingReadback = rendererPickingBuffer;

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        imports,
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const auto it = std::ranges::find(compiled->BufferNames, "Picking.Readback");
    ASSERT_NE(it, compiled->BufferNames.end())
        << "Picking.Readback must appear in the compiled graph when picking is active.";
    const std::size_t index =
        static_cast<std::size_t>(std::distance(compiled->BufferNames.begin(), it));

    ASSERT_LT(index, compiled->BufferImported.size());
    EXPECT_TRUE(compiled->BufferImported[index])
        << "Picking.Readback must be imported (not transient) when the renderer "
        << "supplies a valid buffer handle.";

    ASSERT_LT(index, compiled->BufferHandles.size());
    EXPECT_EQ(compiled->BufferHandles[index], rendererPickingBuffer)
        << "Imported Picking.Readback handle must match the renderer-supplied buffer.";

    ASSERT_LT(index, compiled->BufferInitialStates.size());
    EXPECT_EQ(compiled->BufferInitialStates[index], BufferState::TransferDst)
        << "Initial state must be TransferDst so the framegraph's imported-write "
        << "contract authorises PickingPass's `Write(..., TransferDst)`.";

    ASSERT_LT(index, compiled->BufferFinalStates.size());
    EXPECT_EQ(compiled->BufferFinalStates[index], BufferState::HostReadback)
        << "Final state must be HostReadback so the buffer is host-mappable for "
        << "Slice D.3's BeginFrame() drain.";

    // The recipe-aware validator must accept the imported write — PickingPass
    // is the authorized writer via the description's `Writes` entry +
    // `ImportedWriteAllowed=true` on the resource declaration.
    const FrameRecipeIntrospection recipe = DescribeDefaultFrameRecipe(features);
    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(recipe, *compiled);
    EXPECT_FALSE(validation.HasErrors())
        << (validation.Findings.empty() ? "<no findings>" : validation.Findings.front().Message);
}

TEST(FrameRecipeContract, PickingIdTargetsDeclareTransferSrcUsageForReadbackCopies)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    // EnableDepthPrepass defaults to true.

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    const TextureResourceDesc* entityId = TextureDescByName(graph, *compiled, "EntityId");
    const TextureResourceDesc* primitiveId = TextureDescByName(graph, *compiled, "PrimitiveId");
    ASSERT_NE(entityId, nullptr);
    ASSERT_NE(primitiveId, nullptr);

    EXPECT_TRUE(HasTextureUsage(entityId->Desc.Usage, RHI::TextureUsage::ColorTarget));
    EXPECT_TRUE(HasTextureUsage(entityId->Desc.Usage, RHI::TextureUsage::Sampled));
    EXPECT_TRUE(HasTextureUsage(entityId->Desc.Usage, RHI::TextureUsage::TransferSrc))
        << "Picking readback records CopyTextureToBuffer(EntityId), so Vulkan "
           "requires VK_IMAGE_USAGE_TRANSFER_SRC_BIT.";

    EXPECT_TRUE(HasTextureUsage(primitiveId->Desc.Usage, RHI::TextureUsage::ColorTarget));
    EXPECT_TRUE(HasTextureUsage(primitiveId->Desc.Usage, RHI::TextureUsage::Sampled));
    EXPECT_TRUE(HasTextureUsage(primitiveId->Desc.Usage, RHI::TextureUsage::TransferSrc))
        << "Picking readback records CopyTextureToBuffer(PrimitiveId), so Vulkan "
           "requires VK_IMAGE_USAGE_TRANSFER_SRC_BIT.";
}

TEST(FrameRecipeContract, MissingBackbufferReportsDiagnostic)
{
    RenderGraph graph;
    FrameRecipeImports imports = MakeImports();
    imports.Backbuffer = {};

    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        FrameRecipeFeatures{},
        imports,
        FrameRecipeSizing{});

    EXPECT_FALSE(build.Succeeded);
    EXPECT_NE(build.Diagnostic.find("Backbuffer"), std::string::npos);
    EXPECT_EQ(graph.GetPassCount(), 0u);
}

// ---------------------------------------------------------------------------
// GRAPHICS-073 Slice B — `FrameRecipeImports::ShadowAtlas` + the typed
// `FrameRecipeShadowSizing` seam. The recipe prefers the imported handle
// when valid, and falls back to a transient depth target sized by the
// typed seam (or the viewport when neither is plumbed).
// ---------------------------------------------------------------------------

TEST(FrameRecipeContract, ShadowAtlasUsesImportedHandleWhenProvided)
{
    FrameRecipeFeatures features{};
    features.EnableShadows = true;

    FrameRecipeImports imports = MakeImports();
    const Extrinsic::RHI::TextureHandle shadowAtlasHandle{77u, 1u};
    imports.ShadowAtlas = shadowAtlasHandle;

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        imports,
        FrameRecipeSizing{.Width = 1280u, .Height = 720u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    // The compiled graph should mark ShadowAtlas as imported, resolve its
    // device handle to the one supplied by the caller, AND seed its initial
    // state as `Undefined` so the next-frame compile emits a fresh
    // `Undefined→DepthWrite` barrier at the ShadowPass entry. Cross-frame
    // regression: a previous draft used `InitialState=DepthWrite,
    // FinalState=ShaderRead`, which caused the compiler to seed
    // `prev=DepthWrite` on frame N+1 while the real GPU layout was
    // `ShaderRead` (the prior `FinalState` transition), so no barrier was
    // emitted and ShadowPass recorded a depth-attachment write against a
    // shader-read layout. Pin the `Undefined/DepthWrite` import idiom here.
    bool foundShadow = false;
    for (std::size_t idx = 0; idx < compiled->TextureNames.size(); ++idx)
    {
        if (compiled->TextureNames[idx] != "ShadowAtlas")
        {
            continue;
        }
        foundShadow = true;
        ASSERT_LT(idx, compiled->TextureImported.size());
        EXPECT_TRUE(compiled->TextureImported[idx]);
        ASSERT_LT(idx, compiled->TextureHandles.size());
        EXPECT_EQ(compiled->TextureHandles[idx], shadowAtlasHandle);
        ASSERT_LT(idx, compiled->TextureInitialStates.size());
        EXPECT_EQ(compiled->TextureInitialStates[idx], TextureState::Undefined);
        ASSERT_LT(idx, compiled->TextureFinalStates.size());
        EXPECT_EQ(compiled->TextureFinalStates[idx], TextureState::DepthWrite);
    }
    EXPECT_TRUE(foundShadow);

    const FrameRecipeIntrospection recipe = DescribeDefaultFrameRecipe(features);
    const RenderGraphValidationResult validation = ValidateRecipeCompiledGraph(recipe, *compiled);
    EXPECT_FALSE(validation.HasErrors());
    EXPECT_FALSE(validation.HasWarnings());
}

TEST(FrameRecipeContract, ShadowAtlasFallsBackToTransientWhenImportInvalid)
{
    FrameRecipeFeatures features{};
    features.EnableShadows = true;

    FrameRecipeImports imports = MakeImports();
    // imports.ShadowAtlas left default (invalid).

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        imports,
        FrameRecipeSizing{.Width = 800u, .Height = 600u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    bool foundShadow = false;
    for (std::size_t idx = 0; idx < compiled->TextureNames.size(); ++idx)
    {
        if (compiled->TextureNames[idx] != "ShadowAtlas")
        {
            continue;
        }
        foundShadow = true;
        ASSERT_LT(idx, compiled->TextureImported.size());
        EXPECT_FALSE(compiled->TextureImported[idx]);
    }
    EXPECT_TRUE(foundShadow);
}

TEST(FrameRecipeContract, ShadowAtlasTransientPathAcceptsTypedShadowSizing)
{
    FrameRecipeFeatures features{};
    features.EnableShadows = true;

    FrameRecipeImports imports = MakeImports();
    // No imports.ShadowAtlas — exercise the transient fallback path that
    // consumes `FrameRecipeShadowSizing` instead of the viewport.

    const FrameRecipeShadowSizing shadowSizing{
        .AtlasResolution = 512u,
        .CascadeCount = 3u,
    };

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        imports,
        FrameRecipeSizing{.Width = 1280u, .Height = 720u},
        shadowSizing);
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }

    // The transient ShadowAtlas is allocated and the compiled graph treats it
    // as non-imported — the typed sizing seam only affects the recipe-level
    // texture description, which `CompiledRenderGraph` does not surface for
    // contract assertion. Exercising the new code path without a build
    // failure is the observable contract here; end-to-end atlas sizing is
    // covered by the `ShadowSystem` allocation test in
    // `Test.LightingShadowContracts.cpp`.
    bool foundShadow = false;
    for (std::size_t idx = 0; idx < compiled->TextureNames.size(); ++idx)
    {
        if (compiled->TextureNames[idx] != "ShadowAtlas")
        {
            continue;
        }
        foundShadow = true;
        ASSERT_LT(idx, compiled->TextureImported.size());
        EXPECT_FALSE(compiled->TextureImported[idx]);
    }
    EXPECT_TRUE(foundShadow);
}

// --- BUG-026: selection-ID targets clear to the background sentinel --------
// The R32_UINT `EntityId` / `PrimitiveId` targets must clear to exactly 0.0f
// on every channel: the readback drain reserves `EntityId == 0` for
// background, and a non-zero float clear bit-puns into a garbage UINT value
// (the old scene-color blue cleared EntityId to 0x3DCCCCCD, turning every
// background click into a phantom hit on a non-existent entity).

TEST(FrameRecipeContract, PickingPassClearsSelectionIdTargetsToZero)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;

    FrameRecipeImports imports = MakeImports();
    imports.PickingReadback = Extrinsic::RHI::BufferHandle{0xAB13u, 1u};

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
        graph,
        features,
        imports,
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;

    const auto compiled = graph.Compile();
    ASSERT_TRUE(compiled.has_value());

    const std::uint32_t pickingIndex = PassIndexByName(*compiled, "PickingPass");
    ASSERT_LT(pickingIndex, compiled->PassNames.size());

    std::size_t colorAttachments = 0;
    for (const CompiledRenderPassAttachment& attachment : compiled->RenderPassAttachments)
    {
        if (attachment.PassIndex != pickingIndex || attachment.IsDepthAttachment)
        {
            continue;
        }
        ++colorAttachments;
        EXPECT_EQ(attachment.Load, RHI::LoadOp::Clear);
        EXPECT_EQ(attachment.Format, RHI::Format::R32_UINT);
        EXPECT_EQ(attachment.ClearR, 0.0f);
        EXPECT_EQ(attachment.ClearG, 0.0f);
        EXPECT_EQ(attachment.ClearB, 0.0f);
        EXPECT_EQ(attachment.ClearA, 0.0f);
    }
    EXPECT_EQ(colorAttachments, 2u)
        << "PickingPass must declare exactly the EntityId + PrimitiveId color targets.";
}

// BUG-026: when picking readback is active the PickingPass executor copies the
// pick pixel out of SceneDepth (world-space cursor reconstruction), so the
// depth target must carry transfer-source usage; without active picking the
// depth target keeps its lean usage set.
TEST(FrameRecipeContract, SceneDepthGainsTransferSrcUsageWhenPickingActive)
{
    const auto sceneDepthUsage = [](const bool enablePicking) {
        FrameRecipeFeatures features{};
        features.EnablePicking = enablePicking;
        FrameRecipeImports imports = MakeImports();
        if (enablePicking)
        {
            imports.PickingReadback = Extrinsic::RHI::BufferHandle{0xAB14u, 1u};
        }
        RenderGraph graph;
        const FrameRecipeBuildResult build = BuildDefaultFrameRecipe(
            graph,
            features,
            imports,
            FrameRecipeSizing{.Width = 640u, .Height = 480u});
        EXPECT_TRUE(build.Succeeded) << build.Diagnostic;
        const auto compiled = graph.Compile();
        EXPECT_TRUE(compiled.has_value());
        const std::uint32_t depthIndex = TextureIndexByName(*compiled, "SceneDepth");
        EXPECT_LT(depthIndex, compiled->TextureNames.size());
        const TextureResourceDesc* desc = graph.GetTextureDescByIndex(depthIndex);
        EXPECT_NE(desc, nullptr);
        return desc != nullptr ? desc->Desc.Usage : RHI::TextureUsage{};
    };

    EXPECT_TRUE(HasTextureUsage(sceneDepthUsage(true), RHI::TextureUsage::TransferSrc));
    EXPECT_FALSE(HasTextureUsage(sceneDepthUsage(false), RHI::TextureUsage::TransferSrc));
}
