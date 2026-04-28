module;

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

export module Core.Process;

export namespace Core::Process
{
    // Result of a process execution.
    struct ProcessResult
    {
        int ExitCode = -1;            // Process exit code (0 = success)
        std::string StdOut;           // Captured stdout
        std::string StdErr;           // Captured stderr
        bool TimedOut = false;        // True if the process was killed due to timeout
        bool SpawnFailed = false;     // True if the process could not be spawned
    };

    // Configuration for process execution.
    struct ProcessConfig
    {
        // Executable name or path. If no '/' is present, PATH lookup is used.
        std::string Executable;

        // Arguments (argv[1..n]). argv[0] is set to Executable automatically.
        std::vector<std::string> Args;

        // Optional working directory. Empty = inherit from parent.
        std::string WorkingDirectory;

        // Timeout. Zero means no timeout.
        std::chrono::milliseconds Timeout{0};

        // Whether to capture stdout/stderr. If false, they are discarded.
        bool CaptureOutput = true;
    };

    // Run a process synchronously with structured arguments (no shell interpolation).
    // Uses posix_spawnp for argv-based execution — safe against command injection.
    [[nodiscard]] ProcessResult Run(const ProcessConfig& config);

    // Convenience: check if an executable is available on PATH.
    // Runs `executable --version` silently and checks for successful spawn.
    [[nodiscard]] bool IsExecutableAvailable(const std::string& executable);
}
