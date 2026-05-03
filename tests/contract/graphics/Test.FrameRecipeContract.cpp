#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

import Extrinsic.Graphics.FrameRecipe;
import Extrinsic.Graphics.RenderGraph;
import Extrinsic.RHI.Handles;

namespace
{
    using namespace Extrinsic::Graphics;

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
    ASSERT_TRUE(compiled.has_value()) << graph.GetLastCompileDiagnostic();

    const std::vector<std::string> expected{
        "CullingPass",
        "DepthPrepass",
        "SurfacePass",
        "CompositionPass",
        "LinePass",
        "PointPass",
        "PostProcessPass",
        "ImGuiPass",
        "Present",
    };
    EXPECT_EQ(OrderedPassNames(*compiled), expected);
    EXPECT_EQ(build.DeclaredPassCount, expected.size());
}

TEST(FrameRecipeContract, OptionalResourcesAreGatedByFeatures)
{
    const FrameRecipeIntrospection defaults = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::EntityId));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::ShadowAtlas));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::SelectionOutline));
    EXPECT_FALSE(HasEnabledResource(defaults, FrameRecipeResourceKind::DebugViewRGBA));
    EXPECT_TRUE(HasEnabledResource(defaults, FrameRecipeResourceKind::SceneColorLDR));

    FrameRecipeFeatures allFeatures{};
    allFeatures.EnablePicking = true;
    allFeatures.EnableShadows = true;
    allFeatures.EnableSelectionOutline = true;
    allFeatures.EnableDebugView = true;

    const FrameRecipeIntrospection full = DescribeDefaultFrameRecipe(allFeatures);
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::EntityId));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::PrimitiveId));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::ShadowAtlas));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::SelectionOutline));
    EXPECT_TRUE(HasEnabledResource(full, FrameRecipeResourceKind::DebugViewRGBA));
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
    ASSERT_TRUE(compiled.has_value()) << graph.GetLastCompileDiagnostic();
    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    EXPECT_EQ(std::ranges::find(passNames, "DepthPrepass"), passNames.end());
    EXPECT_NE(std::ranges::find(passNames, "SurfacePass"), passNames.end());
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

    const auto* picking = FindPass(description, FrameRecipePassKind::Picking);
    ASSERT_NE(picking, nullptr);
    EXPECT_TRUE(picking->Enabled);
    EXPECT_TRUE(Contains(picking->Writes, "EntityId"));
    EXPECT_TRUE(Contains(picking->Writes, "PrimitiveId"));

    const auto* present = FindPass(description, FrameRecipePassKind::Present);
    ASSERT_NE(present, nullptr);
    EXPECT_TRUE(Contains(present->Reads, "Backbuffer"));
    EXPECT_TRUE(present->FinalizesBackbuffer);
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




