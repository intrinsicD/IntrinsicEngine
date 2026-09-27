// Local (same-machine) stream sockets: a Unix-domain listener and connections with
// timeout-bounded, non-throwing I/O. Used by the opt-in agent control lane; the
// socket file is created owner-only (0600) and never reachable over the network.
module;

#include <cstdint>
#include <string>
#include <string_view>

export module Extrinsic.Platform.LocalSocket;

export namespace Extrinsic::Platform
{
    enum class LocalSocketStatus : std::uint8_t
    {
        Ok = 0,
        Unsupported,  // no Unix-domain sockets on this platform build
        InvalidPath,  // empty or longer than the platform's socket path limit
        AddressInUse, // another live listener owns the path
        Timeout,
        Closed,       // peer closed or the handle is not open
        SystemError,
    };
    [[nodiscard]] const char* ToString(LocalSocketStatus status) noexcept;

    class LocalSocketConnection
    {
    public:
        LocalSocketConnection() = default;
        ~LocalSocketConnection();
        LocalSocketConnection(LocalSocketConnection&& other) noexcept;
        LocalSocketConnection& operator=(LocalSocketConnection&& other) noexcept;
        LocalSocketConnection(const LocalSocketConnection&) = delete;
        LocalSocketConnection& operator=(const LocalSocketConnection&) = delete;

        [[nodiscard]] bool IsOpen() const noexcept { return m_Handle >= 0; }
        // Waits up to timeoutMs (0 polls) and appends whatever arrived to `buffer`.
        [[nodiscard]] LocalSocketStatus Receive(std::string& buffer, int timeoutMs);
        // Writes all bytes, waiting up to timeoutMs for each stalled chunk.
        [[nodiscard]] LocalSocketStatus SendAll(std::string_view data, int timeoutMs = 5000);
        void Close() noexcept;

    private:
        explicit LocalSocketConnection(int handle) noexcept : m_Handle(handle) {}
        int m_Handle{-1};
        friend class LocalSocketListener;
        friend LocalSocketStatus ConnectLocalSocket(std::string_view path, LocalSocketConnection& out);
    };

    class LocalSocketListener
    {
    public:
        LocalSocketListener() = default;
        ~LocalSocketListener();
        LocalSocketListener(LocalSocketListener&& other) noexcept;
        LocalSocketListener& operator=(LocalSocketListener&& other) noexcept;
        LocalSocketListener(const LocalSocketListener&) = delete;
        LocalSocketListener& operator=(const LocalSocketListener&) = delete;

        // Binds `path` with owner-only permissions. A stale socket file left by a
        // crashed process is replaced; a path with a live listener is AddressInUse.
        [[nodiscard]] LocalSocketStatus Listen(std::string_view path);
        // Waits up to timeoutMs for one client.
        [[nodiscard]] LocalSocketStatus Accept(LocalSocketConnection& out, int timeoutMs);
        [[nodiscard]] bool IsListening() const noexcept { return m_Handle >= 0; }
        [[nodiscard]] const std::string& Path() const noexcept { return m_Path; }
        // Stops listening and removes the socket file this listener created.
        void Close() noexcept;

    private:
        int m_Handle{-1};
        std::string m_Path{};
    };

    [[nodiscard]] LocalSocketStatus ConnectLocalSocket(std::string_view path, LocalSocketConnection& out);
}
