#include <gtest/gtest.h>

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


