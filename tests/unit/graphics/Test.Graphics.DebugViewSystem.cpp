#include <gtest/gtest.h>

#include <string>
#include <vector>

import Extrinsic.Graphics.DebugViewSystem;
import Extrinsic.Graphics.FrameRecipe;

using namespace Extrinsic;

namespace
{
    [[nodiscard]] const Graphics::DebugViewInspectableResource* Find(
        const std::vector<Graphics::DebugViewInspectableResource>& resources,
        const std::string& name)
    {
        for (const Graphics::DebugViewInspectableResource& resource : resources)
        {
            if (resource.Name == name)
            {
                return &resource;
            }
        }
        return nullptr;
    }
}

TEST(GraphicsDebugViewSystem, InspectionTableClassifiesSampleableTargetsAndBuffers)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();

    Graphics::FrameRecipeFeatures features{};
    features.EnablePicking = true;
    features.EnableDebugView = true;
    const Graphics::FrameRecipeIntrospection recipe = Graphics::DescribeDefaultFrameRecipe(features);
    const std::vector<Graphics::DebugViewInspectableResource> resources = debug.BuildInspectionTable(recipe);

    const auto* hdr = Find(resources, "SceneColorHDR");
    ASSERT_NE(hdr, nullptr);
    EXPECT_TRUE(hdr->Enabled);
    EXPECT_EQ(hdr->ResourceClass, Graphics::DebugViewResourceClass::Texture);
    EXPECT_TRUE(hdr->Sampleable);
    EXPECT_TRUE(hdr->Previewable);

    const auto* depth = Find(resources, "SceneDepth");
    ASSERT_NE(depth, nullptr);
    EXPECT_EQ(depth->ResourceClass, Graphics::DebugViewResourceClass::DepthTexture);
    EXPECT_TRUE(depth->Previewable);

    const auto* sceneTable = Find(resources, "GpuWorld.SceneTable");
    ASSERT_NE(sceneTable, nullptr);
    EXPECT_EQ(sceneTable->ResourceClass, Graphics::DebugViewResourceClass::Buffer);
    EXPECT_FALSE(sceneTable->Sampleable);
    EXPECT_FALSE(sceneTable->Previewable);

    const auto* output = Find(resources, "DebugViewRGBA");
    ASSERT_NE(output, nullptr);
    EXPECT_TRUE(output->Enabled);
    EXPECT_TRUE(output->Sampleable);
    EXPECT_FALSE(output->Previewable);
}

TEST(GraphicsDebugViewSystem, ResolvesValidSelectionWithoutFallback)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();
    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = true, .RequestedResourceName = "SceneNormal"});

    Graphics::FrameRecipeFeatures features{};
    const Graphics::FrameRecipeIntrospection recipe = Graphics::DescribeDefaultFrameRecipe(features);
    const Graphics::DebugViewResolvedSelection selection = debug.ResolveSelection(recipe);

    EXPECT_TRUE(selection.Enabled);
    EXPECT_FALSE(selection.UsedFallback);
    EXPECT_EQ(selection.SelectedResourceName, "SceneNormal");
    EXPECT_EQ(selection.SelectedClass, Graphics::DebugViewResourceClass::Texture);
    EXPECT_EQ(debug.GetDiagnostics().LastFallbackReason, Graphics::DebugViewFallbackReason::None);
}

TEST(GraphicsDebugViewSystem, MissingDisabledAndUnsupportedSelectionsFallbackDeterministically)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();
    const Graphics::FrameRecipeIntrospection recipe = Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{});

    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = true, .RequestedResourceName = "NoSuchResource"});
    Graphics::DebugViewResolvedSelection missing = debug.ResolveSelection(recipe);
    EXPECT_TRUE(missing.Enabled);
    EXPECT_TRUE(missing.UsedFallback);
    EXPECT_EQ(missing.SelectedResourceName, "SceneColorLDR");
    EXPECT_EQ(missing.FallbackReason, Graphics::DebugViewFallbackReason::ResourceMissing);
    EXPECT_EQ(debug.GetDiagnostics().MissingResourceCount, 1u);

    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = true, .RequestedResourceName = "EntityId"});
    Graphics::DebugViewResolvedSelection disabled = debug.ResolveSelection(recipe);
    EXPECT_TRUE(disabled.Enabled);
    EXPECT_EQ(disabled.SelectedResourceName, "SceneColorLDR");
    EXPECT_EQ(disabled.FallbackReason, Graphics::DebugViewFallbackReason::ResourceDisabled);
    EXPECT_EQ(debug.GetDiagnostics().DisabledResourceCount, 1u);

    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = true, .RequestedResourceName = "GpuWorld.SceneTable"});
    Graphics::DebugViewResolvedSelection unsupported = debug.ResolveSelection(recipe);
    EXPECT_TRUE(unsupported.Enabled);
    EXPECT_EQ(unsupported.SelectedResourceName, "SceneColorLDR");
    EXPECT_EQ(unsupported.FallbackReason, Graphics::DebugViewFallbackReason::UnsupportedResourceClass);
    EXPECT_EQ(debug.GetDiagnostics().UnsupportedResourceCount, 1u);
}

TEST(GraphicsDebugViewSystem, DisabledDebugViewDoesNotEnablePreviewPass)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();
    debug.SetSettings(Graphics::DebugViewSettings{.Enabled = false, .RequestedResourceName = "SceneColorHDR"});

    const Graphics::DebugViewResolvedSelection selection = debug.ResolveSelection(
        Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{}));

    EXPECT_FALSE(selection.Enabled);
    EXPECT_TRUE(selection.UsedFallback);
    EXPECT_EQ(selection.FallbackReason, Graphics::DebugViewFallbackReason::DebugViewDisabled);
}

TEST(GraphicsDebugViewSystem, InspectionDumpIsStableAndIncludesPreviewability)
{
    Graphics::DebugViewSystem debug;
    debug.Initialize();
    const std::string dump = debug.FormatInspectionDump(Graphics::DescribeDefaultFrameRecipe(Graphics::FrameRecipeFeatures{}));

    EXPECT_NE(dump.find("debug-view-resources:"), std::string::npos);
    EXPECT_NE(dump.find("SceneColorHDR class=texture enabled=true sampleable=true previewable=true"), std::string::npos);
    EXPECT_NE(dump.find("GpuWorld.SceneTable class=buffer enabled=true sampleable=false previewable=false"), std::string::npos);
    EXPECT_NE(dump.find("DebugViewRGBA class=texture enabled=false sampleable=false previewable=false"), std::string::npos);
}

