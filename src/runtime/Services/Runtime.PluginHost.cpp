module;

#include <algorithm>
#include <atomic>
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
#include <thread>
#include <type_traits>
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
import Extrinsic.Runtime.EditorWorkspaceSlot;
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

        // A plugin's GPU work: the job service holds host forwarders to its callables; the host keeps the in-flight
        // probe's for its drain and identity probe.
        struct GpuEntry
        {
            std::function<bool()> InFlight{};
            GpuQueueParticipantHandle Handle{};
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

    // One plugin: its image, what it registered, and its jobs, GPU work, drain state and deferred results. The host
    // lists the loaded ones; host forwarders share a slot, so a copy that outlives its plugin finds its entry empty.
    struct PluginSlot
    {
        std::string Name{};
        std::unique_ptr<PluginImage> Image{};
        int Generation{0};
        std::vector<std::string> Operations{};
        std::vector<std::string> Sections{};
        std::vector<EditorWindowHandle> Windows{};
        std::vector<std::string> HistoryCommands{};
        std::vector<std::unique_ptr<JobEntry>> Jobs{};
        std::shared_ptr<GpuEntry> Gpu{};
        std::vector<std::weak_ptr<DeferredEntry>> Deferred{};
        std::vector<std::function<void()>> Clearers{}; // empty the held callables of its forwarders
        std::function<void()> RetainOperation{}; // test hook: keeps a copy of its first tool's callable alive
        bool Draining{false};
        // Its code the host is running now (main thread and GPU callbacks); an unload waits for zero, since it
        // would destroy the running callable and unmap its code.
        std::atomic<int> Running{0};

        // Destroys the plugin's callables of every pending call and every forwarder (before a close); the
        // forwarders stay, inert.
        void ClearCallables()
        {
            for (const auto& held : Deferred)
                if (const auto entry = held.lock()) *entry = {};
            Deferred.clear();
            for (const auto& clear : Clearers) clear();
            Clearers.clear();
        }
    };

    namespace
    {
        // The plugin whose code the host is running on this thread (the caller of the contract functions).
        thread_local PluginSlot* t_Caller{nullptr};

        // Host code entering a plugin's code: counted as running, and the plugin is the caller until it returns
        // (then the previous caller is again: a plugin's code that runs another's).
        class Entered
        {
        public:
            explicit Entered(PluginSlot& slot) : m_Slot(slot), m_Previous(t_Caller)
            {
                ++m_Slot.Running;
                t_Caller = &m_Slot;
            }
            ~Entered()
            {
                t_Caller = m_Previous;
                --m_Slot.Running;
            }
            Entered(const Entered&) = delete;
            Entered& operator=(const Entered&) = delete;

        private:
            PluginSlot& m_Slot;
            PluginSlot* m_Previous;
        };

        // A forwarder to a plugin callable held by the slot's clearable entry: it enters the slot, and calls
        // nothing once the entry is cleared (returning `inert`).
        // `retain` (test hook) receives a function that copies the plugin's callable out of the entry, beyond the
        // clearing, so the close must refuse.
        template <class R, class... Args>
        std::function<R(Args...)> Forward(const std::shared_ptr<PluginSlot>& slot, std::function<R(Args...)> fn,
                                          std::function<R()> inert = {}, std::function<void()>* retain = nullptr)
        {
            if (!fn) return {};
            auto held = std::make_shared<std::function<R(Args...)>>(std::move(fn));
            // Strong: the plugin's callable lives until ClearCallables, whichever registry drops its forwarder first.
            slot->Clearers.push_back([held] { *held = {}; });
            if (retain != nullptr)
                *retain = [weak = std::weak_ptr(held)]
                {
                    static auto* kept = new std::vector<std::function<R(Args...)>>(); // never destroyed
                    if (const auto entry = weak.lock(); entry && *entry) kept->push_back(*entry);
                };
            return [slot, held, inert = std::move(inert)](Args... args) -> R
            {
                if (!*held)
                {
                    if constexpr (std::is_void_v<R>) return;
                    else return inert ? inert() : R{};
                }
                const Entered entered(*slot);
                return (*held)(std::forward<Args>(args)...);
            };
        }

        // The plugin's section callbacks enter its slot (at staging for a startup plugin, before the boot calls
        // them).
        void ForwardSection(const std::shared_ptr<PluginSlot>& slot, Core::Config::EngineConfigSectionRegistration& section)
        {
            section.Validate = Forward(slot, std::move(section.Validate));
            section.OnChanged = Forward(slot, std::move(section.OnChanged));
        }

        // Open's Register, entered (a call into the plugin while it is not loaded, so it cannot submit work). A Register
        // that fails cleaned up itself: its staged callables die and the image closes, with no shutdown hook.
        bool RegisterInto(PluginSlot& slot, PluginRegistrations& staged, std::string& error)
        {
            bool registered = false;
            {
                const Entered entered(slot);
                registered = slot.Image->Register(staged, error);
            }
            if (registered) return true;
            const int generation = staged.Generation;
            staged = {};
            (void)Close(std::move(slot.Image), generation);
            return false;
        }

        // The plugin's shutdown hook, entered: its calls are refused (it is not loaded, or no longer).
        void ShutdownInto(PluginSlot& slot)
        {
            const Entered entered(slot);
            slot.Image->Shutdown();
        }

        // A plugin whose Register succeeded and that is not committed: every such Register gets one shutdown hook, then
        // its staged callables and its forwarders' die, then the close. `keepMapped`: something may still hold its
        // boot sections (inert now).
        void Discard(StagedPlugin& plugin, const bool keepMapped = false)
        {
            auto& slot = *plugin.Slot;
            ShutdownInto(slot);
            plugin.Registrations = {};
            slot.ClearCallables();
            if (!keepMapped) (void)Close(std::move(slot.Image), slot.Generation);
        }
    }

    std::vector<StagedPlugin> StageStartupPlugins(std::vector<std::unique_ptr<PluginImage>> images,
                                                  Core::Config::EngineConfigSectionRegistry& boot,
                                                  std::vector<std::string>& errors)
    {
        std::vector<StagedPlugin> staged{};
        for (auto& image : images)
        {
            auto slot = std::make_shared<PluginSlot>();
            slot->Image = std::move(image);
            StagedPlugin plugin{.Slot = slot};
            std::string error{};
            const auto drop = [&](std::string why)
            {
                for (const auto& name : plugin.SectionNames) (void)boot.Unregister(name);
                Discard(plugin);
                errors.push_back(std::move(why));
            };
            if (!RegisterInto(*slot, plugin.Registrations, error))
            {
                errors.push_back("register failed: " + error);
                continue;
            }
            slot->Name = plugin.Registrations.Name;
            slot->Generation = plugin.Registrations.Generation;
            if (slot->Name.empty() || std::ranges::any_of(staged, [&](const StagedPlugin& other)
                                                          { return other.Slot->Name == slot->Name; }))
            {
                drop("plugin '" + slot->Name + "': a startup plugin needs a name of its own");
                continue;
            }
            bool registered = true;
            for (auto& section : plugin.Registrations.ConfigSections)
            {
                std::string name = section.DefaultSection.Name;
                ForwardSection(slot, section);
                if (!boot.Register(std::move(section)))
                {
                    registered = false;
                    break;
                }
                plugin.SectionNames.push_back(std::move(name));
            }
            if (!registered)
            {
                drop("plugin '" + slot->Name + "': a config section name is taken");
                continue;
            }
            plugin.Registrations.ConfigSections.clear();
            staged.push_back(std::move(plugin));
        }
        return staged;
    }

    struct PluginHostModule::Impl
    {
        PluginHostOptions Options{};
        ServiceRegistry* Services{nullptr};
        AgentOperationRegistry* Operations{nullptr};
        JobService* Jobs{nullptr};
        std::thread::id MainThread{std::this_thread::get_id()};
        std::vector<std::shared_ptr<PluginSlot>> Plugins{}; // the loaded ones, in load order
        bool LoadPending{false}; // a load, unload or reload whose continuation has not finished (or been dropped)
        // Active payloads of an unloaded plugin's sections, reapplied when a plugin registers them again.
        std::map<std::string, Core::Config::EngineConfigSection> SavedSections{};
        // The workspace plugin jobs belong to: the app's (UseEditorWorkspace), else the host's own.
        EditorWorkspaceSlot Workspace{};

        std::shared_ptr<PluginSlot> Find(const std::string_view name) const
        {
            const auto found = std::ranges::find_if(Plugins, [&](const auto& slot) { return slot->Name == name; });
            return found != Plugins.end() ? *found : nullptr;
        }

        // The plugin a contract call acts for: the one whose code is running, else (a call from outside any plugin
        // call, a test's own code) the only loaded one. Main thread only.
        std::shared_ptr<PluginSlot> Caller(std::string* refusal) const
        {
            const auto refuse = [&](std::string why) -> std::shared_ptr<PluginSlot>
            {
                if (refusal != nullptr) *refusal = std::move(why);
                return nullptr;
            };
            if (std::this_thread::get_id() != MainThread) return refuse("plugin contract calls run on the main thread");
            if (t_Caller != nullptr)
            {
                const auto found = std::ranges::find_if(Plugins, [](const auto& slot) { return slot.get() == t_Caller; });
                return found != Plugins.end() ? *found : refuse("the calling plugin is not loaded");
            }
            if (Plugins.size() == 1u) return Plugins.front();
            return refuse(Plugins.empty() ? "no plugin is loaded" : "several plugins are loaded; call from a plugin");
        }

        // Keeps a deferred result's callables in a host entry of the plugin and hands the agent server forwarders.
        AgentOperationOutcome HoldDeferred(const std::shared_ptr<PluginSlot>& slot, AgentOperationOutcome outcome)
        {
            if (!outcome.Continuation && !outcome.Progress && !outcome.Cancel) return outcome;
            auto entry = std::make_shared<DeferredEntry>(DeferredEntry{
                std::move(outcome.Continuation), std::move(outcome.Progress), std::move(outcome.Cancel)});
            std::erase_if(slot->Deferred, [](const std::weak_ptr<DeferredEntry>& held) { return held.expired(); });
            slot->Deferred.push_back(entry);
            if (entry->Continuation)
                outcome.Continuation = [entry, slot](const AgentOperationContext& context, AgentOperationOutcome& result)
                {
                    if (!entry->Continuation)
                    {
                        result = Fail("plugin_unloaded", "the plugin was unloaded before this call finished");
                        return true;
                    }
                    const Entered entered(*slot);
                    return entry->Continuation(context, result);
                };
            if (entry->Progress)
                outcome.Progress = [entry, slot](const AgentOperationContext& context)
                {
                    if (!entry->Progress) return decltype(entry->Progress(context)){};
                    const Entered entered(*slot);
                    return entry->Progress(context);
                };
            if (entry->Cancel)
                outcome.Cancel = [entry, slot](const AgentOperationContext& context)
                {
                    if (!entry->Cancel) return std::string{};
                    const Entered entered(*slot);
                    return entry->Cancel(context);
                };
            return outcome;
        }

        // The run a job of the plugin belongs to (its run's first job); none when the job is not the plugin's.
        static std::optional<JobToken> RunOf(const PluginSlot& slot, const JobToken job)
        {
            for (const auto& entry : slot.Jobs)
                if (entry->Token == job) return entry->Job.Run.IsValid() ? entry->Job.Run.Token : job;
            return std::nullopt;
        }

        // Cancels every run of the plugin's jobs through the workspace, so its session records the requests (the
        // Jobs window then offers no second cancel); jobs outside it are cancelled directly.
        void CancelRuns(const PluginSlot& slot)
        {
            std::vector<JobToken> runs{};
            for (const auto& entry : slot.Jobs)
                if (const auto run = RunOf(slot, entry->Token); run && std::ranges::find(runs, *run) == runs.end())
                    runs.push_back(*run);
            const EditorProcessingCommands commands = Workspace.Commands();
            (void)CancelEditorRunJobs(commands, runs);
            for (const auto& entry : slot.Jobs) (void)Jobs->Cancel(entry->Token);
        }

        // Startup plugins never committed (the host goes before OnResolve): their hooks run and their callables die; the
        // images stay mapped, as the boot sections may outlive the host.
        void DiscardStartup()
        {
            for (auto& staged : Options.Startup)
                if (staged.Slot && staged.Slot->Image) Discard(staged, true);
            Options.Startup.clear();
        }

        // Commits the staged startup plugins in order; their config sections are already registered (the boot
        // registry). One that fails is logged and closed, its own sections removed; the others are unaffected.
        void CommitStartup()
        {
            auto startup = std::move(Options.Startup);
            Options.Startup.clear();
            auto* config = Services->Find<EngineConfigControl>();
            auto* ui = Services->Find<EditorUiHost>();
            auto* history = Services->Find<EditorCommandHistory>();
            for (auto& staged : startup)
            {
                const auto slot = staged.Slot;
                slot->Sections = std::move(staged.SectionNames);
                const auto failure = config && ui && history
                    ? Commit(staged.Registrations, slot, *config, *ui, *history, {})
                    : std::optional<std::string>{"editor UI host, config control or history missing"};
                if (failure)
                {
                    Core::Log::Error("[PluginHost] the startup plugin '{}' was not committed: {}", slot->Name, *failure);
                    // Its boot sections must go before the close; without config control they cannot, so the image
                    // stays mapped (their callbacks call nothing after the discard).
                    if (config != nullptr)
                        for (const auto& name : slot->Sections) (void)config->UnregisterSection(name);
                    Discard(staged, config == nullptr);
                    continue;
                }
                staged.Registrations = {};
                Plugins.push_back(slot);
                intrinsic_plugin_event("commit");
            }
        }

        std::unique_ptr<PluginImage> Open(const std::string& path, const std::string_view failAt,
                                          AgentOperationOutcome& failure) const
        {
            return Options.Open ? Options.Open(path, failAt, failure) : OpenSharedPlugin(path, failAt, failure);
        }

        // Open and Register; on failure the image is closed again and `failure` says why.
        std::optional<StagedPlugin> Stage(const std::string& path, const std::string_view failAt,
                                       AgentOperationOutcome& failure) const
        {
            auto image = Open(path, failAt, failure);
            if (!image) return std::nullopt;
            StagedPlugin candidate{.Slot = std::make_shared<PluginSlot>()};
            candidate.Slot->Image = std::move(image);
            std::string error{};
            if (!RegisterInto(*candidate.Slot, candidate.Registrations, error))
            {
                failure = Fail("register_failed", error);
                return std::nullopt;
            }
            candidate.Slot->Name = candidate.Registrations.Name;
            candidate.Slot->Generation = candidate.Registrations.Generation;
            return candidate;
        }

        // Owns a pending candidate's image: a load whose continuation is dropped (the caller disconnected) or
        // cannot run closes the image here. Finish takes the candidate out, and with it the ownership.
        static std::shared_ptr<StagedPlugin> Own(StagedPlugin candidate)
        {
            return {new StagedPlugin(std::move(candidate)), [](StagedPlugin* pending)
                    {
                        if (pending->Slot && pending->Slot->Image) Discard(*pending);
                        delete pending;
                    }};
        }

        // All or nothing: any failure unregisters every config section, window and history command this commit
        // added (their registration runs no callback, so nothing else needs undoing). The operation registry is
        // replaced last, so a failure never has to restore it. Every callable is registered through a forwarder
        // that enters the plugin's slot.
        std::optional<std::string> Commit(PluginRegistrations& staged, const std::shared_ptr<PluginSlot>& slot,
                                          EngineConfigControl& config, EditorUiHost& ui,
                                          EditorCommandHistory& history, const std::string_view failAt)
        {
            PluginSlot& plugin = *slot;
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
                ForwardSection(slot, section);
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
                auto invoke = Forward(slot, std::move(operation.Invoke),
                                      std::function<AgentOperationOutcome()>{[]
                                      { return Fail("plugin_unloaded", "the plugin is not loaded"); }},
                                      plugin.RetainOperation ? nullptr : &plugin.RetainOperation);
                operation.Invoke = [this, slot, invoke = std::move(invoke)](const AgentOperationContext& context,
                                                                           const std::string_view arguments)
                { return HoldDeferred(slot, invoke(context, arguments)); };
                if (!operationsCopy.Register(std::move(operation))) return rollback("operations: operation rejected");
                plugin.Operations.push_back(std::move(name));
            }
            if (failAt == "operations") return rollback("operations: injected failure after the operations");

            for (auto& window : staged.Windows)
            {
                window.Draw = Forward(slot, std::move(window.Draw));
                window.OpenStateChanged = Forward(slot, std::move(window.OpenStateChanged));
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
                    [apply = Forward(slot, std::move(command.Apply))](const std::string_view payload, const bool undo)
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
        Json Restore(const PluginSlot& plugin, EngineConfigControl& config)
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

        AgentOperationOutcome Finish(StagedPlugin candidate, EngineConfigControl& config, EditorUiHost& ui,
                                     EditorCommandHistory& history, const std::string_view failAt)
        {
            const auto slot = candidate.Slot;
            if (slot->Name.empty())
            {
                Discard(candidate);
                return Fail("commit_failed", "a plugin needs a name");
            }
            if (Find(slot->Name))
            {
                Discard(candidate);
                return Fail("already_loaded", "a plugin named '" + slot->Name + "' is already loaded");
            }
            const auto failure = Commit(candidate.Registrations, slot, config, ui, history, failAt);
            if (failure)
            {
                Discard(candidate);
                return Fail("commit_failed", *failure);
            }
            candidate.Registrations = {}; // leftover staged callables die
            Plugins.push_back(slot);
            intrinsic_plugin_event("commit");
            const Json restored = Restore(*slot, config);
            return {.Text = Json{{"loaded", true}, {"plugin", slot->Name}, {"generation", slot->Generation},
                                 {"config", restored}}.dump()};
        }

        // Every registration of the plugin out; with `save`, its sections' active payloads are kept for a later
        // load first.
        void Unregister(const PluginSlot& plugin, EngineConfigControl& config, EditorUiHost& ui,
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

        // Shutdown hook, then every registration out (section payloads saved first), its callables cleared, then the
        // close. On callables_alive the plugin is unregistered but stays mapped.
        std::optional<std::string> Unload(const std::shared_ptr<PluginSlot>& slot, EngineConfigControl& config,
                                          EditorUiHost& ui, EditorCommandHistory& history)
        {
            std::erase(Plugins, slot); // its calls are refused from here: the caller is not loaded
            ShutdownInto(*slot);
            Unregister(*slot, config, ui, history, true);
            slot->ClearCallables();
            intrinsic_plugin_event("unload");
            if (!Close(std::move(slot->Image), slot->Generation)) return std::string{"callables_alive"};
            return std::nullopt;
        }

        // A commit or an unload waits while any plugin's code runs on the main thread: the registries it changes
        // may hold the very callable that is running (a tool that polls a load or another plugin's unload).
        bool AnyRunning() const
        {
            return std::ranges::any_of(Plugins, [](const auto& slot) { return slot->Running > 0; });
        }

        // One frame of draining a plugin: destroys the callables of its finalized jobs, and unregisters its GPU work
        // once its last frame retired. True when nothing is left. simulateGpuStall (test hook) treats the GPU work
        // as never retiring.
        bool DrainStep(PluginSlot& slot, const bool simulateGpuStall)
        {
            std::erase_if(slot.Jobs, [](const std::unique_ptr<JobEntry>& entry) { return entry->Finalized; });
            if (slot.Gpu && !simulateGpuStall)
            {
                if (!slot.Gpu->InFlight())
                {
                    Jobs->UnregisterGpuQueueParticipant(slot.Gpu->Handle); // not in flight: no device wait
                    slot.Gpu.reset();
                }
            }
            return slot.Jobs.empty() && !slot.Gpu;
        }

        // One poll of a continuation's drain of a plugin. The first poll starts it: its submissions refused, its
        // jobs cancelled. The deadline is checked before success, so work that ends after it still times out. On
        // timeout admission and GPU recording come back (as they do when the continuation is dropped, through its
        // reservation).
        // shortcut: a cancelled job that has not started is reaped only when a worker reaches it, so another plugin's
        // long jobs on every worker delay this drain, up to drain_timeout (the plugin stays; retry). Reap queued jobs
        // at cancel in the JobService if that bites.
        DrainState PollDrain(PluginSlot& slot, std::optional<std::chrono::steady_clock::time_point>& deadline,
                             const std::chrono::milliseconds budget, const bool simulateGpuStall)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!deadline)
            {
                deadline = now + budget;
                slot.Draining = true;
                CancelRuns(slot);
            }
            if (now >= *deadline)
            {
                slot.Draining = false;
                return DrainState::TimedOut;
            }
            if (!DrainStep(slot, simulateGpuStall)) return DrainState::Pending;
            slot.Draining = false;
            return DrainState::Drained;
        }

        // The commit, unload and reload run on a later frame, after this call's dispatch through the registry
        // returned; the reservation ends when the continuation is destroyed (completed or dropped).
        std::shared_ptr<void> Reserve(std::weak_ptr<PluginSlot> draining = {})
        {
            LoadPending = true;
            return {nullptr, [this, draining = std::move(draining)](void*)
                    {
                        LoadPending = false;
                        // A drain ends with its continuation; a dropped one must not keep refusing work.
                        if (const auto slot = draining.lock()) slot->Draining = false;
                    }};
        }

        struct Hosts
        {
            EngineConfigControl& Config;
            EditorUiHost& Ui;
            EditorCommandHistory& History;
        };

        std::optional<Hosts> FindHosts(const AgentOperationContext& context) const
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
            if (LoadPending) return Fail("load_pending", "another load has not committed yet");

            AgentOperationOutcome failure{};
            auto candidate = Stage(path, failAt, failure);
            if (!candidate) return failure; // a taken name is refused at the commit (Finish)
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
                const auto hosts = FindHosts(context);
                if (!hosts)
                {
                    result = Fail("host_unavailable", "editor UI host, config control or history missing");
                    return true;
                }
                if (AnyRunning()) return false; // polled from plugin code: commit once it returned
                result = Finish(std::exchange(*opened, StagedPlugin{}), hosts->Config, hosts->Ui, hosts->History, failAt);
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

        // The plugin a call names (`plugin`), or the only loaded one when it names none.
        std::shared_ptr<PluginSlot> Target(const Json& arguments, AgentOperationOutcome& failure) const
        {
            if (arguments.contains("plugin"))
            {
                const auto named = arguments["plugin"].is_string() ? Find(arguments["plugin"].get<std::string>()) : nullptr;
                if (!named) failure = Fail("not_loaded", "no plugin of that name is loaded");
                return named;
            }
            if (Plugins.size() == 1u) return Plugins.front();
            if (Plugins.empty())
            {
                failure = Fail("not_loaded", "no plugin is loaded");
                return nullptr;
            }
            std::string names{};
            for (const auto& slot : Plugins) names += (names.empty() ? "" : ", ") + slot->Name;
            failure = Fail("invalid_params", "several plugins are loaded (" + names + "); name one with plugin");
            return nullptr;
        }

        // plugin_unload, and plugin_reload when `path` is set: drain the plugin, unload it, then open, register and
        // commit the new build, which must carry the same name. A failure after the unload leaves that plugin
        // unloaded, with the reason; the other plugins are untouched.
        AgentOperationOutcome UnloadOrReload(const std::string_view argumentsJson, const bool reload)
        {
            const Json arguments = Json::parse(argumentsJson, nullptr, false);
            if (!arguments.is_object() || (reload && !(arguments.contains("path") && arguments["path"].is_string())))
                return Fail("invalid_params", "path is required");
            // A static build without an opener of its own (tests) has no plugin to reload: say why.
            if (reload && !Options.Open && !PluginsSupported()) return Fail("plugins_unsupported", kUnsupported);
            AgentOperationOutcome failure{};
            const auto slot = Target(arguments, failure);
            if (!slot) return failure;
            if (LoadPending) return Fail("load_pending", "another load has not committed yet");
            const std::string path = reload ? arguments["path"].get<std::string>() : std::string{};
            const std::string failAt = arguments.value("fail_at", std::string{});
            // Test hooks: a drain deadline other than 5 s, and GPU work treated as never retiring.
            const auto budget = std::chrono::milliseconds{arguments.value("drain_deadline_ms", kDrainDeadline.count())};
            const bool simulateGpuStall = arguments.value("simulate_gpu_stall", false);
            // Test hook: keep a copy of one plugin callable alive, so the close must refuse.
            if (!reload && arguments.value("retain_callable", false) && slot->RetainOperation) slot->RetainOperation();
            AgentOperationOutcome outcome{.Text = "{}"};
            outcome.Continuation = [this, slot, reservation = Reserve(slot), reload, path, failAt, budget,
                                    simulateGpuStall, deadline = std::optional<std::chrono::steady_clock::time_point>{}](
                const AgentOperationContext& context, AgentOperationOutcome& result) mutable
            {
                const auto hosts = FindHosts(context);
                if (!hosts)
                {
                    result = Fail("host_unavailable", "editor UI host, config control or history missing");
                    return true;
                }
                // Drain first, frame by frame; on the deadline the plugin stays loaded and registered.
                // Not while plugin code is running (a deferred call that unloads its own plugin or another): wait a frame.
                if (AnyRunning()) return false;
                const DrainState drain = PollDrain(*slot, deadline, budget, simulateGpuStall);
                if (drain == DrainState::Pending) return false;
                if (drain == DrainState::TimedOut)
                {
                    result = Fail("drain_timeout", "plugin work did not finish before the deadline; the plugin stays loaded");
                    return true;
                }
                const std::string name = slot->Name;
                if (const auto error = Unload(slot, hosts->Config, hosts->Ui, hosts->History))
                {
                    result = Fail(*error, "a callable of the closing generation is still alive; the image stays mapped");
                    return true;
                }
                if (!reload)
                {
                    result = {.Text = Json{{"unloaded", true}, {"plugin", name}, {"close", Closes().back()}}.dump()};
                    return true;
                }
                AgentOperationOutcome failure{};
                auto candidate = Stage(path, failAt, failure);
                if (candidate && candidate->Slot->Name != name)
                {
                    const std::string other = candidate->Slot->Name;
                    Discard(*candidate);
                    result = Fail("name_mismatch", "the build at path is the plugin '" + other + "', not '" + name +
                                                       "'; '" + name + "' stays unloaded");
                    return true;
                }
                result = candidate ? Finish(std::move(*candidate), hosts->Config, hosts->Ui, hosts->History, failAt)
                                   : failure;
                return true;
            };
            return outcome;
        }

        static Json PluginJson(const PluginSlot& slot)
        {
            return {{"name", slot.Name},
                    {"generation", slot.Generation},
                    {"file", slot.Image->File().string()},
                    {"operations", slot.Operations},
                    {"config_sections", slot.Sections},
                    {"windows", slot.Windows.size()},
                    {"history_commands", slot.HistoryCommands}};
        }

        AgentOperationOutcome List() const
        {
            Json loaded = Json::array();
            for (const auto& slot : Plugins) loaded.push_back(PluginJson(*slot));
            return {.Text = Json{{"loaded", loaded}}.dump()};
        }

        Json PluginJobsJson() const
        {
            Json jobs = Json::array();
            for (const auto& slot : Plugins)
                for (const auto& entry : slot->Jobs)
                    jobs.push_back({{"plugin", slot->Name},
                                    {"token", std::to_string(entry->Token.Index) + ":" +
                                                  std::to_string(entry->Token.Generation)},
                                    {"state", std::string{ToString(Jobs->GetState(entry->Token))}},
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
            Json plugins = Json::array();
            for (const auto& slot : Plugins)
                plugins.push_back({{"name", slot->Name}, {"generation", slot->Generation},
                                   {"range", MappedRange(slot->Image->File())}, {"draining", slot->Draining},
                                   {"gpu", slot->Gpu ? Json{{"in_flight", slot->Gpu->InFlight()}} : Json(nullptr)}});
            // The single plugin's fields, as the single-plugin drivers read them; null unless exactly one is loaded.
            const PluginSlot* only = Plugins.size() == 1u ? Plugins.front().get() : nullptr;
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
                {"loaded", only ? Json(only->Generation) : Json(nullptr)},
                {"loaded_range", only ? MappedRange(only->Image->File()) : Json::array()},
                {"plugins", plugins},
                {"jobs", PluginJobsJson()},
                {"gpu", only ? plugins.front()["gpu"] : Json(nullptr)},
                {"draining", std::ranges::any_of(Plugins, [](const auto& slot) { return slot->Draining; })},
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
        // The services may be gone (an engine that failed before OnResolve): hooks run here find none.
        if (g_Current == this) g_Current = nullptr;
        m_Impl->Services = nullptr;
        m_Impl->DiscardStartup();
    }

    PluginHostModule* PluginHostModule::Current() noexcept { return g_Current; }
    ServiceRegistry* PluginHostModule::Services() const noexcept { return m_Impl->Services; }
    bool PluginHostModule::TestHooks() const noexcept { return m_Impl->Options.TestHooks; }

    RuntimeModuleResult PluginHostModule::OnRegister(EngineSetup& setup)
    {
        m_Impl->Services = &setup.Services();
        m_Impl->Jobs = &setup.Jobs();
        return setup.Services().Provide<PluginHostModule>(*this, Name());
    }

    RuntimeModuleResult PluginHostModule::OnResolve(EngineSetup& setup)
    {
        auto& impl = *m_Impl;
        impl.Workspace.AttachOwn(setup.Worlds(), setup.Services());
        if (!impl.Options.Startup.empty()) impl.CommitStartup();
        return RuntimeModuleOk();
    }

    void PluginHostModule::UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace.Use(workspace);
    }

    void PluginHostModule::ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept
    {
        m_Impl->Workspace.Release(workspace);
    }

    void PluginHostModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        auto& impl = *m_Impl;
        impl.DiscardStartup(); // an engine that never resolved
        auto* config = context.Services.Find<EngineConfigControl>();
        auto* ui = context.Services.Find<EditorUiHost>();
        auto* history = context.Services.Find<EditorCommandHistory>();
        const bool any = !impl.Plugins.empty();
        // Last loaded first, each off the list before its hook (its calls are refused from there). Destroyed without
        // Close, the images stay mapped: callables the engine destroys later (history entries, a dropped call) still
        // find their code, and the process ends.
        while (!impl.Plugins.empty())
        {
            const auto held = std::move(impl.Plugins.back());
            impl.Plugins.pop_back();
            PluginSlot& slot = *held;
            if (slot.Gpu)
            {
                // Already shut down with every queue participant at the engine's BeginShutdown; a no-op then.
                context.Jobs.UnregisterGpuQueueParticipant(slot.Gpu->Handle);
                slot.Gpu.reset();
            }
            // The job service drained before the modules shut down, so every job is finalized; one that is not
            // stays (its image stays mapped too).
            std::erase_if(slot.Jobs, [](const std::unique_ptr<JobEntry>& entry) { return entry->Finalized; });
            ShutdownInto(slot);
            if (config != nullptr && ui != nullptr && history != nullptr)
                impl.Unregister(slot, *config, *ui, *history, false);
            slot.ClearCallables();
        }
        // The own workspace leaves the services it attached to while they are alive (as the agent server's does).
        impl.Workspace.DetachOwn();
        if (any) intrinsic_plugin_event("host_shutdown");
    }

    PluginJobHandle PluginHostModule::SubmitJob(PluginJob job, std::string* refusal)
    {
        auto& impl = *m_Impl;
        const auto slot = impl.Caller(refusal);
        if (!slot || slot->Draining || !job.Work || !job.Completion) return {};
        // A job joins only a run of its own plugin.
        if (job.Run.IsValid() && !Impl::RunOf(*slot, job.Run.Token)) return {};
        const EditorProcessingCommands commands = impl.Workspace.Commands();
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
            // A run's first job is admitted only while no other run (of any plugin) writes the same output.
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
        // A worker runs Work; contract calls from there are refused (they run on the main thread).
        desc.Work = [raw](const JobCancellation& cancellation)
        {
            raw->Job.Work([&cancellation] { return cancellation.IsCancelled(); });
            return JobResultEnvelope::Make(true);
        };
        // Main-thread plugin code runs entered, so an unload it triggers waits until it returned.
        if (raw->Job.IsReadyToApply)
            desc.IsReadyToApply = [raw, slot]
            {
                const Entered entered(*slot);
                return raw->Job.IsReadyToApply();
            };
        // The plugin's verdict is the job's: a stale or failed publication is not reported as published.
        desc.PublishCompletion = [raw, slot](KernelEventBus&, const JobResultEnvelope&)
        {
            raw->Finalized = true;
            const Entered entered(*slot);
            return raw->Job.Completion(true);
        };
        // A publication the plugin refused is finalized too; its Completion already ran.
        desc.FinalizeUnpublishedOnMainThread = [raw, slot]
        {
            if (raw->Finalized) return;
            raw->Finalized = true;
            const Entered entered(*slot);
            (void)raw->Job.Completion(false);
        };
        entry->Token = surface.Submit(std::move(desc), std::move(identity));
        if (!entry->Token.IsValid()) return {};
        const PluginJobHandle handle{entry->Token};
        slot->Jobs.push_back(std::move(entry));
        return handle;
    }

    bool PluginHostModule::CancelJob(const PluginJobHandle job)
    {
        const auto slot = m_Impl->Caller(nullptr);
        const auto run = slot ? Impl::RunOf(*slot, job.Token) : std::nullopt;
        if (!run) return false; // only the plugin's own jobs
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        return CancelEditorRunJobs(commands, std::span<const JobToken>{&*run, 1u}).Requested > 0u;
    }

    void PluginHostModule::ReportJobOutcome(const PluginJobHandle job, const PluginJobDomain resolved,
                                            std::string diagnostic)
    {
        const auto slot = m_Impl->Caller(nullptr);
        if (!slot || !Impl::RunOf(*slot, job.Token)) return; // only the plugin's own jobs
        const EditorProcessingCommands commands = m_Impl->Workspace.Commands();
        const auto& surface = EditorProcessingCommandsAccess::Resolve(commands).JobCommands;
        if (surface.ReportOutcome)
            surface.ReportOutcome(job.Token, {.ResolvedDomain = resolved == PluginJobDomain::Gpu ? EditorJobDomain::GpuCompute
                                                                                               : EditorJobDomain::Cpu,
                                              .Diagnostic = std::move(diagnostic)});
    }

    bool PluginHostModule::StartGpuWork(const AgentOperationContext& context, GpuQueueParticipantDesc work)
    {
        auto& impl = *m_Impl;
        const auto slot = impl.Caller(nullptr);
        if (!slot || slot->Draining || slot->Gpu || context.Jobs == nullptr ||
            !work.RecordFrameCommands || !work.HasInFlightWork || !work.ShutdownAfterDeviceIdle)
            return false;
        // Host forwarders, so the service holds no plugin callable and a copy it keeps after the unload calls
        // nothing; recording stops while draining.
        auto gpu = std::make_shared<GpuEntry>(GpuEntry{.InFlight = Forward(slot, std::move(work.HasInFlightWork))});
        gpu->Handle = context.Jobs->RegisterGpuQueueParticipant({
            .DebugName = "plugin GPU work",
            .Scope = work.Scope,
            .RecordFrameCommands = [slot, record = Forward(slot, std::move(work.RecordFrameCommands))](
                                       RHI::ICommandContext& commands)
            {
                if (!slot->Draining) record(commands);
            },
            .DrainCompletedTransfers = Forward(slot, std::move(work.DrainCompletedTransfers)),
            .HasInFlightWork = gpu->InFlight,
            .ShutdownAfterDeviceIdle = Forward(slot, std::move(work.ShutdownAfterDeviceIdle)),
        });
        slot->Gpu = std::move(gpu);
        return true;
    }

    void PluginHostModule::RegisterOperations(AgentOperationRegistry& registry)
    {
        auto& impl = *m_Impl;
        impl.Operations = &registry;
        const bool hooks = impl.Options.TestHooks;
        constexpr const char* kFailAt = R"("fail_at":{"type":"string","enum":["dlopen","dlsym","config","operations","windows"]})";
        constexpr const char* kDrainHooks = R"("drain_deadline_ms":{"type":"integer","minimum":0},"simulate_gpu_stall":{"type":"boolean"})";
        constexpr const char* kPlugin = R"("plugin":{"type":"string","description":"The plugin's name (plugin_list); optional while one plugin is loaded."})";
        (void)registry.Register({
            .Name = "plugin_load",
            .Title = "Load a plugin",
            .Description = "dlopen a private copy of a plugin built in this build tree (same build id), and commit its "
                           "registrations (config sections, tools, windows, history commands) on the next frame. "
                           "Refused when a plugin of its name is loaded.",
            .InputSchemaJson = Schema(R"("path":{"type":"string"})", hooks,
                                      std::string{kFailAt} + R"(,"probe_pending":{"type":"boolean"},"simulate_drop":{"type":"boolean"})",
                                      "path"),
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, const std::string_view arguments) { return impl.Load(arguments); },
        });
        (void)registry.Register({
            .Name = "plugin_unload",
            .Title = "Unload a plugin",
            .Description = "From the next frame: its jobs cancelled and its GPU work retired (within 5 s, else "
                           "drain_timeout), the plugin's shutdown hook, all its registrations out (config section "
                           "payloads saved for a later load), then dlclose once none of its callables is alive. Other "
                           "plugins keep running.",
            .InputSchemaJson = Schema(kPlugin, hooks, std::string{kDrainHooks} + R"(,"retain_callable":{"type":"boolean"})"),
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, const std::string_view arguments)
            { return impl.UnloadOrReload(arguments, false); },
        });
        (void)registry.Register({
            .Name = "plugin_reload",
            .Title = "Reload a plugin",
            .Description = "From the next frame: unload (draining first, as plugin_unload), then load the build at path, "
                           "which must carry the same plugin name, and reapply the saved config payloads. A failure "
                           "after the unload leaves that plugin unloaded.",
            .InputSchemaJson = Schema(std::string{R"("path":{"type":"string"},)"} + kPlugin, hooks,
                                      std::string{kFailAt} + "," + kDrainHooks, "path"),
            .ReadOnly = false,
            .Invoke = [&impl](const AgentOperationContext&, const std::string_view arguments)
            { return impl.UnloadOrReload(arguments, true); },
        });
        (void)registry.Register({
            .Name = "plugin_list",
            .Title = "The loaded plugins",
            .Description = "Every loaded plugin's name, generation, file and registrations.",
            .Invoke = [&impl](const AgentOperationContext&, std::string_view) { return impl.List(); },
        });
        (void)registry.Register({
            .Name = "app_quit",
            .Title = "Quit the Sandbox",
            .Description = "Requests the engine's exit; the plugin host unloads the plugins during the shutdown. Refused "
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
                           "plugins (and the generation and mapping of the only one), live and invoked plugin callables "
                           "per generation and kind, and every close attempt.",
            .Invoke = [&impl](const AgentOperationContext&, std::string_view) { return impl.Identity(); },
        });
    }
}
