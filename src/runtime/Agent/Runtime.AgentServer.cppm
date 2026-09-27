// MCP server of the agent control lane (ARCH-019, RUNTIME-288). Opt-in: the Sandbox
// creates it only for `--agent-socket`. A background thread owns the owner-only Unix
// socket and newline-delimited JSON-RPC framing; every request runs on the main thread
// in the UiBuild frame phase through the AgentOperationRegistry, so tools use the same
// editor commands, history and config lane as the panels.
module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Runtime.AgentServer;

export import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ModuleLifecycle;

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kAgentProtocolVersion = "2025-06-18";

    // $XDG_RUNTIME_DIR/intrinsic-sandbox.sock, else /tmp/intrinsic-sandbox-<uid>.sock.
    [[nodiscard]] std::string DefaultAgentSocketPath();

    // Socket-free MCP/JSON-RPC core: one message in, the response line out (nullopt for
    // notifications). Supports initialize, ping, tools/list and tools/call; mutating tools
    // are hidden and refused in a read-only session.
    class AgentProtocol
    {
    public:
        AgentProtocol(const AgentOperationRegistry& registry, bool readOnly) noexcept
            : m_Registry(&registry), m_ReadOnly(readOnly) {}
        [[nodiscard]] std::optional<std::string> Handle(std::string_view message, const AgentOperationContext& context);
        [[nodiscard]] bool Initialized() const noexcept { return m_Initialized; }
        [[nodiscard]] const std::string& ClientName() const noexcept { return m_ClientName; }

    private:
        const AgentOperationRegistry* m_Registry{nullptr};
        bool m_ReadOnly{false};
        bool m_Initialized{false};
        std::string m_ClientName{};
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
