// Named operations of the agent control lane (ARCH-019, RUNTIME-287): each entry is a thin
// wrapper over an existing editor command, query or config call, so agents, the batch CLI
// and tests use the same validated paths as the Sandbox UI. Arguments and results are JSON
// text; JSON handling stays inside the implementation units.
module;

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.AgentOperations;

import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ViewCapture;

export namespace Extrinsic::Runtime
{
    struct AgentImage
    {
        std::string MimeType{"image/png"};
        std::string Base64Data{};
    };

    struct AgentOperationContext;
    struct AgentOperationOutcome;

    // Polled once per frame on the main thread until it returns true with the final outcome.
    using AgentOperationContinuation =
        std::function<bool(const AgentOperationContext&, AgentOperationOutcome& outcome)>;

    struct AgentOperationOutcome
    {
        bool IsError{false};
        std::string Text{"{}"}; // JSON document (or a plain message for errors)
        std::string ErrorCode{}; // machine-readable code for errors, e.g. "file_exists"; empty when none
        std::vector<AgentImage> Images{};
        // Set by operations that finish on a later frame (captures); the reply waits for it.
        AgentOperationContinuation Continuation{};
        // For a deferred call: reads this run's own progress (UI-069 read model) where the run key
        // is known (the job the command queued, or the correlation id of its service run).
        // `notifications/progress` follows it; empty for calls with no run key, which report only
        // their age: view captures and `import_file` with `wait` (the asset workflow's own queue,
        // not a job of the editor surface).
        std::function<EditorOperationProgress(const AgentOperationContext&)> Progress{};
    };

    // Borrowed engine services for one call on the main thread; any may be null.
    struct AgentOperationContext
    {
        const EditorWorkspaceAttachment* Attachment{nullptr};
        EngineConfigControl* ConfigControl{nullptr};
        JobService* Jobs{nullptr};
        EditorCommandHistory* History{nullptr};
        ViewCaptureModule* ViewCapture{nullptr};
        // Canonical absolute directories file arguments must stay inside.
        std::vector<std::string> AllowedRoots{};
        std::uint64_t FrameIndex{0};
        // False on minimized frames: nothing is rendered, so GPU work and captures cannot
        // progress. Tools that need a presented frame fail with "viewport_not_presentable".
        bool ViewportPresentable{true};
    };

    using AgentOperationInvoker =
        std::function<AgentOperationOutcome(const AgentOperationContext&, std::string_view argumentsJson)>;

    struct AgentOperationSpec
    {
        std::string Name{};
        std::string Title{};
        std::string Description{};
        std::string InputSchemaJson{R"({"type":"object","properties":{},"additionalProperties":false})"};
        bool ReadOnly{true};
        // A mutation the undo history cannot restore: written files, applied engine config.
        bool Destructive{false};
        // May dispatch GPU work, which only progresses on presented frames: refused while the
        // viewport is not presentable, and a pending call fails with "viewport_not_presentable".
        bool NeedsPresentedFrame{false};
        AgentOperationInvoker Invoke{};
    };

    class AgentOperationRegistry
    {
    public:
        // Fails for empty or duplicate names and missing invokers.
        [[nodiscard]] bool Register(AgentOperationSpec spec);
        [[nodiscard]] const AgentOperationSpec* Find(std::string_view name) const noexcept;
        [[nodiscard]] std::span<const AgentOperationSpec> Entries() const noexcept { return m_Entries; }

    private:
        std::vector<AgentOperationSpec> m_Entries{};
    };

    // Runs one operation: unknown names and mutating operations in a read-only session are
    // errors; history entries recorded during the call are labeled "Agent: <label>".
    [[nodiscard]] AgentOperationOutcome InvokeAgentOperation(
        const AgentOperationRegistry& registry, std::string_view name,
        const AgentOperationContext& context, std::string_view argumentsJson, bool readOnlySession);

    // Resolves a user-supplied path against the allowed roots (relative paths resolve
    // against the first root); nullopt when it escapes every root or no root is set.
    [[nodiscard]] std::optional<std::string> ResolveAgentPath(
        const AgentOperationContext& context, std::string_view path);

    // Scene, selection, config, history, jobs, log and mesh-field operations.
    void RegisterEditorAgentOperations(AgentOperationRegistry& registry);
    // view_screenshot (image only) and view_capture (PNG file) over ViewCaptureModule.
    void RegisterViewCaptureAgentOperations(AgentOperationRegistry& registry);

    [[nodiscard]] std::string EncodeBase64(std::span<const std::uint8_t> bytes);
}
