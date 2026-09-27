// Local Unix-domain socket listener/connection: round trip, permissions, stale files and errors.
#include <filesystem>
#include <string>
#include <thread>
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
