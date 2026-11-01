// ARCH-022 slice 2, S1 and S2 (plugin-host spike): the contract between the Sandbox and a dlopen'ed plugin.
// Built only with INTRINSIC_PLUGIN_SPIKE; the plugin imports this module from the host's BMIs.
module;

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Sandbox.PluginSpike;

import Extrinsic.Runtime.DerivedConfigSections; // re-exports Extrinsic.Core.Config.EngineLoad (app does not import core)
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.UiHostTypes;
import Extrinsic.Runtime.ServiceRegistry;

// Exported by the executable and usable before dlopen and through teardown (it holds no host state):
// appends one line to the file named by INTRINSIC_SPIKE_EVENT_LOG.
export extern "C" void spike_lifecycle_event(const char* event) noexcept;
// Callable accounting (S2): every callable a plugin hands to the host reports each copy (+1) and destruction (-1)
// per generation and kind, and each invocation; the host closes a generation only when none is alive.
export extern "C" void spike_callable(int generation, const char* kind, int delta) noexcept;
export extern "C" void spike_invoked(int generation, const char* kind) noexcept;

export namespace Extrinsic::Sandbox::Spike
{
    // A history command (S2b): the host registers it by name with the editor history, so undo and redo entries
    // hold data and find the current plugin's code when they run. Returns false when it cannot apply.
    struct HistoryCommand
    {
        std::string Name{};
        std::function<bool(std::string_view payload, bool undo)> Apply{};
    };

    // What a plugin's Register adds; the host commits it atomically after the MCP call returned.
    struct StagedRegistrations
    {
        int Generation{0}; // the plugin build's version, for callable accounting
        std::vector<Core::Config::EngineConfigSectionRegistration> ConfigSections{};
        std::vector<Runtime::AgentOperationSpec> Operations{};
        std::vector<Runtime::EditorWindowDescriptor> Windows{};
        std::vector<HistoryCommand> HistoryCommands{};
    };

    // extern "C" entry points of a plugin; nothing throws across them (-fno-exceptions).
    using RegisterFn = bool (*)(StagedRegistrations& staged, std::string& error) noexcept;
    using ShutdownFn = void (*)() noexcept;
    inline constexpr const char* kRegisterSymbol = "intrinsic_plugin_register_v0";
    inline constexpr const char* kShutdownSymbol = "intrinsic_plugin_shutdown_v0";

    // Identity probes: one definition in the executable, reached from the plugin through imported code.
    [[nodiscard]] int& HostCounter() noexcept;
    [[nodiscard]] int& HostThreadLocal() noexcept;
    [[nodiscard]] inline int& HostInlineStatic() noexcept
    {
        static int value = 0;
        return value;
    }

    // Host state a plugin's undoable edits change (S2b); it survives a reload, as scene data does. Edits move it by
    // at most kMaxUndoDelta, which makes an overflow impractical.
    inline constexpr std::int64_t kMaxUndoDelta = 1'000'000;
    [[nodiscard]] std::int64_t& HostUndoValue() noexcept;

    // Records an undo entry through the editor history without the plugin naming editor types. The command form
    // holds data and calls the registered command; the closure form holds the plugin's closures, as today's
    // entries do (the S2b control). Both execute the redo first; false when it fails or no history is available.
    [[nodiscard]] bool RecordCommand(const Runtime::AgentOperationContext& context, std::string label,
                                     std::string command, std::string payload);
    [[nodiscard]] bool RecordClosureCommand(const Runtime::AgentOperationContext& context, std::string label,
                                            std::function<bool()> redo, std::function<bool()> undo);

    // A plugin's CPU job (S2c): Work runs on a worker and polls `cancelled`; Completion runs on the main thread,
    // with false when the job was cancelled.
    struct PluginJob
    {
        std::function<void(const std::function<bool()>& cancelled)> Work{};
        std::function<void(bool published)> Completion{};
    };

    // The host keeps a plugin's jobs and GPU work on the main thread and drains them before an unload: jobs are
    // cancelled and reaped, GPU work (a queue participant that needs RecordFrameCommands, HasInFlightWork and
    // ShutdownAfterDeviceIdle) stops recording and is unregistered once retired. False while draining, without a
    // job service, for incomplete callables, or when the service rejects the work.
    [[nodiscard]] bool SubmitPluginJob(const Runtime::AgentOperationContext& context, PluginJob job);
    [[nodiscard]] bool StartPluginGpuWork(const Runtime::AgentOperationContext& context,
                                          Runtime::GpuQueueParticipantDesc work);
    // The engine's services, for a plugin to find what it needs (the RHI device).
    [[nodiscard]] Runtime::ServiceRegistry& Services() noexcept;

    // Adds plugin_load, plugin_unload, plugin_reload, plugin_host_identity and app_quit; keeps the registry to
    // commit and remove plugin tools later.
    void RegisterHostOperations(Runtime::AgentOperationRegistry& registry, Runtime::ServiceRegistry& services,
                                std::function<void()> requestExit);
}
