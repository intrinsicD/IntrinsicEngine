// MCP server of the agent control lane (ARCH-019, RUNTIME-288). Opt-in: the Sandbox
// creates it only for `--agent-socket`. A background thread owns the owner-only Unix
// socket and newline-delimited JSON-RPC framing; every request runs on the main thread
// through the AgentOperationRegistry, in the UiBuild frame phase (or the Idle phase while
// the window is minimized, where captures fail fast), so tools use the same editor
// commands, history and config lane as the panels.
module;

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module Extrinsic.Runtime.AgentServer;

export import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleLifecycle;

export namespace Extrinsic::Runtime
{
    // MCP revisions the server speaks, newest first. `initialize` echoes a requested version
    // from this list and otherwise answers kAgentProtocolVersion. Results carry
    // `structuredContent` from 2025-06-18 on.
    inline constexpr std::array<std::string_view, 3> kAgentSupportedProtocolVersions{"2025-06-18", "2025-03-26", "2024-11-05"};
    inline constexpr std::string_view kAgentProtocolVersion = kAgentSupportedProtocolVersions[0];

    // $XDG_RUNTIME_DIR/intrinsic-sandbox.sock, else /tmp/intrinsic-sandbox-<uid>.sock.
    [[nodiscard]] std::string DefaultAgentSocketPath();

    // Socket-free MCP/JSON-RPC core: one message in, the response line out (nullopt for
    // notifications). Supports initialize (version negotiation), ping, tools/list and
    // tools/call; mutating tools are hidden and refused in a read-only session. Tool results
    // are text content plus, for 2025-06-18 and newer, `structuredContent` (the JSON object
    // the tool returned, or {"error":{"code","message"}} for coded errors).
    class AgentProtocol
    {
    public:
        AgentProtocol(const AgentOperationRegistry& registry, bool readOnly) noexcept
            : m_Registry(&registry), m_ReadOnly(readOnly) {}
        [[nodiscard]] std::optional<std::string> Handle(std::string_view message, const AgentOperationContext& context);
        // Replies for deferred tool calls that finished this frame, plus `notifications/progress`
        // lines for pending calls that sent a `_meta.progressToken` (at most one per interval).
        // Progress follows the oldest queued or running editor job (determinate: percent of 100
        // with total; otherwise elapsed seconds without total), or the call's own age with message
        // "waiting" when there is none. The unit is fixed at a call's first notification and a
        // notification is sent only when its value exceeds the previous one. The pick is ambiguous
        // with several concurrent jobs, and a cancelled job still counts, until per-call job
        // tokens exist (RUNTIME-279, UI-069).
        [[nodiscard]] std::vector<std::string> PollPending(const AgentOperationContext& context);
        [[nodiscard]] std::size_t PendingCount() const noexcept { return m_Pending.size(); }
        // While this many deferred calls wait, state-changing tools/call requests are refused with
        // -32000 before they run; read-only tools still run and are refused only if they would defer.
        static constexpr std::size_t kMaxPendingCalls = 16;
        void DropPending() noexcept { m_Pending.clear(); }
        void SetProgressInterval(std::chrono::milliseconds interval) noexcept { m_ProgressInterval = interval; }
        [[nodiscard]] bool Initialized() const noexcept { return m_Initialized; }
        [[nodiscard]] const std::string& ClientName() const noexcept { return m_ClientName; }
        [[nodiscard]] const std::string& NegotiatedVersion() const noexcept { return m_NegotiatedVersion; }

    private:
        const AgentOperationRegistry* m_Registry{nullptr};
        bool m_ReadOnly{false};
        bool m_Initialized{false};
        std::string m_ClientName{};
        std::string m_NegotiatedVersion{kAgentProtocolVersion};
        // `notifications/cancelled` turns the entry into a reply-less tombstone that still counts
        // against kMaxPendingCalls until its continuation completes (then it is dropped silently);
        // the editor job keeps running until RUNTIME-279 offers a cancel path.
        enum class ProgressUnit : std::uint8_t { Unset, Percent, Seconds };
        struct PendingCall
        {
            std::string Id{};                // dumped JSON-RPC id
            AgentOperationContinuation Continue{};
            std::string ProgressToken{};     // dumped JSON token; empty when the call sent none
            std::chrono::steady_clock::time_point Started{};
            std::chrono::steady_clock::time_point LastEmit{};
            double LastProgress{-1.0};       // MCP: progress strictly increases per notification
            ProgressUnit Unit{ProgressUnit::Unset}; // fixed at the first emission
            bool Cancelled{false};           // tombstone: no reply, no progress
            bool NeedsPresentedFrame{false}; // fails with viewport_not_presentable while minimized
        };
        std::vector<PendingCall> m_Pending{};
        std::chrono::milliseconds m_ProgressInterval{250};
    };

    struct AgentServerOptions
    {
        std::string SocketPath{};              // empty selects DefaultAgentSocketPath()
        bool ReadOnly{false};
        std::vector<std::string> AllowedRoots{}; // empty selects the working directory
        std::uint32_t MaxCallsPerFrame{4};
        std::size_t MaxMessageBytes{8u << 20u};
        std::chrono::milliseconds ProgressInterval{250}; // between progress notifications of one call
    };

    struct AgentServerStatus
    {
        bool Listening{false};
        bool ClientConnected{false};
        bool ReadOnly{false};
        std::string SocketPath{};
        std::string ClientName{};
        std::vector<std::string> AllowedRoots{};
        std::uint64_t CallsHandled{0};
        std::string LastError{};
    };

    class AgentServerModule final : public IRuntimeModule
    {
    public:
        explicit AgentServerModule(AgentServerOptions options);
        ~AgentServerModule() override;

        [[nodiscard]] std::string_view Name() const noexcept override { return "Runtime.AgentServer"; }
        [[nodiscard]] RuntimeModuleResult OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] AgentServerStatus Status() const;
        // Drops the current client; it may reconnect.
        void DisconnectClient();
        [[nodiscard]] const AgentOperationRegistry& Operations() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
