module;

#include <algorithm>
#include <chrono>
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
#include <span>
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

#include "PluginBuildId.h" // generated: INTRINSIC_PLUGIN_BUILD_ID

module Extrinsic.Runtime.PluginHost;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Logging;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"

// Named only as a reference parameter, as Runtime.JobService does.
extern "C++" { namespace Extrinsic::RHI { class ICommandContext; } }

namespace
{
    using Json = nlohmann::json;
    using CallableCounts = std::map<int, std::map<std::string, long>>; // generation -> kind -> count

    // Leaked on purpose: plugin callables may still report while the engine's statics are destroyed.
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
    // One record per close attempt: generation, live counts, closed, and for a mapped image where it was.
    Json& Closes()
    {
        static auto* closes = new Json(Json::array());
        return *closes;
    }
}

extern "C" void intrinsic_plugin_event(const char* event) noexcept
{
    const char* path = std::getenv("INTRINSIC_PLUGIN_EVENT_LOG");
    if (path == nullptr || event == nullptr) return;
    if (std::FILE* file = std::fopen(path, "a"))
    {
        std::fprintf(file, "%s\n", event);
        std::fclose(file);
    }
}

extern "C" void intrinsic_plugin_callable(const int generation, const char* kind, const int delta) noexcept
{
    const std::lock_guard lock(CallableMutex());
    Live()[generation][kind] += delta;
}

extern "C" void intrinsic_plugin_invoked(const int generation, const char* kind) noexcept
{
    const std::lock_guard lock(CallableMutex());
    ++Invoked()[generation][kind];
}

namespace Extrinsic::Runtime
{
    namespace
    {
        AgentOperationOutcome Fail(std::string code, std::string message)
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

        // The first start and last end of the file's mappings (the kernel lists them in ascending order), as hex
        // strings; empty when it is not mapped.
        Json MappedRange(const std::filesystem::path& file)
        {
            if (file.empty()) return Json::array();
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
        bool Close(std::unique_ptr<PluginImage> image, const std::optional<int> generation)
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
            const std::filesystem::path file = image->File();
            Json record{{"generation", generation ? Json(*generation) : Json(nullptr)}, {"live", live},
                        {"closed", !alive}, {"copy", file.string()}};
            if (!alive)
            {
                record["range"] = MappedRange(file); // where the image was, to attribute a later fault
                image->Close();
                record["unmapped"] = MappedRange(file).empty();
            }
            // Otherwise destroying the image object leaves it mapped for the process's lifetime.
            Closes().push_back(std::move(record));
            return !alive;
        }

#if defined(INTRINSIC_PLUGINS_SUPPORTED)
        class SharedImage final : public PluginImage
        {
        public:
            SharedImage(void* handle, std::filesystem::path file, PluginRegisterFn registerFn, PluginShutdownFn shutdownFn)
                : m_Handle(handle), m_File(std::move(file)), m_Register(registerFn), m_Shutdown(shutdownFn) {}
            // Not closed: the image stays mapped (the ordinary exit, or a callable still alive); its copy goes.
            ~SharedImage() override
            {
                std::error_code ec{};
                std::filesystem::remove(m_File, ec);
            }

            bool Register(PluginRegistrations& staged, std::string& error) noexcept override
            {
                return m_Register(staged, error);
            }
            void Shutdown() noexcept override { m_Shutdown(); }
            void Close() noexcept override
            {
                dlclose(m_Handle);
                m_Handle = nullptr;
            }
            const std::filesystem::path& File() const noexcept override { return m_File; }

        private:
            void* m_Handle{nullptr};
            std::filesystem::path m_File{};
            PluginRegisterFn m_Register{nullptr};
            PluginShutdownFn m_Shutdown{nullptr};
        };

        // Every load opens its own copy, so a rebuilt file loads even while an older image stays mapped.
        std::optional<std::filesystem::path> PrivateCopy(const std::string& path, std::string& error)
        {
            static int copies = 0;
            std::error_code ec{};
            const auto dir = std::filesystem::temp_directory_path(ec) / ("intrinsic-plugins-" + std::to_string(getpid()));
            std::filesystem::create_directories(dir, ec);
            const auto copy = dir / ("plugin-" + std::to_string(++copies) + ".so");
            if (!ec) std::filesystem::copy_file(path, copy, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec)
            {
                error = path + ": " + ec.message();
                return std::nullopt;
            }
            return copy;
        }
#endif

        constexpr const char* kUnsupported = "plugins need the shared engine build";

        std::unique_ptr<PluginImage> OpenShared(const std::string& path, const std::string_view failAt,
                                                AgentOperationOutcome& failure)
        {
#if defined(INTRINSIC_PLUGINS_SUPPORTED)
            std::string error{};
            const auto copy = PrivateCopy(path, error);
            if (!copy)
            {
                failure = Fail("copy_failed", error);
                return nullptr;
            }
            void* handle = failAt == "dlopen" ? nullptr : dlopen(copy->c_str(), RTLD_NOW | RTLD_LOCAL);
            if (handle == nullptr)
            {
                const char* reason = dlerror();
                std::error_code ec{};
                std::filesystem::remove(*copy, ec);
                failure = Fail("dlopen_failed",
                               failAt == "dlopen" ? "injected dlopen failure" : (reason ? reason : "dlopen"));
                return nullptr;
            }
            auto* registerFn =
                failAt == "dlsym" ? nullptr : reinterpret_cast<PluginRegisterFn>(dlsym(handle, kPluginRegisterSymbol));
            auto* shutdownFn = reinterpret_cast<PluginShutdownFn>(dlsym(handle, kPluginShutdownSymbol));
            auto* buildIdFn = reinterpret_cast<PluginBuildIdFn>(dlsym(handle, kPluginBuildIdSymbol));
            auto image = std::make_unique<SharedImage>(handle, *copy, registerFn, shutdownFn);
            const char* missing = registerFn == nullptr ? kPluginRegisterSymbol
                : shutdownFn == nullptr                 ? kPluginShutdownSymbol
                : buildIdFn == nullptr                  ? kPluginBuildIdSymbol
                                                        : nullptr;
            if (missing != nullptr)
            {
                (void)Close(std::move(image), std::nullopt);
                failure = Fail("dlsym_failed", std::string{"missing entry point "} + missing);
                return nullptr;
            }
            if (const std::string_view id = buildIdFn(); id != INTRINSIC_PLUGIN_BUILD_ID)
            {
                failure = Fail("abi_mismatch", "plugin build id " + std::string{id} + ", host " +
                                   INTRINSIC_PLUGIN_BUILD_ID + "; rebuild the plugin in this build tree");
                (void)Close(std::move(image), std::nullopt);
                return nullptr;
            }
            return image;
#else
            (void)path;
            (void)failAt;
            failure = Fail("plugins_unsupported", kUnsupported);
            return nullptr;
#endif
        }
    }

    bool PluginsSupported() noexcept
    {
#if defined(INTRINSIC_PLUGINS_SUPPORTED)
        return true;
#else
        return false;
#endif
    }

    std::unique_ptr<PluginImage> OpenSharedPlugin(const std::string& path, const std::string_view failAt,
                                                  AgentOperationOutcome& failure)
    {
        return OpenShared(path, failAt, failure);
    }

    std::unique_ptr<PluginImage> MakeInProcessPlugin(const PluginRegisterFn registerFn, const PluginShutdownFn shutdownFn)
    {
        class InProcessImage final : public PluginImage
        {
        public:
            InProcessImage(PluginRegisterFn registerFn, PluginShutdownFn shutdownFn)
                : m_Register(registerFn), m_Shutdown(shutdownFn) {}
            bool Register(PluginRegistrations& staged, std::string& error) noexcept override
            {
                return m_Register(staged, error);
            }
            void Shutdown() noexcept override { m_Shutdown(); }
            void Close() noexcept override {}
            const std::filesystem::path& File() const noexcept override { return m_File; }

        private:
            PluginRegisterFn m_Register;
            PluginShutdownFn m_Shutdown;
            std::filesystem::path m_File{};
        };
        return std::make_unique<InProcessImage>(registerFn, shutdownFn);
    }

    std::optional<StagedPlugin> StagePlugin(std::unique_ptr<PluginImage> image, std::string& error)
    {
        StagedPlugin staged{.Image = std::move(image)};
        if (!staged.Image->Register(staged.Registrations, error))
        {
            const int generation = staged.Registrations.Generation;
            staged.Registrations = {};
            (void)Close(std::move(staged.Image), generation);
            return std::nullopt;
        }
        for (const auto& section : staged.Registrations.ConfigSections)
            staged.SectionNames.push_back(section.DefaultSection.Name);
        return staged;
    }

    namespace
    {
        // A plugin job, owned on the main thread; the worker reaches it only through host code and a raw pointer.
        // The service finalizes a job only after its Work returned or when it never started, so a finalized
        // entry is no longer on a worker.
        struct JobEntry
        {
            PluginJob Job{};
            JobToken Token{};
            bool Finalized{false}; // its Completion ran (main thread)
        };

        // A deferred result of a plugin operation: the agent server holds host forwarders that share this entry,
        // which holds the plugin's callables. Dropping the call frees it; an unload empties it first.
        struct DeferredEntry
        {
            AgentOperationContinuation Continuation{};
            decltype(AgentOperationOutcome::Progress) Progress{};
            decltype(AgentOperationOutcome::Cancel) Cancel{};
        };

        struct GpuEntry
        {
            GpuQueueParticipantDesc Work{};
            GpuQueueParticipantHandle Handle{};
        };

        // The plugin that is loaded and committed; everything it registered, by name or handle.
        struct LoadedPlugin
        {
            std::unique_ptr<PluginImage> Image{};
            int Generation{0};
            std::vector<std::string> Operations{};
            std::vector<std::string> Sections{};
            std::vector<EditorWindowHandle> Windows{};
            std::vector<std::string> HistoryCommands{};
        };

        // An opened image whose Register succeeded; committed on a later frame.
        struct Candidate
        {
            std::unique_ptr<PluginImage> Image{};
            PluginRegistrations Staged{};
        };

        constexpr std::chrono::milliseconds kDrainDeadline{5000};
        enum class DrainState : std::uint8_t { Pending, Drained, TimedOut };

        PluginHostModule* g_Current{nullptr};

        // An object schema; the test-hook properties only with --plugin-test-hooks, so without it they are
        // rejected as unknown arguments.
        std::string Schema(std::string properties, const bool hooks, const std::string_view hookProperties,
                           const std::string_view required = {})
        {
            if (hooks && !hookProperties.empty())
                properties += (properties.empty() ? "" : ",") + std::string{hookProperties};
            return R"({"type":"object","properties":{)" + properties + "}" +
                (required.empty() ? std::string{} : R"(,"required":[")" + std::string{required} + R"("])") +
                R"(,"additionalProperties":false})";
        }
    }

    struct PluginHostModule::Impl
    {
        PluginHostOptions Options{};
        ServiceRegistry* Services{nullptr};
        AgentOperationRegistry* Operations{nullptr};
        JobService* Jobs{nullptr};
        std::vector<std::unique_ptr<JobEntry>> PluginJobs{};
        std::unique_ptr<GpuEntry> Gpu{};
        bool Draining{false};
        std::optional<LoadedPlugin> Plugin{};
        bool LoadPending{false}; // a load, unload or reload whose continuation has not finished (or been dropped)
        // Active payloads of an unloaded plugin's sections, reapplied when a plugin registers them again.
        std::map<std::string, Core::Config::EngineConfigSection> SavedSections{};
        WorldRegistry* Worlds{nullptr};
        // The workspace plugin jobs belong to: the app's (UseEditorWorkspace), else this one.
        EditorWorkspaceAttachment OwnWorkspace{};
        EditorWorkspaceAttachment* Workspace{nullptr};
        std::vector<std::weak_ptr<DeferredEntry>> Deferred{};
        // Plugin callables the host is running now (deferred results); an unload waits for zero, since it would
        // destroy the running callable and unmap its code.
        int RunningPluginCode{0};

        // Counts plugin code the host runs on the main thread, for as long as the returned guard lives.
        std::shared_ptr<void> Running()
        {
            ++RunningPluginCode;
            return {nullptr, [this](void*) { --RunningPluginCode; }};
        }

        // Keeps a deferred result's callables in a host entry and hands the agent server forwarders to it.
        AgentOperationOutcome HoldDeferred(AgentOperationOutcome outcome)
        {
            if (!outcome.Continuation && !outcome.Progress && !outcome.Cancel) return outcome;
            auto entry = std::make_shared<DeferredEntry>(DeferredEntry{
                std::move(outcome.Continuation), std::move(outcome.Progress), std::move(outcome.Cancel)});
            std::erase_if(Deferred, [](const std::weak_ptr<DeferredEntry>& held) { return held.expired(); });
            Deferred.push_back(entry);
            const auto running = [this] { return Running(); };
            if (entry->Continuation)
                outcome.Continuation = [entry, running](const AgentOperationContext& context, AgentOperationOutcome& result)
                {
                    if (!entry->Continuation)
                    {
                        result = Fail("plugin_unloaded", "the plugin was unloaded before this call finished");
                        return true;
                    }
                    const auto guard = running();
                    return entry->Continuation(context, result);
                };
            if (entry->Progress)
                outcome.Progress = [entry, running](const AgentOperationContext& context)
                {
                    if (!entry->Progress) return decltype(entry->Progress(context)){};
                    const auto guard = running();
                    return entry->Progress(context);
                };
            if (entry->Cancel)
                outcome.Cancel = [entry, running](const AgentOperationContext& context)
                {
                    if (!entry->Cancel) return std::string{};
                    const auto guard = running();
                    return entry->Cancel(context);
                };
            return outcome;
        }

        // Destroys the plugin's callables of every pending call (before a close); their forwarders stay inert.
        void ClearDeferred()
        {
            for (const auto& held : Deferred)
                if (const auto entry = held.lock()) *entry = {};
            Deferred.clear();
        }

        // The workspace's editor commands for one call. The app's shell prepares a frame every frame; the host's own
        // workspace prepares one here, as the agent server does per call.
        EditorProcessingCommands JobCommands()
        {
            if (Workspace == nullptr) return {};
            if (Workspace == &OwnWorkspace && OwnWorkspace.IsAttached())
                (void)PrepareEditorWorkspaceSnapshotFrame(OwnWorkspace);
            return PrepareEditorProcessingCommands(*Workspace);
        }

        // The run a job belongs to: its run's first job.
        JobToken RunOf(const JobToken job) const
        {
            for (const auto& entry : PluginJobs)
                if (entry->Token == job) return entry->Job.Run.IsValid() ? entry->Job.Run.Token : job;
            return job;
        }

        // Cancels every run of the plugin's jobs through the workspace, so its session records the requests (the
        // Jobs window then offers no second cancel); jobs outside it are cancelled directly.
        void CancelRuns()
        {
            std::vector<JobToken> runs{};
            for (const auto& entry : PluginJobs)
                if (const JobToken run = RunOf(entry->Token); std::ranges::find(runs, run) == runs.end())
                    runs.push_back(run);
            const EditorProcessingCommands commands = JobCommands();
            (void)CancelEditorRunJobs(commands, runs);
            for (const auto& entry : PluginJobs) (void)Jobs->Cancel(entry->Token);
        }

        // Commits the staged startup plugin; its config sections are already registered (the boot registry).
        void CommitStartup()
        {
            auto staged = std::move(*Options.Startup);
            Options.Startup.reset();
            auto* config = Services->Find<EngineConfigControl>();
            auto* ui = Services->Find<EditorUiHost>();
            auto* history = Services->Find<EditorCommandHistory>();
            LoadedPlugin plugin{.Image = std::move(staged.Image), .Generation = staged.Registrations.Generation,
                                .Sections = std::move(staged.SectionNames)};
            const auto failure = config && ui && history
                ? Commit(staged.Registrations, plugin, *config, *ui, *history, {})
                : std::optional<std::string>{"editor UI host, config control or history missing"};
            staged.Registrations = {};
            if (failure)
            {
                Core::Log::Error("[PluginHost] the startup plugin was not committed: {}", *failure);
                // Its boot sections (a commit's rollback already removed them) must go before the close; without
                // config control they cannot, so the image stays mapped.
                if (config == nullptr) return;
                for (const auto& name : plugin.Sections) (void)config->UnregisterSection(name);
                (void)Close(std::move(plugin.Image), plugin.Generation);
                return;
            }
            Plugin = std::move(plugin);
            intrinsic_plugin_event("commit");
        }

        std::unique_ptr<PluginImage> Open(const std::string& path, const std::string_view failAt,
                                          AgentOperationOutcome& failure) const
        {
            return Options.Open ? Options.Open(path, failAt, failure) : OpenSharedPlugin(path, failAt, failure);
        }

        // Open and Register; on failure the image is closed again and `failure` says why.
        std::optional<Candidate> Stage(const std::string& path, const std::string_view failAt,
                                       AgentOperationOutcome& failure) const
        {
            auto image = Open(path, failAt, failure);
            if (!image) return std::nullopt;
            Candidate candidate{.Image = std::move(image)};
            std::string error{};
            if (!candidate.Image->Register(candidate.Staged, error))
            {
                const int generation = candidate.Staged.Generation;
                candidate.Staged = {}; // the staged callables die while their code is still mapped
                (void)Close(std::move(candidate.Image), generation);
                failure = Fail("register_failed", error);
                return std::nullopt;
            }
            return candidate;
        }

        // Owns a pending candidate's image: a load whose continuation is dropped (the caller disconnected) or
        // cannot run closes the image here. Finish takes the candidate out, and with it the ownership.
        static std::shared_ptr<Candidate> Own(Candidate candidate)
        {
            return {new Candidate(std::move(candidate)), [](Candidate* pending)
                    {
                        if (pending->Image)
                        {
                            const int generation = pending->Staged.Generation;
                            pending->Staged = {};
                            (void)Close(std::move(pending->Image), generation);
                        }
                        delete pending;
                    }};
        }

        // All or nothing: any failure unregisters every config section, window and history command this commit
        // added (their registration runs no callback, so nothing else needs undoing). The operation registry is
        // replaced last, so a failure never has to restore it.
        std::optional<std::string> Commit(PluginRegistrations& staged, LoadedPlugin& plugin,
                                          EngineConfigControl& config, EditorUiHost& ui,
                                          EditorCommandHistory& history, const std::string_view failAt)
        {
            const auto rollback = [&](std::string reason) -> std::optional<std::string>
            {
                // The staged operations are only in the copy below, so their unregistration finds nothing.
                Unregister(plugin, config, ui, history, false);
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

            // Without an agent server there is no registry: the plugin's tools are not registered.
            AgentOperationRegistry operationsCopy = Operations != nullptr ? *Operations : AgentOperationRegistry{};
            for (auto& operation : staged.Operations)
            {
                if (Operations == nullptr) break;
                std::string name = operation.Name;
                if (!operation.Invoke) return rollback("operations: operation rejected");
                // The agent server keeps a deferred result beyond the reach of an unload, so the host holds it.
                operation.Invoke = [this, invoke = std::move(operation.Invoke)](const AgentOperationContext& context,
                                                                                const std::string_view arguments)
                { return HoldDeferred(invoke(context, arguments)); };
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

            using Status = EditorCommandHistoryStatus;
            for (auto& command : staged.HistoryCommands)
            {
                // An empty Apply would pass RegisterCommand inside the host's wrapper and only fail when called.
                const bool registered = command.Apply && history.RegisterCommand(command.Name,
                    [apply = std::move(command.Apply)](const std::string_view payload, const bool undo)
                    {
                        if (apply(payload, undo)) return undo ? Status::Undone : Status::Applied;
                        return undo ? Status::UndoFailed : Status::CommandFailed;
                    });
                if (!registered) return rollback("history: command rejected");
                plugin.HistoryCommands.push_back(command.Name);
            }

            if (Operations != nullptr) *Operations = std::move(operationsCopy);
            return std::nullopt;
        }

        // Reapplies the saved payloads of the plugin's sections through preview and apply, so the new
        // registration validates them and its OnChanged receives them. A payload the preview does not keep as
        // saved (rejected, or changed by a fallback) is not applied: the section keeps its default.
        Json Restore(const LoadedPlugin& plugin, EngineConfigControl& config)
        {
            Json report{{"restored", Json::array()}, {"rejected", Json::array()}};
            for (const auto& name : plugin.Sections)
            {
                const auto saved = SavedSections.find(name);
                if (saved == SavedSections.end()) continue;
                auto candidate = config.GetEngineConfigControlState().ActiveConfig;
                Core::Config::UpsertEngineConfigSection(candidate.AppSections, saved->second);
                const auto preview = config.PreviewEngineConfigControlDocument(
                    Core::Config::SerializeEngineConfig(candidate), "plugin-reload:" + name);
                const auto* previewed = Core::Config::FindEngineConfigSection(preview.Preview.Config.AppSections, name);
                const bool restored = Core::Config::IsConfigUsable(preview) && previewed != nullptr &&
                    *previewed == saved->second && config.ApplyEngineConfigHotSubset(preview).Succeeded();
                report[restored ? "restored" : "rejected"].push_back(name);
                SavedSections.erase(saved);
            }
            return report;
        }

        AgentOperationOutcome Finish(Candidate candidate, EngineConfigControl& config, EditorUiHost& ui,
                                     EditorCommandHistory& history, const std::string_view failAt)
        {
            LoadedPlugin plugin{.Image = std::move(candidate.Image), .Generation = candidate.Staged.Generation};
            const auto failure = Commit(candidate.Staged, plugin, config, ui, history, failAt);
            candidate.Staged = {}; // leftover staged callables die before any close
            if (failure)
            {
                (void)Close(std::move(plugin.Image), plugin.Generation);
                return Fail("commit_failed", *failure);
            }
            Plugin = std::move(plugin);
            intrinsic_plugin_event("commit");
            const Json restored = Restore(*Plugin, config);
            return {.Text = Json{{"loaded", true}, {"generation", Plugin->Generation}, {"config", restored}}.dump()};
        }

        // Every registration of the plugin out; with `save`, its sections' active payloads are kept for a later
        // load first.
        void Unregister(const LoadedPlugin& plugin, EngineConfigControl& config, EditorUiHost& ui,
                        EditorCommandHistory& history, const bool save)
        {
            for (const auto& name : plugin.Sections)
            {
                if (const auto* active = Core::Config::FindEngineConfigSection(
                        config.GetEngineConfigControlState().ActiveConfig.AppSections, name); save && active != nullptr)
                    SavedSections[name] = *active;
                (void)config.UnregisterSection(name);
            }
            for (const auto& name : plugin.Operations) (void)Operations->Unregister(name); // none without a registry
            for (const auto handle : plugin.Windows) (void)ui.UnregisterWindow(handle);
            // Entries recorded through these commands stay in the history; they wait for a plugin that registers
            // the same names again.
            for (const auto& name : plugin.HistoryCommands) (void)history.UnregisterCommand(name);
        }

        // Shutdown hook, then every registration out (section payloads saved first), then the close. On
        // callables_alive the plugin is unregistered but stays mapped.
        std::optional<std::string> Unload(EngineConfigControl& config, EditorUiHost& ui, EditorCommandHistory& history)
        {
            LoadedPlugin plugin = std::move(*Plugin);
            Plugin.reset();
            plugin.Image->Shutdown();
            Unregister(plugin, config, ui, history, true);
            ClearDeferred();
            intrinsic_plugin_event("unload");
            if (!Close(std::move(plugin.Image), plugin.Generation)) return std::string{"callables_alive"};
            return std::nullopt;
        }

        // One frame of draining: destroys the plugin callables of every finalized job, and unregisters the GPU
        // work once its last frame retired. True when nothing is left. simulateGpuStall (test hook) treats the
        // GPU work as never retiring.
        bool DrainStep(const bool simulateGpuStall)
        {
            std::erase_if(PluginJobs, [](const std::unique_ptr<JobEntry>& entry) { return entry->Finalized; });
            if (Gpu && !simulateGpuStall && !Gpu->Work.HasInFlightWork())
            {
                Jobs->UnregisterGpuQueueParticipant(Gpu->Handle); // not in flight: no device wait
                Gpu.reset();
            }
            return PluginJobs.empty() && !Gpu;
        }

        // One poll of a continuation's drain. The first poll starts it: submissions refused, jobs cancelled. The
        // deadline is checked before success, so work that ends after it still times out. On timeout admission
        // and GPU recording come back (as they do when the continuation is dropped, through its reservation).
        DrainState PollDrain(std::optional<std::chrono::steady_clock::time_point>& deadline,
                             const std::chrono::milliseconds budget, const bool simulateGpuStall)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!deadline)
            {
                deadline = now + budget;
                Draining = true;
                CancelRuns();
            }
            if (now >= *deadline)
            {
                Draining = false;
                return DrainState::TimedOut;
            }
            if (!DrainStep(simulateGpuStall)) return DrainState::Pending;
            Draining = false;
            return DrainState::Drained;
        }

        // The commit, unload and reload run on a later frame, after this call's dispatch through the registry
        // returned; the reservation ends when the continuation is destroyed (completed or dropped).
        std::shared_ptr<void> Reserve()
        {
            LoadPending = true;
            return {nullptr, [this](void*)
                    {
                        LoadPending = false;
                        // A drain ends with its continuation; a dropped one must not keep refusing work.
                        Draining = false;
                    }};
        }

        struct Hosts
        {
            EngineConfigControl& Config;
            EditorUiHost& Ui;
            EditorCommandHistory& History;
        };

        std::optional<Hosts> Find(const AgentOperationContext& context) const
        {
            auto* ui = Services != nullptr ? Services->Find<EditorUiHost>() : nullptr;
            if (context.ConfigControl == nullptr || context.History == nullptr || ui == nullptr) return std::nullopt;
            return Hosts{*context.ConfigControl, *ui, *context.History};
        }

        AgentOperationOutcome Load(const std::string_view argumentsJson)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || !arguments.contains("path") || !arguments["path"].is_string())
                return Fail("invalid_params", "path is required");
            const std::string path = arguments["path"].get<std::string>();
            const std::string failAt = arguments.value("fail_at", std::string{});
            if (Plugin) return Fail("already_loaded", "a plugin is already loaded");
            if (LoadPending) return Fail("load_pending", "another load has not committed yet");

            AgentOperationOutcome failure{};
            auto candidate = Stage(path, failAt, failure);
            if (!candidate) return failure;
            auto opened = Own(std::move(*candidate));
            // Test hook: drop the pending load as a disconnect would; the owner closes the image.
            if (arguments.value("simulate_drop", false))
            {
                opened.reset();
                return {.Text = Json{{"dropped", true}, {"close", Closes().back()}}.dump()};
            }
            const auto reservation = Reserve();
            // Test hook: a second load in the same dispatch must be refused while this one is pending.
            const std::string second = arguments.value("probe_pending", false)
                ? Load(Json{{"path", path}}.dump()).ErrorCode : std::string{};
            AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [this, opened = std::move(opened), failAt, reservation, second](
                const AgentOperationContext& context, AgentOperationOutcome& result)
            {
                const auto hosts = Find(context);
                if (!hosts)
                {
                    result = Fail("host_unavailable", "editor UI host, config control or history missing");
                    return true;
                }
                result = Finish(std::exchange(*opened, Candidate{}), hosts->Config, hosts->Ui, hosts->History, failAt);
                if (!result.IsError && !second.empty())
                {
                    Json reply = Json::parse(result.Text);
                    reply["second_attempt"] = second;
                    result.Text = reply.dump();
                }
                return true;
            };
            return outcome;
        }

        // plugin_unload, and plugin_reload when `path` is set: drain, unload, then open, register and commit the
        // new build. A failure after the unload leaves no plugin, with the reason.
        AgentOperationOutcome UnloadOrReload(const std::string_view argumentsJson, const bool reload)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || (reload && !(arguments.contains("path") && arguments["path"].is_string())))
                return Fail("invalid_params", "path is required");
            // A static build without an opener of its own (tests) has no plugin to reload: say why.
            if (reload && !Options.Open && !PluginsSupported()) return Fail("plugins_unsupported", kUnsupported);
            if (!Plugin) return Fail("not_loaded", "no plugin is loaded");
            if (LoadPending) return Fail("load_pending", "another load has not committed yet");
            const std::string path = reload ? arguments["path"].get<std::string>() : std::string{};
            const std::string failAt = arguments.value("fail_at", std::string{});
            // Test hooks: a drain deadline other than 5 s, and GPU work treated as never retiring.
            const auto budget = std::chrono::milliseconds{arguments.value("drain_deadline_ms", kDrainDeadline.count())};
            const bool simulateGpuStall = arguments.value("simulate_gpu_stall", false);
            // Test hook: keep a copy of one plugin operation alive, so the close must refuse.
            if (!reload && arguments.value("retain_callable", false) && !Plugin->Operations.empty())
            {
                static auto* retained = new std::vector<AgentOperationSpec>(); // never destroyed
                retained->push_back(*Operations->Find(Plugin->Operations.front()));
            }
            AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [this, reservation = Reserve(), reload, path, failAt, budget, simulateGpuStall,
                                    deadline = std::optional<std::chrono::steady_clock::time_point>{}](
                const AgentOperationContext& context, AgentOperationOutcome& result) mutable
            {
                const auto hosts = Find(context);
                if (!hosts)
                {
                    result = Fail("host_unavailable", "editor UI host, config control or history missing");
                    return true;
                }
                // Drain first, frame by frame; on the deadline the plugin stays loaded and registered.
                // Not while the plugin's own code is running (a deferred call that unloads its plugin): wait a frame.
                if (RunningPluginCode > 0) return false;
                const DrainState drain = PollDrain(deadline, budget, simulateGpuStall);
                if (drain == DrainState::Pending) return false;
                if (drain == DrainState::TimedOut)
                {
                    result = Fail("drain_timeout", "plugin work did not finish before the deadline; the plugin stays loaded");
                    return true;
                }
                if (const auto error = Unload(hosts->Config, hosts->Ui, hosts->History))
                {
                    result = Fail(*error, "a callable of the closing generation is still alive; the image stays mapped");
                    return true;
                }
                if (!reload)
                {
                    result = {.Text = Json{{"unloaded", true}, {"close", Closes().back()}}.dump()};
                    return true;
                }
                AgentOperationOutcome failure{};
                auto candidate = Stage(path, failAt, failure);
                result = candidate ? Finish(std::move(*candidate), hosts->Config, hosts->Ui, hosts->History, failAt)
                                   : failure;
                return true;
            };
            return outcome;
        }

        AgentOperationOutcome List() const
        {
            if (!Plugin) return {.Text = R"({"loaded":null})"};
            return {.Text = Json{{"loaded", {{"generation", Plugin->Generation},
                                             {"file", Plugin->Image->File().string()},
                                             {"operations", Plugin->Operations},
                                             {"config_sections", Plugin->Sections},
                                             {"windows", Plugin->Windows.size()},
                                             {"history_commands", Plugin->HistoryCommands}}}}.dump()};
        }

        Json PluginJobsJson() const
        {
            Json jobs = Json::array();
            for (const auto& entry : PluginJobs)
                jobs.push_back({{"state", std::string{ToString(Jobs->GetState(entry->Token))}},
                                {"finalized", entry->Finalized}});
            return jobs;
        }

        AgentOperationOutcome Identity() const
        {
            using namespace PluginProbes;
            Json windows = Json::array();
            if (const auto* ui = Services != nullptr ? Services->Find<EditorUiHost>() : nullptr)
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
                {"loaded", Plugin ? Json(Plugin->Generation) : Json(nullptr)},
                {"loaded_range", Plugin ? MappedRange(Plugin->Image->File()) : Json::array()},
                {"jobs", PluginJobsJson()},
                {"gpu", Gpu ? Json{{"in_flight", Gpu->Work.HasInFlightWork()}} : Json(nullptr)},
                {"draining", Draining},
                {"invoked", CountsJson(Invoked())},
                {"closes", Closes()},
            };
            return {.Text = value.dump()};
        }
    };

    PluginHostModule::PluginHostModule(PluginHostOptions options) : m_Impl(std::make_unique<Impl>())
    {
        m_Impl->Options = std::move(options);
        g_Current = this;
    }

    PluginHostModule::~PluginHostModule()
    {
        if (g_Current == this) g_Current = nullptr;
    }

    PluginHostModule* PluginHostModule::Current() noexcept { return g_Current; }
    ServiceRegistry* PluginHostModule::Services() const noexcept { return m_Impl->Services; }
    bool PluginHostModule::TestHooks() const noexcept { return m_Impl->Options.TestHooks; }

    RuntimeModuleResult PluginHostModule::OnRegister(EngineSetup& setup)
    {
        m_Impl->Services = &setup.Services();
        m_Impl->Jobs = &setup.Jobs();
        m_Impl->Worlds = &setup.Worlds();
        return setup.Services().Provide<PluginHostModule>(*this, Name());
    }

    RuntimeModuleResult PluginHostModule::OnResolve(EngineSetup&)
    {
        auto& impl = *m_Impl;
        if (impl.Workspace == nullptr)
        {
            impl.OwnWorkspace.Attach(*impl.Worlds, *impl.Services);
            impl.Workspace = &impl.OwnWorkspace;
        }
        if (impl.Options.Startup) impl.CommitStartup();
        return RuntimeModuleOk();
    }

    void PluginHostModule::UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace = &workspace;
    }

    void PluginHostModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        auto& impl = *m_Impl;
        if (impl.Gpu)
        {
            // Already shut down with every queue participant at the engine's BeginShutdown; a no-op then.
            context.Jobs.UnregisterGpuQueueParticipant(impl.Gpu->Handle);
            impl.Gpu.reset();
        }
        // The job service drained before the modules shut down, so every job is finalized; one that is not
        // stays (its image stays mapped too).
        std::erase_if(impl.PluginJobs, [](const std::unique_ptr<JobEntry>& entry) { return entry->Finalized; });
        // The own workspace leaves the services it attached to while they are alive (as the agent server's does).
        const auto detach = [&impl]
        {
            impl.OwnWorkspace.Detach();
            impl.Workspace = nullptr;
        };
        if (!impl.Plugin)
        {
            detach();
            return;
        }
        LoadedPlugin plugin = std::move(*impl.Plugin);
        impl.Plugin.reset();
        plugin.Image->Shutdown();
        auto* config = context.Services.Find<EngineConfigControl>();
        auto* ui = context.Services.Find<EditorUiHost>();
        auto* history = context.Services.Find<EditorCommandHistory>();
        if (config != nullptr && ui != nullptr && history != nullptr)
            impl.Unregister(plugin, *config, *ui, *history, false);
        impl.ClearDeferred();
        detach();
        intrinsic_plugin_event("host_shutdown");
        // Destroyed without Close, the image stays mapped: callables the engine destroys later (history entries, a
        // dropped call) still find their code, and the process ends.
    }

    PluginJobHandle PluginHostModule::SubmitJob(PluginJob job, std::string* refusal)
    {
        auto& impl = *m_Impl;
        if (impl.Draining || !job.Work || !job.Completion) return {};
        const EditorProcessingCommands commands = impl.JobCommands();
        const auto& surface = EditorProcessingCommandsAccess::Resolve(commands).JobCommands;
        if (!surface.Submit) return {};
        EditorJobIdentity identity{
            .Run = job.Run.Token,
            .RequestedDomain = job.Domain == PluginJobDomain::Gpu ? EditorJobDomain::GpuCompute : EditorJobDomain::Cpu};
        if (job.Output)
        {
            identity.EntityId = job.Output->StableEntityId;
            identity.Scope = ToEditorJobScope(job.Output->Property.Domain);
            identity.OutputSemantic = GeometryPresentationSlotSemantic::Displacement;
            identity.OutputName = job.Output->Property.Name;
            // A run's first job is admitted only while no other run writes the same output.
            if (!job.Run.IsValid() && surface.FindActive && surface.FindActive(identity))
            {
                if (refusal != nullptr)
                    *refusal = job.Label + " is already running on '" + identity.OutputName + "'; wait or cancel it.";
                return {};
            }
        }
        auto entry = std::make_unique<JobEntry>();
        entry->Job = std::move(job);
        JobEntry* raw = entry.get();
        JobDesc desc{};
        desc.DebugName = raw->Job.Label.empty() ? std::string{"plugin job"} : raw->Job.Label;
        desc.Work = [raw](const JobCancellation& cancellation)
        {
            raw->Job.Work([&cancellation] { return cancellation.IsCancelled(); });
            return JobResultEnvelope::Make(true);
        };
        // Main-thread plugin code runs counted, so an unload it triggers waits until it returned.
        Impl* host = &impl;
        if (raw->Job.IsReadyToApply)
            desc.IsReadyToApply = [raw, host]
            {
                const auto guard = host->Running();
                return raw->Job.IsReadyToApply();
            };
        // The plugin's verdict is the job's: a stale or failed publication is not reported as published.
        desc.PublishCompletion = [raw, host](KernelEventBus&, const JobResultEnvelope&)
        {
            raw->Finalized = true;
            const auto guard = host->Running();
            return raw->Job.Completion(true);
        };
        // A publication the plugin refused is finalized too; its Completion already ran.
        desc.FinalizeUnpublishedOnMainThread = [raw, host]
        {
            if (raw->Finalized) return;
            raw->Finalized = true;
            const auto guard = host->Running();
            (void)raw->Job.Completion(false);
        };
        entry->Token = surface.Submit(std::move(desc), std::move(identity));
        if (!entry->Token.IsValid()) return {};
        const PluginJobHandle handle{entry->Token};
        impl.PluginJobs.push_back(std::move(entry));
        return handle;
    }

    bool PluginHostModule::CancelJob(const PluginJobHandle job)
    {
        const EditorProcessingCommands commands = m_Impl->JobCommands();
        const JobToken run = m_Impl->RunOf(job.Token);
        return CancelEditorRunJobs(commands, std::span<const JobToken>{&run, 1u}).Requested > 0u;
    }

    void PluginHostModule::ReportJobOutcome(const PluginJobHandle job, const PluginJobDomain resolved,
                                            std::string diagnostic)
    {
        const EditorProcessingCommands commands = m_Impl->JobCommands();
        const auto& surface = EditorProcessingCommandsAccess::Resolve(commands).JobCommands;
        if (surface.ReportOutcome)
            surface.ReportOutcome(job.Token, {.ResolvedDomain = resolved == PluginJobDomain::Gpu ? EditorJobDomain::GpuCompute
                                                                                               : EditorJobDomain::Cpu,
                                              .Diagnostic = std::move(diagnostic)});
    }

    bool PluginHostModule::StartGpuWork(const AgentOperationContext& context, GpuQueueParticipantDesc work)
    {
        auto& impl = *m_Impl;
        if (impl.Draining || impl.Gpu || context.Jobs == nullptr || !work.RecordFrameCommands ||
            !work.HasInFlightWork || !work.ShutdownAfterDeviceIdle)
            return false;
        impl.Gpu = std::make_unique<GpuEntry>(GpuEntry{.Work = std::move(work)});
        // Host forwarders, so the service holds no plugin callable and recording stops while draining.
        GpuEntry* gpu = impl.Gpu.get();
        impl.Gpu->Handle = context.Jobs->RegisterGpuQueueParticipant({
            .DebugName = "plugin GPU work",
            .Scope = gpu->Work.Scope,
            .RecordFrameCommands = [this, gpu](RHI::ICommandContext& commands)
            {
                if (!m_Impl->Draining) gpu->Work.RecordFrameCommands(commands);
            },
            .DrainCompletedTransfers = [gpu]
            {
                if (gpu->Work.DrainCompletedTransfers) gpu->Work.DrainCompletedTransfers();
            },
            .HasInFlightWork = [gpu] { return gpu->Work.HasInFlightWork(); },
            .ShutdownAfterDeviceIdle = [gpu] { gpu->Work.ShutdownAfterDeviceIdle(); },
        });
        return true;
    }

    void PluginHostModule::RegisterOperations(AgentOperationRegistry& registry)
    {
        auto& impl = *m_Impl;
        impl.Operations = &registry;
        const bool hooks = impl.Options.TestHooks;
        constexpr const char* kFailAt = R"("fail_at":{"type":"string","enum":["dlopen","dlsym","config","operations","windows"]})";
        constexpr const char* kDrainHooks = R"("drain_deadline_ms":{"type":"integer","minimum":0},"simulate_gpu_stall":{"type":"boolean"})";
        (void)registry.Register({
            .Name = "plugin_load",
            .Title = "Load a plugin",
            .Description = "dlopen a private copy of a plugin built in this build tree (same build id), and commit its "
                           "registrations (config sections, tools, windows, history commands) on the next frame.",
            .InputSchemaJson = Schema(R"("path":{"type":"string"})", hooks,
                                      std::string{kFailAt} + R"(,"probe_pending":{"type":"boolean"},"simulate_drop":{"type":"boolean"})",
                                      "path"),
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, const std::string_view arguments) { return impl.Load(arguments); },
        });
        (void)registry.Register({
            .Name = "plugin_unload",
            .Title = "Unload the plugin",
            .Description = "From the next frame: its jobs cancelled and its GPU work retired (within 5 s, else "
                           "drain_timeout), the plugin's shutdown hook, all its registrations out (config section "
                           "payloads saved for a later load), then dlclose once none of its callables is alive.",
            .InputSchemaJson = Schema("", hooks, std::string{kDrainHooks} + R"(,"retain_callable":{"type":"boolean"})"),
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, const std::string_view arguments)
            { return impl.UnloadOrReload(arguments, false); },
        });
        (void)registry.Register({
            .Name = "plugin_reload",
            .Title = "Reload the plugin",
            .Description = "From the next frame: unload (draining first, as plugin_unload), then load the build at path "
                           "and reapply the saved config payloads. A failure after the unload leaves no plugin.",
            .InputSchemaJson = Schema(R"("path":{"type":"string"})", hooks, std::string{kFailAt} + "," + kDrainHooks, "path"),
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, const std::string_view arguments)
            { return impl.UnloadOrReload(arguments, true); },
        });
        (void)registry.Register({
            .Name = "plugin_list",
            .Title = "The loaded plugin",
            .Description = "The loaded plugin's generation, file and registrations, or null.",
            .Invoke = [&impl](const AgentOperationContext&, std::string_view) { return impl.List(); },
        });
        (void)registry.Register({
            .Name = "app_quit",
            .Title = "Quit the Sandbox",
            .Description = "Requests the engine's exit; the plugin host unloads the plugin during the shutdown. Refused "
                           "while a load, unload or reload has not finished.",
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, std::string_view)
            {
                // One at a time, so the exit never overlaps a reload.
                if (impl.LoadPending) return Fail("load_pending", "a load, unload or reload has not finished yet");
                if (impl.Options.RequestExit) impl.Options.RequestExit();
                return AgentOperationOutcome{.Text = R"({"quitting":true})"};
            },
        });
        if (!hooks) return;
        (void)registry.Register({
            .Name = "plugin_host_identity",
            .Title = "Host identity probes (test hook)",
            .Description = "Addresses and values of the host's probes, ImGui context and allocator, ImPlot context, "
                           "registered windows with their open state, the process id, the host undo value, the loaded "
                           "generation, live and invoked plugin callables per generation and kind, and every close "
                           "attempt.",
            .Invoke = [&impl](const AgentOperationContext&, std::string_view) { return impl.Identity(); },
        });
    }
}
