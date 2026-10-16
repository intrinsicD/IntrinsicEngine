// Local Unix-domain socket listener/connection: round trip, permissions, stale files and errors.
#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <string>
#include <thread>
#include <utility>
#include <sys/resource.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <gtest/gtest.h>
import Extrinsic.Platform.LocalSocket;
namespace P = Extrinsic::Platform;
namespace
{
    std::string TempSocketPath(const char* tag)
    {
        return (std::filesystem::temp_directory_path() /
                ("intrinsic-" + std::string(tag) + "-" + std::to_string(::getpid()) + ".sock")).string();
    }
}

TEST(LocalSocket, RoundTripsDataBetweenListenerAndClient)
{
    const auto path = TempSocketPath("roundtrip");
    P::LocalSocketListener listener;
    const auto listening = listener.Listen(path);
    if (listening == P::LocalSocketStatus::Unsupported) GTEST_SKIP() << "no Unix-domain sockets on this platform";
    ASSERT_EQ(listening, P::LocalSocketStatus::Ok);
    struct stat info{};
    ASSERT_EQ(::stat(path.c_str(), &info), 0);
    EXPECT_EQ(info.st_mode & 0777, 0600u) << "owner-only socket file";

    P::LocalSocketConnection accepted;
    EXPECT_EQ(listener.Accept(accepted, 0), P::LocalSocketStatus::Timeout);
    std::thread client([&] {
        P::LocalSocketConnection connection;
        ASSERT_EQ(P::ConnectLocalSocket(path, connection), P::LocalSocketStatus::Ok);
        ASSERT_EQ(connection.SendAll("hello\n"), P::LocalSocketStatus::Ok);
        std::string reply;
        while (reply.find('\n') == std::string::npos)
            ASSERT_EQ(connection.Receive(reply, 2000), P::LocalSocketStatus::Ok);
        EXPECT_EQ(reply, "world\n");
    });
    ASSERT_EQ(listener.Accept(accepted, 2000), P::LocalSocketStatus::Ok);
    std::string received;
    while (received.find('\n') == std::string::npos)
        ASSERT_EQ(accepted.Receive(received, 2000), P::LocalSocketStatus::Ok);
    EXPECT_EQ(received, "hello\n");
    ASSERT_EQ(accepted.SendAll("world\n"), P::LocalSocketStatus::Ok);
    client.join();
    std::string rest;
    EXPECT_EQ(accepted.Receive(rest, 2000), P::LocalSocketStatus::Closed) << "peer closed";
    EXPECT_FALSE(accepted.IsOpen());

    listener.Close();
    EXPECT_FALSE(std::filesystem::exists(path)) << "the listener removes its socket file";
}

TEST(LocalSocket, RejectsLiveOwnersAndReplacesStaleFiles)
{
    const auto path = TempSocketPath("owner");
    P::LocalSocketListener first;
    const auto listening = first.Listen(path);
    if (listening == P::LocalSocketStatus::Unsupported) GTEST_SKIP() << "no Unix-domain sockets on this platform";
    ASSERT_EQ(listening, P::LocalSocketStatus::Ok);
    P::LocalSocketListener second;
    EXPECT_EQ(second.Listen(path), P::LocalSocketStatus::AddressInUse);
    EXPECT_TRUE(std::filesystem::exists(path)) << "a live listener's file is kept";

    // A crashed process leaves a socket file without a listener behind.
    const auto stale = TempSocketPath("stale");
    {
        const int raw = ::socket(AF_UNIX, SOCK_STREAM, 0);
        ASSERT_GE(raw, 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::strncpy(address.sun_path, stale.c_str(), sizeof(address.sun_path) - 1);
        ASSERT_EQ(::bind(raw, reinterpret_cast<const sockaddr*>(&address), sizeof(address)), 0);
        ::close(raw); // no unlink: the file stays
    }
    ASSERT_TRUE(std::filesystem::exists(stale));
    P::LocalSocketListener staleOwner;
    EXPECT_EQ(staleOwner.Listen(stale), P::LocalSocketStatus::Ok) << "stale socket files are replaced";
    staleOwner.Close();

    // Paths that must never be touched or cannot be bound.
    const auto regular = TempSocketPath("regular") + ".txt";
    { std::FILE* f = std::fopen(regular.c_str(), "w"); ASSERT_NE(f, nullptr); std::fclose(f); }
    P::LocalSocketListener onFile;
    EXPECT_EQ(onFile.Listen(regular), P::LocalSocketStatus::InvalidPath) << "a regular file is never replaced";
    EXPECT_TRUE(std::filesystem::exists(regular));
    std::filesystem::remove(regular);
    EXPECT_EQ(onFile.Listen(""), P::LocalSocketStatus::InvalidPath);
    EXPECT_EQ(onFile.Listen(std::string(200, 'x')), P::LocalSocketStatus::InvalidPath);

    P::LocalSocketConnection nobody;
    EXPECT_EQ(P::ConnectLocalSocket(TempSocketPath("missing"), nobody), P::LocalSocketStatus::Closed);
    std::string buffer;
    EXPECT_EQ(nobody.Receive(buffer, 0), P::LocalSocketStatus::Closed);
    EXPECT_EQ(nobody.SendAll("x"), P::LocalSocketStatus::Closed);
}


TEST(LocalSocket, SendAllTimesOutWhenPeerDoesNotRead)
{
    // Setup stays in the child so both GTest death-test styles exercise one owner.
    EXPECT_EXIT({
        ::alarm(2);
        const int result = []
        {
            const auto path = TempSocketPath("send-timeout");
            P::LocalSocketListener listener;
            if (listener.Listen(path) != P::LocalSocketStatus::Ok) return 2;
            P::LocalSocketConnection client;
            if (P::ConnectLocalSocket(path, client) != P::LocalSocketStatus::Ok) return 3;
            P::LocalSocketConnection accepted;
            if (listener.Accept(accepted, 200) != P::LocalSocketStatus::Ok) return 4;
            return accepted.SendAll(std::string(8u * 1024u * 1024u, 'x'), 20)
                    == P::LocalSocketStatus::Timeout ? 0 : 1;
        }();
        ::_exit(result);
    }, ::testing::ExitedWithCode(0), "");
}

TEST(LocalSocket, FailedOwnershipProbePreservesLiveSocket)
{
    // FD exhaustion belongs only to the child, never to the test runner.
    EXPECT_EXIT({
        ::alarm(2);
        const int result = []
        {
            const auto path = TempSocketPath("probe-error");
            P::LocalSocketListener listener;
            if (listener.Listen(path) != P::LocalSocketStatus::Ok) return 2;
            struct stat original{};
            if (::lstat(path.c_str(), &original) != 0) return 3;
            struct rlimit limit{};
            if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) return 4;
            limit.rlim_cur = std::min(limit.rlim_cur, rlim_t{64});
            if (::setrlimit(RLIMIT_NOFILE, &limit) != 0) return 5;
            while (::open("/dev/null", O_RDONLY) >= 0) {}
            if (errno != EMFILE) return 6;
            P::LocalSocketListener contender;
            const auto status = contender.Listen(path);
            struct stat current{};
            const bool preserved = ::lstat(path.c_str(), &current) == 0
                && current.st_dev == original.st_dev && current.st_ino == original.st_ino;
            return status == P::LocalSocketStatus::SystemError && preserved ? 0 : 1;
        }();
        ::_exit(result);
    }, ::testing::ExitedWithCode(0), "");
}

TEST(LocalSocket, ClosePreservesReplacementFile)
{
    const auto path = TempSocketPath("replacement-file");
    P::LocalSocketListener listener;
    ASSERT_EQ(listener.Listen(path), P::LocalSocketStatus::Ok);
    ASSERT_EQ(::unlink(path.c_str()), 0);
    std::FILE* replacement = std::fopen(path.c_str(), "w");
    ASSERT_NE(replacement, nullptr);
    std::fputs("replacement data", replacement);
    std::fclose(replacement);

    listener.Close();
    EXPECT_TRUE(std::filesystem::is_regular_file(path));
    std::filesystem::remove(path);
}

TEST(LocalSocket, ClosePreservesSuccessorListenerAfterMove)
{
    const auto path = TempSocketPath("successor");
    P::LocalSocketListener first;
    ASSERT_EQ(first.Listen(path), P::LocalSocketStatus::Ok);
    P::LocalSocketListener moved(std::move(first));
    ASSERT_EQ(::unlink(path.c_str()), 0);
    P::LocalSocketListener successor;
    ASSERT_EQ(successor.Listen(path), P::LocalSocketStatus::Ok);

    moved.Close();
    EXPECT_TRUE(std::filesystem::exists(path));
    P::LocalSocketConnection client;
    EXPECT_EQ(P::ConnectLocalSocket(path, client), P::LocalSocketStatus::Ok);
}

TEST(LocalSocket, MovesTransferSocketPathOwnership)
{
    const auto path = TempSocketPath("move-source");
    const auto replacedPath = TempSocketPath("move-target");
    P::LocalSocketListener first;
    ASSERT_EQ(first.Listen(path), P::LocalSocketStatus::Ok);
    P::LocalSocketListener moved(std::move(first));
    first.Close();
    EXPECT_TRUE(std::filesystem::exists(path));
    P::LocalSocketListener assigned;
    ASSERT_EQ(assigned.Listen(replacedPath), P::LocalSocketStatus::Ok);
    assigned = std::move(moved);
    moved.Close();
    EXPECT_FALSE(std::filesystem::exists(replacedPath));
    EXPECT_TRUE(std::filesystem::exists(path));
    assigned.Close();
    EXPECT_FALSE(std::filesystem::exists(path));
}
