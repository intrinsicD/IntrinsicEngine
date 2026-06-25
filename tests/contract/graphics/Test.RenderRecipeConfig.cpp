#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

import Extrinsic.Core.Geometry2D;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.RenderingContract;

namespace
{
    using namespace Extrinsic::Graphics;

    [[nodiscard]] RenderRecipeConfigContext MakeContext()
    {
        RenderFrameInput input{};
        input.Viewport = Extrinsic::Core::Extent2D{.Width = 1280, .Height = 720};
        input.Camera.Valid = true;
        return RenderRecipeConfigContext{
            .Renderer = MakeCurrentRendererDescriptor(),
            .BaseRecipe = MakeCurrentRendererRecipeDescriptor(),
            .BaseViewOutput = MakeCurrentRendererViewOutputRecipe(input),
            .BaseBindings = MakeCurrentRendererBindingSet(),
        };
    }

    [[nodiscard]] std::string ValidRecipeConfig()
    {
        return std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json(",
  "revision": "unit-test",
  "recipe": {
    "recipeId": "current-renderer.user-preview",
    "fixedCoreName": "Extrinsic.Graphics.FrameRecipe.Default",
    "slots": [
      {
        "name": "lighting",
        "schemaId": "intrinsic.graphics.lighting/user-preview/v1",
        "defaults": "unit-test lighting defaults",
        "requiredCapabilities": ["LightingRecipe"],
        "allowedBindingRoles": ["light-snapshots", "material-table"],
        "usedBindingRoles": ["light-snapshots"],
        "validationRules": ["declared-slot-only"],
        "fallbackPolicy": "Degrade"
      }
    ]
  },
  "viewOutput": {
    "recipeId": "current-renderer.preview-output",
    "view": "Preview",
    "viewport": {"width": 640, "height": 360},
    "renderScale": 1.0,
    "target": "OffscreenTexture",
    "captureRequested": true,
    "readbackRequested": true,
    "mode": "Headless",
    "outputs": [
      {"name": "color", "kind": "Color", "format": "RGBA8_UNORM", "required": true},
      {"name": "readback", "kind": "ReadbackBuffer", "format": "Host-visible buffer", "required": false}
    ]
  },
  "bindingOverrides": [
    {
      "semanticName": "light-snapshots",
      "slot": "lighting",
      "sourceDomain": "Scene",
      "sourceIdentity": "RenderWorld.Lights.UserPreview",
      "sourceRevision": "config-revision",
      "valueType": "Buffer",
      "valueFormat": "LightSnapshot",
      "fallbackPolicy": "Degrade"
    }
  ]
})json";
    }

    [[nodiscard]] const RecipeExtensionSlotDescriptor* FindSlot(
        const RenderRecipeDescriptor& recipe,
        const std::string_view name)
    {
        const auto it = std::find_if(recipe.Slots.begin(),
                                     recipe.Slots.end(),
                                     [name](const RecipeExtensionSlotDescriptor& slot) {
                                         return slot.StableName == name;
                                     });
        return it == recipe.Slots.end() ? nullptr : &*it;
    }

    [[nodiscard]] const BindingIntent* FindBinding(const BindingSet& bindings,
                                                   const std::string_view name)
    {
        const auto it = std::find_if(bindings.Intents.begin(),
                                     bindings.Intents.end(),
                                     [name](const BindingIntent& intent) {
                                         return intent.SemanticName == name;
                                     });
        return it == bindings.Intents.end() ? nullptr : &*it;
    }
}

TEST(RenderRecipeConfig, ValidRecipeConfigLoadsIntoContractValues)
{
    const RenderRecipeConfigContext context = MakeContext();

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(
        ValidRecipeConfig(),
        context,
        RenderRecipeConfigParseOptions{.SourceId = "valid-preview.json"});

    ASSERT_TRUE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Valid);
    EXPECT_EQ(result.SchemaVersion, kRenderRecipeConfigSchemaVersion);
    EXPECT_EQ(result.RendererId, kCurrentRendererContractId);
    EXPECT_EQ(result.Preview.Recipe.RecipeId, "current-renderer.user-preview");
    EXPECT_EQ(result.Preview.Recipe.FixedCoreName, context.BaseRecipe.FixedCoreName);
    EXPECT_EQ(result.Preview.ViewOutput.RecipeId, "current-renderer.preview-output");
    EXPECT_EQ(result.Preview.ViewOutput.View, ViewKind::Preview);
    EXPECT_EQ(result.Preview.ViewOutput.Target, OutputTargetKind::OffscreenTexture);
    EXPECT_TRUE(result.Preview.ViewOutput.ReadbackRequested);
    EXPECT_EQ(result.Preview.ViewOutput.ViewportWidth, 640u);
    EXPECT_EQ(result.Preview.ViewOutput.ViewportHeight, 360u);
    EXPECT_EQ(result.Preview.ParsedSlotCount, 1u);
    EXPECT_EQ(result.Preview.ParsedBindingOverrideCount, 1u);
    EXPECT_TRUE(IsCompatible(result.ContractDiagnostics));

    const RecipeExtensionSlotDescriptor* lighting = FindSlot(result.Preview.Recipe, "lighting");
    ASSERT_NE(lighting, nullptr);
    EXPECT_EQ(lighting->SchemaId, "intrinsic.graphics.lighting/user-preview/v1");
    EXPECT_EQ(lighting->UsedBindingRoles.size(), 1u);
    EXPECT_EQ(lighting->UsedBindingRoles.front(), "light-snapshots");

    const BindingIntent* lights = FindBinding(result.Preview.Bindings, "light-snapshots");
    ASSERT_NE(lights, nullptr);
    EXPECT_EQ(lights->SourceDomain, BindingSourceDomain::Scene);
    EXPECT_EQ(lights->SourceIdentity, "RenderWorld.Lights.UserPreview");
}

TEST(RenderRecipeConfig, FileLoaderUsesSameDryRunValidation)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "IntrinsicEngine.RenderRecipeConfig.Valid.json";
    std::filesystem::remove(path);
    {
        std::ofstream stream{path};
        ASSERT_TRUE(stream);
        stream << ValidRecipeConfig();
    }

    const RenderRecipeConfigLoadResult result = LoadRenderRecipeConfigFile(path.string(), context);
    std::filesystem::remove(path);

    ASSERT_TRUE(IsConfigUsable(result));
    EXPECT_EQ(result.SourceId, path.string());
    EXPECT_EQ(result.Preview.ViewOutput.Target, OutputTargetKind::OffscreenTexture);
}

TEST(RenderRecipeConfig, UnknownSlotsFailClosed)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json(",
  "recipe": {
    "slots": [
      {"name": "ray-traced-gi", "defaults": "not declared"}
    ]
  }
})json";

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);

    EXPECT_FALSE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Unsupported);
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::UnknownRecipeSlot));
}

TEST(RenderRecipeConfig, UnsupportedCapabilityRequirementsAreRejected)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json(",
  "recipe": {
    "slots": [
      {
        "name": "lighting",
        "requiredCapabilities": ["LightingRecipe", "RayTracing"]
      }
    ]
  }
})json";

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);

    EXPECT_FALSE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Unsupported);
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::UnsupportedCapability));
}

TEST(RenderRecipeConfig, RuntimeOwnedBindingDomainsAreRejected)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json(",
  "bindingOverrides": [
    {
      "semanticName": "light-snapshots",
      "slot": "lighting",
      "sourceDomain": "Runtime",
      "sourceIdentity": "Runtime.LiveLights"
    }
  ]
})json";

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);

    EXPECT_FALSE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Invalid);
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::UnsafeBindingDomain));
}

TEST(RenderRecipeConfig, VersionMismatchIsUnsupported)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 2,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json("
})json";

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);

    EXPECT_FALSE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Unsupported);
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::UnsupportedVersion));
}

TEST(RenderRecipeConfig, FallbackAndDegradedStatesRemainDistinct)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json(",
  "degraded": true,
  "fallbacks": [
    {"subject": "lighting", "state": "fallbackApplied", "message": "using default lighting"},
    {"subject": "debug-view", "state": "degraded", "message": "debug overlay disabled"}
  ]
})json";

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);

    EXPECT_TRUE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Degraded);
    EXPECT_GE(CountByState(result, RenderRecipeConfigState::Degraded), 1u);
    EXPECT_GE(CountByState(result, RenderRecipeConfigState::FallbackApplied), 1u);
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::FallbackApplied));
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::DegradedConfig));
}

TEST(RenderRecipeConfig, FixedRendererCoreCannotBeReplacedByConfig)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = std::string{R"json({
  "schema": ")json"} + std::string{kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{kCurrentRendererContractId} + R"json(",
  "recipe": {
    "fixedCoreName": "External.ScriptedFrameCore",
    "slots": [
      {"name": "default-frame-core", "kind": "FixedCore", "defaults": "replace core"}
    ]
  }
})json";

    const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);

    EXPECT_FALSE(IsConfigUsable(result));
    EXPECT_EQ(result.State, RenderRecipeConfigState::Invalid);
    EXPECT_TRUE(HasDiagnostic(result, RenderRecipeConfigDiagnosticCode::FixedCoreMutation));
    EXPECT_EQ(result.Preview.Recipe.FixedCoreName, context.BaseRecipe.FixedCoreName);
}

TEST(RenderRecipeConfig, PreviewValidationIsSideEffectFreeAndInteractiveFast)
{
    const RenderRecipeConfigContext context = MakeContext();
    const std::string document = ValidRecipeConfig();
    const std::string originalRecipeId = context.BaseRecipe.RecipeId;
    const RecipeExtensionSlotDescriptor* originalLighting = FindSlot(context.BaseRecipe, "lighting");
    ASSERT_NE(originalLighting, nullptr);
    const std::string originalLightingSchema = originalLighting->SchemaId;

    const auto started = std::chrono::steady_clock::now();
    for (std::uint32_t i = 0u; i < 64u; ++i)
    {
        const RenderRecipeConfigLoadResult result = PreviewRenderRecipeConfig(document, context);
        ASSERT_TRUE(IsConfigUsable(result));
        ASSERT_TRUE(result.Preview.SideEffectFree);
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 1000);
    EXPECT_EQ(context.BaseRecipe.RecipeId, originalRecipeId);
    const RecipeExtensionSlotDescriptor* lightingAfter = FindSlot(context.BaseRecipe, "lighting");
    ASSERT_NE(lightingAfter, nullptr);
    EXPECT_EQ(lightingAfter->SchemaId, originalLightingSchema);
    EXPECT_EQ(context.BaseViewOutput.RecipeId, kCurrentRendererDefaultViewRecipeId);
}
