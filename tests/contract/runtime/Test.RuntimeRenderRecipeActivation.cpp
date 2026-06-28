#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.SandboxEditorUi;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    class OneFrameApplication final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& /*engine*/) override {}
        void OnSimTick(Runtime::Engine& /*engine*/, double /*fixedDt*/) override {}
        void OnVariableTick(Runtime::Engine& engine, double /*alpha*/, double /*dt*/) override
        {
            engine.RequestExit();
        }
        void OnShutdown(Runtime::Engine& /*engine*/) override {}
    };

    [[nodiscard]] CoreConfig::EngineConfig HeadlessConfig()
    {
        CoreConfig::EngineConfig config = Runtime::CreateReferenceEngineConfig();
        config.Window.Backend = CoreConfig::WindowBackend::Null;
        config.Render.EnablePromotedVulkanDevice = false;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Render.DefaultRecipeConfigPath.clear();
        return config;
    }

    [[nodiscard]] std::filesystem::path TempRecipePath(const std::string& stem)
    {
        return std::filesystem::temp_directory_path() / (stem + ".json");
    }

    void WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        ASSERT_TRUE(file.is_open());
        file << text;
    }

    [[nodiscard]] std::string RenderRecipeConfigDisablingPostprocess(
        const std::string_view recipeId)
    {
        return std::string{R"json({
  "schema": ")json"} + std::string{Graphics::kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{Graphics::kCurrentRendererContractId} + R"json(",
  "recipe": {
    "recipeId": ")json" + std::string{recipeId} + R"json(",
    "fixedCoreName": "Extrinsic.Graphics.FrameRecipe.Default",
    "disabledExtensionSlots": ["postprocess"]
  }
})json";
    }

    [[nodiscard]] std::string InvalidRenderRecipeConfig()
    {
        return std::string{R"json({
  "schema": ")json"} + std::string{Graphics::kRenderRecipeConfigSchemaId} + R"json(",
  "version": 1,
  "rendererId": ")json" + std::string{Graphics::kCurrentRendererContractId} + R"json(",
  "recipe": {
    "disabledExtensionSlots": ["ray-traced-gi"]
  }
})json";
    }

    [[nodiscard]] const Graphics::RenderGraphCommandPassStats* FindCommandPass(
        const Graphics::RenderGraphFrameStats& stats,
        const std::string_view name)
    {
        for (const Graphics::RenderGraphCommandPassStats& pass : stats.CommandRecords.Passes)
        {
            if (pass.Name == name)
            {
                return &pass;
            }
        }
        return nullptr;
    }
}

TEST(RuntimeRenderRecipeActivation, StartupRecipeConfigDisablesPostprocessOnFirstFrame)
{
    const std::filesystem::path path =
        TempRecipePath("intrinsic_runtime_recipe_startup_valid");
    WriteTextFile(path, RenderRecipeConfigDisablingPostprocess("runtime.startup.disable-post"));

    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.DefaultRecipeConfigPath = path.string();
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    engine.Initialize();
    std::filesystem::remove(path);

    const Runtime::RuntimeRenderRecipeState& recipeState = engine.GetRenderRecipeState();
    ASSERT_TRUE(recipeState.HasLastApply);
    EXPECT_EQ(recipeState.LastApply.Status, Runtime::RuntimeRenderRecipeApplyStatus::Applied);
    EXPECT_EQ(recipeState.ActiveSource,
              Runtime::RuntimeRenderRecipeActivationSource::StartupConfigFile);
    ASSERT_TRUE(recipeState.ActiveOverride.has_value());
    ASSERT_EQ(recipeState.ActiveOverride->DisabledExtensionSlots.size(), 1u);
    EXPECT_EQ(recipeState.ActiveOverride->DisabledExtensionSlots.front(), "postprocess");

    engine.Run();

    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_TRUE(stats.FrameRecipeOverrideApplied);
    EXPECT_EQ(stats.FrameRecipeOverrideDisabledSlotCount, 1u);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_NE(FindCommandPass(stats, "Present"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation, MissingStartupRecipeConfigFallsBackToDefault)
{
    const std::filesystem::path path =
        TempRecipePath("intrinsic_runtime_recipe_startup_missing");
    std::filesystem::remove(path);

    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.DefaultRecipeConfigPath = path.string();
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    engine.Initialize();

    const Runtime::RuntimeRenderRecipeState& recipeState = engine.GetRenderRecipeState();
    ASSERT_TRUE(recipeState.HasLastApply);
    EXPECT_EQ(recipeState.LastApply.Status, Runtime::RuntimeRenderRecipeApplyStatus::Rejected);
    EXPECT_TRUE(Graphics::HasDiagnostic(
        recipeState.LastApply.LoadResult,
        Graphics::RenderRecipeConfigDiagnosticCode::LoadError));
    EXPECT_FALSE(recipeState.ActiveOverride.has_value());

    engine.Run();

    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_FALSE(stats.FrameRecipeOverrideActive);
    EXPECT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation, InvalidStartupRecipeConfigFallsBackToDefault)
{
    const std::filesystem::path path =
        TempRecipePath("intrinsic_runtime_recipe_startup_invalid");
    WriteTextFile(path, InvalidRenderRecipeConfig());

    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.DefaultRecipeConfigPath = path.string();
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    engine.Initialize();
    std::filesystem::remove(path);

    const Runtime::RuntimeRenderRecipeState& recipeState = engine.GetRenderRecipeState();
    ASSERT_TRUE(recipeState.HasLastApply);
    EXPECT_EQ(recipeState.LastApply.Status, Runtime::RuntimeRenderRecipeApplyStatus::Rejected);
    EXPECT_TRUE(Graphics::HasDiagnostic(
        recipeState.LastApply.LoadResult,
        Graphics::RenderRecipeConfigDiagnosticCode::UnknownRecipeSlot));
    EXPECT_FALSE(recipeState.ActiveOverride.has_value());

    engine.Run();

    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_FALSE(stats.FrameRecipeOverrideActive);
    EXPECT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation, EditorActivationCommandRoutesThroughRuntimeApplyPath)
{
    CoreConfig::EngineConfig config = HeadlessConfig();
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    engine.Initialize();

    Graphics::RenderRecipeConfigContext recipeContext =
        engine.CreateRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::SandboxEditorContext context{};
    context.RenderRecipeContext = &recipeContext;
    context.RenderRecipeEditorState = &editorState;
    context.RenderRecipeRuntimeState = &engine.GetRenderRecipeState();
    context.PreviewRenderRecipeDocument =
        [&engine](const std::string& document, const std::string& sourceId)
        {
            return engine.PreviewRenderRecipeConfigDocument(document, sourceId);
        };
    context.ApplyRenderRecipePreview =
        [&engine](const Graphics::RenderRecipeConfigLoadResult& loadResult)
        {
            return engine.ApplyRenderRecipeConfigPreview(
                loadResult,
                Runtime::RuntimeRenderRecipeActivationSource::Editor);
        };
    context.RenderRecipeCommandsAvailable = true;

    Runtime::SandboxEditorRenderRecipeCommandResult result =
        Runtime::ApplySandboxEditorRenderRecipeCommand(
            context,
            Runtime::SandboxEditorRenderRecipeCommand{
                .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::PreviewDraft,
                .Document =
                    RenderRecipeConfigDisablingPostprocess("runtime.editor.disable-post"),
                .SourceId = "editor-preview.json",
            });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Previewed);

    result = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::ActivatePreview,
        });
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Status,
              Runtime::SandboxEditorRenderRecipeCommandStatus::Activated);

    const Runtime::RuntimeRenderRecipeState& recipeState = engine.GetRenderRecipeState();
    ASSERT_TRUE(recipeState.ActiveOverride.has_value());
    EXPECT_EQ(recipeState.ActiveSource,
              Runtime::RuntimeRenderRecipeActivationSource::Editor);
    EXPECT_EQ(recipeState.ActiveConfig.Preview.Recipe.RecipeId,
              "runtime.editor.disable-post");

    const Runtime::SandboxEditorRenderRecipeEditorModel model =
        Runtime::BuildSandboxEditorRenderRecipeEditorModel(context);
    EXPECT_EQ(model.ActiveRecipeId, "runtime.editor.disable-post");

    engine.Run();

    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_TRUE(stats.FrameRecipeOverrideApplied);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
}
