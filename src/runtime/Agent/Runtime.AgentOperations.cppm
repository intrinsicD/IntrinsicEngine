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
        bool Destructive{false}; // a mutation the undo history does not cover (written files)
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
