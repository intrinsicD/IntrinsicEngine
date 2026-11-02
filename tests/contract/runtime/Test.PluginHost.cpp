// ARCH-022: the plugin host's contract, driven with in-process images (no dlopen), so it runs in static builds too.
// dlopen itself is covered by the fixture plugin's drivers on the shared dev build (plugins/spike).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
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
        FixtureImage(std::function<bool(Runtime::PluginRegistrations&)> registerFn, Log& log)
            : m_Register(std::move(registerFn)), m_Log(log) {}
        bool Register(Runtime::PluginRegistrations& staged, std::string& error) noexcept override
        {
            if (m_Register(staged)) return true;
            error = "the fixture refused";
            return false;
        }
        void Shutdown() noexcept override { m_Log.push_back("shutdown"); }
        void Close() noexcept override { m_Log.push_back("close"); }
        const std::filesystem::path& File() const noexcept override { return m_File; }

    private:
        std::function<bool(Runtime::PluginRegistrations&)> m_Register;
        Log& m_Log;
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

    // A fixture plugin: config section "fixture" ({"value": N}), tool fixture_echo, window view.fixture and history
    // command fixture.add. Its callables log what they receive.
    std::function<bool(Runtime::PluginRegistrations&)> Fixture(const int generation, Log& log)
    {
        return [generation, &log](Runtime::PluginRegistrations& staged)
        {
            staged.Generation = generation;
            staged.ConfigSections.push_back({
                .DefaultSection = {.Name = "fixture", .SchemaId = "test.fixture", .SchemaVersion = 1u,
                                   .PayloadJson = R"({"value":0})"},
                .Validate = [](const std::string_view document, std::string_view, std::string_view)
                { return Validate(document); },
                .OnChanged = [&log](const Config::EngineConfigSection&, const Config::EngineConfigSection& current)
                { log.push_back("changed " + current.PayloadJson); },
            });
            staged.Operations.push_back({
                .Name = "fixture_echo",
                .Invoke = [t = Tracked{generation}](const Runtime::AgentOperationContext&, std::string_view)
                { return Runtime::AgentOperationOutcome{.Text = R"({"echo":true})"}; },
            });
            staged.Windows.push_back({.Id = "view.fixture", .MenuPath = {"View"}, .Title = "Fixture", .Draw = [](bool&) {}});
            staged.HistoryCommands.push_back({.Name = "fixture.add", .Apply = [](std::string_view, bool) { return true; }});
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
            std::optional<Runtime::StagedPlugin> Startup{};
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
                hostOptions.Open = [this](const std::string&, std::string_view, Runtime::AgentOperationOutcome&)
                { return std::make_unique<FixtureImage>(Next, Events); };
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
    EXPECT_EQ(listed["loaded"]["generation"], 101);
    EXPECT_EQ(listed["loaded"]["config_sections"], Json::array({"fixture"}));

    const auto unloaded = harness.Call("plugin_unload");
    ASSERT_FALSE(unloaded.IsError) << unloaded.Text;
    EXPECT_TRUE(harness.NothingRegistered());
    EXPECT_EQ(harness.Events, (Log{"shutdown", "close"}));
    EXPECT_EQ(Json::parse(harness.Call("plugin_list").Text)["loaded"], nullptr);
}

TEST(PluginHost, AFailedCommitRollsEveryRegistrationBackAndClosesTheImage)
{
    Harness harness{};
    harness.Next = Fixture(102, harness.Events);
    const auto reply = harness.Call("plugin_load", {{"path", "fixture"}, {"fail_at", "windows"}});
    EXPECT_EQ(reply.ErrorCode, "commit_failed");
    EXPECT_TRUE(harness.NothingRegistered());
    EXPECT_EQ(harness.Events, (Log{"close"}));
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
    std::atomic<bool> release{false};
    Harness harness{};
    const std::unique_ptr<std::atomic<bool>, void (*)(std::atomic<bool>*)> releaseOnExit{
        &release, [](std::atomic<bool>* flag) { *flag = true; }};
    ASSERT_TRUE(harness.Loaded(125));
    const auto run = Runtime::SubmitPluginJob(harness.Context, {
        .Label = "Stubborn job",
        .Output = OutputOf(7, "a"),
        .Work = [&release](const std::function<bool()>&) { while (!release) std::this_thread::sleep_for(std::chrono::milliseconds(1)); },
        .Completion = [](const bool published) { return published; },
    });
    ASSERT_TRUE(run.IsValid());
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
    std::pair<Runtime::StagedPlugin, Config::EngineConfigSectionRegistry> StageFixture(Log& log)
    {
        g_StartupEvents = &log;
        std::string error;
        auto staged = Runtime::StagePlugin(Runtime::MakeInProcessPlugin(&StartupRegister, &StartupShutdown), error);
        EXPECT_TRUE(staged.has_value()) << error;
        Config::EngineConfigSectionRegistry sections;
        for (auto& section : staged->Registrations.ConfigSections) EXPECT_TRUE(sections.Register(std::move(section)));
        staged->Registrations.ConfigSections.clear();
        return {std::move(*staged), std::move(sections)};
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
