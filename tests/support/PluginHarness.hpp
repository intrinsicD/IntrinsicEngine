// ARCH-022: the point-cloud editing engine (PointCloudEditingHarness.hpp) with one feature plugin as the Sandbox
// loads it: registered in-process as a startup plugin before the config boot, and the plugin host using the shell's
// workspace, so plugin runs are rows of its Jobs window. A plugin's own harness adds its helpers.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "PointCloudEditingHarness.hpp"

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginHost;
import Extrinsic.Runtime.PluginSections;
import Extrinsic.Runtime.StepRuns;

namespace Intrinsic::Tests::Plugins
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    using Json = nlohmann::json;
    using PointCloudEditing::HeadlessConfig;

    // What exists before the engine and outlives it: the boot sections and config with the plugin's section, the
    // staged plugin, and the tool registry the engine's shutdown unregisters the plugin's tools from.
    struct Boot
    {
        R::AgentOperationRegistry Registry{};
        Config::EngineConfigSectionRegistry Sections{};
        std::vector<R::StagedPlugin> Startup{};
        Config::EngineConfig Booted{};

        Boot(const R::PluginRegisterFn registerFn, const R::PluginShutdownFn shutdownFn, Config::EngineConfig config)
        {
            std::vector<std::string> errors;
            std::vector<std::unique_ptr<R::PluginImage>> images;
            images.push_back(R::MakeInProcessPlugin(registerFn, shutdownFn));
            Startup = R::StageStartupPlugins(std::move(images), Sections, errors);
            EXPECT_EQ(Startup.size(), 1u) << (errors.empty() ? "" : errors.front());
            Config::PopulateEngineConfigSectionDefaults(config, Sections);
            Booted = std::move(config);
        }
    };

    struct Harness : Boot, PointCloudEditing::Harness
    {
        R::AgentOperationContext Context{};

        // `moreModules` composes what a scenario needs besides (a camera for pixel checks).
        Harness(const R::PluginRegisterFn registerFn, const R::PluginShutdownFn shutdownFn,
                Config::EngineConfig config = HeadlessConfig(),
                std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>(),
                const std::function<void(RuntimeTestKernel&)>& moreModules = {})
            : Boot(registerFn, shutdownFn, std::move(config)),
              PointCloudEditing::Harness(std::move(Booted), std::move(application), std::move(Sections),
                                         [this, registerFn, shutdownFn, &moreModules](RuntimeTestKernel& engine)
                                         {
                                             // A load of any path opens the plugin in-process again, as a shared
                                             // build would load its library: unload and reload work in every build.
                                             auto host = std::make_unique<R::PluginHostModule>(R::PluginHostOptions{
                                                 .Open = [registerFn, shutdownFn](const std::string&, std::string_view,
                                                                                  R::AgentOperationOutcome&)
                                                 { return R::MakeInProcessPlugin(registerFn, shutdownFn); },
                                                 .Startup = std::move(Startup)});
                                             host->RegisterOperations(Registry); // as the agent server does
                                             engine.AddModule(std::move(host));
                                             if (moreModules) moreModules(engine);
                                         })
        {
            Engine.Services().Find<R::PluginHostModule>()->UseEditorWorkspace(Shell);
            // The editing harness composes the step runs; the agent server registers their tools as here.
            Engine.Services().Find<R::StepRunsService>()->RegisterOperations(Registry);
            Context = {.ConfigControl = Engine.Services().Find<R::EngineConfigControl>(),
                       .Jobs = &Engine.Jobs(),
                       .History = &History()};
        }

        // A preview once the input validation answered, as the window shows it.
        template <class Preview>
        std::string Settled(Preview preview)
        {
            std::string reason;
            (void)PointCloudEditing::Harness::Settled([&]
            {
                reason = preview();
                return !reason.starts_with("Checking");
            });
            return reason;
        }

        // Applies the settings through config control, as the window's Apply and config_apply do.
        template <const auto& Section>
        [[nodiscard]] testing::AssertionResult Apply(const R::DerivedSectionConfig<Section>& value)
        {
            std::string error;
            if (R::ApplyPluginSection<Section>(value, error)) return testing::AssertionSuccess();
            return testing::AssertionFailure() << error;
        }

        // Frames until the run's one terminal result.
        template <class Run>
        const auto& Wait(const Run& run, const int frames = 5000)
        {
            for (int frame = 0; !run.Final && frame < frames; ++frame) Frame();
            EXPECT_TRUE(run.Final.has_value()) << "the run did not end";
            using Result = std::remove_cvref_t<decltype(*run.Final)>;
            static const Result none{.Status = "timeout"};
            return run.Final ? *run.Final : none;
        }

        // Calls a tool as the agent server does, polling its continuation once per frame.
        R::AgentOperationOutcome Call(const std::string_view name, const Json& arguments = Json::object())
        {
            auto outcome = R::InvokeAgentOperation(Registry, name, Context, arguments.dump(), false);
            for (int frame = 0; outcome.Continuation && frame < 5000; ++frame)
            {
                R::AgentOperationOutcome result{};
                if (outcome.Continuation(Context, result)) return result;
                Frame();
            }
            return outcome;
        }

        // The shell session's job records, as its Jobs window lists them.
        std::vector<R::EditorJobRecord> Jobs()
        {
            (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell);
            return R::GetEditorJobs(R::PrepareEditorProcessingCommands(Shell));
        }
    };
}
