#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <gtest/gtest.h>

import Extrinsic.Core.Config.Engine;
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

namespace Core = Extrinsic::Core;
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

    Runtime::EngineConfigControl& ComposeConfigControl(
        Runtime::Engine& engine)
    {
        return engine.EmplaceModule<Runtime::EngineConfigControl>();
    }

    class DeleteStartupRecipeModule final : public Runtime::IRuntimeModule
    {
    public:
        explicit DeleteStartupRecipeModule(std::filesystem::path path)
            : m_Path(std::move(path))
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "A.DeleteStartupRecipe";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup&) override
        {
            std::error_code error{};
            RemovedDuringRegistration =
                std::filesystem::remove(m_Path, error) && !error;
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup&) override
        {
            return Core::Ok();
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext&) override
        {
        }

        bool RemovedDuringRegistration{false};

    private:
        std::filesystem::path m_Path{};
    };

    struct EarlyResolveConsumerState
    {
        bool ServiceAbsentDuringRegistration{false};
        bool UsedServiceDuringResolve{false};
        bool ServiceAbsentDuringShutdown{false};
        Runtime::RuntimeRenderRecipeApplyResult Apply{};
    };

    class EarlyResolveConfigConsumerModule final
        : public Runtime::IRuntimeModule
    {
    public:
        explicit EarlyResolveConfigConsumerModule(
            EarlyResolveConsumerState& state)
            : m_State(state)
        {
        }

        [[nodiscard]] std::string_view Name() const noexcept override
        {
            return "A.ConfigControlResolveConsumer";
        }

        [[nodiscard]] Core::Result OnRegister(
            Runtime::EngineSetup& setup) override
        {
            m_State.ServiceAbsentDuringRegistration =
                setup.Services().Find<Runtime::EngineConfigControl>() ==
                nullptr;
            return Core::Ok();
        }

        [[nodiscard]] Core::Result OnResolve(
            Runtime::EngineSetup& setup) override
        {
            Runtime::EngineConfigControl* const control =
                setup.Services().Find<Runtime::EngineConfigControl>();
            if (control == nullptr)
            {
                return Core::Err(Core::ErrorCode::ResourceNotFound);
            }
            m_State.Apply =
                control->ActivateRenderRecipeConfigDocument(
                    RenderRecipeConfigDisablingPostprocess(
                        "runtime.resolve-consumer"),
                    "resolve-consumer.json",
                    Runtime::RuntimeRenderRecipeActivationSource::Programmatic);
            m_State.UsedServiceDuringResolve =
                m_State.Apply.Succeeded();
            return m_State.UsedServiceDuringResolve
                ? Core::Ok()
                : Core::Err(Core::ErrorCode::InvalidState);
        }

        void OnShutdown(
            Runtime::RuntimeModuleShutdownContext& context) override
        {
            m_State.ServiceAbsentDuringShutdown =
                context.Services.Find<Runtime::EngineConfigControl>() ==
                nullptr;
        }

    private:
        EarlyResolveConsumerState& m_State;
    };
}

TEST(RuntimeRenderRecipeActivation, StartupRecipeConfigDisablesPostprocessOnFirstFrame)
{
    const std::filesystem::path path =
        TempRecipePath("intrinsic_runtime_recipe_startup_valid");
    WriteTextFile(path, RenderRecipeConfigDisablingPostprocess("runtime.startup.disable-post"));

    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.DefaultRecipeConfigPath = path.string();
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    ComposeConfigControl(engine);
    engine.Initialize();
    std::filesystem::remove(path);

    Runtime::EngineConfigControl* configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    const Runtime::RuntimeRenderRecipeState& recipeState =
        configControl->GetRenderRecipeState();
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
    ComposeConfigControl(engine);
    engine.Initialize();

    Runtime::EngineConfigControl* configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    const Runtime::RuntimeRenderRecipeState& recipeState =
        configControl->GetRenderRecipeState();
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
    ComposeConfigControl(engine);
    engine.Initialize();
    std::filesystem::remove(path);

    Runtime::EngineConfigControl* configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    const Runtime::RuntimeRenderRecipeState& recipeState =
        configControl->GetRenderRecipeState();
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

TEST(RuntimeRenderRecipeActivation,
     EmptyStartupRecipePathWithComposedControlUsesDefault)
{
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>());
    ComposeConfigControl(engine);
    engine.Initialize();

    Runtime::EngineConfigControl* const configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    const Runtime::RuntimeRenderRecipeState& recipeState =
        configControl->GetRenderRecipeState();
    EXPECT_FALSE(recipeState.HasLastApply);
    EXPECT_FALSE(recipeState.HasActiveConfig);
    EXPECT_FALSE(recipeState.ActiveOverride.has_value());

    engine.Run();
    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_FALSE(stats.FrameRecipeOverrideActive);
    EXPECT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation,
     ValidStartupRecipeAppliesWhenControlModuleIsOmitted)
{
    const std::filesystem::path path =
        TempRecipePath("intrinsic_runtime_recipe_startup_valid_omitted");
    WriteTextFile(
        path,
        RenderRecipeConfigDisablingPostprocess(
            "runtime.startup.omitted-control"));

    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.DefaultRecipeConfigPath = path.string();
    Runtime::Engine engine(
        std::move(config),
        std::make_unique<OneFrameApplication>());
    engine.Initialize();
    std::filesystem::remove(path);

    EXPECT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        nullptr);
    engine.Run();

    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_TRUE(stats.FrameRecipeOverrideApplied);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);
    EXPECT_NE(FindCommandPass(stats, "Present"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation,
     MissingAndInvalidStartupRecipesUseDefaultWhenControlModuleIsOmitted)
{
    const auto expectDefaultFrame =
        [](const std::filesystem::path& path)
        {
            CoreConfig::EngineConfig config = HeadlessConfig();
            config.Render.DefaultRecipeConfigPath = path.string();
            Runtime::Engine engine(
                std::move(config),
                std::make_unique<OneFrameApplication>());
            engine.Initialize();
            EXPECT_EQ(
                engine.Services().Find<Runtime::EngineConfigControl>(),
                nullptr);
            engine.Run();
            const Graphics::RenderGraphFrameStats& stats =
                engine.GetRenderer().GetLastRenderGraphStats();
            EXPECT_FALSE(stats.FrameRecipeOverrideActive);
            EXPECT_NE(
                FindCommandPass(stats, "PostProcessPass"),
                nullptr);
            engine.Shutdown();
        };

    const std::filesystem::path missingPath =
        TempRecipePath("intrinsic_runtime_recipe_startup_missing_omitted");
    std::filesystem::remove(missingPath);
    expectDefaultFrame(missingPath);

    const std::filesystem::path invalidPath =
        TempRecipePath("intrinsic_runtime_recipe_startup_invalid_omitted");
    WriteTextFile(invalidPath, InvalidRenderRecipeConfig());
    expectDefaultFrame(invalidPath);
    std::filesystem::remove(invalidPath);
}

TEST(RuntimeRenderRecipeActivation,
     EmptyStartupRecipePathUsesDefaultWhenControlModuleIsOmitted)
{
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>());
    engine.Initialize();

    EXPECT_EQ(
        engine.Services().Find<Runtime::EngineConfigControl>(),
        nullptr);
    engine.Run();
    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_FALSE(stats.FrameRecipeOverrideActive);
    EXPECT_NE(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation,
     ComposedControlCopiesAlreadyAppliedStartupStateWithoutReloading)
{
    const std::filesystem::path path =
        TempRecipePath("intrinsic_runtime_recipe_startup_single_load");
    WriteTextFile(
        path,
        RenderRecipeConfigDisablingPostprocess(
            "runtime.startup.single-load"));

    CoreConfig::EngineConfig config = HeadlessConfig();
    config.Render.DefaultRecipeConfigPath = path.string();
    Runtime::Engine engine(
        std::move(config),
        std::make_unique<OneFrameApplication>());
    auto deletingModule =
        std::make_unique<DeleteStartupRecipeModule>(path);
    DeleteStartupRecipeModule* const deletingModuleAddress =
        deletingModule.get();
    engine.AddModule(std::move(deletingModule));
    ComposeConfigControl(engine);
    engine.Initialize();

    ASSERT_TRUE(deletingModuleAddress->RemovedDuringRegistration);
    ASSERT_FALSE(std::filesystem::exists(path));
    Runtime::EngineConfigControl* const configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);
    const Runtime::RuntimeRenderRecipeState& recipeState =
        configControl->GetRenderRecipeState();
    ASSERT_TRUE(recipeState.HasLastApply);
    EXPECT_EQ(
        recipeState.LastApply.Status,
        Runtime::RuntimeRenderRecipeApplyStatus::Applied);
    EXPECT_EQ(
        recipeState.ActiveConfig.Preview.Recipe.RecipeId,
        "runtime.startup.single-load");

    engine.Shutdown();
}

TEST(RuntimeRenderRecipeActivation,
     ConfigControlServiceIsFullyUsableByEarlierResolveConsumer)
{
    EarlyResolveConsumerState consumerState{};
    Runtime::Engine engine(
        HeadlessConfig(),
        std::make_unique<OneFrameApplication>());
    engine.AddModule(
        std::make_unique<EarlyResolveConfigConsumerModule>(
            consumerState));
    Runtime::EngineConfigControl& expectedControl =
        ComposeConfigControl(engine);
    engine.Initialize();

    EXPECT_TRUE(consumerState.ServiceAbsentDuringRegistration);
    EXPECT_TRUE(consumerState.UsedServiceDuringResolve);
    ASSERT_TRUE(consumerState.Apply.Succeeded());
    Runtime::EngineConfigControl* const resolvedControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_EQ(resolvedControl, &expectedControl);
    EXPECT_EQ(
        resolvedControl->GetRenderRecipeState()
            .ActiveConfig.Preview.Recipe.RecipeId,
        "runtime.resolve-consumer");

    engine.Run();
    const Graphics::RenderGraphFrameStats& stats =
        engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.FrameRecipeOverrideActive);
    EXPECT_EQ(FindCommandPass(stats, "PostProcessPass"), nullptr);

    engine.Shutdown();
    EXPECT_TRUE(consumerState.ServiceAbsentDuringShutdown);
}

TEST(RuntimeRenderRecipeActivation, EditorActivationCommandRoutesThroughRuntimeApplyPath)
{
    CoreConfig::EngineConfig config = HeadlessConfig();
    Runtime::Engine engine(config, std::make_unique<OneFrameApplication>());
    ComposeConfigControl(engine);
    engine.Initialize();
    Runtime::EngineConfigControl* configControl =
        engine.Services().Find<Runtime::EngineConfigControl>();
    ASSERT_NE(configControl, nullptr);

    Graphics::RenderRecipeConfigContext recipeContext =
        configControl->CreateRenderRecipeConfigContext();
    Runtime::SandboxEditorRenderRecipeEditorState editorState{};
    Runtime::SandboxEditorContext context{};
    context.RenderRecipeContext = &recipeContext;
    context.RenderRecipeEditorState = &editorState;
    context.RenderRecipeRuntimeState = &configControl->GetRenderRecipeState();
    context.PreviewRenderRecipeDocument =
        [configControl](const std::string& document,
                        const std::string& sourceId)
        {
            return configControl->PreviewRenderRecipeConfigDocument(
                document,
                sourceId);
        };
    context.ApplyRenderRecipePreview =
        [configControl](const Graphics::RenderRecipeConfigLoadResult& loadResult)
        {
            return configControl->ApplyRenderRecipeConfigPreview(
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

    const Runtime::RuntimeRenderRecipeState& recipeState =
        configControl->GetRenderRecipeState();
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
