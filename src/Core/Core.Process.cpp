module;

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

// POSIX process management
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// environ must be declared in the global module fragment (before 'module ...;')
// because unistd.h already declares it in the global module, and Clang 20
// modules reject redeclaration across module boundaries.
extern char** environ; // NOLINT

module Core.Process;

import Core.Logging;

namespace Core::Process
{
    namespace
    {
        // RAII wrapper for a pipe pair. Uses pipe2(O_CLOEXEC) for defense in depth:
        // prevents child processes from inheriting stale pipe fds even if
        // posix_spawn file actions miss one.
        struct Pipe
        {
            int ReadEnd = -1;
            int WriteEnd = -1;

            bool Create()
            {
                int fds[2];
                if (pipe2(fds, O_CLOEXEC) != 0)
                    return false;
                ReadEnd = fds[0];
                WriteEnd = fds[1];
                return true;
            }

            void CloseRead()
            {
                if (ReadEnd >= 0) { close(ReadEnd); ReadEnd = -1; }
            }

            void CloseWrite()
            {
                if (WriteEnd >= 0) { close(WriteEnd); WriteEnd = -1; }
            }

            ~Pipe()
            {
                CloseRead();
                CloseWrite();
            }

            Pipe() = default;
            Pipe(const Pipe&) = delete;
            Pipe& operator=(const Pipe&) = delete;
            Pipe(Pipe&&) = delete;
            Pipe& operator=(Pipe&&) = delete;
        };

        // Read all available data from a non-blocking file descriptor into a string.
        // Returns true if EOF was reached (read returned 0), false if EAGAIN/EWOULDBLOCK.
        [[nodiscard]] bool DrainFd(int fd, std::string& out)
        {
            std::array<char, 4096> buf{};
            for (;;)
            {
                const ssize_t n = read(fd, buf.data(), buf.size());
                if (n > 0)
                {
                    out.append(buf.data(), static_cast<size_t>(n));
                }
                else if (n == 0)
                {
                    return true; // EOF
                }
                else
                {
                    // EAGAIN/EWOULDBLOCK = no more data right now, not EOF.
                    return false;
                }
            }
        }

        // Set a file descriptor to non-blocking mode.
        void SetNonBlocking(int fd)
        {
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0)
                fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        // Build the argv array from config. Caller owns the strings via config lifetime.
        std::vector<char*> BuildArgv(const ProcessConfig& config)
        {
            std::vector<char*> argv;
            argv.reserve(config.Args.size() + 2);
            argv.push_back(const_cast<char*>(config.Executable.c_str()));
            for (const auto& arg : config.Args)
                argv.push_back(const_cast<char*>(arg.c_str()));
            argv.push_back(nullptr);
            return argv;
        }

        // Gracefully terminate a process: SIGTERM, then poll with WNOHANG for up to
        // 100ms, then escalate to SIGKILL if still alive. Reaps the child.
        void TerminateAndReap(pid_t pid)
        {
            kill(pid, SIGTERM);

            // Check if process died from SIGTERM before escalating.
            constexpr int kMaxChecks = 10;
            for (int i = 0; i < kMaxChecks; ++i)
            {
                int status = 0;
                const pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid || w < 0)
                    return; // Process already exited or error (already reaped).

                struct timespec ts{};
                ts.tv_nsec = 10'000'000; // 10ms
                nanosleep(&ts, nullptr);
            }

            // Still alive after 100ms — force-kill.
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }

        // Wait for process with optional timeout.
        // Returns true if process exited, false if timed out.
        bool WaitForProcess(pid_t pid, int& status, std::chrono::milliseconds timeout)
        {
            if (timeout.count() <= 0)
            {
                waitpid(pid, &status, 0);
                return true;
            }

            const auto deadline = std::chrono::steady_clock::now() + timeout;
            constexpr auto kPollInterval = std::chrono::milliseconds(50);

            while (std::chrono::steady_clock::now() < deadline)
            {
                const pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid)
                    return true;
                if (w < 0)
                    return true;

                const auto remaining = deadline - std::chrono::steady_clock::now();
                const auto sleepTime = std::min(kPollInterval, std::chrono::duration_cast<std::chrono::milliseconds>(remaining));
                if (sleepTime.count() > 0)
                {
                    struct timespec ts{};
                    ts.tv_nsec = sleepTime.count() * 1'000'000;
                    nanosleep(&ts, nullptr);
                }
            }

            return false;
        }
    }

    ProcessResult Run(const ProcessConfig& config)
    {
        ProcessResult result;

        if (config.Executable.empty())
        {
            result.SpawnFailed = true;
            result.StdErr = "Empty executable name";
            return result;
        }

        // Create pipes for stdout and stderr capture.
        Pipe stdoutPipe, stderrPipe;
        if (config.CaptureOutput)
        {
            if (!stdoutPipe.Create() || !stderrPipe.Create())
            {
                result.SpawnFailed = true;
                // strerror is thread-safe on glibc (uses thread-local buffer).
                result.StdErr = std::string("Failed to create pipes: ") + strerror(errno);
                return result;
            }
        }

        // Configure spawn file actions.
        posix_spawn_file_actions_t fileActions;
        posix_spawn_file_actions_init(&fileActions);

        if (config.CaptureOutput)
        {
            // Child: close read ends, redirect stdout/stderr to write ends.
            // Note: pipe fds are O_CLOEXEC, but posix_spawn file actions run
            // before exec, so we need explicit dup2 to set up the redirections.
            posix_spawn_file_actions_addclose(&fileActions, stdoutPipe.ReadEnd);
            posix_spawn_file_actions_addclose(&fileActions, stderrPipe.ReadEnd);
            posix_spawn_file_actions_adddup2(&fileActions, stdoutPipe.WriteEnd, STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&fileActions, stderrPipe.WriteEnd, STDERR_FILENO);
            posix_spawn_file_actions_addclose(&fileActions, stdoutPipe.WriteEnd);
            posix_spawn_file_actions_addclose(&fileActions, stderrPipe.WriteEnd);
        }
        else
        {
            posix_spawn_file_actions_addopen(&fileActions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
            posix_spawn_file_actions_addopen(&fileActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        }

        if (!config.WorkingDirectory.empty())
        {
#if defined(__linux__) && defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
            posix_spawn_file_actions_addchdir_np(&fileActions, config.WorkingDirectory.c_str());
#else
            Core::Log::Warn("[Process] Working directory override not supported on this platform, ignoring: {}",
                            config.WorkingDirectory);
#endif
        }

        auto argv = BuildArgv(config);

        // Spawn the child process. The parent's full environment is inherited.
        // This is appropriate for tool invocations (compilers, etc.) but would need
        // environment sanitization if used for untrusted executables.
        pid_t pid = 0;
        const int spawnErr = posix_spawnp(&pid,
                                          config.Executable.c_str(),
                                          &fileActions,
                                          nullptr,
                                          argv.data(),
                                          environ);

        posix_spawn_file_actions_destroy(&fileActions);

        if (spawnErr != 0)
        {
            result.SpawnFailed = true;
            result.StdErr = std::string("posix_spawnp failed: ") + strerror(spawnErr);
            return result;
        }

        // Parent: close write ends so we get EOF when child exits.
        if (config.CaptureOutput)
        {
            stdoutPipe.CloseWrite();
            stderrPipe.CloseWrite();
        }

        // Read stdout/stderr while waiting for the process.
        if (config.CaptureOutput)
        {
            SetNonBlocking(stdoutPipe.ReadEnd);
            SetNonBlocking(stderrPipe.ReadEnd);

            const bool hasTimeout = config.Timeout.count() > 0;
            const auto deadline = hasTimeout
                ? std::chrono::steady_clock::now() + config.Timeout
                : std::chrono::steady_clock::time_point::max();

            // Poll both fds until EOF on both (read returns 0), or timeout.
            std::array<struct pollfd, 2> pfds{};
            pfds[0].fd = stdoutPipe.ReadEnd;
            pfds[0].events = POLLIN;
            pfds[1].fd = stderrPipe.ReadEnd;
            pfds[1].events = POLLIN;

            int openFds = 2;
            while (openFds > 0)
            {
                int timeoutMs = -1;
                if (hasTimeout)
                {
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now());
                    if (remaining.count() <= 0)
                    {
                        result.TimedOut = true;
                        TerminateAndReap(pid);
                        // Final drain after child is dead.
                        DrainFd(stdoutPipe.ReadEnd, result.StdOut);
                        DrainFd(stderrPipe.ReadEnd, result.StdErr);
                        return result;
                    }
                    timeoutMs = static_cast<int>(remaining.count());
                }

                const int pollResult = poll(pfds.data(), 2, timeoutMs);
                if (pollResult < 0)
                {
                    if (errno == EINTR)
                        continue;
                    break;
                }

                // Drain data and check for EOF. POLLHUP may arrive with or without
                // POLLIN; we must keep reading until read() returns 0 (EOF), not
                // just until POLLHUP fires, to avoid losing buffered data.
                for (int i = 0; i < 2; ++i)
                {
                    if (pfds[i].fd < 0)
                        continue;
                    if (pfds[i].revents & (POLLIN | POLLHUP))
                    {
                        auto& buf = (i == 0) ? result.StdOut : result.StdErr;
                        const bool eof = DrainFd(pfds[i].fd, buf);
                        if (eof)
                        {
                            pfds[i].fd = -1;
                            --openFds;
                        }
                    }
                }
            }
        }

        // Wait for process completion.
        int status = 0;
        if (!WaitForProcess(pid, status, config.CaptureOutput ? std::chrono::milliseconds(0) : config.Timeout))
        {
            result.TimedOut = true;
            TerminateAndReap(pid);
            return result;
        }

        if (WIFEXITED(status))
            result.ExitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            result.ExitCode = 128 + WTERMSIG(status);
        else
            result.ExitCode = -1;

        return result;
    }

    bool IsExecutableAvailable(const std::string& executable)
    {
        ProcessConfig config;
        config.Executable = executable;
        config.Args = {"--version"};
        config.CaptureOutput = false;
        config.Timeout = std::chrono::milliseconds(5000);

        const auto result = Run(config);
        // We only check if the process could be spawned (executable found on PATH).
        // The exit code is irrelevant — some tools return non-zero for --version.
        return !result.SpawnFailed && !result.TimedOut;
    }
}
