// ARCH-022: the plugin host's contract, driven with in-process images (no dlopen), so it runs in static builds too.
// dlopen itself is covered by the fixture plugin's drivers on the shared dev build (plugins/fixture).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.PluginHost;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.UiHostTypes;

namespace
{
    namespace Config = Extrinsic::Core::Config;
    namespace Runtime = Extrinsic::Runtime;
    using Json = nlohmann::json;
    using Log = std::vector<std::string>;

    // An in-process image: Register is the fixture's; the shutdown hook and the close are logged.
    class FixtureImage final : public Runtime::PluginImage
    {
    public:
        FixtureImage(std::function<bool(Runtime::PluginRegistrations&)> registerFn, Log& log,
                     std::function<void()> onShutdown = {})
            : m_Register(std::move(registerFn)), m_Log(log), m_OnShutdown(std::move(onShutdown)) {}
        bool Register(Runtime::PluginRegistrations& staged, std::string& error) noexcept override
        {
            if (m_Register(staged)) return true;
            error = "the fixture refused";
            return false;
        }
        void Shutdown() noexcept override
        {
            m_Log.push_back("shutdown");
            if (m_OnShutdown) m_OnShutdown();
        }
        void Close() noexcept override { m_Log.push_back("close"); }
        const std::filesystem::path& File() const noexcept override { return m_File; }

    private:
        std::function<bool(Runtime::PluginRegistrations&)> m_Register;
        Log& m_Log;
        std::function<void()> m_OnShutdown;
        std::filesystem::path m_File{};
    };

    // Reports every copy and destruction of a callable to the host's accounting, as a plugin that counts does.
    class Tracked
    {
    public:
        explicit Tracked(const int generation) noexcept : m_Generation(generation)
        {
            intrinsic_plugin_callable(m_Generation, "operation", +1);
        }
        Tracked(const Tracked& other) noexcept : m_Generation(other.m_Generation)
        {
            intrinsic_plugin_callable(m_Generation, "operation", +1);
        }
        Tracked& operator=(const Tracked&) = delete;
        ~Tracked() { intrinsic_plugin_callable(m_Generation, "operation", -1); }

    private:
        int m_Generation;
    };

    Config::EngineConfigSectionValidationResult Validate(const std::string_view document)
    {
        const Json value = Json::parse(document, nullptr, false);
        if (!value.is_object() || !value.contains("value") || !value["value"].is_number_integer())
            return {.State = Config::EngineConfigState::Invalid};
        return {.State = Config::EngineConfigState::Valid, .CanonicalPayloadJson = value.dump(), .ParsedFieldCount = 1u};
    }

    // A fixture plugin named `name` (default "fixture"): config section <name> ({"value": N}), tool <name>_echo,
    // window view.<name> and history command <name>.add. Its callables log what they receive.
    std::function<bool(Runtime::PluginRegistrations&)> Fixture(const int generation, Log& log,
                                                               const std::string name = "fixture")
    {
        return [generation, &log, name](Runtime::PluginRegistrations& staged)
        {
            staged.Name = name;
            staged.Generation = generation;
            staged.ConfigSections.push_back({
                .DefaultSection = {.Name = name, .SchemaId = "test." + name, .SchemaVersion = 1u,
                                   .PayloadJson = R"({"value":0})"},
                .Validate = [](const std::string_view document, std::string_view, std::string_view)
                { return Validate(document); },
                .OnChanged = [&log](const Config::EngineConfigSection&, const Config::EngineConfigSection& current)
                { log.push_back("changed " + current.PayloadJson); },
            });
            staged.Operations.push_back({
                .Name = name + "_echo",
                .Invoke = [t = Tracked{generation}](const Runtime::AgentOperationContext&, std::string_view)
                { return Runtime::AgentOperationOutcome{.Text = R"({"echo":true})"}; },
            });
            staged.Windows.push_back({.Id = "view." + name, .MenuPath = {"View"}, .Title = name, .Draw = [](bool&) {}});
            staged.HistoryCommands.push_back({.Name = name + ".add", .Apply = [](std::string_view, bool) { return true; }});
            return true;
        };
    }

    Config::EngineConfig HeadlessConfig()
    {
        Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled = false;
        config.Camera.Enabled = false;
        config.Window.Backend = Config::WindowBackend::Null;
        return config;
    }

    // Composed before the host, so it shuts down right after it: records what the host left registered.
    class ShutdownProbe final : public Runtime::IRuntimeModule
    {
    public:
        explicit ShutdownProbe(std::function<void(Runtime::RuntimeModuleShutdownContext&)> probe) : m_Probe(std::move(probe)) {}
        std::string_view Name() const noexcept override { return "Test.PluginHostShutdownProbe"; }
        Extrinsic::Core::Result OnRegister(Runtime::EngineSetup&) override { return Extrinsic::Core::Ok(); }
        void OnShutdown(Runtime::RuntimeModuleShutdownContext& context) override { m_Probe(context); }

    private:
        std::function<void(Runtime::RuntimeModuleShutdownContext&)> m_Probe;
    };

    // A headless engine with the services a plugin registers with, and the plugin host composed last.
    struct Harness
    {
        Log Events{};
        std::function<bool(Runtime::PluginRegistrations&)> Next{}; // what the next opened image registers
        std::map<std::string, std::function<bool(Runtime::PluginRegistrations&)>> Builds{}; // by path, before Next
        std::map<std::string, std::function<void()>> OnShutdownOf{}; // by path: run in the image's shutdown hook
        // Outlives the engine, whose shutdown unregisters the plugin's tools (the agent server's registry does too).
        Runtime::AgentOperationRegistry Registry{};
        Intrinsic::Tests::RuntimeTestKernel Engine;
        Runtime::PluginHostModule* Host{nullptr};
        Runtime::AgentOperationContext Context{};

        // The app's editor workspace (the Sandbox shell's): plugin jobs are rows of its session.
        Runtime::EditorWorkspaceAttachment Shell{};

        struct Options
        {
            bool TestHooks{true};
            bool InProcess{true};
            std::function<void(Runtime::RuntimeModuleShutdownContext&)> Probe{};
            std::vector<Runtime::StagedPlugin> Startup{};
            Config::EngineConfigSectionRegistry BootSections{};
            bool Agent{true};
            Config::EngineConfig Config{HeadlessConfig()};
            bool Ui{true};
            bool UseShell{true}; // false: plugin jobs go through the host's own workspace
        };

        Harness() : Harness(Options{}) {}
        explicit Harness(Options options)
            : Engine(std::move(options.Config), std::make_unique<Intrinsic::Tests::OneFrameApplication>())
        {
            const bool testHooks = options.TestHooks, inProcess = options.InProcess, agent = options.Agent;
            auto probe = std::move(options.Probe);
            auto startup = std::move(options.Startup);
            Engine.EmplaceModule<Runtime::AsyncWorkModule>(); // provides the JobService the editor session submits to
            Engine.EmplaceModule<Runtime::EngineConfigControl>(std::move(options.BootSections));
            if (options.Ui) Engine.EmplaceModule<Runtime::EditorUiModule>();
            Engine.EmplaceModule<Runtime::SceneDocumentModule>();
            if (probe) Engine.AddModule(std::make_unique<ShutdownProbe>(std::move(probe)));
            Runtime::PluginHostOptions hostOptions{.TestHooks = testHooks, .Startup = std::move(startup)};
            if (inProcess)
                hostOptions.Open = [this](const std::string& path, std::string_view, Runtime::AgentOperationOutcome&)
                {
                    const auto build = Builds.find(path);
                    const auto hook = OnShutdownOf.find(path);
                    return std::make_unique<FixtureImage>(build != Builds.end() ? build->second : Next, Events,
                                                          hook != OnShutdownOf.end() ? hook->second : nullptr);
                };
            auto host = std::make_unique<Runtime::PluginHostModule>(std::move(hostOptions));
            Host = host.get();
            Engine.AddModule(std::move(host));
            // The agent server registers the host's tools before the engine initializes.
            if (agent) Host->RegisterOperations(Registry);
            Engine.Initialize();
            if (options.UseShell)
            {
                Shell.Attach(Engine.Worlds(), Engine.Services());
                (void)Runtime::PrepareEditorWorkspaceSnapshotFrame(Shell); // the shell prepares one every frame
                Host->UseEditorWorkspace(Shell);
            }
            Context = {.ConfigControl = Engine.Services().Find<Runtime::EngineConfigControl>(),
                       .Jobs = &Engine.Jobs(),
                       .History = Engine.Services().Find<Runtime::EditorCommandHistory>()};
        }

        // Calls a tool as the agent server does: its continuation is polled, one "frame" at a time with job
        // completions drained in between, until it answers; then it is destroyed.
        Runtime::AgentOperationOutcome Call(const std::string_view name, const Json& arguments = Json::object())
        {
            auto outcome = Runtime::InvokeAgentOperation(Registry, name, Context, arguments.dump(), false);
            for (int frame = 0; outcome.Continuation && frame < 10000; ++frame)
            {
                Runtime::AgentOperationOutcome result{};
                if (outcome.Continuation(Context, result)) return result;
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return outcome;
        }

        [[nodiscard]] testing::AssertionResult Loaded(const int generation)
        {
            Next = Fixture(generation, Events);
            const auto reply = Call("plugin_load", {{"path", "fixture"}});
            if (!reply.IsError && Json::parse(reply.Text).value("generation", -1) == generation)
                return testing::AssertionSuccess();
            return testing::AssertionFailure() << reply.ErrorCode << ": " << reply.Text;
        }

        [[nodiscard]] bool SectionRegistered() const
        {
            return Context.ConfigControl->SectionRegistry().Find("fixture") != nullptr;
        }
        [[nodiscard]] bool WindowRegistered()
        {
            auto* ui = Engine.Services().Find<Runtime::EditorUiHost>();
            if (ui == nullptr) return false;
            for (const auto& entry : ui->BuildWindowMenuModel())
                if (entry.Id == "view.fixture") return true;
            return false;
        }
        // Registering the name again fails while the plugin's command holds it.
        [[nodiscard]] bool HistoryCommandRegistered() const
        {
            using Status = Runtime::EditorCommandHistoryStatus;
            if (!Context.History->RegisterCommand("fixture.add", [](std::string_view, bool) { return Status::Applied; }))
                return true;
            (void)Context.History->UnregisterCommand("fixture.add");
            return false;
        }
        // The engine shuts down while the shell is attached, as in the Sandbox (completions may report to it).
        ~Harness() { Engine.Shutdown(); }

        // The shell session's job records, as its Jobs window lists them.
        std::vector<Runtime::EditorJobRecord> Jobs()
        {
            (void)Runtime::PrepareEditorWorkspaceSnapshotFrame(Shell);
            return Runtime::GetEditorJobs(Runtime::PrepareEditorProcessingCommands(Shell));
        }
        void Drain(const int frames = 50)
        {
            for (int frame = 0; frame < frames; ++frame)
            {
                (void)Engine.Jobs().DrainCompletions(Engine.Events());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }

        [[nodiscard]] bool NothingRegistered()
        {
            return Registry.Find("fixture_echo") == nullptr && !SectionRegistered() && !WindowRegistered() &&
                !HistoryCommandRegistered();
        }
    };

    // A plugin job that runs until it is cancelled and logs its completion; returns once the job is running.
    testing::AssertionResult StartCancellableJob(const Runtime::AgentOperationContext& context, Log& log)
    {
        auto started = std::make_shared<std::atomic<bool>>(false);
        const bool submitted = Runtime::SubmitPluginJob(context, {
            .Label = "Fixture job",
            .Work = [started](const std::function<bool()>& cancelled)
            {
                *started = true;
                while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            },
            .Completion = [&log](const bool published)
            {
                log.push_back(published ? "job_published" : "job_finalized");
                return published;
            },
        }).IsValid();
        if (!submitted) return testing::AssertionFailure() << "the job was not submitted";
        while (!*started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return testing::AssertionSuccess();
    }
}

TEST(PluginHost, LoadCommitsEveryRegistrationAndUnloadRemovesThemBeforeTheClose)
{
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(101));
    EXPECT_NE(harness.Registry.Find("fixture_echo"), nullptr);
    EXPECT_TRUE(harness.SectionRegistered());
    EXPECT_TRUE(harness.WindowRegistered());
    EXPECT_TRUE(harness.HistoryCommandRegistered());
    const auto echo = harness.Call("fixture_echo");
    EXPECT_FALSE(echo.IsError) << echo.Text;
    const Json listed = Json::parse(harness.Call("plugin_list").Text);
    ASSERT_EQ(listed["loaded"].size(), 1u) << listed.dump();
    EXPECT_EQ(listed["loaded"][0]["name"], "fixture");
    EXPECT_EQ(listed["loaded"][0]["generation"], 101);
    EXPECT_EQ(listed["loaded"][0]["config_sections"], Json::array({"fixture"}));

    const auto unloaded = harness.Call("plugin_unload");
    ASSERT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_TRUE(harness.NothingRegistered());
    EXPECT_EQ(harness.Events, (Log{"shutdown", "close"}));
    EXPECT_EQ(Json::parse(harness.Call("plugin_list").Text)["loaded"], Json::array());
}

TEST(PluginHost, AFailedCommitRollsEveryRegistrationBackAndClosesTheImage)
{
    Harness harness{};
    harness.Next = Fixture(102, harness.Events);
    const auto reply = harness.Call("plugin_load", {{"path", "fixture"}, {"fail_at", "windows"}});
    EXPECT_EQ(reply.ErrorCode, "commit_failed");
    EXPECT_TRUE(harness.NothingRegistered());
    EXPECT_EQ(harness.Events, (Log{"shutdown", "close"})); // its Register succeeded: one shutdown hook
    ASSERT_TRUE(harness.Loaded(102)); // nothing left behind blocks a later load
}

TEST(PluginHost, AReloadReappliesTheSavedConfigPayloadToTheNewRegistration)
{
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(103));
    auto& config = *harness.Context.ConfigControl;
    auto candidate = config.GetEngineConfigControlState().ActiveConfig;
    Config::UpsertEngineConfigSection(candidate.AppSections, {.Name = "fixture", .SchemaId = "test.fixture",
                                                              .SchemaVersion = 1u, .PayloadJson = R"({"value":12})"});
    const auto preview = config.PreviewEngineConfigControlDocument(Config::SerializeEngineConfig(candidate), "test");
    ASSERT_TRUE(config.ApplyEngineConfigHotSubset(preview).Succeeded());

    harness.Next = Fixture(104, harness.Events);
    const auto reply = harness.Call("plugin_reload", {{"path", "fixture"}});
    ASSERT_FALSE(reply.IsError) << reply.Text;
    EXPECT_EQ(Json::parse(reply.Text)["config"]["restored"], Json::array({"fixture"}));
    EXPECT_EQ(harness.Events, (Log{R"(changed {"value":12})", "shutdown", "close", R"(changed {"value":12})"}));
}

TEST(PluginHost, TheCloseGuardKeepsAnImageMappedWhileOneOfItsCallablesIsAlive)
{
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(105));
    const auto reply = harness.Call("plugin_unload", {{"retain_callable", true}});
    EXPECT_EQ(reply.ErrorCode, "callables_alive");
    EXPECT_TRUE(harness.NothingRegistered());
    EXPECT_EQ(harness.Events, (Log{"shutdown"})); // unregistered, not closed
}

TEST(PluginHost, AnUnloadDrainsThePluginsJobsBeforeItsShutdownHook)
{
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(106));
    ASSERT_TRUE(StartCancellableJob(harness.Context, harness.Events));

    const auto reply = harness.Call("plugin_unload");
    ASSERT_FALSE(reply.IsError) << reply.Text;
    EXPECT_EQ(harness.Events, (Log{"job_finalized", "shutdown", "close"}));
}

namespace
{
    // Logs its destruction once: shared by the three callables of one deferred result.
    struct DeferredProbe
    {
        Log* Events{nullptr};
        ~DeferredProbe() { Events->push_back("deferred_destroyed"); }
    };

    // fixture_deferred: a deferred result whose continuation answers once `finish` is set; its progress and cancel
    // are logged. `tag` names the generation in the log.
    std::function<bool(Runtime::PluginRegistrations&)> DeferredFixture(const int generation, std::string tag, Log& log,
                                                                      const bool& finish,
                                                                      std::function<void()>* inside = nullptr)
    {
        return [generation, tag, &log, &finish, inside](Runtime::PluginRegistrations& staged)
        {
            staged.Name = "fixture";
            staged.Generation = generation;
            staged.Operations.push_back({
                .Name = "fixture_deferred",
                .Invoke = [tag, &log, &finish, inside](const Runtime::AgentOperationContext&, std::string_view)
                {
                    auto probe = std::make_shared<DeferredProbe>();
                    probe->Events = &log;
                    Runtime::AgentOperationOutcome outcome{};
                    outcome.Continuation = [probe, tag, &log, &finish, inside](const Runtime::AgentOperationContext&,
                                                                              Runtime::AgentOperationOutcome& result)
                    {
                        log.push_back("continue " + tag);
                        if (inside != nullptr && *inside) (*inside)();
                        if (!finish) return false;
                        result = {.Text = R"({"done":true})"};
                        return true;
                    };
                    outcome.Progress = [probe](const Runtime::AgentOperationContext&)
                    { return decltype(Runtime::AgentOperationOutcome::Progress)::result_type{}; };
                    outcome.Cancel = [probe, &log](const Runtime::AgentOperationContext&)
                    {
                        log.push_back("cancel");
                        return std::string{"cancelled"};
                    };
                    return outcome;
                },
            });
            return true;
        };
    }

    Runtime::AgentOperationOutcome CallDeferred(Harness& harness)
    {
        return Runtime::InvokeAgentOperation(harness.Registry, "fixture_deferred", harness.Context, "{}", false);
    }
}

TEST(PluginHost, ADeferredResultIsHeldByTheHostAndReleasedWhenItCompletes)
{
    Harness harness{};
    bool finish = false;
    harness.Next = DeferredFixture(110, "a", harness.Events, finish);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    {
        auto outcome = CallDeferred(harness);
        ASSERT_TRUE(outcome.Continuation && outcome.Progress && outcome.Cancel);
        Runtime::AgentOperationOutcome result{};
        EXPECT_FALSE(outcome.Continuation(harness.Context, result));
        EXPECT_EQ(outcome.Cancel(harness.Context), "cancelled");
        finish = true;
        EXPECT_TRUE(outcome.Continuation(harness.Context, result));
        EXPECT_EQ(result.Text, R"({"done":true})");
    } // the agent server drops a finished call
    EXPECT_EQ(harness.Events, (Log{"continue a", "cancel", "continue a", "deferred_destroyed"}));
}

TEST(PluginHost, ADroppedDeferredCallReleasesThePluginsCallables)
{
    Harness harness{};
    bool finish = false;
    harness.Next = DeferredFixture(111, "a", harness.Events, finish);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    { auto outcome = CallDeferred(harness); } // a disconnect drops the pending call
    EXPECT_EQ(harness.Events, (Log{"deferred_destroyed"}));
}

TEST(PluginHost, AnUnloadDestroysAPendingCallsCallablesBeforeTheCloseAndTheCallAnswersUnloaded)
{
    Harness harness{};
    bool finish = false;
    harness.Next = DeferredFixture(112, "a", harness.Events, finish);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    auto outcome = CallDeferred(harness);
    ASSERT_FALSE(harness.Call("plugin_unload").IsError);
    EXPECT_EQ(harness.Events, (Log{"shutdown", "deferred_destroyed", "close"}));
    Runtime::AgentOperationOutcome result{};
    EXPECT_TRUE(outcome.Continuation(harness.Context, result));
    EXPECT_EQ(result.ErrorCode, "plugin_unloaded");
    EXPECT_EQ(outcome.Cancel(harness.Context), "");
    EXPECT_EQ(harness.Events.size(), 3u); // no plugin code ran
}

TEST(PluginHost, AnOldDeferredCallNeverReachesTheReloadedGeneration)
{
    Harness harness{};
    bool finish = false;
    harness.Next = DeferredFixture(113, "a", harness.Events, finish);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    auto old = CallDeferred(harness);
    harness.Next = DeferredFixture(114, "b", harness.Events, finish);
    ASSERT_FALSE(harness.Call("plugin_reload", {{"path", "fixture"}}).IsError);
    Runtime::AgentOperationOutcome result{};
    EXPECT_TRUE(old.Continuation(harness.Context, result));
    EXPECT_EQ(result.ErrorCode, "plugin_unloaded");
    auto fresh = CallDeferred(harness);
    EXPECT_FALSE(fresh.Continuation(harness.Context, result));
    EXPECT_EQ(harness.Events, (Log{"shutdown", "deferred_destroyed", "close", "continue b"}));
}

TEST(PluginHost, ADeferredCallThatUnloadsItsOwnPluginFinishesBeforeTheUnload)
{
    Harness harness{};
    bool finish = false;
    std::function<void()> inside{};
    harness.Next = DeferredFixture(116, "a", harness.Events, finish, &inside);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    auto unload = Runtime::AgentOperationOutcome{};
    bool unloadedInside = true, fired = false;
    inside = [&]
    {
        if (fired) return;
        fired = true;
        unload = Runtime::InvokeAgentOperation(harness.Registry, "plugin_unload", harness.Context, "{}", false);
        Runtime::AgentOperationOutcome result{};
        unloadedInside = unload.Continuation && unload.Continuation(harness.Context, result);
    };
    auto outcome = CallDeferred(harness);
    finish = true;
    Runtime::AgentOperationOutcome result{};
    EXPECT_TRUE(outcome.Continuation(harness.Context, result)); // the plugin's code returns normally
    EXPECT_FALSE(unloadedInside);                               // the unload waited
    outcome = {};
    Runtime::AgentOperationOutcome unloaded{};
    EXPECT_TRUE(unload.Continuation(harness.Context, unloaded));
    EXPECT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_EQ(harness.Events, (Log{"continue a", "deferred_destroyed", "shutdown", "close"}));
}

TEST(PluginHost, AJobCompletionThatUnloadsItsOwnPluginFinishesBeforeTheUnload)
{
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(127));
    Runtime::AgentOperationOutcome unload{};
    bool unloadedInside = true;
    ASSERT_TRUE(Runtime::SubmitPluginJob(harness.Context, {
        .Label = "Self-unloading job",
        .Work = [](const std::function<bool()>&) {},
        .Completion = [&](const bool applied)
        {
            unload = Runtime::InvokeAgentOperation(harness.Registry, "plugin_unload", harness.Context, "{}", false);
            Runtime::AgentOperationOutcome result{};
            unloadedInside = unload.Continuation && unload.Continuation(harness.Context, result);
            harness.Events.push_back("completion returns");
            return applied;
        },
    }).IsValid());
    harness.Drain();
    EXPECT_FALSE(unloadedInside); // the unload waited for the completion
    Runtime::AgentOperationOutcome unloaded{};
    ASSERT_TRUE(unload.Continuation);
    EXPECT_TRUE(unload.Continuation(harness.Context, unloaded));
    EXPECT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_EQ(harness.Events, (Log{"completion returns", "shutdown", "close"}));
}

TEST(PluginHost, ADrainTimeoutKeepsTheLoadedPluginsDeferredCallWorking)
{
    std::atomic<bool> release{false}, started{false}; // outlive the harness, whose drain waits for the job
    Harness harness{};
    bool finish = false;
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    harness.Next = DeferredFixture(115, "a", harness.Events, finish);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    auto outcome = CallDeferred(harness);
    ASSERT_TRUE(Runtime::SubmitPluginJob(harness.Context, {
        .Label = "Stubborn job",
        .Work = [&release, &started](const std::function<bool()>&)
        {
            started = true;
            while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        },
        .Completion = [](const bool published) { return published; },
    }).IsValid());
    // Running, not queued: a queued job is cancelled before it starts and leaves nothing to drain.
    while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(harness.Call("plugin_unload", {{"drain_deadline_ms", 20}}).ErrorCode, "drain_timeout");
    finish = true;
    Runtime::AgentOperationOutcome result{};
    EXPECT_TRUE(outcome.Continuation(harness.Context, result));
    EXPECT_EQ(result.Text, R"({"done":true})");
}


namespace
{
    Runtime::PluginJobOutput OutputOf(const std::uint32_t entity, std::string name)
    {
        return {.StableEntityId = entity,
                .Property = {.Domain = Runtime::GeometryElementDomain::PointCloudPoint, .Name = std::move(name),
                             .ValueKind = Geometry::PropertyValueKind::Vec3}};
    }

    // A job that waits until released (or cancelled) and logs its completion with `tag`.
    Runtime::PluginJob HeldJob(std::string tag, Log& log, std::atomic<bool>& release, std::optional<Runtime::PluginJobOutput> output,
                               const bool verdict = true)
    {
        return {.Label = "Fixture " + tag,
                .Output = std::move(output),
                .Work = [&release](const std::function<bool()>& cancelled)
                { while (!release && !cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1)); },
                .Completion = [tag, &log, verdict](const bool applied)
                {
                    log.push_back(tag + (applied ? " applied" : " finalized"));
                    return applied && verdict;
                }};
    }

    std::optional<Runtime::EditorJobRecord> RecordOf(Harness& harness, const Runtime::PluginJobHandle job)
    {
        for (auto& record : harness.Jobs())
            if (record.Token == job.Token) return record;
        return std::nullopt;
    }
}

TEST(PluginHost, APluginJobIsARowOfTheShellSessionAndItsCompletionIsTheVerdict)
{
    std::atomic<bool> release{false};
    Harness harness{};
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    ASSERT_TRUE(harness.Loaded(120));
    const auto good = Runtime::SubmitPluginJob(harness.Context, HeldJob("good", harness.Events, release, OutputOf(7, "a")));
    const auto bad = Runtime::SubmitPluginJob(harness.Context, HeldJob("bad", harness.Events, release, OutputOf(7, "b"), false));
    ASSERT_TRUE(good.IsValid() && bad.IsValid());
    const auto row = RecordOf(harness, good);
    ASSERT_TRUE(row.has_value());
    EXPECT_EQ(row->Name, "Fixture good");
    EXPECT_EQ(row->Identity.EntityId, 7u);
    EXPECT_EQ(row->Identity.OutputName, "a");
    Runtime::ReportPluginJobOutcome(good, Runtime::PluginJobDomain::Gpu, "ran on the GPU");
    release = true;
    harness.Drain();
    EXPECT_EQ(RecordOf(harness, good)->State, Runtime::JobState::Published);
    EXPECT_NE(RecordOf(harness, bad)->State, Runtime::JobState::Published); // a failed publication is not reported published
    // One terminal completion each: the refused publication is not finalized a second time.
    EXPECT_EQ(std::count(harness.Events.begin(), harness.Events.end(), "bad applied"), 1);
    EXPECT_EQ(std::count(harness.Events.begin(), harness.Events.end(), "bad finalized"), 0);
    EXPECT_EQ(RecordOf(harness, good)->ResolvedJobDomain, Runtime::EditorJobDomain::GpuCompute);
}

TEST(PluginHost, AdmissionRefusesASecondRunOnTheSameOutputOnly)
{
    std::atomic<bool> release{false};
    Harness harness{};
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    ASSERT_TRUE(harness.Loaded(121));
    const auto first = Runtime::SubmitPluginJob(harness.Context, HeldJob("first", harness.Events, release, OutputOf(7, "a")));
    ASSERT_TRUE(first.IsValid());
    std::string refusal;
    EXPECT_FALSE(Runtime::SubmitPluginJob(harness.Context, HeldJob("same", harness.Events, release, OutputOf(7, "a")), &refusal).IsValid());
    EXPECT_NE(refusal.find("already running"), std::string::npos) << refusal;
    EXPECT_TRUE(Runtime::SubmitPluginJob(harness.Context, HeldJob("other", harness.Events, release, OutputOf(7, "b"))).IsValid());
    auto stage = HeldJob("stage", harness.Events, release, OutputOf(7, "a"));
    stage.Run = first; // a later stage of the same run is admitted
    EXPECT_TRUE(Runtime::SubmitPluginJob(harness.Context, std::move(stage)).IsValid());
}

TEST(PluginHost, CancellingAPluginRunFinalizesItsJobs)
{
    std::atomic<bool> release{false};
    Harness harness{};
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    ASSERT_TRUE(harness.Loaded(122));
    const auto run = Runtime::SubmitPluginJob(harness.Context, HeldJob("run", harness.Events, release, OutputOf(7, "a")));
    ASSERT_TRUE(run.IsValid());
    EXPECT_TRUE(Runtime::CancelPluginJob(run));
    harness.Drain();
    EXPECT_EQ(harness.Events, (Log{"run finalized"}));
    EXPECT_EQ(RecordOf(harness, run)->State, Runtime::JobState::Cancelled);
}

TEST(PluginHost, AGatedJobCompletesOnlyOnceItsGateOpensAndADrainCancelsIt)
{
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(123));
    bool ready = false;
    const auto gated = [&](std::string tag)
    {
        return Runtime::PluginJob{.Label = "Gated " + tag,
                                  .Work = [](const std::function<bool()>&) {},
                                  .IsReadyToApply = [&ready] { return ready; },
                                  .Completion = [tag, &harness](const bool applied)
                                  {
                                      harness.Events.push_back(tag + (applied ? " applied" : " finalized"));
                                      return applied;
                                  }};
    };
    ASSERT_TRUE(Runtime::SubmitPluginJob(harness.Context, gated("a")).IsValid());
    harness.Drain();
    EXPECT_TRUE(harness.Events.empty()); // finished on the worker, parked behind the gate
    ready = true;
    harness.Drain();
    EXPECT_EQ(harness.Events, (Log{"a applied"}));
    ready = false;
    ASSERT_TRUE(Runtime::SubmitPluginJob(harness.Context, gated("b")).IsValid());
    harness.Drain();
    ASSERT_FALSE(harness.Call("plugin_unload").IsError);
    EXPECT_EQ(harness.Events, (Log{"a applied", "b finalized", "shutdown", "close"}));
}

TEST(PluginHost, CancellingALaterStageCancelsItsWholeRun)
{
    std::atomic<bool> release{false};
    Harness harness{};
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    ASSERT_TRUE(harness.Loaded(124));
    const auto root = Runtime::SubmitPluginJob(harness.Context, HeldJob("root", harness.Events, release, OutputOf(7, "a")));
    auto stage = HeldJob("stage", harness.Events, release, OutputOf(7, "a"));
    stage.Run = root;
    const auto later = Runtime::SubmitPluginJob(harness.Context, std::move(stage));
    ASSERT_TRUE(root.IsValid() && later.IsValid());
    EXPECT_TRUE(Runtime::CancelPluginJob(later));
    harness.Drain();
    EXPECT_EQ(RecordOf(harness, root)->State, Runtime::JobState::Cancelled);
    EXPECT_EQ(RecordOf(harness, later)->State, Runtime::JobState::Cancelled);
}

TEST(PluginHost, ADrainCancelsThroughTheWorkspaceSoTheJobsWindowSeesTheRequest)
{
    std::atomic<bool> release{false}, started{false};
    Harness harness{};
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    ASSERT_TRUE(harness.Loaded(125));
    const auto run = Runtime::SubmitPluginJob(harness.Context, {
        .Label = "Stubborn job",
        .Output = OutputOf(7, "a"),
        .Work = [&release, &started](const std::function<bool()>&)
        {
            started = true;
            while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        },
        .Completion = [](const bool published) { return published; },
    });
    ASSERT_TRUE(run.IsValid());
    // Running, not queued: a queued job is cancelled before it starts and leaves nothing to drain.
    while (!started) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(harness.Call("plugin_unload", {{"drain_deadline_ms", 20}}).ErrorCode, "drain_timeout");
    (void)Runtime::PrepareEditorWorkspaceSnapshotFrame(harness.Shell);
    EXPECT_TRUE(Runtime::IsEditorRunCancelRequested(Runtime::PrepareEditorProcessingCommands(harness.Shell), run.Token));
}

TEST(PluginHost, WithoutAnAppWorkspaceJobsGoThroughTheHostsOwn)
{
    Harness harness{{.UseShell = false}};
    ASSERT_TRUE(harness.Loaded(126));
    ASSERT_TRUE(StartCancellableJob(harness.Context, harness.Events));
    ASSERT_FALSE(harness.Call("plugin_unload").IsError);
    EXPECT_EQ(harness.Events, (Log{"job_finalized", "shutdown", "close"}));
}

namespace
{
    Log* g_StartupEvents = nullptr;
    bool StartupRegister(Runtime::PluginRegistrations& staged, std::string&) noexcept
    {
        return Fixture(130, *g_StartupEvents)(staged);
    }
    void StartupShutdown() noexcept { g_StartupEvents->push_back("shutdown"); }

    // Stages the in-process fixture as the Sandbox does before the config boot: its section joins the boot registry.
    std::pair<std::vector<Runtime::StagedPlugin>, Config::EngineConfigSectionRegistry> StageFixture(Log& log)
    {
        g_StartupEvents = &log;
        std::vector<std::string> errors;
        std::vector<std::unique_ptr<Runtime::PluginImage>> images;
        images.push_back(Runtime::MakeInProcessPlugin(&StartupRegister, &StartupShutdown));
        Config::EngineConfigSectionRegistry sections;
        auto staged = Runtime::StageStartupPlugins(std::move(images), sections, errors);
        EXPECT_EQ(staged.size(), 1u) << (errors.empty() ? "" : errors.front());
        return {std::move(staged), std::move(sections)};
    }

    // A boot config whose saved payload for the fixture's section differs from the default.
    Config::EngineConfig BootConfigWithSavedPayload(const Config::EngineConfigSectionRegistry& sections)
    {
        auto config = HeadlessConfig();
        Config::PopulateEngineConfigSectionDefaults(config, sections);
        Config::UpsertEngineConfigSection(config.AppSections, {.Name = "fixture", .SchemaId = "test.fixture",
                                                               .SchemaVersion = 1u, .PayloadJson = R"({"value":7})"});
        return config;
    }
}

TEST(PluginHost, AStartupPluginLoadsItsSavedPayloadAtBootAndWorksWithoutAnAgentServer)
{
    Log startupLog;
    auto [staged, sections] = StageFixture(startupLog);
    auto config = BootConfigWithSavedPayload(sections);
    Harness harness{{.Startup = std::move(staged), .BootSections = std::move(sections), .Agent = false,
                     .Config = std::move(config)}};
    EXPECT_TRUE(harness.SectionRegistered());
    EXPECT_TRUE(harness.WindowRegistered());
    EXPECT_TRUE(harness.HistoryCommandRegistered());
    EXPECT_EQ(harness.Registry.Find("fixture_echo"), nullptr); // no agent server, no tools
    const auto* section = Config::FindEngineConfigSection(
        harness.Context.ConfigControl->GetEngineConfigControlState().ActiveConfig.AppSections, "fixture");
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->PayloadJson, R"({"value":7})");
}

TEST(PluginHost, AStartupPluginThatCannotCommitLeavesNoBootSectionBehind)
{
    Log startupLog;
    auto [staged, sections] = StageFixture(startupLog);
    auto config = BootConfigWithSavedPayload(sections);
    Harness harness{{.Startup = std::move(staged), .BootSections = std::move(sections), .Config = std::move(config),
                     .Ui = false}};
    EXPECT_FALSE(harness.SectionRegistered()); // no UI host: the commit failed, and the boot section went too
    EXPECT_EQ(startupLog, (Log{"shutdown"})) << "its Register succeeded: one shutdown hook";
}

TEST(PluginHost, AStartupPluginUnloadsAndReloadsLikeALoadedOne)
{
    Log startupLog;
    auto [staged, sections] = StageFixture(startupLog);
    auto config = BootConfigWithSavedPayload(sections);
    Harness harness{{.Startup = std::move(staged), .BootSections = std::move(sections), .Config = std::move(config)}};
    EXPECT_NE(harness.Registry.Find("fixture_echo"), nullptr);
    harness.Next = Fixture(131, harness.Events);
    const auto reply = harness.Call("plugin_reload", {{"path", "fixture"}});
    ASSERT_FALSE(reply.IsError) << reply.Text;
    EXPECT_EQ(Json::parse(reply.Text)["config"]["restored"], Json::array({"fixture"}));
    EXPECT_EQ(startupLog, (Log{"shutdown"}));
    ASSERT_FALSE(harness.Call("plugin_unload").IsError);
    EXPECT_TRUE(harness.NothingRegistered());
}

TEST(PluginHost, TestHooksAreRejectedWithoutTheFlag)
{
    Harness plain{{.TestHooks = false}};
    plain.Next = Fixture(108, plain.Events);
    const auto reply = plain.Call("plugin_load", {{"path", "fixture"}, {"fail_at", "windows"}});
    EXPECT_EQ(reply.ErrorCode, "invalid_params");
    EXPECT_TRUE(plain.Events.empty()); // nothing was opened
    EXPECT_EQ(plain.Registry.Find("plugin_host_identity"), nullptr);
    EXPECT_FALSE(Runtime::PluginProbes::RecordClosureCommand(plain.Context, "closure", [] { return true; },
                                                             [] { return true; }));
}

TEST(PluginHost, TestHooksAreAcceptedWithTheFlag)
{
    Harness hooked{{.TestHooks = true}};
    EXPECT_NE(hooked.Registry.Find("plugin_host_identity"), nullptr);
    EXPECT_TRUE(Runtime::PluginProbes::RecordClosureCommand(hooked.Context, "closure", [] { return true; },
                                                            [] { return true; }));
}

TEST(PluginHost, AStaticBuildAnswersPluginsUnsupported)
{
    Harness harness{{.InProcess = false}};
    const Json path = {{"path", "/nonexistent/libplugin.so"}};
    // A shared build gets as far as the private copy, or finds no plugin to reload.
    const auto load = harness.Call("plugin_load", path);
    EXPECT_EQ(load.ErrorCode, Runtime::PluginsSupported() ? "copy_failed" : "plugins_unsupported") << load.Text;
    const auto reload = harness.Call("plugin_reload", path);
    EXPECT_EQ(reload.ErrorCode, Runtime::PluginsSupported() ? "not_loaded" : "plugins_unsupported") << reload.Text;
}

TEST(PluginHost, TheOrdinaryExitSettlesGpuWorkThenJobsThenRunsTheShutdownHookAndRemovesEveryRegistration)
{
    Log* events = nullptr;
    bool clean = false;
    Harness* self = nullptr;
    Harness harness{{.Probe = [&](Runtime::RuntimeModuleShutdownContext&)
                     {
                         clean = self->NothingRegistered();
                         events->push_back("host_done");
                     }}};
    self = &harness;
    events = &harness.Events;
    ASSERT_TRUE(harness.Loaded(109));
    ASSERT_TRUE(StartCancellableJob(harness.Context, harness.Events));
    ASSERT_TRUE(Runtime::StartPluginGpuWork(harness.Context, {
        .RecordFrameCommands = [](auto&) {},
        .HasInFlightWork = [] { return true; },
        .ShutdownAfterDeviceIdle = [events] { events->push_back("gpu_shutdown"); },
    }));

    harness.Engine.Shutdown();
    // The probe sorts before the host, so it shuts down after it; so do the modules a plugin registers with.
    for (const std::string_view name : {Runtime::EngineConfigControl{}.Name(), Runtime::EditorUiModule{}.Name(),
                                        Runtime::SceneDocumentModule{}.Name(), std::string_view{"Runtime.AgentServer"}})
        EXPECT_LT(name, harness.Host->Name());
    // No "close": the image stays mapped.
    EXPECT_EQ(harness.Events, (Log{"gpu_shutdown", "job_finalized", "shutdown", "host_done"}));
    EXPECT_TRUE(clean);
}

// ARCH-022 step 2d-0: several plugins at once, each loaded, drained, unloaded and reloaded by name.
namespace
{
    // A worker plugin named `name`: tools that submit a job (running until cancelled, or until `release` with
    // `stubborn`), cancel or join one by token, call another tool before submitting their own, and submit from a
    // worker thread; a window that submits a job when drawn while `drawSubmits` is set.
    struct WorkerState
    {
        std::atomic<bool> Release{false};
        std::atomic<int> WorkerSubmission{-1}; // 1 accepted, 0 refused, -1 not yet tried
        bool DrawSubmits{false};
        std::vector<std::string> DrawTokens{};
        std::atomic<bool> Stubborn{false}; // ignores cancellation until Release
        Runtime::AgentOperationRegistry* Registry{nullptr};
        Runtime::JobService* GpuJobs{nullptr}; // where <name>_gpu registers its GPU work; null: the context's
        std::vector<std::string> GpuEvents{};
        bool InFlightSubmits{false};
        int InFlightSubmission{-1}; // what a contract call from HasInFlightWork got: 1 accepted, 0 refused
    };

    std::string TokenText(const Runtime::PluginJobHandle handle)
    {
        return std::to_string(handle.Token.Index) + ":" + std::to_string(handle.Token.Generation);
    }

    Runtime::PluginJobHandle ParseHandle(const std::string_view arguments)
    {
        const Json value = Json::parse(arguments, nullptr, false);
        const std::string token = value.is_object() ? value.value("token", std::string{}) : std::string{};
        const auto colon = token.find(':');
        if (colon == std::string::npos) return {};
        return {Runtime::JobToken{std::uint32_t(std::stoul(token.substr(0, colon))),
                                  std::uint32_t(std::stoul(token.substr(colon + 1)))}};
    }

    constexpr std::string_view kTokenSchema =
        R"({"type":"object","properties":{"token":{"type":"string"}},"additionalProperties":false})";
    constexpr std::string_view kToolSchema =
        R"({"type":"object","properties":{"tool":{"type":"string"},"arguments":{"type":"object"}},"additionalProperties":false})";

    Runtime::PluginJob LoopingJob(std::string label, WorkerState& state)
    {
        return {.Label = std::move(label),
                .Work = [&state](const std::function<bool()>& cancelled)
                {
                    while (!(state.Stubborn ? state.Release.load() : cancelled() || state.Release.load()))
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                },
                .Completion = [](const bool published) { return published; }};
    }

    std::function<bool(Runtime::PluginRegistrations&)> Worker(const std::string name, const int generation,
                                                              WorkerState& state)
    {
        return [name, generation, &state](Runtime::PluginRegistrations& staged)
        {
            using Outcome = Runtime::AgentOperationOutcome;
            staged.Name = name;
            staged.Generation = generation;
            const auto submit = [name, &state](const Runtime::AgentOperationContext& context)
            {
                std::string refusal{};
                const auto handle = Runtime::SubmitPluginJob(context, LoopingJob(name + " job", state), &refusal);
                return handle.IsValid() ? Json{{"token", TokenText(handle)}} : Json{{"refused", refusal}};
            };
            staged.Operations.push_back({.Name = name + "_job", .Invoke =
                [submit](const Runtime::AgentOperationContext& context, std::string_view)
                { return Outcome{.Text = submit(context).dump()}; }});
            staged.Operations.push_back({.Name = name + "_cancel", .InputSchemaJson = std::string{kTokenSchema}, .Invoke =
                [](const Runtime::AgentOperationContext&, const std::string_view arguments)
                { return Outcome{.Text = Json{{"cancelled", Runtime::CancelPluginJob(ParseHandle(arguments))}}.dump()}; }});
            staged.Operations.push_back({.Name = name + "_report", .InputSchemaJson = std::string{kTokenSchema}, .Invoke =
                [](const Runtime::AgentOperationContext&, const std::string_view arguments)
                {
                    Runtime::ReportPluginJobOutcome(ParseHandle(arguments), Runtime::PluginJobDomain::Gpu, "reported");
                    return Outcome{.Text = "{}"};
                }});
            staged.Operations.push_back({.Name = name + "_join", .InputSchemaJson = std::string{kTokenSchema}, .Invoke =
                [name, &state](const Runtime::AgentOperationContext& context, const std::string_view arguments)
                {
                    auto job = LoopingJob(name + " stage", state);
                    job.Run = ParseHandle(arguments);
                    return Outcome{.Text = Json{{"joined", Runtime::SubmitPluginJob(context, std::move(job), nullptr).IsValid()}}.dump()};
                }});
            staged.Operations.push_back({.Name = name + "_nest", .InputSchemaJson = std::string{kToolSchema}, .Invoke =
                [submit, &state](const Runtime::AgentOperationContext& context, const std::string_view arguments)
                {
                    const std::string tool = Json::parse(arguments).value("tool", std::string{});
                    const auto inner = Runtime::InvokeAgentOperation(*state.Registry, tool, context, "{}", false);
                    return Outcome{.Text = Json{{"inner", Json::parse(inner.Text)}, {"outer", submit(context)}}.dump()};
                }});
            // Calls a host tool and polls its continuation a few times before returning, as plugin code might.
            staged.Operations.push_back({.Name = name + "_host", .InputSchemaJson = std::string{kToolSchema}, .Invoke =
                [&state](const Runtime::AgentOperationContext& context, const std::string_view arguments)
                {
                    const Json request = Json::parse(arguments);
                    auto outcome = Runtime::InvokeAgentOperation(*state.Registry, request.value("tool", std::string{}),
                                                                 context, request.value("arguments", Json::object()).dump(),
                                                                 false);
                    bool finished = !outcome.Continuation;
                    for (int poll = 0; !finished && poll < 20; ++poll)
                    {
                        Runtime::AgentOperationOutcome result{};
                        if (outcome.Continuation(context, result))
                        {
                            finished = true;
                            outcome = std::move(result);
                        }
                    }
                    return Outcome{.Text = Json{{"finished", finished}, {"error", outcome.ErrorCode}}.dump()};
                }});
            staged.Operations.push_back({.Name = name + "_gpu", .Invoke =
                [name, &state](const Runtime::AgentOperationContext& context, std::string_view)
                {
                    auto target = context;
                    if (state.GpuJobs != nullptr) target.Jobs = state.GpuJobs;
                    const bool started = Runtime::StartPluginGpuWork(target, {
                        .RecordFrameCommands = [&state](auto&) { state.GpuEvents.push_back("record"); },
                        .DrainCompletedTransfers = [&state] { state.GpuEvents.push_back("transfers"); },
                        .HasInFlightWork = [name, &state]
                        {
                            state.GpuEvents.push_back("in_flight");
                            if (state.InFlightSubmits && state.InFlightSubmission < 0)
                                state.InFlightSubmission = Runtime::SubmitPluginJob(
                                    {}, LoopingJob(name + " probed job", state), nullptr).IsValid() ? 1 : 0;
                            return false;
                        },
                        .ShutdownAfterDeviceIdle = [&state] { state.GpuEvents.push_back("shutdown"); },
                    });
                    return Outcome{.Text = Json{{"started", started}}.dump()};
                }});
            staged.Operations.push_back({.Name = name + "_worker", .Invoke =
                [name, &state](const Runtime::AgentOperationContext& context, std::string_view)
                {
                    const auto handle = Runtime::SubmitPluginJob(context, {
                        .Label = name + " worker job",
                        .Work = [context, &state](const std::function<bool()>&)
                        {
                            state.WorkerSubmission = Runtime::SubmitPluginJob(context, LoopingJob("nested", state), nullptr)
                                .IsValid() ? 1 : 0;
                        },
                        .Completion = [](const bool published) { return published; },
                    }, nullptr);
                    return Outcome{.Text = Json{{"token", TokenText(handle)}}.dump()};
                }});
            staged.Windows.push_back({.Id = "view." + name, .MenuPath = {"View"}, .Title = name, .Draw =
                [name, &state](bool&)
                {
                    if (!state.DrawSubmits) return;
                    const auto handle = Runtime::SubmitPluginJob({}, LoopingJob(name + " drawn job", state), nullptr);
                    state.DrawTokens.push_back(handle.IsValid() ? TokenText(handle) : "refused");
                }});
            return true;
        };
    }

    // A token's job in the host's probe (null when it lists none).
    Json JobOf(Harness& harness, const std::string& token)
    {
        for (const auto& job : Json::parse(harness.Call("plugin_host_identity").Text)["jobs"])
            if (job["token"] == token) return job;
        return nullptr;
    }

    std::string OwnerOf(Harness& harness, const std::string& token)
    {
        const Json job = JobOf(harness, token);
        return job.is_object() ? job["plugin"].get<std::string>() : std::string{};
    }

    bool StartsRunning(Harness& harness, const std::string& token)
    {
        for (int frame = 0; frame < 2000; ++frame)
        {
            if (const Json job = JobOf(harness, token); job.is_object() && job["state"] == "running") return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    }

    Json Reply(const Runtime::AgentOperationOutcome& outcome)
    {
        return Json::parse(outcome.Text, nullptr, false);
    }
}

TEST(PluginHost, TwoPluginsLoadByNameAndUnloadingOneKeepsTheOther)
{
    Harness harness{};
    harness.Builds["a"] = Fixture(201, harness.Events, "alpha");
    harness.Builds["b"] = Fixture(202, harness.Events, "beta");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    const Json listed = Reply(harness.Call("plugin_list"));
    ASSERT_EQ(listed["loaded"].size(), 2u);
    EXPECT_EQ(listed["loaded"][1]["name"], "beta");
    const auto unnamed = harness.Call("plugin_unload");
    EXPECT_EQ(unnamed.ErrorCode, "invalid_params");
    EXPECT_NE(unnamed.Text.find("alpha, beta"), std::string::npos) << unnamed.Text;
    EXPECT_EQ(harness.Call("plugin_unload", {{"plugin", "gamma"}}).ErrorCode, "not_loaded");

    const auto unloaded = harness.Call("plugin_unload", {{"plugin", "alpha"}});
    ASSERT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_EQ(Reply(unloaded)["plugin"], "alpha");
    EXPECT_EQ(harness.Registry.Find("alpha_echo"), nullptr);
    EXPECT_EQ(harness.Context.ConfigControl->SectionRegistry().Find("alpha"), nullptr);
    EXPECT_NE(harness.Context.ConfigControl->SectionRegistry().Find("beta"), nullptr);
    EXPECT_FALSE(harness.Call("beta_echo").IsError);
    // With one plugin left, no name is needed.
    EXPECT_FALSE(harness.Call("plugin_unload").IsError);
    EXPECT_EQ(Reply(harness.Call("plugin_list"))["loaded"], Json::array());
}

TEST(PluginHost, ASecondLoadOfALoadedNameIsRefused)
{
    Harness harness{};
    harness.Builds["a"] = Fixture(203, harness.Events, "alpha");
    harness.Builds["a2"] = Fixture(204, harness.Events, "alpha");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    EXPECT_EQ(harness.Call("plugin_load", {{"path", "a2"}}).ErrorCode, "already_loaded");
    const Json listed = Reply(harness.Call("plugin_list"));
    ASSERT_EQ(listed["loaded"].size(), 1u);
    EXPECT_EQ(listed["loaded"][0]["generation"], 203);
}

TEST(PluginHost, ContractCallsActForTheCallingPluginsOwnJobsOnly)
{
    WorkerState alpha{}, beta{};
    Harness harness{};
    alpha.Registry = beta.Registry = &harness.Registry;
    harness.Builds["a"] = Worker("alpha", 210, alpha);
    harness.Builds["b"] = Worker("beta", 211, beta);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    const std::string a = Reply(harness.Call("alpha_job")).value("token", std::string{});
    const std::string b = Reply(harness.Call("beta_job")).value("token", std::string{});
    ASSERT_FALSE(a.empty());
    ASSERT_FALSE(b.empty());
    EXPECT_EQ(OwnerOf(harness, a), "alpha");
    EXPECT_EQ(OwnerOf(harness, b), "beta");
    EXPECT_EQ(Reply(harness.Call("beta_cancel", {{"token", a}}))["cancelled"], false); // not beta's job
    EXPECT_EQ(Reply(harness.Call("beta_join", {{"token", a}}))["joined"], false);      // not beta's run
    const auto domain = [&harness, &a] { return RecordOf(harness, ParseHandle(Json{{"token", a}}.dump()))->ResolvedJobDomain; };
    ASSERT_FALSE(harness.Call("beta_report", {{"token", a}}).IsError);
    EXPECT_NE(domain(), Runtime::EditorJobDomain::GpuCompute); // not beta's job
    ASSERT_FALSE(harness.Call("alpha_report", {{"token", a}}).IsError);
    EXPECT_EQ(domain(), Runtime::EditorJobDomain::GpuCompute);
    EXPECT_EQ(Reply(harness.Call("alpha_join", {{"token", a}}))["joined"], true);
    EXPECT_EQ(Reply(harness.Call("alpha_cancel", {{"token", a}}))["cancelled"], true);
    // From outside any plugin call, with two plugins loaded, a contract call has no plugin to act for.
    std::string refusal{};
    EXPECT_FALSE(Runtime::SubmitPluginJob(harness.Context, LoopingJob("orphan", alpha), &refusal).IsValid());
    EXPECT_NE(refusal.find("several plugins"), std::string::npos) << refusal;
}

TEST(PluginHost, ADrainOfOnePluginLeavesTheOthersJobRunning)
{
    WorkerState alpha{}, beta{};
    // Two workers, one per plugin's job: with one, alpha's cancelled job would wait behind beta's (PollDrain's note).
    auto config = HeadlessConfig();
    config.Simulation.WorkerThreadCount = 2u;
    Harness harness{{.Config = std::move(config)}};
    harness.Builds["a"] = Worker("alpha", 212, alpha);
    harness.Builds["b"] = Worker("beta", 213, beta);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    const std::string b = Reply(harness.Call("beta_job")).value("token", std::string{});
    ASSERT_TRUE(StartsRunning(harness, b));
    // A stubborn alpha job: alpha's drain times out, alpha stays loaded and admits work again; beta is untouched.
    // Released on any exit, so a failed assertion fails instead of the teardown waiting for it.
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &alpha.Release, [](std::atomic<bool>* flag) { *flag = true; }};
    alpha.Stubborn = true;
    const std::string a = Reply(harness.Call("alpha_job")).value("token", std::string{});
    ASSERT_TRUE(StartsRunning(harness, a));
    EXPECT_EQ(harness.Call("plugin_unload", {{"plugin", "alpha"}, {"drain_deadline_ms", 20}}).ErrorCode, "drain_timeout");
    alpha.Release = true;
    EXPECT_FALSE(Reply(harness.Call("alpha_job")).value("token", std::string{}).empty()) << "alpha admits work again";
    EXPECT_EQ(OwnerOf(harness, b), "beta");
    const auto unloaded = harness.Call("plugin_unload", {{"plugin", "alpha"}});
    ASSERT_FALSE(unloaded.IsError) << unloaded.Text;
    // Beta's job was not cancelled by alpha's drain: still beta's, and its cancel is a new request; beta still submits.
    EXPECT_EQ(OwnerOf(harness, b), "beta");
    EXPECT_FALSE(Reply(harness.Call("beta_job")).value("token", std::string{}).empty());
    EXPECT_EQ(Reply(harness.Call("beta_cancel", {{"token", b}}))["cancelled"], true);
    beta.Release = true;
}

TEST(PluginHost, ANestedCallActsForTheInnerPluginAndThenForTheOuterOneAgain)
{
    WorkerState alpha{}, beta{};
    Harness harness{};
    alpha.Registry = beta.Registry = &harness.Registry;
    harness.Builds["a"] = Worker("alpha", 214, alpha);
    harness.Builds["b"] = Worker("beta", 215, beta);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    const Json nested = Reply(harness.Call("alpha_nest", {{"tool", "beta_job"}}));
    const std::string inner = nested["inner"].value("token", std::string{});
    const std::string outer = nested["outer"].value("token", std::string{});
    EXPECT_EQ(OwnerOf(harness, inner), "beta") << nested.dump();
    EXPECT_EQ(OwnerOf(harness, outer), "alpha") << nested.dump();
    alpha.Release = beta.Release = true;
}

TEST(PluginHost, AContractCallFromAWorkerThreadIsRefused)
{
    WorkerState alpha{};
    Harness harness{};
    harness.Builds["a"] = Worker("alpha", 216, alpha);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(Reply(harness.Call("alpha_worker")).value("token", std::string{}).empty());
    for (int frame = 0; frame < 2000 && alpha.WorkerSubmission < 0; ++frame)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(alpha.WorkerSubmission, 0) << "a job's Work runs on a worker; contract calls run on the main thread";
}

TEST(PluginHost, AWindowActsForItsPluginAndAToolForwarderIsInertAfterTheUnload)
{
    WorkerState alpha{}, beta{};
    Harness harness{};
    harness.Builds["a"] = Worker("alpha", 217, alpha);
    harness.Builds["b"] = Worker("beta", 218, beta);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    auto* ui = harness.Engine.Services().Find<Runtime::EditorUiHost>();
    ASSERT_NE(ui, nullptr);
    ASSERT_TRUE(ui->SetWindowOpen("view.alpha", true));
    alpha.DrawSubmits = true;
    (void)ui->Windows().DrawOpenWindows();
    ASSERT_EQ(alpha.DrawTokens.size(), 1u);
    EXPECT_EQ(OwnerOf(harness, alpha.DrawTokens.front()), "alpha");
    // A copy of a forwarder, as a caller may keep one across the unload, outlives it and calls nothing.
    const auto* spec = harness.Registry.Find("alpha_job");
    ASSERT_NE(spec, nullptr);
    auto invoke = spec->Invoke;
    alpha.Release = true;
    ASSERT_FALSE(harness.Call("plugin_unload", {{"plugin", "alpha"}}).IsError);
    EXPECT_EQ(invoke(harness.Context, "{}").ErrorCode, "plugin_unloaded");
    EXPECT_FALSE(ui->SetWindowOpen("view.alpha", true));
}

TEST(PluginHost, AReloadKeepsThePluginsNameOrLeavesItUnloaded)
{
    Harness harness{};
    harness.Builds["a"] = Fixture(220, harness.Events, "alpha");
    harness.Builds["b"] = Fixture(221, harness.Events, "beta");
    harness.Builds["g"] = Fixture(222, harness.Events, "gamma");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "g"}}).IsError);
    const auto reply = harness.Call("plugin_reload", {{"plugin", "alpha"}, {"path", "b"}});
    EXPECT_EQ(reply.ErrorCode, "name_mismatch") << reply.Text;
    const Json listed = Reply(harness.Call("plugin_list"));
    ASSERT_EQ(listed["loaded"].size(), 1u);
    EXPECT_EQ(listed["loaded"][0]["name"], "gamma");
    EXPECT_EQ(harness.Registry.Find("beta_echo"), nullptr);
    EXPECT_FALSE(harness.Call("gamma_echo").IsError);
}

TEST(PluginHost, AStartupPluginWithATakenNameOrSectionIsDroppedAndTheOthersStay)
{
    Log log;
    std::vector<std::unique_ptr<Runtime::PluginImage>> images;
    images.push_back(std::make_unique<FixtureImage>(Fixture(230, log, "alpha"), log));
    images.push_back(std::make_unique<FixtureImage>([&log](Runtime::PluginRegistrations& staged)
    {
        // Its own section, but the name is taken.
        if (!Fixture(231, log, "alpha2")(staged)) return false;
        staged.Name = "alpha";
        return true;
    }, log));
    images.push_back(std::make_unique<FixtureImage>([&log](Runtime::PluginRegistrations& staged)
    {
        // Named beta, but its section is alpha's.
        if (!Fixture(232, log, "alpha")(staged)) return false;
        staged.Name = "beta";
        return true;
    }, log));
    images.push_back(std::make_unique<FixtureImage>(Fixture(233, log, "gamma"), log));
    Config::EngineConfigSectionRegistry sections;
    std::vector<std::string> errors;
    auto staged = Runtime::StageStartupPlugins(std::move(images), sections, errors);
    ASSERT_EQ(staged.size(), 2u);
    EXPECT_EQ(errors.size(), 2u);
    EXPECT_NE(sections.Find("alpha"), nullptr);
    EXPECT_NE(sections.Find("gamma"), nullptr);
    EXPECT_EQ(std::count(log.begin(), log.end(), "shutdown"), 2) << "each dropped plugin's Register succeeded";
    EXPECT_EQ(std::count(log.begin(), log.end(), "close"), 2);
    Harness harness{{.Startup = std::move(staged), .BootSections = std::move(sections)}};
    const Json listed = Reply(harness.Call("plugin_list"));
    ASSERT_EQ(listed["loaded"].size(), 2u);
    EXPECT_EQ(listed["loaded"][0]["name"], "alpha");
    EXPECT_EQ(listed["loaded"][1]["name"], "gamma");
}

TEST(PluginHost, AStartupPluginThatFailsToCommitRemovesOnlyItsOwnSections)
{
    Log log;
    std::vector<std::unique_ptr<Runtime::PluginImage>> images;
    images.push_back(std::make_unique<FixtureImage>(Fixture(240, log, "alpha"), log));
    images.push_back(std::make_unique<FixtureImage>([&log](Runtime::PluginRegistrations& staged)
    {
        // Its window is alpha's, so its commit fails after alpha's.
        if (!Fixture(241, log, "beta")(staged)) return false;
        staged.Windows.front().Id = "view.alpha";
        return true;
    }, log));
    Config::EngineConfigSectionRegistry sections;
    std::vector<std::string> errors;
    auto staged = Runtime::StageStartupPlugins(std::move(images), sections, errors);
    ASSERT_EQ(staged.size(), 2u);
    auto config = HeadlessConfig();
    Config::PopulateEngineConfigSectionDefaults(config, sections);
    Config::UpsertEngineConfigSection(config.AppSections, {.Name = "alpha", .SchemaId = "test.alpha",
                                                           .SchemaVersion = 1u, .PayloadJson = R"({"value":9})"});
    Harness harness{{.Startup = std::move(staged), .BootSections = std::move(sections), .Config = std::move(config)}};
    const Json listed = Reply(harness.Call("plugin_list"));
    ASSERT_EQ(listed["loaded"].size(), 1u);
    EXPECT_EQ(listed["loaded"][0]["name"], "alpha");
    const auto& registry = harness.Context.ConfigControl->SectionRegistry();
    EXPECT_NE(registry.Find("alpha"), nullptr);
    EXPECT_EQ(registry.Find("beta"), nullptr);
    const auto* section = Config::FindEngineConfigSection(
        harness.Context.ConfigControl->GetEngineConfigControlState().ActiveConfig.AppSections, "alpha");
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->PayloadJson, R"({"value":9})");
}

TEST(PluginHost, TwoPluginsWithOneGenerationShareTheCloseGuard)
{
    // The documented limitation: callable accounting counts by generation, so a live callable of one plugin
    // refuses the close of another with the same generation (the image stays mapped, which is safe).
    Harness harness{};
    harness.Builds["a"] = Fixture(250, harness.Events, "alpha");
    harness.Builds["b"] = Fixture(250, harness.Events, "beta");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    EXPECT_EQ(harness.Call("plugin_unload", {{"plugin", "alpha"}, {"retain_callable", true}}).ErrorCode, "callables_alive");
    EXPECT_EQ(harness.Call("plugin_unload", {{"plugin", "beta"}}).ErrorCode, "callables_alive");
}

TEST(PluginHost, ADeferredResultOfOnePluginSurvivesTheOthersUnload)
{
    Harness harness{};
    bool finish = false;
    harness.Builds["d"] = DeferredFixture(260, "a", harness.Events, finish);
    harness.Builds["a"] = Fixture(261, harness.Events, "alpha");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "d"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    auto pending = CallDeferred(harness);
    ASSERT_TRUE(pending.Continuation);
    ASSERT_FALSE(harness.Call("plugin_unload", {{"plugin", "alpha"}}).IsError);
    finish = true;
    Runtime::AgentOperationOutcome result{};
    ASSERT_TRUE(pending.Continuation(harness.Context, result));
    EXPECT_EQ(result.Text, R"({"done":true})");
    EXPECT_EQ(std::count(harness.Events.begin(), harness.Events.end(), "cancel"), 0);
}

TEST(PluginHost, ALoadOrAnotherPluginsUnloadPolledFromPluginCodeWaitsUntilItReturned)
{
    // A commit or an unload changes the registries that hold the running tool's own wrapper: it must not happen
    // inside that call.
    WorkerState alpha{}, beta{};
    Harness harness{};
    alpha.Registry = beta.Registry = &harness.Registry;
    harness.Builds["a"] = Worker("alpha", 270, alpha);
    harness.Builds["b"] = Worker("beta", 271, beta);
    harness.Builds["g"] = Fixture(272, harness.Events, "gamma");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    const Json load = Reply(harness.Call("alpha_host", {{"tool", "plugin_load"}, {"arguments", {{"path", "g"}}}}));
    EXPECT_EQ(load["finished"], false) << load.dump();
    EXPECT_EQ(harness.Registry.Find("gamma_echo"), nullptr);
    const Json unload = Reply(harness.Call("alpha_host", {{"tool", "plugin_unload"}, {"arguments", {{"plugin", "beta"}}}}));
    EXPECT_EQ(unload["finished"], false) << unload.dump();
    EXPECT_NE(harness.Registry.Find("beta_job"), nullptr);
    // Dropped with the call, neither leaves anything pending: from outside plugin code both go through.
    EXPECT_FALSE(harness.Call("plugin_load", {{"path", "g"}}).IsError);
    EXPECT_FALSE(harness.Call("plugin_unload", {{"plugin", "beta"}}).IsError);
}

TEST(PluginHost, TheIdentityProbeAsksGpuWorkAsItsPlugin)
{
    WorkerState alpha{}, beta{};
    Harness harness{};
    harness.Builds["a"] = Worker("alpha", 273, alpha);
    harness.Builds["b"] = Worker("beta", 274, beta);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    ASSERT_EQ(Reply(harness.Call("alpha_gpu"))["started"], true);
    alpha.InFlightSubmits = true;
    const Json identity = Reply(harness.Call("plugin_host_identity"));
    EXPECT_EQ(identity["plugins"][0]["gpu"]["in_flight"], false);
    EXPECT_EQ(alpha.InFlightSubmission, 1) << "the probe runs alpha's callback as alpha";
    alpha.Release = true;
}

TEST(PluginHost, GpuForwardersAServiceStillHoldsAreInertAfterTheUnload)
{
    // The host unregisters its GPU work from the host's job service; one registered elsewhere keeps the forwarders,
    // as a service that holds a copy across the unload would.
    Runtime::JobService elsewhere{};
    WorkerState alpha{};
    alpha.GpuJobs = &elsewhere;
    Harness harness{};
    harness.Builds["a"] = Worker("alpha", 275, alpha);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_EQ(Reply(harness.Call("alpha_gpu"))["started"], true);
    EXPECT_FALSE(elsewhere.HasGpuQueueWork());
    (void)elsewhere.DrainGpuQueueCompletedTransfers();
    EXPECT_EQ(alpha.GpuEvents, (std::vector<std::string>{"in_flight", "transfers"}));
    ASSERT_FALSE(harness.Call("plugin_unload").IsError);
    const auto seen = alpha.GpuEvents.size();
    EXPECT_FALSE(elsewhere.HasGpuQueueWork());
    (void)elsewhere.DrainGpuQueueCompletedTransfers();
    (void)elsewhere.ShutdownGpuQueueParticipants();
    EXPECT_EQ(alpha.GpuEvents.size(), seen) << "no plugin callback after the unload";
}

TEST(PluginHost, APluginsRegisterAndShutdownHookCannotSubmitWorkAsAnotherPlugin)
{
    // While its Register or shutdown hook runs a plugin is not loaded, so the host refuses its contract calls, even
    // with another plugin loaded (which a call from outside any plugin would act for). The reason is the host's:
    // at the ordinary exit the job service refuses submissions too.
    WorkerState alpha{}, beta{};
    std::vector<std::string> seen{};
    const auto submit = [&alpha, &seen](const std::string& when)
    {
        std::string refusal{};
        const bool accepted = Runtime::SubmitPluginJob({}, LoopingJob("early", alpha), &refusal).IsValid();
        seen.push_back(when + ": " + (accepted ? std::string{"accepted"} : refusal));
    };
    Harness harness{};
    alpha.Release = true; // an accepted job ends at once
    harness.Builds["b"] = Worker("beta", 280, beta);
    harness.Builds["a"] = [&submit, &harness](Runtime::PluginRegistrations& staged)
    {
        submit("register");
        return Fixture(281, harness.Events, "alpha")(staged);
    };
    harness.OnShutdownOf["a"] = [&submit] { submit("shutdown"); };
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "b"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_unload", {{"plugin", "alpha"}}).IsError);
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    // A second alpha is discarded after its Register: its shutdown hook runs entered, as a plugin not loaded.
    EXPECT_EQ(harness.Call("plugin_load", {{"path", "a"}}).ErrorCode, "already_loaded");
    harness.Engine.Shutdown(); // the ordinary exit: alpha, loaded last, shuts down first
    const std::string refused = "the calling plugin is not loaded";
    EXPECT_EQ(seen, (std::vector<std::string>{"register: " + refused, "shutdown: " + refused, "register: " + refused,
                                              "register: " + refused, "shutdown: " + refused, "shutdown: " + refused}));
    beta.Release = true;
}

// ARCH-022 template step (Q69, Q70): every successful Register gets exactly one shutdown hook before the image closes,
// committed or not; then the plugin's callables die, then the close. A Register that fails gets none.
namespace
{
    // Logs when the last copy of the plugin's callables it is captured in dies.
    struct Sentinel
    {
        Log& Events;
        ~Sentinel() { Events.push_back("callables_died"); }
    };

    std::function<bool(Runtime::PluginRegistrations&)> Sentinelled(std::function<bool(Runtime::PluginRegistrations&)> build,
                                                                   Log& log)
    {
        return [build = std::move(build), &log](Runtime::PluginRegistrations& staged)
        {
            if (!build(staged)) return false;
            auto& invoke = staged.Operations.front().Invoke;
            invoke = [inner = std::move(invoke), sentinel = std::make_shared<Sentinel>(log)](
                         const Runtime::AgentOperationContext& context, const std::string_view arguments)
            { return inner(context, arguments); };
            return true;
        };
    }
}

TEST(PluginHost, EveryDiscardedCandidateGetsOneShutdownHookBeforeItsCallablesDieAndTheClose)
{
    const Log discarded{"shutdown", "callables_died", "close"};
    {
        SCOPED_TRACE("a taken name");
        Harness harness{};
        harness.Builds["a"] = Fixture(310, harness.Events, "alpha");
        harness.Builds["a2"] = Sentinelled(Fixture(311, harness.Events, "alpha"), harness.Events);
        ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
        harness.Events.clear();
        EXPECT_EQ(harness.Call("plugin_load", {{"path", "a2"}}).ErrorCode, "already_loaded");
        EXPECT_EQ(harness.Events, discarded);
    }
    {
        SCOPED_TRACE("a failed commit");
        Harness harness{};
        harness.Builds["a"] = Sentinelled(Fixture(312, harness.Events, "alpha"), harness.Events);
        EXPECT_EQ(harness.Call("plugin_load", {{"path", "a"}, {"fail_at", "windows"}}).ErrorCode, "commit_failed");
        EXPECT_EQ(harness.Events, discarded);
    }
    {
        SCOPED_TRACE("a reload to another name");
        Harness harness{};
        harness.Builds["a"] = Fixture(313, harness.Events, "alpha");
        harness.Builds["b"] = Sentinelled(Fixture(314, harness.Events, "beta"), harness.Events);
        ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
        harness.Events.clear();
        EXPECT_EQ(harness.Call("plugin_reload", {{"plugin", "alpha"}, {"path", "b"}}).ErrorCode, "name_mismatch");
        EXPECT_EQ(harness.Events, (Log{"shutdown", "close", "shutdown", "callables_died", "close"})); // alpha, then beta
    }
    {
        SCOPED_TRACE("a pending load dropped before its commit");
        Harness harness{};
        harness.Builds["a"] = Sentinelled(Fixture(315, harness.Events, "alpha"), harness.Events);
        EXPECT_FALSE(harness.Call("plugin_load", {{"path", "a"}, {"simulate_drop", true}}).IsError);
        EXPECT_EQ(harness.Events, discarded);
    }
    {
        SCOPED_TRACE("a failed Register");
        Harness harness{};
        harness.Builds["a"] = [](Runtime::PluginRegistrations&) { return false; };
        EXPECT_EQ(harness.Call("plugin_load", {{"path", "a"}}).ErrorCode, "register_failed");
        EXPECT_EQ(harness.Events, (Log{"close"})) << "a Register that fails cleans up itself: no hook";
    }
}

TEST(PluginHost, ADiscardTheCloseGuardKeepsMappedStillRunsItsShutdownHookOnce)
{
    // Alpha's live callable of the shared generation keeps the discarded beta's image mapped (the documented
    // generation limitation): its hook ran, its image was not closed.
    Harness harness{};
    harness.Builds["a"] = Fixture(320, harness.Events, "alpha");
    harness.Builds["b"] = Fixture(320, harness.Events, "beta");
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "a"}}).IsError);
    harness.Events.clear();
    EXPECT_EQ(harness.Call("plugin_load", {{"path", "b"}, {"fail_at", "windows"}}).ErrorCode, "commit_failed");
    EXPECT_EQ(harness.Events, (Log{"shutdown"}));
    EXPECT_EQ(Json::parse(harness.Call("plugin_host_identity").Text)["closes"].back()["closed"], false);
}

TEST(PluginHost, StartupPluginsTheHostNeverCommittedGetTheirHookAndCallNothingAfterwards)
{
    // The host goes before OnResolve: each staged plugin's hook runs once, its forwarders call nothing, and the image
    // stays mapped (the boot registry outlives the host and still holds the sections).
    Log log;
    Config::EngineConfigSectionRegistry sections;
    std::vector<std::string> errors;
    std::vector<std::unique_ptr<Runtime::PluginImage>> images;
    images.push_back(std::make_unique<FixtureImage>([&log](Runtime::PluginRegistrations& staged)
    {
        if (!Fixture(330, log, "alpha")(staged)) return false;
        staged.ConfigSections.front().Validate = [&log](const std::string_view document, std::string_view, std::string_view)
        {
            log.push_back("validated");
            return Validate(document);
        };
        return true;
    }, log));
    auto staged = Runtime::StageStartupPlugins(std::move(images), sections, errors);
    ASSERT_EQ(staged.size(), 1u);
    log.clear(); // the boot registry validated the default payload at staging
    {
        Runtime::PluginHostModule host{{.Startup = std::move(staged)}};
    }
    EXPECT_EQ(log, (Log{"shutdown"}));
    const auto* section = sections.Find("alpha");
    ASSERT_NE(section, nullptr);
    (void)section->Validate(R"({"value":1})", "test", "alpha");
    EXPECT_EQ(log, (Log{"shutdown"})) << "the section's validator calls nothing after the discard";
}

// The contract's run protocol: stages join the first stage's run, the waiting tool's outcome, its cancel hook.
namespace
{
    struct RunResult
    {
        std::string Status{};
    };

    std::string RunText(const RunResult& result) { return result.Status; }
}

TEST(PluginHost, RunStagesJoinTheFirstStagesRunAndTheWaitingToolsCancelEndsThem)
{
    WorkerState state{};
    Harness harness{};
    ASSERT_TRUE(harness.Loaded(340));
    const auto run = std::make_shared<Runtime::PluginRun<RunResult>>();
    std::string refusal;
    ASSERT_TRUE(Runtime::SubmitPluginRunStage(*run, LoopingJob("first", state), refusal)) << refusal;
    const auto first = run->Handle;
    ASSERT_TRUE(Runtime::SubmitPluginRunStage(*run, LoopingJob("second", state), refusal)) << refusal;
    EXPECT_EQ(run->Handle.Token, first.Token) << "the run keeps its first stage's handle";
    const auto jobs = harness.Jobs();
    const auto second = std::ranges::find_if(jobs, [](const auto& job) { return job.Name == "second"; });
    ASSERT_NE(second, jobs.end());
    EXPECT_EQ(second->Identity.Run, first.Token) << "a later stage joins the first stage's run";
    auto outcome = Runtime::WaitForPluginRun(run, &RunText);
    ASSERT_TRUE(outcome.Cancel);
    EXPECT_EQ(outcome.Cancel(harness.Context), "cancel requested");
    harness.Drain();
    for (const auto& job : harness.Jobs()) EXPECT_EQ(job.State, Runtime::JobState::Cancelled) << job.Name;
    EXPECT_EQ(outcome.Cancel(harness.Context), "the run is not active");
}

TEST(PluginHost, TheWaitingToolAnswersAtOnceOrWhenTheRunEnds)
{
    const auto ended = std::make_shared<Runtime::PluginRun<RunResult>>();
    ended->Final = RunResult{"applied"};
    const auto immediate = Runtime::WaitForPluginRun(ended, &RunText);
    EXPECT_EQ(immediate.Text, "applied");
    EXPECT_FALSE(immediate.IsError);
    EXPECT_FALSE(immediate.Continuation);
    const auto running = std::make_shared<Runtime::PluginRun<RunResult>>(Runtime::PluginRun<RunResult>{.Pending = {"pending"}});
    auto waiting = Runtime::WaitForPluginRun(running, &RunText);
    EXPECT_EQ(waiting.Text, "pending");
    ASSERT_TRUE(waiting.Continuation);
    Runtime::AgentOperationOutcome result{};
    EXPECT_FALSE(waiting.Continuation({}, result));
    running->Final = RunResult{"stale"};
    EXPECT_TRUE(waiting.Continuation({}, result));
    EXPECT_EQ(result.Text, "stale");
    EXPECT_TRUE(result.IsError) << "any status but applied is an error";
    EXPECT_EQ(result.ErrorCode, "stale");
    EXPECT_EQ(waiting.Cancel({}), "the run is not active") << "a run without a job";
}

// ARCH-022 Q89: a run its jobs do not finish (a host-driven GPU run) folds its state into Final through `refresh`,
// before every look at it, and ends through its own `cancel`.
TEST(PluginHost, TheWaitingToolRefreshesAHostDrivenRunAndCancelsItThroughItsHook)
{
    const auto run = std::make_shared<Runtime::PluginRun<RunResult>>(Runtime::PluginRun<RunResult>{.Pending = {"pending"}});
    bool ended = false, cancelled = false;
    unsigned refreshes = 0;
    const auto refresh = [&]
    {
        ++refreshes;
        if (ended) run->Final = RunResult{"applied"};
    };
    auto outcome = Runtime::WaitForPluginRun(run, &RunText, refresh, [&] { return !std::exchange(cancelled, true); });
    EXPECT_EQ(refreshes, 1u) << "refreshed before the first answer";
    EXPECT_EQ(outcome.Text, "pending");
    ASSERT_TRUE(outcome.Continuation);
    Runtime::AgentOperationOutcome result{};
    EXPECT_FALSE(outcome.Continuation({}, result));
    ended = true;
    EXPECT_TRUE(outcome.Continuation({}, result)) << "the refresh set Final";
    EXPECT_EQ(result.Text, "applied");
    EXPECT_FALSE(result.IsError);
    EXPECT_EQ(outcome.Cancel({}), "cancel requested") << "the hook, not CancelPluginRun (the run has no job)";
    EXPECT_EQ(outcome.Cancel({}), "the run is not active");
    const auto settled = std::make_shared<Runtime::PluginRun<RunResult>>();
    const auto immediate = Runtime::WaitForPluginRun(settled, &RunText, [&] { settled->Final = RunResult{"stale"}; });
    EXPECT_EQ(immediate.ErrorCode, "stale") << "a refresh that ends the run answers at once";
    EXPECT_FALSE(immediate.Continuation);
}
