#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <future>
#include <thread>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <string>

import Extrinsic.Core.Process;

namespace Process = Extrinsic::Core::Process;

TEST(CoreProcess, EmptyExecutableFailsToSpawn)
{
    Process::ProcessConfig config;
    config.Executable = "";

    const auto result = Process::Run(config);
    EXPECT_TRUE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_FALSE(result.StdErr.empty());
}

TEST(CoreProcess, RunTrueReturnsZeroExitCode)
{
    Process::ProcessConfig config;
    config.Executable = "true";
    config.CaptureOutput = false;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
}

TEST(CoreProcess, RunFalseReturnsNonZeroExitCode)
{
    Process::ProcessConfig config;
    config.Executable = "false";
    config.CaptureOutput = false;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_NE(result.ExitCode, 0);
}

TEST(CoreProcess, CapturesStdoutAndStderrIndependently)
{
    Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "echo out_content; echo err_content >&2"};
    config.CaptureOutput = true;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("out_content"), std::string::npos);
    EXPECT_NE(result.StdErr.find("err_content"), std::string::npos);
    EXPECT_EQ(result.StdOut.find("err_content"), std::string::npos);
    EXPECT_EQ(result.StdErr.find("out_content"), std::string::npos);
}

TEST(CoreProcess, ArgumentsWithSpacesQuotesAndDollarSignsAreLiteral)
{
    Process::ProcessConfig config;
    config.Executable = "printf";
    config.Args = {"%s|%s|%s", "hello   world", "it's a \"test\"", "$HOME"};
    config.CaptureOutput = true;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("hello   world|it's a \"test\"|$HOME"), std::string::npos);
}

TEST(CoreProcess, CaptureDisabledRoutesOutputToDevNull)
{
    Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "echo should_not_appear; echo should_not_appear >&2"};
    config.CaptureOutput = false;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_TRUE(result.StdOut.empty());
    EXPECT_TRUE(result.StdErr.empty());
}

TEST(CoreProcess, TimeoutWithoutCaptureKillsProcess)
{
    Process::ProcessConfig config;
    config.Executable = "sleep";
    config.Args = {"60"};
    config.Timeout = std::chrono::milliseconds(200);
    config.CaptureOutput = false;

    const auto start = std::chrono::steady_clock::now();
    const auto result = Process::Run(config);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_TRUE(result.TimedOut);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(CoreProcess, TimeoutStillAppliesAfterCapturedPipesClose)
{
    Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "exec >&- 2>&-; exec sleep 3"};
    config.Timeout = std::chrono::milliseconds(500);
    config.CaptureOutput = true;
    const auto start = std::chrono::steady_clock::now();
    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_TRUE(result.TimedOut);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
}

TEST(CoreProcess, TimeoutWithCapturePreservesPartialOutput)
{
    Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "echo partial_output; sleep 60"};
    config.Timeout = std::chrono::milliseconds(500);
    config.CaptureOutput = true;

    const auto start = std::chrono::steady_clock::now();
    const auto result = Process::Run(config);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_TRUE(result.TimedOut);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_NE(result.StdOut.find("partial_output"), std::string::npos);
}

TEST(CoreProcess, WorkingDirectoryIsRespected)
{
#if defined(__linux__) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
    const auto tempDir = std::filesystem::temp_directory_path();

    Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "pwd"};
    config.WorkingDirectory = tempDir.string();
    config.CaptureOutput = true;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find(tempDir.string()), std::string::npos);
#else
    GTEST_SKIP() << "Working directory override is only supported on Linux glibc builds";
#endif
}

TEST(CoreProcess, IsExecutableAvailableReportsPresence)
{
    EXPECT_TRUE(Process::IsExecutableAvailable("echo"));
    EXPECT_FALSE(Process::IsExecutableAvailable("this_does_not_exist_42"));
}

TEST(CoreProcess, SignalKilledProcessReturns128PlusSignal)
{
    Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "kill -9 $$"};
    config.CaptureOutput = false;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 128 + 9);
}

TEST(CoreProcess, LargeOutputIsCapturedFully)
{
    Process::ProcessConfig config;
    config.Executable = "dd";
    config.Args = {"if=/dev/zero", "bs=1024", "count=100", "status=none"};
    config.Timeout = std::chrono::seconds(10);
    config.CaptureOutput = true;

    const auto result = Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_EQ(result.StdOut.size(), 102400u);
}

namespace
{
    struct SignalDisposition
    {
        int Signal;
        struct sigaction Previous{};
        bool Installed{false};
        SignalDisposition(int signal, void (*handler)(int)) : Signal(signal)
        {
            struct sigaction action{};
            action.sa_handler = handler;
            sigemptyset(&action.sa_mask);
            Installed = sigaction(Signal, &action, &Previous) == 0;
        }
        ~SignalDisposition()
        {
            if (Installed)
                sigaction(Signal, &Previous, nullptr);
        }
    };
}

TEST(CoreProcess, InterruptedBlockingWaitStillReturnsChildExitStatus)
{
    SignalDisposition signal(SIGUSR1, +[](int) {});
    ASSERT_TRUE(signal.Installed);
    struct Pipes
    {
        int Ready[2]{-1, -1};
        int Release[2]{-1, -1};
        ~Pipes()
        {
            for (const int fd : {Ready[0], Ready[1], Release[0], Release[1]})
                if (fd >= 0)
                    close(fd);
        }
    } pipes;
    ASSERT_EQ(pipe(pipes.Ready), 0);
    ASSERT_EQ(pipe(pipes.Release), 0);
    Process::ProcessConfig config;
    config.Executable = "sh";
    config.CaptureOutput = false;
    config.Args = {"-c",
        "printf '%s\\n' \"$$\" > /proc/self/fd/" + std::to_string(pipes.Ready[1]) +
            "; read -r release < /proc/self/fd/" + std::to_string(pipes.Release[0]) + "; exit 37"};
    std::atomic<pid_t> threadId{0};
    std::promise<Process::ProcessResult> promise;
    auto result = promise.get_future();
    std::thread worker(
        [&]
        {
            threadId.store(static_cast<pid_t>(syscall(SYS_gettid)));
            promise.set_value(Process::Run(config));
        });
    pollfd ready{pipes.Ready[0], POLLIN, 0};
    const bool childReady = poll(&ready, 1, 2000) == 1;
    EXPECT_TRUE(childReady);
    pid_t child = -1;
    if (childReady)
    {
        char bytes[32]{};
        const auto count = read(pipes.Ready[0], bytes, sizeof(bytes) - 1);
        if (count > 0)
            child = static_cast<pid_t>(std::strtol(bytes, nullptr, 10));
    }
    bool waiting = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (childReady && std::chrono::steady_clock::now() < deadline)
    {
        // The child remains blocked on Release: observing wait4 makes signal delivery deterministic.
        std::ifstream state("/proc/self/task/" + std::to_string(threadId.load()) + "/syscall");
        long call = -1;
        if (state >> call && call == SYS_wait4)
        {
            waiting = true;
            break;
        }
        std::this_thread::yield();
    }
    EXPECT_TRUE(waiting) << "The process runner must reach waitpid before the signal";
    if (waiting)
    {
        EXPECT_EQ(pthread_kill(worker.native_handle(), SIGUSR1), 0);
        EXPECT_EQ(result.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout)
            << "An interrupted wait must not return while the child is still held";
    }
    EXPECT_EQ(write(pipes.Release[1], "go\n", 3), 3);
    worker.join();
    const auto actual = result.get();
    EXPECT_EQ(actual.ExitCode, 37);
    EXPECT_FALSE(actual.TimedOut);
    EXPECT_FALSE(actual.SpawnFailed);
    // Reap the child even when the old implementation returned early.
    if (child > 0)
    {
        int status = 0;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR)
        {
        }
    }
}

TEST(CoreProcess, WaitErrorIsNotReportedAsSuccessfulExit)
{
    SignalDisposition ignored(SIGCHLD, SIG_IGN);
    ASSERT_TRUE(ignored.Installed);
    Process::ProcessConfig config;
    config.Executable = "true";
    config.CaptureOutput = false;
    const auto result = Process::Run(config);
    EXPECT_EQ(result.ExitCode, -1);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_NE(result.StdErr.find("waitpid"), std::string::npos);
    EXPECT_FALSE(Process::IsExecutableAvailable("true"));
}
