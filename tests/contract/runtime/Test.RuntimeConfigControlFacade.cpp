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

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Graphics.CurrentRendererContractAdapter;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SandboxEditorFacades;

namespace CoreConfig = Extrinsic::Core::Config;
namespace Graphics = Extrinsic::Graphics;
namespace Runtime = Extrinsic::Runtime;

namespace
{
    class OneFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double /*alpha*/, double /*dt*/) override
        {
            auto& engine = Kernel();
            engine.RequestExit();
        }
        void Shutdown() override {}
    };

    class TwoFrameApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        void Resolve() override {}
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            ++m_Frames;
            if (m_Frames >= 2u)
            {
                engine.RequestExit();
            }
        }
        void Shutdown() override {}

    private:
        std::uint32_t m_Frames{0u};
    };

    class GpuProfilingUiEndToggleModule final :
        public Runtime::IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "Test.GpuProfilingUiEndToggle";
        }

        [[nodiscard]] Extrinsic::Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            if (Extrinsic::Core::Result registered =
                    setup.RegisterFrameHook(
                        Runtime::FramePhase::UiEndCapture,
                        [this](Runtime::RuntimeFrameHookContext& context)
                        {
                            Runtime::EngineConfigControl* control =
                                context.Services.Find<
                                    Runtime::EngineConfigControl>();
                            if (control == nullptr)
                            {
                                return;
                            }
                            CoreConfig::EngineConfig candidate =
                                control->GetEngineConfigControlState()
                                    .ActiveConfig;
                            candidate.Render.EnableGpuProfiling =
                                m_ToggleCount == 0u;
                            const CoreConfig::EngineConfigLoadResult preview =
                                control
                                    ->PreviewEngineConfigControlDocument(
                                        CoreConfig::SerializeEngineConfig(
                                            candidate),
                                        "test.ui-end-gpu-profiling");
                            ApplyResults.push_back(
                                control->ApplyEngineConfigHotSubset(
                                    preview,
                                    Runtime::RuntimeConfigControlSource::
                                        Editor));
                            ++m_ToggleCount;
                        });
                !registered.has_value())
            {
                return registered;
            }
            return setup.RegisterFrameHook(
                Runtime::FramePhase::Maintenance,
                [this](Runtime::RuntimeFrameHookContext& context)
                {
                    const Graphics::IRenderer* renderer =
                        context.Services.Find<Graphics::IRenderer>();
                    if (renderer != nullptr)
                    {
                        ObservedStatuses.push_back(
                            renderer->GetLastRenderGraphStats()
                                .GpuProfile.Status);
                    }
                });
        }

        [[nodiscard]] Extrinsic::Core::Result OnResolve(
            Runtime::EngineSetup&) override
        {
            return Extrinsic::Core::Ok();
        }

        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override {}

        std::vector<Runtime::RuntimeEngineConfigApplyResult> ApplyResults{};
        std::vector<Graphics::RenderGraphGpuProfileStatus> ObservedStatuses{};

    private:
        std::uint32_t m_ToggleCount{0u};
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

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>();
    engine.Initialize();
    Runtime::EngineConfigControl* configControlService =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControlService, nullptr);
    Runtime::EngineConfigControl& configControl =
        *configControlService;

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
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>();
    engine.Initialize();
    Runtime::EngineConfigControl* configControlService =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControlService, nullptr);
    Runtime::EngineConfigControl& configControl =
        *configControlService;

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Window.Width += 1;
    candidate.Render.EnableGpuProfiling = true;
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
    EXPECT_FALSE(engine.GetEngineConfig().Render.EnableGpuProfiling);

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade,
     GpuProfilingHotApplyIsSynchronousForEditorAndAgentCli)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>();
    engine.Initialize();
    Runtime::EngineConfigControl* configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    EXPECT_FALSE(engine.GetEngineConfig().Render.EnableGpuProfiling);

    const auto applyProfiling =
        [&](const bool enabled,
            const Runtime::RuntimeConfigControlSource source,
            const std::string_view sourceId)
        {
            CoreConfig::EngineConfig candidate =
                configControl->GetEngineConfigControlState().ActiveConfig;
            candidate.Render.EnableGpuProfiling = enabled;
            const CoreConfig::EngineConfigLoadResult preview =
                configControl->PreviewEngineConfigControlDocument(
                    CoreConfig::SerializeEngineConfig(candidate),
                    std::string{sourceId});
            EXPECT_TRUE(CoreConfig::IsConfigUsable(preview));
            return configControl->ApplyEngineConfigHotSubset(
                preview,
                source);
        };

    const Runtime::RuntimeEngineConfigApplyResult editorApply =
        applyProfiling(
            true,
            Runtime::RuntimeConfigControlSource::Editor,
            "editor-gpu-profiling");
    ASSERT_EQ(editorApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_EQ(editorApply.Source,
              Runtime::RuntimeConfigControlSource::Editor);
    EXPECT_TRUE(editorApply.EngineConfigApplied);
    EXPECT_TRUE(editorApply.GpuProfilingChanged);
    EXPECT_TRUE(engine.GetEngineConfig().Render.EnableGpuProfiling);
    EXPECT_TRUE(configControl->GetEngineConfigControlState()
                    .ActiveConfig.Render.EnableGpuProfiling);

    const Runtime::RuntimeEngineConfigApplyResult agentApply =
        applyProfiling(
            false,
            Runtime::RuntimeConfigControlSource::AgentCli,
            "agent-gpu-profiling");
    ASSERT_EQ(agentApply.Status,
              Runtime::RuntimeEngineConfigApplyStatus::Applied);
    EXPECT_EQ(agentApply.Source,
              Runtime::RuntimeConfigControlSource::AgentCli);
    EXPECT_TRUE(agentApply.GpuProfilingChanged);
    EXPECT_FALSE(engine.GetEngineConfig().Render.EnableGpuProfiling);

    const Runtime::RuntimeEngineConfigApplyResult noChange =
        applyProfiling(
            false,
            Runtime::RuntimeConfigControlSource::AgentCli,
            "agent-gpu-profiling-no-change");
    EXPECT_EQ(noChange.Status,
              Runtime::RuntimeEngineConfigApplyStatus::NoChange);
    EXPECT_FALSE(noChange.GpuProfilingChanged);

    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade,
     BootGpuProfilingConfigReachesRendererWithoutControlOrEditor)
{
    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.EnableGpuProfiling = true;
    Intrinsic::Tests::RuntimeTestKernel engine(std::move(config),
                                               std::make_unique<OneFrameApplication>());
    engine.Initialize();
    EXPECT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        nullptr);

    engine.Run();

    EXPECT_NE(
        engine.GetRenderer().GetLastRenderGraphStats().GpuProfile.Status,
        Graphics::RenderGraphGpuProfileStatus::Disabled);
    engine.Shutdown();
}

TEST(RuntimeConfigControlFacade,
     UiEndHotToggleControlsTheSameFramesImmutableRenderSnapshot)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<TwoFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>();
    GpuProfilingUiEndToggleModule& toggle =
        engine.EmplaceModule<GpuProfilingUiEndToggleModule>();
    engine.Initialize();
    engine.Run();

    ASSERT_EQ(toggle.ApplyResults.size(), 2u);
    ASSERT_TRUE(toggle.ApplyResults[0].Succeeded());
    ASSERT_TRUE(toggle.ApplyResults[1].Succeeded());
    EXPECT_TRUE(toggle.ApplyResults[0].GpuProfilingChanged);
    EXPECT_TRUE(toggle.ApplyResults[1].GpuProfilingChanged);
    ASSERT_EQ(toggle.ObservedStatuses.size(), 2u);
    EXPECT_NE(
        toggle.ObservedStatuses[0],
        Graphics::RenderGraphGpuProfileStatus::Disabled);
    EXPECT_EQ(
        toggle.ObservedStatuses[1],
        Graphics::RenderGraphGpuProfileStatus::Disabled);
    EXPECT_FALSE(engine.GetEngineConfig().Render.EnableGpuProfiling);

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
            const Runtime::EngineConfigControl* configControl =
                engineAddress != nullptr
                    ? engineAddress->Services()
                          .Find<Runtime::EngineConfigControl>()
                    : nullptr;
            callbacksObservedCommittedState &=
                previous.PayloadJson == R"json({"value":0})json" &&
                live != nullptr && *live == current &&
                configControl != nullptr &&
                configControl->GetEngineConfigControlState()
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
            const Runtime::EngineConfigControl* configControl =
                engineAddress != nullptr
                    ? engineAddress->Services()
                          .Find<Runtime::EngineConfigControl>()
                    : nullptr;
            callbacksObservedCommittedState &=
                previous.PayloadJson == R"json({"value":0})json" &&
                live != nullptr && *live == current &&
                configControl != nullptr &&
                configControl->GetEngineConfigControlState()
                    .LastApply.SectionChanged(current.Name);
        })));

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>(
        std::move(registry));
    engineAddress = &engine;
    engine.Initialize();
    Runtime::EngineConfigControl* configControlService =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControlService, nullptr);
    Runtime::EngineConfigControl& configControl =
        *configControlService;

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

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>(
        std::move(registry));
    engine.Initialize();
    Runtime::EngineConfigControl* configControlService =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControlService, nullptr);
    Runtime::EngineConfigControl& configControl =
        *configControlService;

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
    candidate.AppSections.clear();
    const CoreConfig::EngineConfigLoadResult omittedSectionPreview =
        configControl.PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "agent-omitted-section.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(omittedSectionPreview));
    const Runtime::RuntimeEngineConfigApplyResult omittedSectionApply =
        configControl.ApplyEngineConfigHotSubset(
            omittedSectionPreview);
    EXPECT_EQ(
        omittedSectionApply.Status,
        Runtime::RuntimeEngineConfigApplyStatus::NoChange);
    EXPECT_EQ(callbackCount, 1u);
    const CoreConfig::EngineConfigSection* retainedSection =
        CoreConfig::FindEngineConfigSection(
            engine.GetEngineConfig().AppSections,
            "test");
    ASSERT_NE(retainedSection, nullptr);
    EXPECT_EQ(
        retainedSection->PayloadJson,
        R"json({"value":1})json");

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

    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>();
    engine.Initialize();
    Runtime::EngineConfigControl* configControlService =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControlService, nullptr);
    Runtime::EngineConfigControl& configControl =
        *configControlService;

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

TEST(RuntimeConfigControlFacade,
     ShutdownAndReinitializeWithdrawRebindAndResetExactControl)
{
    const std::filesystem::path recipePath =
        TempPath("intrinsic_runtime_config_control_reinitialize_recipe");
    WriteTextFile(
        recipePath,
        RenderRecipeConfigDisablingPostprocess(
            "runtime.reinitialize.recipe"));

    std::uint32_t callbackCount = 0u;
    CoreConfig::EngineConfigSectionRegistry registry{};
    ASSERT_TRUE(registry.Register(MakeTestSectionRegistration(
        "lifecycle",
        R"json({"value":0})json",
        [&](const CoreConfig::EngineConfigSection&,
            const CoreConfig::EngineConfigSection&)
        {
            ++callbackCount;
        })));

    auto configControl =
        std::make_unique<Runtime::EngineConfigControl>(
            std::move(registry));
    Runtime::EngineConfigControl* const exactControl =
        configControl.get();
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.AddModule(std::move(configControl));
    engine.Initialize();

    ASSERT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        exactControl);
    EXPECT_EQ(callbackCount, 0u);

    CoreConfig::EngineConfig candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath = recipePath.string();
    CoreConfig::UpsertEngineConfigSection(
        candidate.AppSections,
        CoreConfig::EngineConfigSection{
            .Name = "lifecycle",
            .SchemaId = "test.lifecycle",
            .SchemaVersion = 1u,
            .PayloadJson = R"json({"value":1})json",
        });
    const CoreConfig::EngineConfigLoadResult firstPreview =
        exactControl->PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "lifecycle-first-apply.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(firstPreview));
    const Runtime::RuntimeEngineConfigApplyResult firstApply =
        exactControl->ApplyEngineConfigHotSubset(
            firstPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(firstApply.Succeeded());
    EXPECT_EQ(callbackCount, 1u);
    ASSERT_TRUE(
        exactControl->GetRenderRecipeState()
            .ActiveOverride.has_value());
    const Graphics::FrameRecipeOverride staleOverride =
        *exactControl->GetRenderRecipeState().ActiveOverride;

    candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath.clear();
    const CoreConfig::EngineConfigLoadResult emptyPathPreview =
        exactControl->PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "lifecycle-empty-path.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(emptyPathPreview));
    const Runtime::RuntimeEngineConfigApplyResult emptyPathApply =
        exactControl->ApplyEngineConfigHotSubset(
            emptyPathPreview,
            Runtime::RuntimeConfigControlSource::AgentCli);
    ASSERT_TRUE(emptyPathApply.Succeeded());
    EXPECT_TRUE(emptyPathApply.DefaultRecipeConfigPathChanged);
    EXPECT_FALSE(
        exactControl->GetRenderRecipeState()
            .ActiveOverride.has_value());
    EXPECT_FALSE(
        engine.GetRenderer()
            .GetActiveFrameRecipeOverride()
            .has_value());
    EXPECT_EQ(callbackCount, 1u);

    engine.GetRenderer().SetActiveFrameRecipeOverride(
        staleOverride);
    ASSERT_TRUE(
        engine.GetRenderer()
            .GetActiveFrameRecipeOverride()
            .has_value());
    engine.Shutdown();

    EXPECT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        nullptr);
    EXPECT_FALSE(
        exactControl->GetRenderRecipeState().HasLastApply);
    EXPECT_FALSE(
        exactControl->GetEngineConfigControlState()
            .HasLastApply);
    EXPECT_FALSE(
        exactControl->GetRenderRecipeState()
            .ActiveOverride.has_value());
    const Runtime::RuntimeRenderRecipeApplyResult staleRecipeApply =
        exactControl->ActivateRenderRecipeConfigDocument(
            RenderRecipeConfigDisablingPostprocess(
                "runtime.stale-control"),
            "stale-control.json");
    EXPECT_EQ(
        staleRecipeApply.Status,
        Runtime::RuntimeRenderRecipeApplyStatus::MissingRenderer);
    const Runtime::RuntimeEngineConfigApplyResult staleConfigApply =
        exactControl->ApplyEngineConfigHotSubset(
            emptyPathPreview);
    EXPECT_EQ(
        staleConfigApply.Status,
        Runtime::RuntimeEngineConfigApplyStatus::Rejected);

    engine.Initialize();
    ASSERT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        exactControl);
    EXPECT_EQ(callbackCount, 1u);
    EXPECT_TRUE(
        engine.GetEngineConfig()
            .Render.DefaultRecipeConfigPath.empty());
    EXPECT_FALSE(
        exactControl->GetRenderRecipeState().HasLastApply);
    EXPECT_FALSE(
        exactControl->GetEngineConfigControlState()
            .HasLastApply);
    EXPECT_FALSE(
        exactControl->GetRenderRecipeState()
            .ActiveOverride.has_value());
    EXPECT_FALSE(
        engine.GetRenderer()
            .GetActiveFrameRecipeOverride()
            .has_value());

    candidate = engine.GetEngineConfig();
    candidate.Render.DefaultRecipeConfigPath = recipePath.string();
    const CoreConfig::EngineConfigLoadResult secondPreview =
        exactControl->PreviewEngineConfigControlDocument(
            CoreConfig::SerializeEngineConfig(candidate),
            "lifecycle-second-apply.json");
    ASSERT_TRUE(CoreConfig::IsConfigUsable(secondPreview));
    const Runtime::RuntimeEngineConfigApplyResult secondApply =
        exactControl->ApplyEngineConfigHotSubset(
            secondPreview,
            Runtime::RuntimeConfigControlSource::Programmatic);
    ASSERT_TRUE(secondApply.Succeeded());
    EXPECT_EQ(callbackCount, 1u);
    ASSERT_TRUE(
        engine.GetRenderer()
            .GetActiveFrameRecipeOverride()
            .has_value());

    engine.Run();
    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
    EXPECT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        nullptr);
    std::filesystem::remove(recipePath);
}

TEST(RuntimeConfigControlFacade, EditorAndAgentPreviewUseSameFacadeResult)
{
    Intrinsic::Tests::RuntimeTestKernel engine(HeadlessConfig(),
                                               std::make_unique<OneFrameApplication>());
    engine.EmplaceModule<Runtime::EngineConfigControl>();
    engine.Initialize();
    Runtime::EngineConfigControl* configControlService =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControlService, nullptr);
    Runtime::EngineConfigControl& configControl =
        *configControlService;

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
