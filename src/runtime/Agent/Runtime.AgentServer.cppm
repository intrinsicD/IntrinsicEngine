// MCP server of the agent control lane (ARCH-019, RUNTIME-288). Opt-in: the Sandbox
// creates it only for `--agent-socket`. A background thread owns the owner-only Unix
// socket and newline-delimited JSON-RPC framing; every request runs on the main thread
// in the UiBuild frame phase through the AgentOperationRegistry, so tools use the same
// editor commands, history and config lane as the panels.
module;

#include <array>
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
        // Replies for deferred tool calls that finished this frame.
        [[nodiscard]] std::vector<std::string> PollPending(const AgentOperationContext& context);
        [[nodiscard]] std::size_t PendingCount() const noexcept { return m_Pending.size(); }
        void DropPending() noexcept { m_Pending.clear(); }
        [[nodiscard]] bool Initialized() const noexcept { return m_Initialized; }
        [[nodiscard]] const std::string& ClientName() const noexcept { return m_ClientName; }
        [[nodiscard]] const std::string& NegotiatedVersion() const noexcept { return m_NegotiatedVersion; }

    private:
        const AgentOperationRegistry* m_Registry{nullptr};
        bool m_ReadOnly{false};
        bool m_Initialized{false};
        std::string m_ClientName{};
        std::string m_NegotiatedVersion{kAgentProtocolVersion};
        std::vector<std::pair<std::string, AgentOperationContinuation>> m_Pending{}; // (JSON id, continuation)
    };

    struct AgentServerOptions
    {
        std::string SocketPath{};              // empty selects DefaultAgentSocketPath()
        bool ReadOnly{false};
        std::vector<std::string> AllowedRoots{}; // empty selects the working directory
        std::uint32_t MaxCallsPerFrame{4};
        std::size_t MaxMessageBytes{8u << 20u};
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
