#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

    [[nodiscard]] CoreConfig::EngineConfigSectionRegistration
    MakeTestSectionRegistration(
        std::string name,
        std::string defaultPayload,
        CoreConfig::EngineConfigSectionChangedCallback onChanged = {})
    {
        const std::string schemaId = "test." + name;
        return CoreConfig::EngineConfigSectionRegistration{
            .DefaultSection = CoreConfig::EngineConfigSection{
                .Name = std::move(name),
                .SchemaId = schemaId,
                .SchemaVersion = 1u,
                .PayloadJson = std::move(defaultPayload),
            },
            .Validate =
                [](const std::string_view documentPayloadJson,
                   const std::string_view /*referencePayloadJson*/,
                   const std::string_view /*diagnosticSubject*/)
                {
                    return CoreConfig::EngineConfigSectionValidationResult{
                        .State = CoreConfig::EngineConfigState::Valid,
                        .CanonicalPayloadJson =
                            std::string{documentPayloadJson},
                        .ParsedFieldCount = 1u,
                    };
                },
            .OnChanged = std::move(onChanged),
        };
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

TEST(RuntimeConfigControlFacade,
     GenericSectionChangesAreLexicallyReportedAndCallbacksObserveCommit)
{
    Runtime::Engine* engineAddress = nullptr;
    std::vector<std::string> callbackOrder{};
    bool callbacksObservedCommittedState = true;

    CoreConfig::EngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(MakeTestSectionRegistration(
        "zeta",
        R"json({"value":0})json",
        [&](const CoreConfig::EngineConfigSection& previous,
            const CoreConfig::EngineConfigSection& current)
        {
            callbackOrder.push_back(current.Name);
            const CoreConfig::EngineConfigSection* live =
                engineAddress != nullptr
                    ? CoreConfig::FindEngineConfigSection(
                          engineAddress->GetEngineConfig().AppSections,
                          current.Name)
                    : nullptr;
            callbacksObservedCommittedState &=
                previous.PayloadJson == R"json({"value":0})json" &&
                live != nullptr && *live == current &&
                engineAddress->GetConfigControl()
                    .GetEngineConfigControlState()
                    .LastApply.SectionChanged(current.Name);
        })));
    ASSERT_TRUE(registry.Register(MakeTestSectionRegistration(
        "alpha",
        R"json({"value":0})json",
        [&](const CoreConfig::EngineConfigSection& previous,
            const CoreConfig::EngineConfigSection& current)
        {
            callbackOrder.push_back(current.Name);
            const CoreConfig::EngineConfigSection* live =
                engineAddress != nullptr
                    ? CoreConfig::FindEngineConfigSection(
                          engineAddress->GetEngineConfig().AppSections,
                          current.Name)
                    : nullptr;
            callbacksObservedCommittedState &=
                previous.PayloadJson == R"json({"value":0})json" &&
                live != nullptr && *live == current &&
                engineAddress->GetConfigControl()
                    .GetEngineConfigControlState()
                    .LastApply.SectionChanged(current.Name);
        })));

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>(),
        std::move(registry));
    engineAddress = &engine;
    engine.Initialize();
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    CoreConfig::UpsertEngineConfigSection(
        candidate.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = "zeta",
            .SchemaId = "test.zeta",
            .SchemaVersion = 1u,
            .PayloadJson = R"json({"value":2})json",
        });
    CoreConfig::UpsertEngineConfigSection(
        candidate.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = "alpha",
            .SchemaId = "test.alpha",
            .SchemaVersion = 1u,
            .PayloadJson = R"json({"value":1})json",
        });

    const CoreConfig::EngineConfigLoadResult preview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-generic-sections.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));
    EXPECT_TRUE(callbackOrder.empty());

    const Runtime::RuntimeEngineConfigApplyResult apply =
        configControl.ApplyEngineConfigHotSubset(
            preview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(apply.Succeeded());
    EXPECT_EQ(apply.Status, Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_TRUE(apply.EngineConfigApplied);
    EXPECT_FALSE(apply.DefaultRecipeConfigPathChanged);
    EXPECT_EQ(apply.ChangedSectionNames,
              (std::vector<std::string>{"alpha", "zeta"}));
    EXPECT_TRUE(apply.SectionChanged("alpha"));
    EXPECT_TRUE(apply.SectionChanged("zeta"));
    EXPECT_FALSE(apply.SectionChanged("missing"));
    EXPECT_EQ(callbackOrder,
              (std::vector<std::string>{"alpha", "zeta"}));
    EXPECT_TRUE(callbacksObservedCommittedState);

    const CoreConfig::EngineConfigSection* activeAlpha =
        CoreConfig::FindEngineConfigSection(
            engine.GetEngineConfig().AppSections,
            "alpha");
    ASSERT_NE(activeAlpha, nullptr);
    EXPECT_EQ(activeAlpha->PayloadJson, R"json({"value":1})json");

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade,
     SectionCallbacksDoNotRunForPreviewNoChangeOrRejectedApply)
{
    std::uint32_t callbackCount = 0u;
    CoreConfig::EngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(MakeTestSectionRegistration(
        "test",
        R"json({"value":0})json",
        [&](const CoreConfig::EngineConfigSection&,
            const CoreConfig::EngineConfigSection&)
        {
            ++callbackCount;
        })));

    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>(),
        std::move(registry));
    engine.Initialize();
    Runtime::EngineConfigControl& configControl = engine.GetConfigControl();

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    CoreConfig::UpsertEngineConfigSection(
        candidate.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = "test",
            .SchemaId = "test.test",
            .SchemaVersion = 1u,
            .PayloadJson = R"json({"value":1})json",
        });
    const CoreConfig::EngineConfigLoadResult preview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-callback-contract.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(preview));
    EXPECT_EQ(callbackCount, 0u);

    const Runtime::RuntimeEngineConfigApplyResult applied =
        configControl.ApplyEngineConfigHotSubset(preview);
    ASSERT_EQ(applied.Status, Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_EQ(callbackCount, 1u);

    const Runtime::RuntimeEngineConfigApplyResult noChange =
        configControl.ApplyEngineConfigHotSubset(preview);
    EXPECT_EQ(noChange.Status, Runtime::RuntimeEngineConfigApplyStatus::NoChange);
    EXPECT_EQ(callbackCount, 1u);

    candidate = engine.GetEngineConfig();
    candidate.Window.Width += 1;
    CoreConfig::UpsertEngineConfigSection(
        candidate.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = "test",
            .SchemaId = "test.test",
            .SchemaVersion = 1u,
            .PayloadJson = R"json({"value":2})json",
        });
    const CoreConfig::EngineConfigLoadResult rejectedPreview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-rejected-callback.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(rejectedPreview));

    const Runtime::RuntimeEngineConfigApplyResult rejected =
        configControl.ApplyEngineConfigHotSubset(rejectedPreview);
    EXPECT_EQ(rejected.Status, Runtime::RuntimeEngineConfigApplyStatus::Rejected);
    EXPECT_EQ(callbackCount, 1u);

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
