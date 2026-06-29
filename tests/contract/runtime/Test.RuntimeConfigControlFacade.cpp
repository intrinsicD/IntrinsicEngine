#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
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

    [[nodiscard]] std::filesystem::path TempPath(const std::string& stem)
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

    [[nodiscard]] bool ContainsField(const std::vector<std::string>& fields,
                                     const std::string_view field)
    {
        return std::find(fields.begin(), fields.end(), field) != fields.end();
    }
}

TEST(RuntimeConfigControlFacade, AgentCliControlsRecipeAndEngineConfigWithoutUi)
{
    const std::filesystem::path recipePath =
        TempPath("intrinsic_runtime_config_control_agent_recipe");
    WriteTextFile(recipePath, RenderRecipeConfigDisablingPostprocess(
                                  "runtime.agent.config-control"));

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    const Graphics::RenderRecipeConfigLoadResult preview =
        engine.PreviewRenderRecipeConfigDocument(
            RenderRecipeConfigDisablingPostprocess("runtime.agent.preview"),
            "agent-preview.json");
    ASSERT_TRUE(Graphics::IsConfigUsable(preview));

    const Runtime::RuntimeRenderRecipeApplyResult directApply =
        engine.ApplyRenderRecipeConfigPreview(
            preview,
            Runtime::RuntimeRenderRecipeActivationSource::AgentCli);
    ASSERT_TRUE(directApply.Succeeded());
    EXPECT_EQ(engine.GetRenderRecipeState().ActiveSource,
              Runtime::RuntimeRenderRecipeActivationSource::AgentCli);

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath = recipePath.string();
    const CoreConfig::EngineConfigLoadResult configPreview =
        engine.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-engine-config.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        engine.ApplyEngineConfigHotSubset(
            configPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(configApply.Succeeded());
    EXPECT_EQ(configApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_TRUE(configApply.EngineConfigApplied);
    EXPECT_TRUE(configApply.DefaultRecipeConfigPathChanged);
    EXPECT_TRUE(configApply.RecipeApply.Succeeded());
    EXPECT_EQ(configApply.RecipeApply.Source,
              Runtime::RuntimeRenderRecipeActivationSource::AgentCli);
    EXPECT_EQ(engine.GetEngineConfig().Render.DefaultRecipeConfigPath,
              recipePath.string());
    EXPECT_EQ(engine.GetEngineConfigControlState().ActiveConfig.Render
                  .DefaultRecipeConfigPath,
              recipePath.string());

    std::filesystem::remove(recipePath);
    engine.Run();

    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade, BootOnlyEngineConfigDifferencesAreRejected)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Window.Width += 1;
    const CoreConfig::EngineConfigLoadResult configPreview =
        engine.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-boot-only.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        engine.ApplyEngineConfigHotSubset(
            configPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    EXPECT_FALSE(configApply.Succeeded());
    EXPECT_EQ(configApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_TRUE(ContainsField(configApply.RejectedBootOnlyFields, "window.width"));
    EXPECT_EQ(engine.GetEngineConfig().Window.Width,
              HeadlessConfig().Window.Width);

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade, InvalidHotRecipeConfigPreservesActiveOverride)
{
    const std::filesystem::path invalidPath =
        TempPath("intrinsic_runtime_config_control_invalid_recipe");
    WriteTextFile(invalidPath, InvalidRenderRecipeConfig());

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    const Runtime::RuntimeRenderRecipeApplyResult baselineApply =
        engine.ActivateRenderRecipeConfigDocument(
            RenderRecipeConfigDisablingPostprocess("runtime.agent.baseline"),
            "baseline-preview.json",
            Runtime::RuntimeRenderRecipeActivationSource::AgentCli);
    ASSERT_TRUE(baselineApply.Succeeded());
    ASSERT_TRUE(engine.GetRenderRecipeState().ActiveOverride.has_value());
    EXPECT_EQ(engine.GetRenderRecipeState().ActiveConfig.Preview.Recipe.RecipeId,
              "runtime.agent.baseline");

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath = invalidPath.string();
    const CoreConfig::EngineConfigLoadResult configPreview =
        engine.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-invalid-hot-recipe.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        engine.ApplyEngineConfigHotSubset(
            configPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    EXPECT_FALSE(configApply.Succeeded());
    EXPECT_EQ(configApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_EQ(configApply.RecipeApply.Status,
              Runtime::RuntimeRenderRecipeApplyStatus::Rejected);
    EXPECT_EQ(engine.GetEngineConfig().Render.DefaultRecipeConfigPath, "");
    ASSERT_TRUE(engine.GetRenderRecipeState().ActiveOverride.has_value());
    EXPECT_EQ(engine.GetRenderRecipeState().ActiveConfig.Preview.Recipe.RecipeId,
              "runtime.agent.baseline");

    std::filesystem::remove(invalidPath);
    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade, EditorAndAgentPreviewUseSameFacadeResult)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();

    const std::string document =
        RenderRecipeConfigDisablingPostprocess("runtime.shared.config-control");
    const Graphics::RenderRecipeConfigLoadResult agentPreview =
        engine.PreviewRenderRecipeConfigDocument(document, "shared-preview.json");
    ASSERT_TRUE(Graphics::IsConfigUsable(agentPreview));

    Graphics::RenderRecipeConfigContext recipeContext =
        engine.CreateRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::SandboxEditorContext context{};
    context.RenderRecipeContext = &recipeContext;
    context.RenderRecipeEditorState = &editorState;
    context.RenderRecipeRuntimeState = &engine.GetRenderRecipeState();
    context.PreviewRenderRecipeDocument =
        [&engine](const std::string& draft, const std::string& sourceId)
        {
            return engine.PreviewRenderRecipeConfigDocument(draft, sourceId);
        };
    context.ApplyRenderRecipePreview =
        [&engine](const Graphics::RenderRecipeConfigLoadResult& loadResult)
        {
            return engine.ApplyRenderRecipeConfigPreview(
                loadResult,
                Runtime::RuntimeRenderRecipeActivationSource::Editor);
        };
    context.RenderRecipeCommandsAvailable = true;

    Runtime::SandboxEditorRenderRecipeCommandResult editorResult =
        Runtime::ApplySandboxEditorRenderRecipeCommand(
            context,
            Runtime::SandboxEditorRenderRecipeCommand{
                .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::PreviewDraft,
                .Document = document,
                .SourceId = "shared-preview.json",
            });
    ASSERT_TRUE(editorResult.Succeeded());
    ASSERT_TRUE(editorState.HasLastPreview);
    EXPECT_EQ(editorState.LastPreview.State, agentPreview.State);
    EXPECT_EQ(editorState.LastPreview.Preview.Recipe.RecipeId,
              agentPreview.Preview.Recipe.RecipeId);
    EXPECT_EQ(editorState.LastPreview.Preview.DisabledExtensionSlots,
              agentPreview.Preview.DisabledExtensionSlots);

    editorResult = Runtime::ApplySandboxEditorRenderRecipeCommand(
        context,
        Runtime::SandboxEditorRenderRecipeCommand{
            .Kind = Runtime::SandboxEditorRenderRecipeCommandKind::ActivatePreview,
        });
    ASSERT_TRUE(editorResult.Succeeded());
    EXPECT_EQ(engine.GetRenderRecipeState().ActiveSource,
              Runtime::RuntimeRenderRecipeActivationSource::Editor);
    EXPECT_EQ(engine.GetRenderRecipeState().ActiveConfig.Preview.Recipe.RecipeId,
              agentPreview.Preview.Recipe.RecipeId);

    engine.Shutdown();
}
