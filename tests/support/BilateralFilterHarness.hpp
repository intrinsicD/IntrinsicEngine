// ARCH-022: the point-cloud editing engine (PointCloudEditingHarness.hpp) with the bilateral filter plugin as the
// Sandbox loads it: registered in-process as a startup plugin before the config boot, and the plugin host using the
// shell's workspace, so plugin runs are rows of its Jobs window.
#pragma once

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
import Intrinsic.Plugin.BilateralFilter;

namespace Intrinsic::Tests::BilateralFilter
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace BF = Intrinsic::Plugins::BilateralFilter;
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

        explicit Boot(Config::EngineConfig config)
        {
            std::vector<std::string> errors;
            std::vector<std::unique_ptr<R::PluginImage>> images;
            images.push_back(R::MakeInProcessPlugin(&BF::Register, &BF::Shutdown));
            Startup = R::StageStartupPlugins(std::move(images), Sections, errors);
            EXPECT_EQ(Startup.size(), 1u) << (errors.empty() ? "" : errors.front());
            Config::PopulateEngineConfigSectionDefaults(config, Sections);
            Booted = std::move(config);
        }
    };

    struct Harness : Boot, PointCloudEditing::Harness
    {
        R::AgentOperationContext Context{};

        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>())
            : Boot(std::move(config)),
              PointCloudEditing::Harness(std::move(Booted), std::move(application), std::move(Sections),
                                         [this](RuntimeTestKernel& engine)
                                         {
                                             auto host = std::make_unique<R::PluginHostModule>(
                                                 R::PluginHostOptions{.Startup = std::move(Startup)});
                                             host->RegisterOperations(Registry); // as the agent server does
                                             engine.AddModule(std::move(host));
                                         })
        {
            Engine.Services().Find<R::PluginHostModule>()->UseEditorWorkspace(Shell);
            Context = {.ConfigControl = Engine.Services().Find<R::EngineConfigControl>(),
                       .Jobs = &Engine.Jobs(),
                       .History = &History()};
        }

        // Preview once the input validation answered, as the window shows it.
        std::string Settled(const BF::Config& config)
        {
            std::string reason;
            (void)PointCloudEditing::Harness::Settled([&]
            {
                reason = BF::Preview(config);
                return !reason.starts_with("Checking");
            });
            return reason;
        }

        // Applies the settings through config control, as the window's Apply and config_apply do.
        [[nodiscard]] testing::AssertionResult Apply(const BF::Config& config)
        {
            std::string error;
            if (BF::ApplyConfig(config, error)) return testing::AssertionSuccess();
            return testing::AssertionFailure() << error;
        }

        // Frames until the run's one terminal result.
        const BF::Result& Wait(const BF::Run& run, const int frames = 5000)
        {
            for (int frame = 0; !run.Final && frame < frames; ++frame) Frame();
            EXPECT_TRUE(run.Final.has_value()) << "the run did not end";
            static const BF::Result none{.Status = "timeout"};
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
