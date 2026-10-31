// ARCH-022 slice 2 (plugin-host spike): loads, unloads and reloads one plugin in the running Sandbox. Built only
// with INTRINSIC_PLUGIN_SPIKE. S1: a load commits its registrations atomically. S2: every callable a plugin hands
// over is counted per generation, and an image is closed only when none of its callables is alive.
module;

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <dlfcn.h>
#include <imgui.h>
#include <implot.h>
#include <nlohmann/json.hpp>
#include <unistd.h>

module Extrinsic.Sandbox.PluginSpike;

import Extrinsic.Runtime.DerivedConfigSections; // re-exports Extrinsic.Core.Config.EngineLoad (app does not import core)
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ServiceRegistry;

namespace
{
    using CallableCounts = std::map<int, std::map<std::string, long>>; // generation -> kind -> count

    // Leaked on purpose: plugin callables may still report while the executable's statics are destroyed.
    std::mutex& CallableMutex()
    {
        static auto* mutex = new std::mutex();
        return *mutex;
    }
    CallableCounts& Live()
    {
        static auto* counts = new CallableCounts();
        return *counts;
    }
    CallableCounts& Invoked()
    {
        static auto* counts = new CallableCounts();
        return *counts;
    }
}

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

extern "C" void spike_callable(const int generation, const char* kind, const int delta) noexcept
{
    const std::lock_guard lock(CallableMutex());
    Live()[generation][kind] += delta;
}

extern "C" void spike_invoked(const int generation, const char* kind) noexcept
{
    const std::lock_guard lock(CallableMutex());
    ++Invoked()[generation][kind];
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

    std::int64_t& HostUndoValue() noexcept
    {
        static std::int64_t value = 0;
        return value;
    }

    bool RecordCommand(const Runtime::AgentOperationContext& context, std::string label, std::string command,
                       std::string payload)
    {
        if (context.History == nullptr) return false;
        return context.History->Execute(context.History->MakeRegisteredCommandRecord(
            std::move(label), std::move(command), std::move(payload))).Succeeded();
    }

    bool RecordClosureCommand(const Runtime::AgentOperationContext& context, std::string label,
                              std::function<bool()> redo, std::function<bool()> undo)
    {
        if (context.History == nullptr) return false;
        using Status = Runtime::EditorCommandHistoryStatus;
        return context.History->Execute({
            .Label = std::move(label),
            .Redo = [redo = std::move(redo)] { return redo() ? Status::Applied : Status::CommandFailed; },
            .Undo = [undo = std::move(undo)]
            {
                spike_lifecycle_event("closure_undo"); // attributes the S2b control's crash to this call
                return undo() ? Status::Undone : Status::UndoFailed;
            },
        }).Succeeded();
    }

    namespace
    {
        using Json = nlohmann::json;

        // The plugin that is loaded and committed; everything it registered, by name or handle.
        struct LoadedPlugin
        {
            void* Handle{nullptr};
            std::filesystem::path Copy{};
            int Generation{0};
            ShutdownFn Shutdown{nullptr};
            std::vector<std::string> Operations{};
            std::vector<std::string> Sections{};
            std::vector<Runtime::EditorWindowHandle> Windows{};
            std::vector<std::string> HistoryCommands{};
        };

        // An opened image whose Register succeeded; committed on a later frame.
        struct Candidate
        {
            void* Handle{nullptr};
            std::filesystem::path Copy{};
            ShutdownFn Shutdown{nullptr};
            StagedRegistrations Staged{};
        };

        bool Close(void* handle, const std::filesystem::path& copy, std::optional<int> generation);

        // Owns a pending candidate's image: a load whose continuation is dropped (the caller disconnected) or
        // cannot run closes the image here. Finish takes the candidate out, and with it the ownership.
        std::shared_ptr<Candidate> Own(Candidate candidate)
        {
            return {new Candidate(std::move(candidate)), [](Candidate* pending)
                    {
                        if (pending->Handle != nullptr)
                        {
                            const int generation = pending->Staged.Generation;
                            pending->Staged = {};
                            (void)Close(pending->Handle, pending->Copy, generation);
                        }
                        delete pending;
                    }};
        }

        Runtime::AgentOperationRegistry* g_Operations{nullptr};
        std::optional<LoadedPlugin> g_Plugin{};
        bool g_LoadPending{false}; // a load, unload or reload whose continuation has not finished (or been dropped)
        int g_Copies{0};
        Json g_Closes = Json::array(); // one record per close attempt: generation, live counts, closed, unmapped
        // Active payloads of an unloaded plugin's sections, reapplied when a plugin registers them again.
        std::map<std::string, Core::Config::EngineConfigSection> g_SavedSections{};

        Runtime::AgentOperationOutcome Fail(std::string code, std::string message)
        {
            return {.IsError = true, .Text = std::move(message), .ErrorCode = std::move(code)};
        }

        template <typename T>
        std::uintptr_t Address(T* pointer) { return reinterpret_cast<std::uintptr_t>(pointer); }

        Json CountsJson(const CallableCounts& counts)
        {
            const std::lock_guard lock(CallableMutex());
            Json value = Json::object();
            for (const auto& [generation, kinds] : counts)
                for (const auto& [kind, count] : kinds) value[std::to_string(generation)][kind] = count;
            return value;
        }

        // Every load opens its own copy, so a rebuilt file loads even while an older image stays mapped.
        std::optional<std::filesystem::path> PrivateCopy(const std::string& path, std::string& error)
        {
            std::error_code ec{};
            const auto dir = std::filesystem::temp_directory_path(ec) / ("intrinsic-spike-" + std::to_string(getpid()));
            std::filesystem::create_directories(dir, ec);
            const auto copy = dir / ("spike-" + std::to_string(++g_Copies) + ".so");
            if (!ec) std::filesystem::copy_file(path, copy, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = path + ": " + ec.message();
                return std::nullopt;
            }
            return copy;
        }

        // The first start and last end of the file's mappings (the kernel lists them in ascending order), as hex
        // strings; empty when it is not mapped.
        Json MappedRange(const std::filesystem::path& file)
        {
            std::ifstream maps("/proc/self/maps");
            std::string low{}, high{};
            for (std::string line; std::getline(maps, line);)
            {
                if (line.find(file.string()) == std::string::npos) continue;
                const auto dash = line.find('-');
                if (low.empty()) low = "0x" + line.substr(0, dash);
                high = "0x" + line.substr(dash + 1, line.find(' ') - dash - 1);
            }
            return low.empty() ? Json::array() : Json::array({low, high});
        }

        // Closes an image only when no callable of its generation is alive (otherwise it stays mapped, so no
        // callable can outlive its code); records the attempt either way.
        bool Close(void* handle, const std::filesystem::path& copy, const std::optional<int> generation)
        {
            Json live = Json::object();
            bool alive = false;
            if (generation)
            {
                const std::lock_guard lock(CallableMutex());
                for (const auto& [kind, count] : Live()[*generation])
                {
                    live[kind] = count;
                    alive = alive || count != 0;
                }
            }
            Json record{{"generation", generation ? Json(*generation) : Json(nullptr)}, {"live", live},
                        {"closed", !alive}, {"copy", copy.string()}};
            if (!alive)
            {
                record["range"] = MappedRange(copy); // where the image was, to attribute a later fault
                dlclose(handle);
                record["unmapped"] = MappedRange(copy).empty();
                std::error_code ec{};
                std::filesystem::remove(copy, ec);
            }
            g_Closes.push_back(std::move(record));
            return !alive;
        }

        // dlopen, dlsym and Register; on failure the image is closed again and `failure` says why.
        std::optional<Candidate> Open(const std::string& path, const std::string_view failAt,
                                      Runtime::AgentOperationOutcome& failure)
        {
            std::string error{};
            const auto copy = PrivateCopy(path, error);
            if (!copy)
            {
                failure = Fail("copy_failed", error);
                return std::nullopt;
            }
            void* handle = failAt == "dlopen" ? nullptr : dlopen(copy->c_str(), RTLD_NOW | RTLD_LOCAL);
            if (handle == nullptr)
            {
                const char* reason = dlerror();
                std::error_code ec{};
                std::filesystem::remove(*copy, ec);
                failure = Fail("dlopen_failed", failAt == "dlopen" ? "injected dlopen failure" : (reason ? reason : "dlopen"));
                return std::nullopt;
            }
            auto* registerFn = failAt == "dlsym" ? nullptr : reinterpret_cast<RegisterFn>(dlsym(handle, kRegisterSymbol));
            auto* shutdownFn = reinterpret_cast<ShutdownFn>(dlsym(handle, kShutdownSymbol));
            if (registerFn == nullptr || shutdownFn == nullptr)
            {
                (void)Close(handle, *copy, std::nullopt);
                failure = Fail("dlsym_failed", "entry points missing");
                return std::nullopt;
            }
            Candidate candidate{.Handle = handle, .Copy = *copy, .Shutdown = shutdownFn};
            if (!registerFn(candidate.Staged, error))
            {
                const int generation = candidate.Staged.Generation;
                candidate.Staged = {}; // the staged callables die while their code is still mapped
                (void)Close(handle, *copy, generation);
                failure = Fail("register_failed", error);
                return std::nullopt;
            }
            return candidate;
        }

        // All or nothing: any failure unregisters every config section and window this commit added (their
        // registration runs no callback, so nothing else needs undoing). The operation registry is replaced
        // last, so a failure never has to restore it.
        std::optional<std::string> Commit(StagedRegistrations& staged, LoadedPlugin& plugin,
                                          Runtime::EngineConfigControl& config, Runtime::EditorUiHost& ui,
                                          Runtime::EditorCommandHistory* history, const std::string_view failAt)
        {
            const auto rollback = [&](std::string reason) -> std::optional<std::string>
            {
                for (const auto& name : plugin.Sections) (void)config.UnregisterSection(name);
                for (const auto handle : plugin.Windows) (void)ui.UnregisterWindow(handle);
                for (const auto& name : plugin.HistoryCommands) (void)history->UnregisterCommand(name);
                plugin.Sections.clear();
                plugin.Windows.clear();
                plugin.Operations.clear();
                plugin.HistoryCommands.clear();
                return reason;
            };

            for (auto& section : staged.ConfigSections)
            {
                std::string name = section.DefaultSection.Name;
                if (!config.RegisterSection(std::move(section))) return rollback("config: section rejected");
                plugin.Sections.push_back(std::move(name));
            }
            if (failAt == "config") return rollback("config: injected failure after the config sections");

            Runtime::AgentOperationRegistry operationsCopy = *g_Operations;
            for (auto& operation : staged.Operations)
            {
                std::string name = operation.Name;
                if (!operationsCopy.Register(std::move(operation))) return rollback("operations: operation rejected");
                plugin.Operations.push_back(std::move(name));
            }
            if (failAt == "operations") return rollback("operations: injected failure after the operations");

            for (auto& window : staged.Windows)
            {
                const auto handle = ui.RegisterWindow(std::move(window));
                if (!handle.IsValid()) return rollback("windows: window rejected");
                plugin.Windows.push_back(handle);
            }
            if (failAt == "windows") return rollback("windows: injected failure after the windows");

            using Status = Runtime::EditorCommandHistoryStatus;
            for (auto& command : staged.HistoryCommands)
            {
                // An empty Apply would pass RegisterCommand inside the host's wrapper and only fail when called.
                const bool registered = command.Apply && history->RegisterCommand(command.Name,
                    [apply = std::move(command.Apply)](const std::string_view payload, const bool undo)
                    {
                        if (apply(payload, undo)) return undo ? Status::Undone : Status::Applied;
                        return undo ? Status::UndoFailed : Status::CommandFailed;
                    });
                if (!registered) return rollback("history: command rejected");
                plugin.HistoryCommands.push_back(command.Name);
            }

            *g_Operations = std::move(operationsCopy);
            return std::nullopt;
        }

        // Reapplies the saved payloads of the plugin's sections through preview and apply, so the new
        // registration validates them and its OnChanged receives them. A payload the preview does not keep as
        // saved (rejected, or changed by a fallback) is not applied: the section keeps its default.
        Json Restore(const LoadedPlugin& plugin, Runtime::EngineConfigControl& config)
        {
            Json report{{"restored", Json::array()}, {"rejected", Json::array()}};
            for (const auto& name : plugin.Sections)
            {
                const auto saved = g_SavedSections.find(name);
                if (saved == g_SavedSections.end()) continue;
                auto candidate = config.GetEngineConfigControlState().ActiveConfig;
                Core::Config::UpsertEngineConfigSection(candidate.AppSections, saved->second);
                const auto preview = config.PreviewEngineConfigControlDocument(
                    Core::Config::SerializeEngineConfig(candidate), "plugin-reload:" + name);
                const auto* previewed = Core::Config::FindEngineConfigSection(preview.Preview.Config.AppSections, name);
                const bool restored = Core::Config::IsConfigUsable(preview) && previewed != nullptr &&
                    *previewed == saved->second && config.ApplyEngineConfigHotSubset(preview).Succeeded();
                report[restored ? "restored" : "rejected"].push_back(name);
                g_SavedSections.erase(saved);
            }
            return report;
        }

        Runtime::AgentOperationOutcome Finish(Candidate candidate, Runtime::EngineConfigControl& config,
                                              Runtime::EditorUiHost& ui, Runtime::EditorCommandHistory* history,
                                              const std::string_view failAt)
        {
            LoadedPlugin plugin{.Handle = candidate.Handle, .Copy = candidate.Copy,
                                .Generation = candidate.Staged.Generation, .Shutdown = candidate.Shutdown};
            const auto failure = Commit(candidate.Staged, plugin, config, ui, history, failAt);
            candidate.Staged = {}; // leftover staged callables die before any close
            if (failure)
            {
                (void)Close(plugin.Handle, plugin.Copy, plugin.Generation);
                return Fail("commit_failed", *failure);
            }
            g_Plugin = std::move(plugin);
            spike_lifecycle_event("commit");
            const Json restored = Restore(*g_Plugin, config);
            return {.Text = Json{{"loaded", true}, {"generation", g_Plugin->Generation}, {"config", restored}}.dump()};
        }

        // Shutdown hook, then every registration out (section payloads saved first), then the close. On
        // callables_alive the plugin is unregistered but stays mapped.
        std::optional<std::string> Unload(Runtime::EngineConfigControl& config, Runtime::EditorUiHost& ui,
                                          Runtime::EditorCommandHistory* history)
        {
            LoadedPlugin plugin = std::move(*g_Plugin);
            g_Plugin.reset();
            plugin.Shutdown();
            for (const auto& name : plugin.Sections)
            {
                if (const auto* active = Core::Config::FindEngineConfigSection(
                        config.GetEngineConfigControlState().ActiveConfig.AppSections, name))
                    g_SavedSections[name] = *active;
                (void)config.UnregisterSection(name);
            }
            for (const auto& name : plugin.Operations) (void)g_Operations->Unregister(name);
            for (const auto handle : plugin.Windows) (void)ui.UnregisterWindow(handle);
            // Entries recorded through these commands stay in the history; they wait for a plugin that registers
            // the same names again.
            for (const auto& name : plugin.HistoryCommands) (void)history->UnregisterCommand(name);
            spike_lifecycle_event("unload");
            if (!Close(plugin.Handle, plugin.Copy, plugin.Generation)) return std::string{"callables_alive"};
            return std::nullopt;
        }

        // The commit, unload and reload run on a later frame, after this call's dispatch through the registry
        // returned; the reservation ends when the continuation is destroyed (completed or dropped).
        std::shared_ptr<void> Reserve()
        {
            g_LoadPending = true;
            return {nullptr, [](void*) { g_LoadPending = false; }};
        }

        Runtime::EditorUiHost* Ui(Runtime::ServiceRegistry& services, const Runtime::AgentOperationContext& context)
        {
            return context.ConfigControl != nullptr && context.History != nullptr
                ? services.Find<Runtime::EditorUiHost>() : nullptr;
        }

        Runtime::AgentOperationOutcome Load(Runtime::ServiceRegistry& services, std::string_view argumentsJson)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || !arguments.contains("path") || !arguments["path"].is_string())
                return Fail("invalid_params", "path is required");
            const std::string path = arguments["path"].get<std::string>();
            const std::string failAt = arguments.value("fail_at", std::string{});
            if (g_Plugin) return Fail("already_loaded", "a plugin is already loaded");
            if (g_LoadPending) return Fail("load_pending", "another load has not committed yet");

            Runtime::AgentOperationOutcome failure{};
            auto candidate = Open(path, failAt, failure);
            if (!candidate) return failure;
            auto opened = Own(std::move(*candidate));
            // Spike test hook: drop the pending load as a disconnect would; the owner closes the image.
            if (arguments.value("simulate_drop", false))
            {
                opened.reset();
                return {.Text = Json{{"dropped", true}, {"close", g_Closes.back()}}.dump()};
            }
            const auto reservation = Reserve();
            // Spike test hook: a second load in the same dispatch must be refused while this one is pending.
            const std::string second = arguments.value("probe_pending", false)
                ? Load(services, Json{{"path", path}}.dump()).ErrorCode : std::string{};
            Runtime::AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [&services, opened = std::move(opened), failAt, reservation, second](const Runtime::AgentOperationContext& context,
                                                         Runtime::AgentOperationOutcome& result)
            {
                auto* ui = Ui(services, context);
                if (ui == nullptr)
                {
                    result = Fail("host_unavailable", "editor UI host, config control or history missing");
                    return true;
                }
                result = Finish(std::exchange(*opened, Candidate{}), *context.ConfigControl, *ui, context.History, failAt);
                if (!result.IsError)
                {
                    Json reply = Json::parse(result.Text);
                    reply["second_attempt"] = second;
                    result.Text = reply.dump();
                }
                return true;
            };
            return outcome;
        }

        // plugin_unload, and plugin_reload when `path` is set: unload, then open, register and commit the new
        // build. A failure after the unload leaves no plugin, with the reason.
        Runtime::AgentOperationOutcome UnloadOrReload(Runtime::ServiceRegistry& services, std::string_view argumentsJson,
                                                      const bool reload)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || (reload && !(arguments.contains("path") && arguments["path"].is_string())))
                return Fail("invalid_params", "path is required");
            if (!g_Plugin) return Fail("not_loaded", "no plugin is loaded");
            if (g_LoadPending) return Fail("load_pending", "another load has not committed yet");
            const std::string path = reload ? arguments["path"].get<std::string>() : std::string{};
            const std::string failAt = arguments.value("fail_at", std::string{});
            // Spike test hook: keep a copy of one plugin operation alive, so the close must refuse.
            if (!reload && arguments.value("retain_callable", false) && !g_Plugin->Operations.empty())
            {
                static auto* retained = new std::vector<Runtime::AgentOperationSpec>(); // never destroyed
                retained->push_back(*g_Operations->Find(g_Plugin->Operations.front()));
            }
            Runtime::AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [&services, reservation = Reserve(), reload, path, failAt](
                const Runtime::AgentOperationContext& context, Runtime::AgentOperationOutcome& result)
            {
                auto* ui = Ui(services, context);
                if (ui == nullptr)
                {
                    result = Fail("host_unavailable", "editor UI host, config control or history missing");
                    return true;
                }
                if (const auto error = Unload(*context.ConfigControl, *ui, context.History))
                {
                    result = Fail(*error, "a callable of the closing generation is still alive; the image stays mapped");
                    return true;
                }
                if (!reload)
                {
                    result = {.Text = Json{{"unloaded", true}, {"close", g_Closes.back()}}.dump()};
                    return true;
                }
                Runtime::AgentOperationOutcome failure{};
                auto candidate = Open(path, failAt, failure);
                result = candidate ? Finish(std::move(*candidate), *context.ConfigControl, *ui, context.History, failAt)
                                   : failure;
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
                {"pid", getpid()},
                {"undo_value", HostUndoValue()},
                {"live", CountsJson(Live())},
                {"loaded", g_Plugin ? Json(g_Plugin->Generation) : Json(nullptr)},
                {"loaded_range", g_Plugin ? MappedRange(g_Plugin->Copy) : Json::array()},
                {"invoked", CountsJson(Invoked())},
                {"closes", g_Closes},
            };
            return {.Text = value.dump()};
        }
    }

    void RegisterHostOperations(Runtime::AgentOperationRegistry& registry, Runtime::ServiceRegistry& services,
                                std::function<void()> requestExit)
    {
        g_Operations = &registry;
        constexpr const char* kFailAt = "fail_at injects a failure at dlopen, dlsym, config, operations or windows. ";
        (void)registry.Register({
            .Name = "plugin_load",
            .Title = "Load a plugin (ARCH-022 spike)",
            .Description = std::string{"dlopen a private copy of a plugin and commit its registrations on the next "
                                       "frame. "} + kFailAt + "probe_pending tries a second load in the same "
                                       "dispatch and reports its error code; simulate_drop drops the pending "
                                       "load as a disconnect would.",
            .InputSchemaJson = R"({"type":"object","properties":{"path":{"type":"string"},"fail_at":{"type":"string"},"probe_pending":{"type":"boolean"},"simulate_drop":{"type":"boolean"}},"required":["path"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [&services](const Runtime::AgentOperationContext&, std::string_view arguments)
            { return Load(services, arguments); },
        });
        (void)registry.Register({
            .Name = "plugin_unload",
            .Title = "Unload the plugin (ARCH-022 spike)",
            .Description = "On the next frame: the plugin's shutdown hook, all its registrations out (config section "
                           "payloads saved for a later load), then dlclose once none of its callables is alive. "
                           "retain_callable (test hook) keeps a copy of one plugin operation alive.",
            .InputSchemaJson = R"({"type":"object","properties":{"retain_callable":{"type":"boolean"}},"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [&services](const Runtime::AgentOperationContext&, std::string_view arguments)
            { return UnloadOrReload(services, arguments, false); },
        });
        (void)registry.Register({
            .Name = "plugin_reload",
            .Title = "Reload the plugin (ARCH-022 spike)",
            .Description = std::string{"On the next frame: unload, then load the build at path and reapply the saved "
                                       "config payloads. A failure after the unload leaves no plugin. "} + kFailAt,
            .InputSchemaJson = R"({"type":"object","properties":{"path":{"type":"string"},"fail_at":{"type":"string"}},"required":["path"],"additionalProperties":false})",
            .ReadOnly = false,
            .Invoke = [&services](const Runtime::AgentOperationContext&, std::string_view arguments)
            { return UnloadOrReload(services, arguments, true); },
        });
        (void)registry.Register({
            .Name = "plugin_host_identity",
            .Title = "Host identity probes (ARCH-022 spike)",
            .Description = "Addresses and values of the host's probes, ImGui context and allocator, ImPlot context, "
                           "registered windows with their open state, the process id, the host undo value, the loaded "
                           "generation, live and invoked plugin callables per generation and kind, and every close "
                           "attempt.",
            .Invoke = [&services](const Runtime::AgentOperationContext&, std::string_view) { return HostIdentity(services); },
        });
        (void)registry.Register({
            .Name = "app_quit",
            .Title = "Quit the Sandbox (ARCH-022 spike)",
            .Description = "Calls the loaded plugin's shutdown hook, then requests the engine's exit.",
            .ReadOnly = false,
            .Invoke = [requestExit = std::move(requestExit)](const Runtime::AgentOperationContext&, std::string_view)
            {
                if (g_Plugin) g_Plugin->Shutdown();
                requestExit();
                return Runtime::AgentOperationOutcome{.Text = R"({"quitting":true})"};
            },
        });
    }
}
