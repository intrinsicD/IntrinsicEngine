// ARCH-022 slice 2, S1 (plugin-host spike): loads one plugin into the running Sandbox and commits its
// registrations atomically. Built only with INTRINSIC_PLUGIN_SPIKE.
module;

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
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
import Extrinsic.Runtime.Engine;

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

        struct LoadedPlugin
        {
            void* Handle{nullptr};
            ShutdownFn Shutdown{nullptr};
            bool Committed{false};
        };

        Runtime::AgentOperationRegistry* g_Operations{nullptr};
        LoadedPlugin g_Plugin{};

        Runtime::AgentOperationOutcome Fail(std::string code, std::string message)
        {
            return {.IsError = true, .Text = std::move(message), .ErrorCode = std::move(code)};
        }

        std::string Address(const void* pointer) { return std::format("{}", pointer); }

        template <typename Fn>
        std::string FunctionAddress(Fn function)
        {
            return Address(reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(function)));
        }

        // All or nothing: the originals are kept until every step succeeded; any failure restores both
        // registries and unregisters every window this commit added.
        std::optional<std::string> Commit(StagedRegistrations staged, Runtime::EngineConfigControl& config,
                                          Runtime::EditorUiHost& ui, std::string_view failAt)
        {
            Core::Config::EngineConfigSectionRegistry& sections = config.SectionRegistry();
            const Core::Config::EngineConfigSectionRegistry originalSections = sections;
            const Runtime::AgentOperationRegistry originalOperations = *g_Operations;
            std::vector<Runtime::EditorWindowHandle> added{};
            const auto rollback = [&](std::string reason) -> std::optional<std::string>
            {
                sections = originalSections;
                *g_Operations = originalOperations;
                for (const auto handle : added) (void)ui.UnregisterWindow(handle);
                return reason;
            };

            Core::Config::EngineConfigSectionRegistry sectionsCopy = sections;
            for (auto& section : staged.ConfigSections)
                if (!sectionsCopy.Register(std::move(section))) return rollback("config section rejected");
            if (failAt == "config") return rollback("injected failure after the config sections");

            Runtime::AgentOperationRegistry operationsCopy = *g_Operations;
            for (auto& operation : staged.Operations)
                if (!operationsCopy.Register(std::move(operation))) return rollback("operation rejected");
            if (failAt == "operations") return rollback("injected failure after the operations");

            for (auto& window : staged.Windows)
            {
                const auto handle = ui.RegisterWindow(std::move(window));
                if (!handle.IsValid()) return rollback("window rejected");
                added.push_back(handle);
            }
            if (failAt == "windows") return rollback("injected failure after the windows");

            sections = std::move(sectionsCopy);
            if (failAt == "replace") return rollback("injected failure between the two replacements");
            *g_Operations = std::move(operationsCopy);
            return std::nullopt;
        }

        Runtime::AgentOperationOutcome Load(Runtime::Engine& engine, std::string_view argumentsJson)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || !arguments.contains("path") || !arguments["path"].is_string())
                return Fail("invalid_params", "path is required");
            const std::string path = arguments["path"].get<std::string>();
            const std::string failAt = arguments.value("fail_at", std::string{});
            if (g_Plugin.Committed) return Fail("already_loaded", "a plugin is already loaded");

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
            const PluginHostContext host{.ImGuiContext = ImGui::GetCurrentContext()};
            if (failAt == "register" || !registerFn(host, staged, error))
                return Fail("register_failed", failAt == "register" ? "injected Register failure" : error);

            // The commit runs on a later frame, after this call's dispatch through the registry returned.
            Runtime::AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [&engine, staged = std::move(staged), failAt, handle, shutdownFn](
                const Runtime::AgentOperationContext& context, Runtime::AgentOperationOutcome& result) mutable
            {
                auto* ui = engine.Services().Find<Runtime::EditorUiHost>();
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
                g_Plugin = {.Handle = handle, .Shutdown = shutdownFn, .Committed = true};
                spike_lifecycle_event("commit");
                result = {.Text = R"({"loaded":true})"};
                return true;
            };
            return outcome;
        }

        Runtime::AgentOperationOutcome HostIdentity(Runtime::Engine& engine)
        {
            Json windows = Json::array();
            if (const auto* ui = engine.Services().Find<Runtime::EditorUiHost>())
                for (const auto& entry : ui->BuildWindowMenuModel()) windows.push_back(entry.Id);
            ImGuiMemAllocFunc alloc = nullptr;
            ImGuiMemFreeFunc free = nullptr;
            void* userData = nullptr;
            ImGui::GetAllocatorFunctions(&alloc, &free, &userData);
            const Json value = {
                {"counter", {{"address", Address(&HostCounter())}, {"value", HostCounter()}}},
                {"inline_static", {{"address", Address(&HostInlineStatic())}, {"value", HostInlineStatic()}}},
                {"thread_local", {{"address", Address(&HostThreadLocal())}, {"value", HostThreadLocal()}}},
                {"imgui_context", Address(ImGui::GetCurrentContext())},
                {"imgui_alloc", FunctionAddress(alloc)},
                {"imgui_free", FunctionAddress(free)},
                {"imgui_alloc_user_data", Address(userData)},
                {"implot_context", Address(ImPlot::GetCurrentContext())},
                {"windows", windows},
            };
            return {.Text = value.dump()};
        }
    }

    void RegisterHostOperations(Runtime::AgentOperationRegistry& registry, Runtime::Engine& engine)
    {
        g_Operations = &registry;
        (void)registry.Register({
            .Name = "plugin_load",
            .Title = "Load a plugin (ARCH-022 spike)",
            .Description = "dlopen a plugin and commit its registrations on the next frame. fail_at injects a "
                           "failure at dlopen, dlsym, register, config, operations, windows or replace.",
            .InputSchemaJson = R"({"type":"object","properties":{"path":{"type":"string"},"fail_at":{"type":"string"}},"required":["path"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [&engine](const Runtime::AgentOperationContext&, std::string_view arguments)
            { return Load(engine, arguments); },
        });
        (void)registry.Register({
            .Name = "plugin_host_identity",
            .Title = "Host identity probes (ARCH-022 spike)",
            .Description = "Addresses and values of the host's probes, ImGui context and allocator, ImPlot context, "
                           "registered window ids.",
            .Invoke = [&engine](const Runtime::AgentOperationContext&, std::string_view) { return HostIdentity(engine); },
        });
        (void)registry.Register({
            .Name = "app_quit",
            .Title = "Quit the Sandbox (ARCH-022 spike)",
            .Description = "Calls the loaded plugin's shutdown hook, then requests the engine's exit.",
            .ReadOnly = false,
            .Invoke = [&engine](const Runtime::AgentOperationContext&, std::string_view)
            {
                if (g_Plugin.Committed && g_Plugin.Shutdown != nullptr) g_Plugin.Shutdown();
                engine.RequestExit();
                return Runtime::AgentOperationOutcome{.Text = R"({"quitting":true})"};
            },
        });
    }
}
