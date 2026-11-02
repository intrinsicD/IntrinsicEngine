// ARCH-022: an engine composed as the Sandbox composes it for the bilateral filter plugin: the plugin registered
// in-process as a startup plugin before the config boot, the point-cloud editing service, the plugin host and the
// app's editor workspace (the shell's), so plugin runs are rows of its Jobs window.
#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginHost;
import Extrinsic.Runtime.PointCloudEditing;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SpatialIndexCache;
import Intrinsic.Plugin.BilateralFilter;

extern "C" bool intrinsic_bilateral_filter_register(Extrinsic::Runtime::PluginRegistrations& staged,
                                                    std::string& error) noexcept;
extern "C" void intrinsic_bilateral_filter_shutdown() noexcept;

namespace Intrinsic::Tests::BilateralFilter
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace BF = Intrinsic::Plugins::BilateralFilter;
    using Json = nlohmann::json;

    inline Config::EngineConfig HeadlessConfig()
    {
        Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Config::WindowBackend::Null;
        return config;
    }

    struct Harness
    {
        // Filled by Boot in the engine's initializer, so declared before it.
        Config::EngineConfigSectionRegistry m_Sections{};
        std::optional<R::StagedPlugin> m_Startup{};
        // Outlives the engine, whose shutdown unregisters the plugin's tools.
        R::AgentOperationRegistry Registry{};
        RuntimeTestKernel Engine;
        R::EditorWorkspaceAttachment Shell{};
        R::AgentOperationContext Context{};

        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>(),
                         const bool ui = true)
            : Engine(Boot(config), std::move(application))
        {
            Engine.EmplaceModule<R::AsyncWorkModule>();
            Engine.EmplaceModule<R::EngineConfigControl>(std::move(m_Sections));
            if (ui) Engine.EmplaceModule<R::EditorUiModule>();
            Engine.EmplaceModule<R::SceneDocumentModule>();
            Engine.EmplaceModule<R::SpatialIndexCache>();
            Engine.EmplaceModule<R::PointCloudEditingModule>();
            auto host = std::make_unique<R::PluginHostModule>(R::PluginHostOptions{.Startup = std::move(m_Startup)});
            host->RegisterOperations(Registry); // as the agent server does before the engine initializes
            Engine.AddModule(std::move(host));
            Engine.Initialize();
            Shell.Attach(Engine.Worlds(), Engine.Services());
            (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell); // the shell prepares one every frame
            Engine.Services().Find<R::PluginHostModule>()->UseEditorWorkspace(Shell);
            Engine.Services().Find<R::PointCloudEditingService>()->UseEditorWorkspace(Shell);
            Context = {.ConfigControl = Engine.Services().Find<R::EngineConfigControl>(),
                       .Jobs = &Engine.Jobs(),
                       .History = Engine.Services().Find<R::EditorCommandHistory>()};
        }
        ~Harness() { Engine.Shutdown(); }

        Extrinsic::ECS::Scene::Registry& Scene() { return *Engine.Worlds().Get(Engine.ActiveWorld()); }
        R::EditorCommandHistory& History() { return *Context.History; }

        // A frame as the Sandbox runs it: the shell prepares its workspace, commands run, completions apply.
        void Frame()
        {
            (void)R::PrepareEditorWorkspaceSnapshotFrame(Shell);
            Engine.Commands().Drain(Scene());
            (void)Engine.Jobs().DrainCompletions(Engine.Events());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Preview asked again until the asynchronous input validation answered, as the window asks each frame. The
        // queued check runs on the command bus; a new workspace frame in between would supersede it.
        std::string Settled(const BF::Config& config)
        {
            auto reason = BF::Preview(config);
            for (int attempt = 0; reason.starts_with("Checking") && attempt < 50; ++attempt)
            {
                Engine.Commands().Drain(Scene());
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                reason = BF::Preview(config);
            }
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

        // Stages the plugin before the boot: its section joins the boot registry, so the config carries its payload.
        Config::EngineConfig Boot(Config::EngineConfig config)
        {
            std::string error;
            auto staged = R::StagePlugin(R::MakeInProcessPlugin(&intrinsic_bilateral_filter_register,
                                                                &intrinsic_bilateral_filter_shutdown),
                                         error);
            EXPECT_TRUE(staged.has_value()) << error;
            if (!staged) return config;
            for (auto& section : staged->Registrations.ConfigSections) EXPECT_TRUE(m_Sections.Register(std::move(section)));
            staged->Registrations.ConfigSections.clear();
            m_Startup = std::move(*staged);
            Config::PopulateEngineConfigSectionDefaults(config, m_Sections);
            return config;
        }
    };
}
