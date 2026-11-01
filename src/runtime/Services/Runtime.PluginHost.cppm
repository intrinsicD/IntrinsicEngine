// ARCH-022: the plugin host, a runtime module. It loads one plugin at a time from a private copy, commits the
// plugin's registrations atomically, drains its jobs and GPU work before an unload, closes an image only when none
// of its callables is alive, and reapplies saved config payloads on a reload. It shuts down before every other
// runtime module (see Name), while the services it unregisters from are still alive.
module;

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.PluginHost;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Runtime.PluginContract;
import Extrinsic.Runtime.ServiceRegistry;

export namespace Extrinsic::Runtime
{
    // An opened plugin image: the host's only view of dlopen, so tests can hand it in-process images.
    class PluginImage
    {
    public:
        virtual ~PluginImage() = default;
        [[nodiscard]] virtual bool Register(PluginRegistrations& staged, std::string& error) noexcept = 0;
        virtual void Shutdown() noexcept = 0;
        // Unmaps the image; the host calls it only when none of the image's callables is alive. Destroyed without
        // it, the image stays mapped.
        virtual void Close() noexcept = 0;
        // The file mapped for this image (a private copy); empty for an in-process image.
        [[nodiscard]] virtual const std::filesystem::path& File() const noexcept = 0;
    };

    // Opens the build at `path`; null with `failure` set when it cannot. `failAt` ("dlopen" or "dlsym") injects a
    // failure (test hook).
    using PluginOpener = std::function<std::unique_ptr<PluginImage>(
        const std::string& path, std::string_view failAt, AgentOperationOutcome& failure)>;

    // False in a static engine build: plugins need the shared engine libraries.
    [[nodiscard]] bool PluginsSupported() noexcept;

    struct PluginHostOptions
    {
        // Fault injection and probes for the fixture plugin's drivers (--plugin-test-hooks): the tools' hook
        // arguments, plugin_host_identity and the closure form of undo entries. Without it they are rejected.
        bool TestHooks{false};
        PluginOpener Open{}; // empty selects dlopen of a private copy (shared engine builds only)
        std::function<void()> RequestExit{};
    };

    class PluginHostModule final : public IRuntimeModule
    {
    public:
        explicit PluginHostModule(PluginHostOptions options);
        ~PluginHostModule() override;
        PluginHostModule(const PluginHostModule&) = delete;
        PluginHostModule& operator=(const PluginHostModule&) = delete;

        // Modules register in name order and shut down in reverse. "~" sorts after every "Runtime." module, so the
        // host shuts down first, while the services its plugin registered with are alive.
        [[nodiscard]] std::string_view Name() const noexcept override { return "~Runtime.PluginHost"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        // The ordinary exit (after the GPU work was shut down and the jobs drained): the plugin's shutdown hook,
        // then every registration out. The image stays mapped; the process ends.
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        // Adds plugin_load, plugin_unload, plugin_reload, plugin_list and app_quit, and with test hooks
        // plugin_host_identity; keeps the registry to commit and remove plugin tools later.
        void RegisterOperations(AgentOperationRegistry& registry);

        // The contract's host functions reach the composed host through these.
        [[nodiscard]] static PluginHostModule* Current() noexcept;
        [[nodiscard]] bool SubmitJob(const AgentOperationContext& context, PluginJob job);
        [[nodiscard]] bool StartGpuWork(const AgentOperationContext& context, GpuQueueParticipantDesc work);
        [[nodiscard]] ServiceRegistry* Services() const noexcept;
        [[nodiscard]] bool TestHooks() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
