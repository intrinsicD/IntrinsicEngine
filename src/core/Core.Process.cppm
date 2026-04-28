module;

#include <chrono>
#include <string>
#include <vector>

export module Extrinsic.Core.Process;

// -----------------------------------------------------------------------
// Extrinsic::Core::Process — Safe structured process execution.
//
// Uses posix_spawnp with an explicit argv array — no shell interpolation,
// no command injection. Arguments containing spaces, quotes, or shell
// metacharacters are passed verbatim as separate argv elements.
//
// NEVER use std::system() anywhere in the codebase; use this API instead.
// See CLAUDE.md §"Structured Process Execution" for policy rationale.
// -----------------------------------------------------------------------

export namespace Extrinsic::Core::Process
{
    struct ProcessResult
    {
        int         ExitCode    = -1;
        std::string StdOut{};
        std::string StdErr{};
        bool TimedOut    = false; // Process was killed after Timeout elapsed.
        bool SpawnFailed = false; // posix_spawnp could not launch the process.
    };

    struct ProcessConfig
    {
        // Executable name or absolute path.
        // If no '/' is present, PATH lookup is used via posix_spawnp.
        std::string Executable{};

        // argv[1..n]. argv[0] is set to Executable automatically.
        std::vector<std::string> Args{};

        // Optional working directory. Empty = inherit from parent process.
        std::string WorkingDirectory{};

        // Execution timeout. Zero = no timeout.
        std::chrono::milliseconds Timeout{0};

        // Capture stdout/stderr into ProcessResult. If false, output goes
        // to /dev/null (saves pipe overhead for fire-and-forget spawns).
        bool CaptureOutput = true;
    };

    // Run a process synchronously. Returns when the process exits or times out.
    // Safe to call from any thread.
    [[nodiscard]] ProcessResult Run(const ProcessConfig& config);

    // Returns true if `executable` is found on PATH.
    // Spawns `executable --version` without capture; ignores exit code.
    [[nodiscard]] bool IsExecutableAvailable(const std::string& executable);
}

