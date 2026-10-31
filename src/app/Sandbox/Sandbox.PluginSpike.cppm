// ARCH-022 slice 2, S1 (plugin-host spike): the contract between the Sandbox and a dlopen'ed plugin.
// Built only with INTRINSIC_PLUGIN_SPIKE; the plugin imports this module from the host's BMIs.
module;

#include <string>
#include <vector>

export module Extrinsic.Sandbox.PluginSpike;

import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.Engine;

// Exported by the executable and usable before dlopen and through teardown (it holds no host state):
// appends one line to the file named by INTRINSIC_SPIKE_EVENT_LOG.
export extern "C" void spike_lifecycle_event(const char* event) noexcept;

export namespace Extrinsic::Sandbox::Spike
{
    // What a plugin's Register adds; the host commits it atomically after the MCP call returned.
    struct StagedRegistrations
    {
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

    // Adds plugin_load, plugin_host_identity and app_quit; keeps the registry to commit plugin tools later.
    void RegisterHostOperations(Runtime::AgentOperationRegistry& registry, Runtime::Engine& engine);
}
