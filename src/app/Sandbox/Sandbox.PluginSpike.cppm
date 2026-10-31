// ARCH-022 slice 2, S1 and S2 (plugin-host spike): the contract between the Sandbox and a dlopen'ed plugin.
// Built only with INTRINSIC_PLUGIN_SPIKE; the plugin imports this module from the host's BMIs.
module;

#include <functional>
#include <string>
#include <vector>

export module Extrinsic.Sandbox.PluginSpike;

import Extrinsic.Runtime.DerivedConfigSections; // re-exports Extrinsic.Core.Config.EngineLoad (app does not import core)
import Extrinsic.Runtime.AgentOperations;
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
    // What a plugin's Register adds; the host commits it atomically after the MCP call returned.
    struct StagedRegistrations
    {
        int Generation{0}; // the plugin build's version, for callable accounting
        std::vector<Core::Config::EngineConfigSectionRegistration> ConfigSections{};
        std::vector<Runtime::AgentOperationSpec> Operations{};
        std::vector<Runtime::EditorWindowDescriptor> Windows{};
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

    // Adds plugin_load, plugin_unload, plugin_reload, plugin_host_identity and app_quit; keeps the registry to
    // commit and remove plugin tools later.
    void RegisterHostOperations(Runtime::AgentOperationRegistry& registry, Runtime::ServiceRegistry& services,
                                std::function<void()> requestExit);
}
