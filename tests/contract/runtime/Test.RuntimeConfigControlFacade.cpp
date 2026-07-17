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
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.SandboxEditorFacades;

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
        config.Simulation.WorkerThreadCount = 1u;
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
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    const Graphics::RenderRecipeConfigLoadResult preview =
        configControl.PreviewRenderRecipeConfigDocument(
            RenderRecipeConfigDisablingPostprocess("runtime.agent.preview"),
            "agent-preview.json");
    ASSERT_TRUE(Graphics::IsConfigUsable(preview));

    const Runtime::RuntimeRenderRecipeApplyResult directApply =
        configControl.ApplyRenderRecipeConfigPreview(
            preview,
            Runtime::RuntimeRenderRecipeActivationSource::AgentCli);
    ASSERT_TRUE(directApply.Succeeded());
    EXPECT_EQ(configControl.GetRenderRecipeState().ActiveSource,
              Runtime::RuntimeRenderRecipeActivationSource::AgentCli);

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath = recipePath.string();
    const CoreConfig::EngineConfigLoadResult configPreview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-engine-config.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        configControl.ApplyEngineConfigHotSubset(
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
    EXPECT_EQ(configControl.GetEngineConfigControlState().ActiveConfig.Render
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
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Window.Width += 1;
    const CoreConfig::EngineConfigLoadResult configPreview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-boot-only.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        configControl.ApplyEngineConfigHotSubset(
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

TEST(RuntimeConfigControlFacade, SandboxProgressivePoissonConfigIsHotApplied)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Sandbox.ProgressivePoisson.Dimension = 2u;
    candidate.Sandbox.ProgressivePoisson.GridWidth = 9u;
    candidate.Sandbox.ProgressivePoisson.MaxLevels = 10u;
    candidate.Sandbox.ProgressivePoisson.HashLoadFactor = 0.5;
    candidate.Sandbox.ProgressivePoisson.RadiusAlpha = 0.2;
    candidate.Sandbox.ProgressivePoisson.RandomizeGridOrigin = false;
    candidate.Sandbox.ProgressivePoisson.GridOriginSeed = 91u;
    candidate.Sandbox.ProgressivePoisson.ShuffleWithinLevels = false;
    candidate.Sandbox.ProgressivePoisson.ShuffleSeed = 12345u;
    candidate.Sandbox.ProgressivePoisson.PrefixCount = 17u;
    candidate.Sandbox.ProgressivePoisson.Channel =
        CoreConfig::ProgressivePoissonPlaygroundChannel::Phase;
    candidate.Sandbox.ProgressivePoisson.Backend =
        CoreConfig::ProgressivePoissonPlaygroundBackend::VulkanCompute;
    candidate.Sandbox.ProgressivePoisson.MeshSurfaceSampleCount = 96u;
    candidate.Sandbox.ProgressivePoisson.MeshSurfaceSampleSeed = 7u;
    candidate.Sandbox.ProgressivePoisson.MeshSurfaceMinTriangleArea = 1.0e-8;
    candidate.Sandbox.ProgressivePoisson.MeshSurfaceInterpolateNormals = false;
    candidate.Sandbox.ProgressivePoisson.AutoRunOnEdit = false;
    candidate.Sandbox.ProgressivePoisson.DebounceSeconds = 0.5;

    const CoreConfig::EngineConfigLoadResult configPreview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-progressive-poisson.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        configControl.ApplyEngineConfigHotSubset(
            configPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(configApply.Succeeded());
    EXPECT_EQ(configApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_TRUE(configApply.EngineConfigApplied);
    EXPECT_FALSE(configApply.DefaultRecipeConfigPathChanged);
    EXPECT_TRUE(configApply.SandboxProgressivePoissonChanged);

    const CoreConfig::ProgressivePoissonPlaygroundConfig& active =
        engine.GetEngineConfig().Sandbox.ProgressivePoisson;
    EXPECT_EQ(active.Dimension, 2u);
    EXPECT_EQ(active.GridWidth, 9u);
    EXPECT_EQ(active.MaxLevels, 10u);
    EXPECT_DOUBLE_EQ(active.HashLoadFactor, 0.5);
    EXPECT_DOUBLE_EQ(active.RadiusAlpha, 0.2);
    EXPECT_FALSE(active.RandomizeGridOrigin);
    EXPECT_EQ(active.GridOriginSeed, 91u);
    EXPECT_FALSE(active.ShuffleWithinLevels);
    EXPECT_EQ(active.ShuffleSeed, 12345u);
    EXPECT_EQ(active.PrefixCount, 17u);
    EXPECT_EQ(active.Channel,
              CoreConfig::ProgressivePoissonPlaygroundChannel::Phase);
    EXPECT_EQ(active.Backend,
              CoreConfig::ProgressivePoissonPlaygroundBackend::VulkanCompute);
    EXPECT_EQ(active.MeshSurfaceSampleCount, 96u);
    EXPECT_EQ(active.MeshSurfaceSampleSeed, 7u);
    EXPECT_DOUBLE_EQ(active.MeshSurfaceMinTriangleArea, 1.0e-8);
    EXPECT_FALSE(active.MeshSurfaceInterpolateNormals);
    EXPECT_FALSE(active.AutoRunOnEdit);
    EXPECT_DOUBLE_EQ(active.DebounceSeconds, 0.5);
    EXPECT_EQ(configControl.GetEngineConfigControlState().ActiveConfig.Sandbox
                  .ProgressivePoisson.GridWidth,
              9u);

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade,
     ParameterizationViewFieldsAreIndividuallyHotApplied)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    const auto applyCandidate = [&configControl](
                                    CoreConfig::EngineConfig candidate,
                                    const std::string& sourceId)
    {
        const CoreConfig::EngineConfigLoadResult preview =
            configControl.PreviewEngineConfigControlDocument(
                CoreConfig::SerializeEngineConfig(candidate),
                sourceId);
        EXPECT_TRUE(CoreConfig::IsConfigUsable(preview));

        const Runtime::RuntimeEngineConfigApplyResult result =
            configControl.ApplyEngineConfigHotSubset(
                preview,
                Runtime::RuntimeConfigControlSource::AgentCli);
        EXPECT_TRUE(result.Succeeded());
        EXPECT_EQ(result.Status,
                  Runtime::RuntimeEngineConfigApplyStatus::Applied);
        EXPECT_TRUE(result.EngineConfigApplied);
        EXPECT_TRUE(result.SandboxParameterizationChanged);
        EXPECT_FALSE(result.SandboxProgressivePoissonChanged);
        EXPECT_FALSE(result.DefaultRecipeConfigPathChanged);
        EXPECT_EQ(result.Source,
                  Runtime::RuntimeConfigControlSource::AgentCli);
    };

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Sandbox.Parameterization.View.RenderMode =
        CoreConfig::ParameterizationUvRenderMode::GpuShaded;
    applyCandidate(candidate, "agent-parameterization-view-render-mode.json");
    EXPECT_EQ(engine.GetEngineConfig().Sandbox.Parameterization.View.RenderMode,
              CoreConfig::ParameterizationUvRenderMode::GpuShaded);

    candidate = engine.GetEngineConfig();
    candidate.Sandbox.Parameterization.View.BackgroundMode =
        CoreConfig::ParameterizationUvBackgroundMode::Checker;
    applyCandidate(candidate, "agent-parameterization-view-background.json");
    EXPECT_EQ(
        engine.GetEngineConfig().Sandbox.Parameterization.View.BackgroundMode,
        CoreConfig::ParameterizationUvBackgroundMode::Checker);

    candidate = engine.GetEngineConfig();
    candidate.Sandbox.Parameterization.View.ShowDistortionHeatmap = true;
    applyCandidate(candidate, "agent-parameterization-view-heatmap.json");
    EXPECT_TRUE(engine.GetEngineConfig()
                    .Sandbox.Parameterization.View.ShowDistortionHeatmap);
    EXPECT_EQ(configControl.GetEngineConfigControlState().LastApply.Source,
              Runtime::RuntimeConfigControlSource::AgentCli);

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade, InvalidHotRecipeConfigPreservesActiveOverride)
{
    const std::filesystem::path invalidPath =
        TempPath("intrinsic_runtime_config_control_invalid_recipe");
    WriteTextFile(invalidPath, InvalidRenderRecipeConfig());

    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    const Runtime::RuntimeRenderRecipeApplyResult baselineApply =
        configControl.ActivateRenderRecipeConfigDocument(
            RenderRecipeConfigDisablingPostprocess("runtime.agent.baseline"),
            "baseline-preview.json",
            Runtime::RuntimeRenderRecipeActivationSource::AgentCli);
    ASSERT_TRUE(baselineApply.Succeeded());
    ASSERT_TRUE(configControl.GetRenderRecipeState().ActiveOverride.has_value());
    EXPECT_EQ(configControl.GetRenderRecipeState().ActiveConfig.Preview.Recipe.RecipeId,
              "runtime.agent.baseline");

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath = invalidPath.string();
    const CoreConfig::EngineConfigLoadResult configPreview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-invalid-hot-recipe.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(configPreview));

    const Runtime::RuntimeEngineConfigApplyResult configApply =
        configControl.ApplyEngineConfigHotSubset(
            configPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    EXPECT_FALSE(configApply.Succeeded());
    EXPECT_EQ(configApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_EQ(configApply.RecipeApply.Status,
              Runtime::RuntimeRenderRecipeApplyStatus::Rejected);
    EXPECT_EQ(engine.GetEngineConfig().Render.DefaultRecipeConfigPath, "");
    ASSERT_TRUE(configControl.GetRenderRecipeState().ActiveOverride.has_value());
    EXPECT_EQ(configControl.GetRenderRecipeState().ActiveConfig.Preview.Recipe.RecipeId,
              "runtime.agent.baseline");

    std::filesystem::remove(invalidPath);
    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade, EditorAndAgentPreviewUseSameFacadeResult)
{
    Runtime::Engine engine(HeadlessConfig(), std::make_unique<OneFrameApplication>());
    engine.Initialize();
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    const std::string document =
        RenderRecipeConfigDisablingPostprocess("runtime.shared.config-control");
    const Graphics::RenderRecipeConfigLoadResult agentPreview =
        configControl.PreviewRenderRecipeConfigDocument(
            document,
            "shared-preview.json");
    ASSERT_TRUE(Graphics::IsConfigUsable(agentPreview));

    Graphics::RenderRecipeConfigContext recipeContext =
        configControl.CreateRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::SandboxEditorContext context{};
    context.RenderRecipeContext = &recipeContext;
    context.RenderRecipeEditorState = &editorState;
    context.RenderRecipeRuntimeState = &configControl.GetRenderRecipeState();
    context.PreviewRenderRecipeDocument =
        [&configControl](const std::string& draft, const std::string& sourceId)
        {
            return configControl.PreviewRenderRecipeConfigDocument(
                draft,
                sourceId);
        };
    context.ApplyRenderRecipePreview =
        [&configControl](const Graphics::RenderRecipeConfigLoadResult& loadResult)
        {
            return configControl.ApplyRenderRecipeConfigPreview(
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
    EXPECT_EQ(configControl.GetRenderRecipeState().ActiveSource,
              Runtime::RuntimeRenderRecipeActivationSource::Editor);
    EXPECT_EQ(configControl.GetRenderRecipeState().ActiveConfig.Preview.Recipe.RecipeId,
              agentPreview.Preview.Recipe.RecipeId);

    engine.Shutdown();
}
