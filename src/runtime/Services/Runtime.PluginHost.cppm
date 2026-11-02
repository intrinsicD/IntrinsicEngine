// ARCH-022: the plugin host, a runtime module. It loads one plugin at a time from a private copy, commits the
// plugin's registrations atomically, drains its jobs and GPU work before an unload, closes an image only when none
// of its callables is alive, and reapplies saved config payloads on a reload. It shuts down before every other
// runtime module (see Name), while the services it unregisters from are still alive.
module;

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.PluginHost;

import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
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
    // dlopen of a private copy of the build at `path`, the v1 entry points, the same build id; null with
    // `failure` set when it cannot (plugins_unsupported in a static build). `failAt` injects a failure (test hook).
    [[nodiscard]] std::unique_ptr<PluginImage> OpenSharedPlugin(const std::string& path, std::string_view failAt,
                                                                AgentOperationOutcome& failure);
    // A plugin linked into the executable (static builds), behind the same seam; its close does nothing.
    [[nodiscard]] std::unique_ptr<PluginImage> MakeInProcessPlugin(PluginRegisterFn registerFn,
                                                                   PluginShutdownFn shutdownFn);

    // A built-in plugin opened and registered before the config boot: the Sandbox moves its config sections into
    // the boot section registry (so a saved payload loads), and the host commits the rest when it resolves.
    struct StagedPlugin
    {
        std::unique_ptr<PluginImage> Image{};
        PluginRegistrations Registrations{};
        std::vector<std::string> SectionNames{}; // the sections moved to the boot registry
    };
    [[nodiscard]] std::optional<StagedPlugin> StagePlugin(std::unique_ptr<PluginImage> image, std::string& error);

    struct PluginHostOptions
    {
        // Fault injection and probes for the fixture plugin's drivers (--plugin-test-hooks): the tools' hook
        // arguments, plugin_host_identity and the closure form of undo entries. Without it they are rejected.
        bool TestHooks{false};
        PluginOpener Open{}; // empty selects OpenSharedPlugin
        std::function<void()> RequestExit{};
        std::optional<StagedPlugin> Startup{}; // committed when the host resolves
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
        // Commits the startup plugin; without a workspace from the app, attaches one of its own.
        [[nodiscard]] RuntimeModuleResult OnResolve(EngineSetup& setup) override;
        // The ordinary exit (after the GPU work was shut down and the jobs drained): the plugin's shutdown hook,
        // then every registration out. The image stays mapped; the process ends.
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        // The editor workspace plugin jobs belong to: the Sandbox gives its shell's, so every plugin run is a row of
        // its Jobs window, whether it started from a window or over MCP.
        void UseEditorWorkspace(EditorWorkspaceAttachment& workspace) noexcept;
        // Before that workspace detaches: back to the host's own.
        void ReleaseEditorWorkspace(const EditorWorkspaceAttachment& workspace) noexcept;

        // Adds plugin_load, plugin_unload, plugin_reload, plugin_list and app_quit, and with test hooks
        // plugin_host_identity; keeps the registry to commit and remove plugin tools later.
        void RegisterOperations(AgentOperationRegistry& registry);

        // The contract's host functions reach the composed host through these.
        [[nodiscard]] static PluginHostModule* Current() noexcept;
        [[nodiscard]] PluginJobHandle SubmitJob(PluginJob job, std::string* refusal);
        bool CancelJob(PluginJobHandle job);
        void ReportJobOutcome(PluginJobHandle job, PluginJobDomain resolved, std::string diagnostic);
        [[nodiscard]] bool StartGpuWork(const AgentOperationContext& context, GpuQueueParticipantDesc work);
        [[nodiscard]] ServiceRegistry* Services() const noexcept;
        [[nodiscard]] bool TestHooks() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
