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
            .LinesIndexedArgs = Extrinsic::RHI::BufferHandle{12u, 1u},
            .LinesCount = Extrinsic::RHI::BufferHandle{13u, 1u},
            .PointsNonIndexedArgs = Extrinsic::RHI::BufferHandle{14u, 1u},
            .PointsCount = Extrinsic::RHI::BufferHandle{15u, 1u},
            // GRAPHICS-074 Slice D.2 — renderer-owned host-visible
            // `Picking.Readback` buffer is now imported by the recipe rather
            // than transient; supply a valid handle so picking-enabled
            // contract tests keep their compile path. Tests that disable
            // picking ignore this import (recipe drops the resource).
            .PickingReadback = Extrinsic::RHI::BufferHandle{16u, 1u},
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
        "PostProcessPass",
        "ImGuiPass",
        "Present",
    };
    EXPECT_EQ(OrderedPassNames(*compiled), expected);
    EXPECT_EQ(build.DeclaredPassCount, expected.size());
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
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
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
    EXPECT_TRUE(Contains(culling->Writes, "Cull.Lines.IndexedArgs"));
    EXPECT_TRUE(Contains(culling->Writes, "Cull.Points.NonIndexedArgs"));

    const auto* line = FindPass(description, FrameRecipePassKind::Line);
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(Contains(line->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(line->Reads, "Cull.Lines.IndexedArgs"));
    EXPECT_TRUE(Contains(line->Reads, "Cull.Lines.Count"));
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
    // GRAPHICS-074 recipe-side follow-up — picking samples the
    // depth-prepass-populated SceneDepth so the depth-equal pipeline picks
    // the nearest-surface fragment per pixel instead of last-fragment-winning.
    EXPECT_TRUE(Contains(picking->Reads, "SceneDepth"));
    EXPECT_TRUE(Contains(picking->Reads, "Cull.SurfaceOpaque.IndexedArgs"));
    EXPECT_TRUE(Contains(picking->Reads, "Cull.Lines.IndexedArgs"));
    EXPECT_TRUE(Contains(picking->Reads, "Cull.Points.NonIndexedArgs"));

    const auto* present = FindPass(description, FrameRecipePassKind::Present);
    ASSERT_NE(present, nullptr);
    EXPECT_TRUE(Contains(present->Reads, "Backbuffer"));
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

// GRAPHICS-074 recipe-side follow-up — `EntityId` is shared between
// PickingPass and SelectionOutlinePass. When picking is gated off but
// SelectionOutline is enabled, the recipe must still allocate `EntityId`
// (the outline pass reads it) but must continue to drop the picking-only
// `PrimitiveId` and `Picking.Readback` resources.
TEST(FrameRecipeContract, EntityIdSurvivesForSelectionOutlineWithoutPicking)
{
    FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableDepthPrepass = false;
    features.EnableSelectionOutline = true;

    const FrameRecipeIntrospection description = DescribeDefaultFrameRecipe(features);
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
    EXPECT_EQ(std::ranges::find(compiled->BufferNames, "Picking.Readback"), compiled->BufferNames.end());
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

// GRAPHICS-032A — minimal-debug-surface recipe contract tests. The recipe is
// opt-in; it declares exactly two passes
// (`Pass.Surface.MinimalDebug` then `Pass.Present.MinimalDebug`), validates
// surface prerequisites, finalizes the imported `Backbuffer` through a direct
// visible-triangle color write, and surfaces a
// per-build `MissingPrerequisiteCount` (mirrored into the renderer's
// `MinimalRecipeMissingPrerequisiteCount` counter) when material/pipeline or
// surface-bucket residency is absent. No pass body lands in this slice
// (GRAPHICS-032B/C own the bodies).

TEST(FrameRecipeContract, MinimalDebugSurfaceRecipeDeclaresTwoPassesInOrderWithStableLabels)
{
    const FrameRecipeIntrospection description = DescribeMinimalDebugSurfaceRecipe();
    ASSERT_EQ(description.Passes.size(), 2u);
    EXPECT_EQ(description.Passes[0].Name, kMinimalDebugSurfacePassName);
    EXPECT_EQ(description.Passes[0].Kind, FrameRecipePassKind::Surface);
    EXPECT_TRUE(description.Passes[0].Enabled);
    EXPECT_FALSE(description.Passes[0].FinalizesBackbuffer);
    EXPECT_TRUE(description.Passes[0].Writes.empty());
    EXPECT_TRUE(Contains(description.Passes[0].Reads, "Material.Buffer"));
    EXPECT_TRUE(Contains(description.Passes[0].Reads, "GpuWorld.SceneTable"));
    EXPECT_TRUE(Contains(description.Passes[0].Reads, "Cull.SurfaceOpaque.IndexedArgs"));
    EXPECT_TRUE(Contains(description.Passes[0].Reads, "Cull.SurfaceOpaque.Count"));

    EXPECT_EQ(description.Passes[1].Name, kMinimalDebugPresentPassName);
    EXPECT_EQ(description.Passes[1].Kind, FrameRecipePassKind::Present);
    EXPECT_TRUE(description.Passes[1].Enabled);
    EXPECT_TRUE(description.Passes[1].FinalizesBackbuffer);
    EXPECT_TRUE(description.Passes[1].Reads.empty());
    EXPECT_TRUE(Contains(description.Passes[1].Writes, "Backbuffer"));

    // The minimal recipe must not declare any default-recipe pass.
    for (const FrameRecipePassDeclaration& pass : description.Passes)
    {
        EXPECT_NE(pass.Name, std::string_view{"CullingPass"});
        EXPECT_NE(pass.Name, std::string_view{"DepthPrepass"});
        EXPECT_NE(pass.Name, std::string_view{"SurfacePass"});
        EXPECT_NE(pass.Name, std::string_view{"CompositionPass"});
        EXPECT_NE(pass.Name, std::string_view{"LinePass"});
        EXPECT_NE(pass.Name, std::string_view{"PointPass"});
        EXPECT_NE(pass.Name, std::string_view{"PostProcessPass"});
        EXPECT_NE(pass.Name, std::string_view{"ImGuiPass"});
        EXPECT_NE(pass.Name, std::string_view{"Present"});
    }

    const auto* backbuffer = FindResource(description, FrameRecipeResourceKind::Backbuffer);
    ASSERT_NE(backbuffer, nullptr);
    EXPECT_TRUE(backbuffer->Imported);
    EXPECT_TRUE(backbuffer->Backbuffer);
    EXPECT_TRUE(backbuffer->ImportedWriteAllowed);
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::SceneDepth));
    EXPECT_FALSE(HasEnabledResource(description, FrameRecipeResourceKind::SceneColorHDR));
    EXPECT_TRUE(HasEnabledResource(description, FrameRecipeResourceKind::MaterialBuffer));
    EXPECT_TRUE(HasEnabledResource(description, FrameRecipeResourceKind::SurfaceOpaqueIndexedArgs));
    EXPECT_TRUE(HasEnabledResource(description, FrameRecipeResourceKind::SurfaceOpaqueCount));

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildMinimalDebugSurfaceRecipe(
        graph,
        MakeImports(),
        FrameRecipeSizing{.Width = 640u, .Height = 480u});
    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;
    EXPECT_EQ(build.DeclaredPassCount, 2u);
    EXPECT_EQ(build.MissingPrerequisiteCount, 0u);

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    const std::vector<std::string> expected{
        std::string{kMinimalDebugSurfacePassName},
        std::string{kMinimalDebugPresentPassName},
    };
    EXPECT_EQ(OrderedPassNames(*compiled), expected);
}

TEST(FrameRecipeContract, MinimalAndDefaultRecipesAreMutuallyIsolated)
{
    const FrameRecipeIntrospection minimal = DescribeMinimalDebugSurfaceRecipe();
    const FrameRecipeIntrospection defaults = DescribeDefaultFrameRecipe(FrameRecipeFeatures{});

    for (const FrameRecipePassDeclaration& pass : minimal.Passes)
    {
        EXPECT_NE(pass.Name, std::string_view{"CullingPass"});
        EXPECT_NE(pass.Name, std::string_view{"SurfacePass"});
        EXPECT_NE(pass.Name, std::string_view{"Present"});
    }
    for (const FrameRecipePassDeclaration& pass : defaults.Passes)
    {
        EXPECT_NE(pass.Name, kMinimalDebugSurfacePassName);
        EXPECT_NE(pass.Name, kMinimalDebugPresentPassName);
    }
}

TEST(FrameRecipeContract, MinimalDebugSurfaceRecipeCountsMissingPrerequisites)
{
    // Missing material residency, surface-opaque bucket residency, and
    // scene-table residency each increment `MissingPrerequisiteCount` even
    // though the recipe still compiles. Backbuffer must remain valid; an
    // invalid backbuffer is a hard build failure (mirrors default-recipe).
    FrameRecipeImports imports = MakeImports();
    imports.MaterialBuffer = {};
    imports.SurfaceOpaqueIndexedArgs = {};
    imports.SurfaceOpaqueCount = {};
    imports.SceneTable = {};

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildMinimalDebugSurfaceRecipe(
        graph,
        imports,
        FrameRecipeSizing{.Width = 320u, .Height = 240u});

    ASSERT_TRUE(build.Succeeded) << build.Diagnostic;
    EXPECT_EQ(build.DeclaredPassCount, 2u);
    // One increment for material, one for the surface-opaque bucket (counted
    // jointly because both halves must be present), and one for scene-table.
    EXPECT_EQ(build.MissingPrerequisiteCount, 3u);

    const auto compiled = graph.Compile();
    {
        const auto& compileResult = graph.GetLastCompileValidationResult();
        ASSERT_TRUE(compiled.has_value())
            << (compileResult.Findings.empty() ? "<no findings>" : compileResult.Findings.front().Message);
    }
    const std::vector<std::string> passNames = OrderedPassNames(*compiled);
    EXPECT_EQ(passNames.size(), 2u);
}

TEST(FrameRecipeContract, MinimalDebugSurfaceRecipeRequiresValidBackbuffer)
{
    FrameRecipeImports imports = MakeImports();
    imports.Backbuffer = {};

    RenderGraph graph;
    const FrameRecipeBuildResult build = BuildMinimalDebugSurfaceRecipe(
        graph,
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




