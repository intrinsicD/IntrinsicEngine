module;

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#define INTRINSIC_LOCAL_SOCKET_POSIX 1
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#endif

module Extrinsic.Platform.LocalSocket;

namespace Extrinsic::Platform
{
    const char* ToString(const LocalSocketStatus status) noexcept
    {
        switch (status)
        {
        case LocalSocketStatus::Ok: return "Ok";
        case LocalSocketStatus::Unsupported: return "Unsupported";
        case LocalSocketStatus::InvalidPath: return "InvalidPath";
        case LocalSocketStatus::AddressInUse: return "AddressInUse";
        case LocalSocketStatus::Timeout: return "Timeout";
        case LocalSocketStatus::Closed: return "Closed";
        case LocalSocketStatus::SystemError: return "SystemError";
        }
        return "Unknown";
    }

#if INTRINSIC_LOCAL_SOCKET_POSIX
    namespace
    {
        bool MakeAddress(std::string_view path, sockaddr_un& address)
        {
            address = {};
            address.sun_family = AF_UNIX;
            if (path.empty() || path.size() >= sizeof(address.sun_path)) return false;
            std::memcpy(address.sun_path, path.data(), path.size());
            return true;
        }

        void CloseHandle(int& handle) noexcept
        {
            if (handle >= 0) ::close(handle);
            handle = -1;
        }

        // Returns > 0 when ready, 0 on timeout, < 0 on error (EINTR retried).
        int WaitFor(int handle, short events, int timeoutMs)
        {
            pollfd entry{handle, events, 0};
            for (;;)
            {
                const int ready = ::poll(&entry, 1, timeoutMs);
                if (ready >= 0 || errno != EINTR) return ready;
            }
        }

        int OpenStream()
        {
            const int handle = ::socket(AF_UNIX, SOCK_STREAM, 0);
            if (handle >= 0) (void)::fcntl(handle, F_SETFD, FD_CLOEXEC);
            return handle;
        }
    }

    LocalSocketConnection::~LocalSocketConnection() { Close(); }
    LocalSocketConnection::LocalSocketConnection(LocalSocketConnection&& other) noexcept
        : m_Handle(std::exchange(other.m_Handle, -1)) {}
    LocalSocketConnection& LocalSocketConnection::operator=(LocalSocketConnection&& other) noexcept
    {
        if (this != &other) { Close(); m_Handle = std::exchange(other.m_Handle, -1); }
        return *this;
    }
    void LocalSocketConnection::Close() noexcept { CloseHandle(m_Handle); }

    LocalSocketStatus LocalSocketConnection::Receive(std::string& buffer, const int timeoutMs)
    {
        if (m_Handle < 0) return LocalSocketStatus::Closed;
        const int ready = WaitFor(m_Handle, POLLIN, timeoutMs);
        if (ready == 0) return LocalSocketStatus::Timeout;
        if (ready < 0) return LocalSocketStatus::SystemError;
        char chunk[4096];
        ssize_t count;
        do { count = ::recv(m_Handle, chunk, sizeof(chunk), 0); } while (count < 0 && errno == EINTR);
        if (count == 0) { Close(); return LocalSocketStatus::Closed; }
        if (count < 0) return errno == EAGAIN ? LocalSocketStatus::Timeout : LocalSocketStatus::SystemError;
        buffer.append(chunk, static_cast<std::size_t>(count));
        return LocalSocketStatus::Ok;
    }

    LocalSocketStatus LocalSocketConnection::SendAll(std::string_view data, const int timeoutMs)
    {
        if (m_Handle < 0) return LocalSocketStatus::Closed;
        while (!data.empty())
        {
            const int ready = WaitFor(m_Handle, POLLOUT, timeoutMs);
            if (ready == 0) return LocalSocketStatus::Timeout;
            if (ready < 0) return LocalSocketStatus::SystemError;
#ifdef MSG_NOSIGNAL
            const ssize_t sent = ::send(m_Handle, data.data(), data.size(), MSG_NOSIGNAL | MSG_DONTWAIT);
#else
            const ssize_t sent = ::send(m_Handle, data.data(), data.size(), MSG_DONTWAIT);
#endif
            if (sent < 0)
            {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                if (errno == EPIPE || errno == ECONNRESET) { Close(); return LocalSocketStatus::Closed; }
                return LocalSocketStatus::SystemError;
            }
            data.remove_prefix(static_cast<std::size_t>(sent));
        }
        return LocalSocketStatus::Ok;
    }

    LocalSocketListener::~LocalSocketListener() { Close(); }
    LocalSocketListener::LocalSocketListener(LocalSocketListener&& other) noexcept
        : m_Handle(std::exchange(other.m_Handle, -1)), m_Path(std::move(other.m_Path))
        , m_PathDevice(std::exchange(other.m_PathDevice, 0))
        , m_PathInode(std::exchange(other.m_PathInode, 0)) {}
    LocalSocketListener& LocalSocketListener::operator=(LocalSocketListener&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_Handle = std::exchange(other.m_Handle, -1);
            m_Path = std::move(other.m_Path);
            m_PathDevice = std::exchange(other.m_PathDevice, 0);
            m_PathInode = std::exchange(other.m_PathInode, 0);
        }
        return *this;
    }

    void LocalSocketListener::Close() noexcept
    {
        if (m_Handle >= 0 && !m_Path.empty())
        {
            struct stat current{};
            if (::lstat(m_Path.c_str(), &current) == 0 && S_ISSOCK(current.st_mode)
                && current.st_dev == m_PathDevice && current.st_ino == m_PathInode)
                ::unlink(m_Path.c_str());
        }
        CloseHandle(m_Handle);
        m_Path.clear();
        m_PathDevice = 0;
        m_PathInode = 0;
    }

    LocalSocketStatus LocalSocketListener::Listen(std::string_view path)
    {
        Close();
        sockaddr_un address{};
        if (!MakeAddress(path, address)) return LocalSocketStatus::InvalidPath;
        struct stat existing{};
        if (::lstat(address.sun_path, &existing) == 0)
        {
            if (!S_ISSOCK(existing.st_mode)) return LocalSocketStatus::InvalidPath; // never replace a regular file
            LocalSocketConnection probe;
            const auto status = ConnectLocalSocket(path, probe);
            if (status == LocalSocketStatus::Ok) return LocalSocketStatus::AddressInUse;
            if (status != LocalSocketStatus::Closed) return status;
            ::unlink(address.sun_path); // stale socket of a crashed process
        }
        int handle = OpenStream();
        if (handle < 0) return LocalSocketStatus::SystemError;
        // Owner-only from creation: no window in which other users could connect.
        const mode_t previous = ::umask(0177);
        const int bound = ::bind(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        ::umask(previous);
        if (bound != 0)
        {
            const bool inUse = errno == EADDRINUSE;
            CloseHandle(handle);
            return inUse ? LocalSocketStatus::AddressInUse : LocalSocketStatus::SystemError;
        }
        struct stat boundPath{};
        if (::lstat(address.sun_path, &boundPath) != 0 || !S_ISSOCK(boundPath.st_mode))
        {
            CloseHandle(handle);
            return LocalSocketStatus::SystemError;
        }
        m_Handle = handle;
        m_Path.assign(path);
        m_PathDevice = static_cast<std::uint64_t>(boundPath.st_dev);
        m_PathInode = static_cast<std::uint64_t>(boundPath.st_ino);
        if (::listen(handle, 4) != 0)
        {
            Close();
            return LocalSocketStatus::SystemError;
        }
        return LocalSocketStatus::Ok;
    }

    LocalSocketStatus LocalSocketListener::Accept(LocalSocketConnection& out, const int timeoutMs)
    {
        if (m_Handle < 0) return LocalSocketStatus::Closed;
        const int ready = WaitFor(m_Handle, POLLIN, timeoutMs);
        if (ready == 0) return LocalSocketStatus::Timeout;
        if (ready < 0) return LocalSocketStatus::SystemError;
        int client;
        do { client = ::accept(m_Handle, nullptr, nullptr); } while (client < 0 && errno == EINTR);
        if (client < 0) return errno == EAGAIN ? LocalSocketStatus::Timeout : LocalSocketStatus::SystemError;
        (void)::fcntl(client, F_SETFD, FD_CLOEXEC);
        out = LocalSocketConnection{client};
        return LocalSocketStatus::Ok;
    }

    LocalSocketStatus ConnectLocalSocket(std::string_view path, LocalSocketConnection& out)
    {
        sockaddr_un address{};
        if (!MakeAddress(path, address)) return LocalSocketStatus::InvalidPath;
        int handle = OpenStream();
        if (handle < 0) return LocalSocketStatus::SystemError;
        int connected;
        do { connected = ::connect(handle, reinterpret_cast<const sockaddr*>(&address), sizeof(address)); }
        while (connected != 0 && errno == EINTR);
        if (connected != 0)
        {
            const bool missing = errno == ENOENT || errno == ECONNREFUSED;
            CloseHandle(handle);
            return missing ? LocalSocketStatus::Closed : LocalSocketStatus::SystemError;
        }
        out = LocalSocketConnection{handle};
        return LocalSocketStatus::Ok;
    }
#else
    LocalSocketConnection::~LocalSocketConnection() = default;
    LocalSocketConnection::LocalSocketConnection(LocalSocketConnection&& other) noexcept
        : m_Handle(std::exchange(other.m_Handle, -1)) {}
    LocalSocketConnection& LocalSocketConnection::operator=(LocalSocketConnection&& other) noexcept
    { m_Handle = std::exchange(other.m_Handle, -1); return *this; }
    void LocalSocketConnection::Close() noexcept { m_Handle = -1; }
    LocalSocketStatus LocalSocketConnection::Receive(std::string&, int) { return LocalSocketStatus::Unsupported; }
    LocalSocketStatus LocalSocketConnection::SendAll(std::string_view, int) { return LocalSocketStatus::Unsupported; }
    LocalSocketListener::~LocalSocketListener() = default;
    LocalSocketListener::LocalSocketListener(LocalSocketListener&& other) noexcept
        : m_Handle(std::exchange(other.m_Handle, -1)), m_Path(std::move(other.m_Path))
        , m_PathDevice(std::exchange(other.m_PathDevice, 0))
        , m_PathInode(std::exchange(other.m_PathInode, 0)) {}
    LocalSocketListener& LocalSocketListener::operator=(LocalSocketListener&& other) noexcept
    {
        if (this != &other)
        {
            m_Handle = std::exchange(other.m_Handle, -1);
            m_Path = std::move(other.m_Path);
            m_PathDevice = std::exchange(other.m_PathDevice, 0);
            m_PathInode = std::exchange(other.m_PathInode, 0);
        }
        return *this;
    }
    void LocalSocketListener::Close() noexcept
    {
        m_Handle = -1;
        m_Path.clear();
        m_PathDevice = 0;
        m_PathInode = 0;
    }
    LocalSocketStatus LocalSocketListener::Listen(std::string_view) { return LocalSocketStatus::Unsupported; }
    LocalSocketStatus LocalSocketListener::Accept(LocalSocketConnection&, int) { return LocalSocketStatus::Unsupported; }
    LocalSocketStatus ConnectLocalSocket(std::string_view, LocalSocketConnection&) { return LocalSocketStatus::Unsupported; }
#endif
}
