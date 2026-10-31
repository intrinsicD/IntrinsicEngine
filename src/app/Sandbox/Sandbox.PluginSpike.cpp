// ARCH-022 slice 2, S1 (plugin-host spike): loads one plugin into the running Sandbox and commits its
// registrations atomically. Built only with INTRINSIC_PLUGIN_SPIKE.
module;

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <dlfcn.h>
#include <imgui.h>
#include <implot.h>
#include <nlohmann/json.hpp>

module Extrinsic.Sandbox.PluginSpike;

import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ServiceRegistry;

extern "C" void spike_lifecycle_event(const char* event) noexcept
{
    const char* path = std::getenv("INTRINSIC_SPIKE_EVENT_LOG");
    if (path == nullptr || event == nullptr) return;
    if (std::FILE* file = std::fopen(path, "a"))
    {
        std::fprintf(file, "%s\n", event);
        std::fclose(file);
    }
}

namespace Extrinsic::Sandbox::Spike
{
    int& HostCounter() noexcept
    {
        static int value = 0;
        return value;
    }

    int& HostThreadLocal() noexcept
    {
        thread_local int value = 0;
        return value;
    }

    namespace
    {
        using Json = nlohmann::json;

        Runtime::AgentOperationRegistry* g_Operations{nullptr};
        ShutdownFn g_Shutdown{nullptr}; // set once a plugin is committed; S1 never unloads it
        bool g_LoadPending{false};      // a load whose commit has not finished (or been dropped) yet

        Runtime::AgentOperationOutcome Fail(std::string code, std::string message)
        {
            return {.IsError = true, .Text = std::move(message), .ErrorCode = std::move(code)};
        }

        template <typename T>
        std::uintptr_t Address(T* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }

        // All or nothing: the original config sections are kept until both registries are replaced; any
        // failure restores them and unregisters every window this commit added. The operation registry is
        // replaced last, so a failure never has to restore it.
        std::optional<std::string> Commit(StagedRegistrations staged, Runtime::EngineConfigControl& config,
                                          Runtime::EditorUiHost& ui, std::string_view failAt)
        {
            Core::Config::EngineConfigSectionRegistry& sections = config.SectionRegistry();
            const Core::Config::EngineConfigSectionRegistry originalSections = sections;
            std::vector<Runtime::EditorWindowHandle> added{};
            const auto rollback = [&](std::string reason) -> std::optional<std::string>
            {
                sections = originalSections;
                for (const auto handle : added) (void)ui.UnregisterWindow(handle);
                return reason;
            };

            Core::Config::EngineConfigSectionRegistry sectionsCopy = sections;
            for (auto& section : staged.ConfigSections)
                if (!sectionsCopy.Register(std::move(section))) return rollback("config: section rejected");
            if (failAt == "config") return rollback("config: injected failure after the config sections");

            Runtime::AgentOperationRegistry operationsCopy = *g_Operations;
            for (auto& operation : staged.Operations)
                if (!operationsCopy.Register(std::move(operation))) return rollback("operations: operation rejected");
            if (failAt == "operations") return rollback("operations: injected failure after the operations");

            for (auto& window : staged.Windows)
            {
                const auto handle = ui.RegisterWindow(std::move(window));
                if (!handle.IsValid()) return rollback("windows: window rejected");
                added.push_back(handle);
            }
            if (failAt == "windows") return rollback("windows: injected failure after the windows");

            sections = std::move(sectionsCopy);
            if (failAt == "replace") return rollback("replace: injected failure between the two replacements");
            *g_Operations = std::move(operationsCopy);
            return std::nullopt;
        }

        Runtime::AgentOperationOutcome Load(Runtime::ServiceRegistry& services, std::string_view argumentsJson)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || !arguments.contains("path") || !arguments["path"].is_string())
                return Fail("invalid_params", "path is required");
            const std::string path = arguments["path"].get<std::string>();
            const std::string failAt = arguments.value("fail_at", std::string{});
            if (g_Shutdown != nullptr) return Fail("already_loaded", "a plugin is already loaded");
            if (g_LoadPending) return Fail("load_pending", "another load has not committed yet");

            void* handle = failAt == "dlopen" ? nullptr : dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
            if (handle == nullptr)
            {
                const char* error = dlerror();
                return Fail("dlopen_failed", failAt == "dlopen" ? "injected dlopen failure" : (error ? error : "dlopen"));
            }
            // Never dlclose'd in S1: a failed attempt keeps the library mapped, so no callback can outlive its code.
            auto* registerFn = failAt == "dlsym" ? nullptr : reinterpret_cast<RegisterFn>(dlsym(handle, kRegisterSymbol));
            auto* shutdownFn = reinterpret_cast<ShutdownFn>(dlsym(handle, kShutdownSymbol));
            if (registerFn == nullptr || shutdownFn == nullptr) return Fail("dlsym_failed", "entry points missing");

            StagedRegistrations staged{};
            std::string error{};
            if (failAt == "register" || !registerFn(staged, error))
                return Fail("register_failed", failAt == "register" ? "injected Register failure" : error);

            // The commit runs on a later frame, after this call's dispatch through the registry returned. The
            // reservation ends when the continuation is destroyed: after it completed, or when the call is dropped.
            g_LoadPending = true;
            const std::shared_ptr<void> reservation(nullptr, [](void*) { g_LoadPending = false; });
            // Spike test hook: a second load in the same dispatch must be refused while this one is pending.
            const std::string second = arguments.value("probe_pending", false)
                ? Load(services, Json{{"path", path}}.dump()).ErrorCode : std::string{};
            Runtime::AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [&services, staged = std::move(staged), failAt, shutdownFn, reservation, second](
                const Runtime::AgentOperationContext& context, Runtime::AgentOperationOutcome& result) mutable
            {
                auto* ui = services.Find<Runtime::EditorUiHost>();
                if (ui == nullptr || context.ConfigControl == nullptr)
                {
                    result = Fail("host_unavailable", "editor UI host or config control missing");
                    return true;
                }
                if (const auto failure = Commit(std::move(staged), *context.ConfigControl, *ui, failAt))
                {
                    result = Fail("commit_failed", *failure);
                    return true;
                }
                g_Shutdown = shutdownFn;
                spike_lifecycle_event("commit");
                result = {.Text = Json{{"loaded", true}, {"second_attempt", second}}.dump()};
                return true;
            };
            return outcome;
        }

        Runtime::AgentOperationOutcome HostIdentity(Runtime::ServiceRegistry& services)
        {
            Json windows = Json::array();
            if (const auto* ui = services.Find<Runtime::EditorUiHost>())
                for (const auto& entry : ui->BuildWindowMenuModel())
                    windows.push_back({{"id", entry.Id}, {"open", entry.Open}});
            ImGuiMemAllocFunc alloc = nullptr;
            ImGuiMemFreeFunc free = nullptr;
            void* userData = nullptr;
            ImGui::GetAllocatorFunctions(&alloc, &free, &userData);
            const Json value = {
                {"counter", {{"address", Address(&HostCounter())}, {"value", HostCounter()}}},
                {"inline_static", {{"address", Address(&HostInlineStatic())}, {"value", HostInlineStatic()}}},
                {"thread_local", {{"address", Address(&HostThreadLocal())}, {"value", HostThreadLocal()}}},
                {"imgui_context", Address(ImGui::GetCurrentContext())},
                {"imgui_alloc", reinterpret_cast<std::uintptr_t>(alloc)},
                {"imgui_free", reinterpret_cast<std::uintptr_t>(free)},
                {"imgui_alloc_user_data", Address(userData)},
                {"implot_context", Address(ImPlot::GetCurrentContext())},
                {"windows", windows},
            };
            return {.Text = value.dump()};
        }
    }

    void RegisterHostOperations(Runtime::AgentOperationRegistry& registry, Runtime::ServiceRegistry& services,
                                std::function<void()> requestExit)
    {
        g_Operations = &registry;
        (void)registry.Register({
            .Name = "plugin_load",
            .Title = "Load a plugin (ARCH-022 spike)",
            .Description = "dlopen a plugin and commit its registrations on the next frame. fail_at injects a "
                           "failure at dlopen, dlsym, register, config, operations, windows or replace; "
                           "probe_pending tries a second load in the same dispatch and reports its error code.",
            .InputSchemaJson = R"({"type":"object","properties":{"path":{"type":"string"},"fail_at":{"type":"string"},"probe_pending":{"type":"boolean"}},"required":["path"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [&services](const Runtime::AgentOperationContext&, std::string_view arguments)
            { return Load(services, arguments); },
        });
        (void)registry.Register({
            .Name = "plugin_host_identity",
            .Title = "Host identity probes (ARCH-022 spike)",
            .Description = "Addresses and values of the host's probes, ImGui context and allocator, ImPlot context, "
                           "registered windows with their open state.",
            .Invoke = [&services](const Runtime::AgentOperationContext&, std::string_view) { return HostIdentity(services); },
        });
        (void)registry.Register({
            .Name = "app_quit",
            .Title = "Quit the Sandbox (ARCH-022 spike)",
            .Description = "Calls the loaded plugin's shutdown hook, then requests the engine's exit.",
            .ReadOnly = false,
            .Invoke = [requestExit = std::move(requestExit)](const Runtime::AgentOperationContext&, std::string_view)
            {
                if (g_Shutdown != nullptr) g_Shutdown();
                requestExit();
                return Runtime::AgentOperationOutcome{.Text = R"({"quitting":true})"};
            },
        });
    }
}
