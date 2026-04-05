// Test_Process.cpp
// Tests for the Core::Process structured process spawn API.

#include <gtest/gtest.h>
#include <chrono>
#include <string>

import Core.Process;

// ---------------------------------------------------------------------------
// Basic spawn and exit code
// ---------------------------------------------------------------------------

TEST(Process, RunTrueReturnsZeroExitCode)
{
    Core::Process::ProcessConfig config;
    config.Executable = "true";
    config.CaptureOutput = false;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
}

TEST(Process, RunFalseReturnsNonZeroExitCode)
{
    Core::Process::ProcessConfig config;
    config.Executable = "false";
    config.CaptureOutput = false;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_NE(result.ExitCode, 0);
}

TEST(Process, NonExistentExecutableFailsToSpawn)
{
    Core::Process::ProcessConfig config;
    config.Executable = "this_executable_does_not_exist_42";
    config.CaptureOutput = false;

    auto result = Core::Process::Run(config);
    EXPECT_TRUE(result.SpawnFailed);
}

TEST(Process, EmptyExecutableNameFailsToSpawn)
{
    Core::Process::ProcessConfig config;
    config.Executable = "";

    auto result = Core::Process::Run(config);
    EXPECT_TRUE(result.SpawnFailed);
}

// ---------------------------------------------------------------------------
// stdout/stderr capture
// ---------------------------------------------------------------------------

TEST(Process, CapturesStdout)
{
    Core::Process::ProcessConfig config;
    config.Executable = "echo";
    config.Args = {"hello world"};
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("hello world"), std::string::npos);
}

TEST(Process, CapturesStderr)
{
    Core::Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "echo 'error message' >&2"};
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdErr.find("error message"), std::string::npos);
}

TEST(Process, StdoutAndStderrAreSeparate)
{
    Core::Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "echo out_content; echo err_content >&2"};
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("out_content"), std::string::npos);
    EXPECT_NE(result.StdErr.find("err_content"), std::string::npos);
    EXPECT_EQ(result.StdOut.find("err_content"), std::string::npos);
    EXPECT_EQ(result.StdErr.find("out_content"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Timeout
// ---------------------------------------------------------------------------

TEST(Process, TimeoutKillsLongRunningProcess)
{
    Core::Process::ProcessConfig config;
    config.Executable = "sleep";
    config.Args = {"60"};
    config.Timeout = std::chrono::milliseconds(200);
    config.CaptureOutput = false;

    const auto start = std::chrono::steady_clock::now();
    auto result = Core::Process::Run(config);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(result.TimedOut);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}

TEST(Process, NoTimeoutAllowsCompletion)
{
    Core::Process::ProcessConfig config;
    config.Executable = "echo";
    config.Args = {"done"};
    config.Timeout = std::chrono::milliseconds(0);
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("done"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Arguments with special characters (no shell interpolation)
// ---------------------------------------------------------------------------

TEST(Process, ArgumentsWithSpacesAreNotShellSplit)
{
    Core::Process::ProcessConfig config;
    config.Executable = "echo";
    config.Args = {"hello   world"};
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("hello   world"), std::string::npos);
}

TEST(Process, ArgumentsWithQuotesArePassedLiterally)
{
    Core::Process::ProcessConfig config;
    config.Executable = "printf";
    config.Args = {"%s", "it's a \"test\""};
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("it's a \"test\""), std::string::npos);
}

TEST(Process, ArgumentsWithDollarSignsAreNotExpanded)
{
    Core::Process::ProcessConfig config;
    config.Executable = "printf";
    config.Args = {"%s", "$HOME"};
    config.CaptureOutput = true;

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_EQ(result.ExitCode, 0);
    EXPECT_NE(result.StdOut.find("$HOME"), std::string::npos);
}

// ---------------------------------------------------------------------------
// IsExecutableAvailable
// ---------------------------------------------------------------------------

TEST(Process, IsExecutableAvailableForEcho)
{
    EXPECT_TRUE(Core::Process::IsExecutableAvailable("echo"));
}

TEST(Process, IsExecutableAvailableForNonexistent)
{
    EXPECT_FALSE(Core::Process::IsExecutableAvailable("this_does_not_exist_42"));
}

// ---------------------------------------------------------------------------
// Timeout with capture enabled (separate code path from no-capture timeout)
// ---------------------------------------------------------------------------

TEST(Process, TimeoutWithCaptureKillsProcess)
{
    Core::Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "echo partial_output; sleep 60"};
    config.Timeout = std::chrono::milliseconds(500);
    config.CaptureOutput = true;

    const auto start = std::chrono::steady_clock::now();
    const auto result = Core::Process::Run(config);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_TRUE(result.TimedOut);
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    // Should have captured the output written before the timeout.
    EXPECT_NE(result.StdOut.find("partial_output"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Large output (stress pipe drain logic)
// ---------------------------------------------------------------------------

TEST(Process, LargeOutputIsCapturedFully)
{
    // Generate ~100KB of output.
    Core::Process::ProcessConfig config;
    config.Executable = "dd";
    config.Args = {"if=/dev/zero", "bs=1024", "count=100", "status=none"};
    config.CaptureOutput = true;
    config.Timeout = std::chrono::milliseconds(10'000);

    const auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 0);
    // Should capture exactly 100*1024 = 102400 bytes.
    EXPECT_EQ(result.StdOut.size(), 102400u);
}

// ---------------------------------------------------------------------------
// Signal-killed process exit code
// ---------------------------------------------------------------------------

TEST(Process, SignalKilledProcessReturns128PlusSignal)
{
    Core::Process::ProcessConfig config;
    config.Executable = "sh";
    config.Args = {"-c", "kill -9 $$"};
    config.CaptureOutput = false;

    const auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_EQ(result.ExitCode, 128 + 9); // SIGKILL = 9
}

// ---------------------------------------------------------------------------
// Shader compiler integration (glslc)
// ---------------------------------------------------------------------------

TEST(Process, GlslcCompileFailureReturnsNonZeroExitCode)
{
    if (!Core::Process::IsExecutableAvailable("glslc"))
        GTEST_SKIP() << "glslc not available on PATH";

    Core::Process::ProcessConfig config;
    config.Executable = "glslc";
    config.Args = {"/tmp/nonexistent_shader_file.vert", "-o", "/dev/null"};
    config.CaptureOutput = true;
    config.Timeout = std::chrono::milliseconds(10'000);

    auto result = Core::Process::Run(config);
    EXPECT_FALSE(result.SpawnFailed);
    EXPECT_FALSE(result.TimedOut);
    EXPECT_NE(result.ExitCode, 0);
    EXPECT_FALSE(result.StdErr.empty());
}
