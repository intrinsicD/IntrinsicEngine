// ARCH-022: the plugin host's contract, driven with in-process images (no dlopen), so it runs in static builds too.
// dlopen itself is covered by the fixture plugin's drivers on the shared dev build (plugins/spike).
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
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
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

        explicit Harness(const bool testHooks = true, const bool inProcess = true,
                         std::function<void(Runtime::RuntimeModuleShutdownContext&)> probe = {})
            : Engine(HeadlessConfig(), std::make_unique<Intrinsic::Tests::OneFrameApplication>())
        {
            Engine.EmplaceModule<Runtime::EngineConfigControl>();
            Engine.EmplaceModule<Runtime::EditorUiModule>();
            Engine.EmplaceModule<Runtime::SceneDocumentModule>();
            if (probe) Engine.AddModule(std::make_unique<ShutdownProbe>(std::move(probe)));
            Runtime::PluginHostOptions options{.TestHooks = testHooks};
            if (inProcess)
                options.Open = [this](const std::string&, std::string_view, Runtime::AgentOperationOutcome&)
                { return std::make_unique<FixtureImage>(Next, Events); };
            auto host = std::make_unique<Runtime::PluginHostModule>(std::move(options));
            Host = host.get();
            Engine.AddModule(std::move(host));
            Engine.Initialize();
            Host->RegisterOperations(Registry);
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
            for (const auto& entry : Engine.Services().Find<Runtime::EditorUiHost>()->BuildWindowMenuModel())
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
            .Work = [started](const std::function<bool()>& cancelled)
            {
                *started = true;
                while (!cancelled()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            },
            .Completion = [&log](const bool published) { log.push_back(published ? "job_published" : "job_finalized"); },
        });
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

TEST(PluginHost, ADeferredPluginResultIsRefusedAndItsCallablesReleasedAtOnce)
{
    Harness harness{};
    const auto held = std::make_shared<int>(0);
    const std::weak_ptr<int> watch = held;
    using Progress = decltype(Runtime::AgentOperationOutcome::Progress)::result_type;
    // Each operation defers through one of the three callables the agent server would keep.
    const std::vector<std::pair<std::string, std::function<void(Runtime::AgentOperationOutcome&)>>> deferrals{
        {"fixture_continuation", [held](Runtime::AgentOperationOutcome& outcome)
         {
             outcome.Continuation = [held](const Runtime::AgentOperationContext&, Runtime::AgentOperationOutcome&)
             {
                 (void)held;
                 return true;
             };
         }},
        {"fixture_progress", [held](Runtime::AgentOperationOutcome& outcome)
         {
             outcome.Progress = [held](const Runtime::AgentOperationContext&)
             {
                 (void)held;
                 return Progress{};
             };
         }},
        {"fixture_cancel", [held](Runtime::AgentOperationOutcome& outcome)
         {
             outcome.Cancel = [held](const Runtime::AgentOperationContext&)
             {
                 (void)held;
                 return std::string{};
             };
         }},
    };
    harness.Next = [&deferrals](Runtime::PluginRegistrations& staged)
    {
        staged.Generation = 107;
        for (const auto& [name, defer] : deferrals)
            staged.Operations.push_back({
                .Name = name,
                .Invoke = [defer](const Runtime::AgentOperationContext&, std::string_view)
                {
                    Runtime::AgentOperationOutcome outcome{};
                    defer(outcome);
                    return outcome;
                },
            });
        return true;
    };
    ASSERT_FALSE(harness.Call("plugin_load", {{"path", "fixture"}}).IsError);
    const long before = watch.use_count(); // the handles of the test, the deferrals and the registered operations
    for (const auto& [name, defer] : deferrals)
    {
        const auto reply = Runtime::InvokeAgentOperation(harness.Registry, name, harness.Context, "{}", false);
        EXPECT_EQ(reply.ErrorCode, "deferred_unsupported") << name;
        EXPECT_FALSE(reply.Continuation || reply.Progress || reply.Cancel) << name;
        EXPECT_EQ(watch.use_count(), before) << name; // the deferred callable's copy is gone
    }
}

TEST(PluginHost, TestHooksAreRejectedWithoutTheFlag)
{
    Harness plain{false};
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
    Harness hooked{true};
    EXPECT_NE(hooked.Registry.Find("plugin_host_identity"), nullptr);
    EXPECT_TRUE(Runtime::PluginProbes::RecordClosureCommand(hooked.Context, "closure", [] { return true; },
                                                            [] { return true; }));
}

TEST(PluginHost, AStaticBuildAnswersPluginsUnsupported)
{
    Harness harness{true, false};
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
    Harness harness{true, true, [&](Runtime::RuntimeModuleShutdownContext&)
                    {
                        clean = self->NothingRegistered();
                        events->push_back("host_done");
                    }};
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
