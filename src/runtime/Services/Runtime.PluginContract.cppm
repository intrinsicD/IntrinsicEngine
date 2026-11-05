// ARCH-022: what a plugin may use. A plugin is a shared library built in the same tree as the engine; it exports
// the v1 entry points below, adds its registrations through them, and hands the host callables through these APIs
// only, so the host holds every one of them and can remove them all before it closes the image.
module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.PluginContract;

import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.UiHostTypes;

// Test instruments a plugin may call; they hold no host state, so they work before a load and through teardown.
// Appends one line to the file named by INTRINSIC_PLUGIN_EVENT_LOG.
export extern "C" void intrinsic_plugin_event(const char* event) noexcept;
// Callable accounting: a plugin that reports each copy (+1) and destruction (-1) of the callables it hands over, per
// generation and kind, and each invocation, also has a close refused while one of them is alive.
export extern "C" void intrinsic_plugin_callable(int generation, const char* kind, int delta) noexcept;
export extern "C" void intrinsic_plugin_invoked(int generation, const char* kind) noexcept;

export namespace Extrinsic::Runtime
{
    // An editor history command: the host registers it by name, so undo and redo entries hold data and find the
    // current plugin's code when they run. Returns false when it cannot apply.
    struct PluginHistoryCommand
    {
        std::string Name{};
        std::function<bool(std::string_view payload, bool undo)> Apply{};
    };

    // What a plugin's register entry adds; the host commits it atomically on a later frame.
    struct PluginRegistrations
    {
        std::string Name{}; // the plugin's name, unique among loaded plugins (plugin_unload, plugin_reload)
        int Generation{0}; // the plugin build's version, for callable accounting
        std::vector<Core::Config::EngineConfigSectionRegistration> ConfigSections{};
        std::vector<AgentOperationSpec> Operations{}; // a deferred result is held by the host
        std::vector<EditorWindowDescriptor> Windows{};
        std::vector<PluginHistoryCommand> HistoryCommands{};
    };

    // The v1 entry points (extern "C"; nothing throws across them). v1 means the same build: the build id
    // (INTRINSIC_PLUGIN_BUILD_ID from the generated PluginBuildId.h, which the plugin returns) covers the toolchain,
    // standard library, flags, definitions, build type, sanitizers, shared engine and every engine module interface.
    // The host refuses another id, and a missing entry point.
    using PluginRegisterFn = bool (*)(PluginRegistrations& staged, std::string& error) noexcept;
    using PluginShutdownFn = void (*)() noexcept;
    using PluginBuildIdFn = const char* (*)() noexcept;
    inline constexpr const char* kPluginRegisterSymbol = "intrinsic_plugin_register_v1";
    inline constexpr const char* kPluginShutdownSymbol = "intrinsic_plugin_shutdown_v1";
    inline constexpr const char* kPluginBuildIdSymbol = "intrinsic_plugin_build_id_v1";

    // Records an undo entry through the editor history without the plugin naming editor types: the entry holds
    // data and calls the registered command. Executes the redo first; false when it fails or no history exists.
    [[nodiscard]] bool RecordPluginCommand(const AgentOperationContext& context, std::string label,
                                           std::string command, std::string payload);

    enum class PluginJobDomain : std::uint8_t { Cpu, Gpu };

    // The output a job writes: it makes the job a row of the editor's Jobs window, and admission refuses a second
    // run on the same output.
    struct PluginJobOutput
    {
        std::uint32_t StableEntityId{0u};
        GeometryPropertyRef Property{};
    };

    struct PluginJobHandle
    {
        JobToken Token{};
        [[nodiscard]] bool IsValid() const noexcept { return Token.IsValid(); }
    };

    // A plugin's job. Work runs on a worker and polls `cancelled`. IsReadyToApply (optional) is polled on the main
    // thread before Completion. Completion runs on the main thread with false when the job was cancelled or its
    // run abandoned, and returns whether it published (the job's verdict). A later stage of a run joins it through
    // `Run`, the handle of the run's first job.
    struct PluginJob
    {
        std::string Label{};
        std::optional<PluginJobOutput> Output{};
        PluginJobHandle Run{};
        PluginJobDomain Domain{PluginJobDomain::Cpu};
        std::function<void(const std::function<bool()>& cancelled)> Work{};
        std::function<bool()> IsReadyToApply{};
        std::function<bool(bool applied)> Completion{};
    };

    // The host keeps a plugin's jobs and GPU work and drains them before an unload: jobs are cancelled and reaped,
    // GPU work (a queue participant that needs RecordFrameCommands, HasInFlightWork and ShutdownAfterDeviceIdle)
    // stops recording and is unregistered once retired. Invalid while draining, without a host or job service,
    // for incomplete callables, when another run is active on the same output (`refusal` says why), or when the
    // service rejects the work.
    [[nodiscard]] PluginJobHandle SubmitPluginJob(const AgentOperationContext& context, PluginJob job,
                                                  std::string* refusal = nullptr);
    // Cancels the job's run, as the Jobs window does; false when it is not active.
    bool CancelPluginJob(PluginJobHandle job);
    // What the run resolved to, for the Jobs window: the domain it ran in and its diagnostic.
    void ReportPluginJobOutcome(PluginJobHandle job, PluginJobDomain resolved, std::string diagnostic);
    [[nodiscard]] bool StartPluginGpuWork(const AgentOperationContext& context, GpuQueueParticipantDesc work);
    // The engine's services, for a plugin to find what it needs (the RHI device); null without a host.
    [[nodiscard]] ServiceRegistry* PluginServices() noexcept;
    // A host service the plugin uses, or null while the host does not offer it.
    template <class T>
    [[nodiscard]] T* PluginService()
    {
        auto* services = PluginServices();
        return services != nullptr ? services->Find<T>() : nullptr;
    }

    // A plugin's run of one or more stages, shared by its window and a waiting MCP call: `Final` is its one terminal
    // result, set on the main thread. `Handle` is the first stage's job; later stages join its run.
    template <class Result>
    struct PluginRun
    {
        Result Pending{};
        std::optional<Result> Final{};
        PluginJobHandle Handle{};
    };

    // Submits a stage of `run` (main thread): the first becomes the run's handle, a later one joins its run. False with
    // the host's reason in `refusal` (possibly empty) when it was refused.
    template <class Result>
    [[nodiscard]] bool SubmitPluginRunStage(PluginRun<Result>& run, PluginJob job, std::string& refusal)
    {
        job.Run = run.Handle; // invalid for the first stage
        const auto handle = SubmitPluginJob({}, std::move(job), &refusal);
        if (!run.Handle.IsValid()) run.Handle = handle;
        return handle.IsValid();
    }

    // Cancels the run's jobs, as the Jobs window does; false when nothing is active.
    template <class Result>
    bool CancelPluginRun(const PluginRun<Result>& run)
    {
        return run.Handle.IsValid() && CancelPluginJob(run.Handle);
    }

    // An MCP tool's outcome for a started run, whose texts are `text(result)`: the answer when it already ended, else
    // the pending result's text now, a continuation that answers once `Final` is set, and a cancel hook (MCP
    // notifications/cancelled). The answer succeeds when the result's Status is "applied"; any other status is the
    // error code. A run its jobs do not finish (a host-driven GPU run) passes `refresh`, which may set `Final` and runs
    // before each look at it, and `cancel` (true when a cancel was requested) in place of CancelPluginRun.
    template <class Result, class Text>
    [[nodiscard]] AgentOperationOutcome WaitForPluginRun(const std::shared_ptr<PluginRun<Result>>& run, Text text,
                                                         std::function<void()> refresh = {},
                                                         std::function<bool()> cancel = {})
    {
        const auto answer = [text](const Result& result)
        {
            const bool applied = result.Status == "applied";
            return AgentOperationOutcome{.IsError = !applied, .Text = text(result),
                                         .ErrorCode = applied ? std::string{} : result.Status};
        };
        if (refresh) refresh();
        if (run->Final) return answer(*run->Final);
        AgentOperationOutcome outcome{.Text = text(run->Pending)};
        outcome.Continuation = [run, answer, refresh](const AgentOperationContext&, AgentOperationOutcome& result)
        {
            if (refresh) refresh();
            if (!run->Final) return false;
            result = answer(*run->Final);
            return true;
        };
        outcome.Cancel = [run, cancel](const AgentOperationContext&)
        {
            const bool requested = cancel ? cancel() : CancelPluginRun(*run);
            return std::string{requested ? "cancel requested" : "the run is not active"};
        };
        return outcome;
    }
}

// Test instruments for the fixture plugin (plugins/fixture) and its drivers.
export namespace Extrinsic::Runtime::PluginProbes
{
    // One definition in the engine, reached from a plugin through imported code: the identity probes.
    [[nodiscard]] int& HostCounter() noexcept;
    [[nodiscard]] int& HostThreadLocal() noexcept;
    [[nodiscard]] inline int& HostInlineStatic() noexcept
    {
        static int value = 0;
        return value;
    }

    // Host state the fixture's undoable edits change; it survives a reload, as scene data does. Edits move it by at
    // most kMaxUndoDelta, which makes an overflow impractical.
    inline constexpr std::int64_t kMaxUndoDelta = 1'000'000;
    [[nodiscard]] std::int64_t& HostUndoValue() noexcept;

    // The closure form of an undo entry: it holds the plugin's closures, as editor entries do today (the negative
    // control for registered commands). False unless the Sandbox runs with --plugin-test-hooks.
    [[nodiscard]] bool RecordClosureCommand(const AgentOperationContext& context, std::string label,
                                            std::function<bool()> redo, std::function<bool()> undo);
}
