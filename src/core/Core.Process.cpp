module;

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// environ is declared in the global scope by unistd.h; we re-declare here
// for posix_spawnp. Must live in the global module fragment (before 'module;').
extern char** environ; // NOLINT(readability-redundant-declaration)

module Extrinsic.Core.Process;

import Extrinsic.Core.Logging;

namespace Extrinsic::Core::Process
{
    namespace
    {
        struct Pipe
        {
            int ReadEnd  = -1;
            int WriteEnd = -1;

            bool Create() noexcept
            {
                int fds[2];
                if (pipe2(fds, O_CLOEXEC) != 0) return false;
                ReadEnd  = fds[0];
                WriteEnd = fds[1];
                return true;
            }
            void CloseRead()  noexcept { if (ReadEnd  >= 0) { close(ReadEnd);  ReadEnd  = -1; } }
            void CloseWrite() noexcept { if (WriteEnd >= 0) { close(WriteEnd); WriteEnd = -1; } }
            ~Pipe() { CloseRead(); CloseWrite(); }
            Pipe() = default;
            Pipe(const Pipe&) = delete;
            Pipe& operator=(const Pipe&) = delete;
        };

        void SetNonBlocking(int fd) noexcept
        {
            const int flags = fcntl(fd, F_GETFL, 0);
            if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        // Returns true when EOF is reached on fd.
        bool DrainFd(int fd, std::string& out)
        {
            std::array<char, 4096> buf{};
            for (;;)
            {
                const ssize_t n = read(fd, buf.data(), buf.size());
                if (n > 0)  { out.append(buf.data(), static_cast<std::size_t>(n)); }
                else if (n == 0) { return true; }   // EOF
                else             { return false; }  // EAGAIN / EWOULDBLOCK
            }
        }

        std::vector<char*> BuildArgv(const ProcessConfig& cfg)
        {
            std::vector<char*> argv;
            argv.reserve(cfg.Args.size() + 2);
            argv.push_back(const_cast<char*>(cfg.Executable.c_str()));
            for (const auto& a : cfg.Args)
                argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);
            return argv;
        }

        void TerminateAndReap(pid_t pid) noexcept
        {
            kill(pid, SIGTERM);
            for (int i = 0; i < 10; ++i)
            {
                int status = 0;
                const pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid || w < 0) return;
                struct timespec ts{}; ts.tv_nsec = 10'000'000;
                nanosleep(&ts, nullptr);
            }
            kill(pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }

        // Returns true if process exited, false on timeout.
        bool WaitForProcess(pid_t pid, int& status,
                            std::chrono::milliseconds timeout) noexcept
        {
            if (timeout.count() <= 0)
            {
                waitpid(pid, &status, 0);
                return true;
            }
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            while (std::chrono::steady_clock::now() < deadline)
            {
                const pid_t w = waitpid(pid, &status, WNOHANG);
                if (w == pid || w < 0) return true;
                const auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - std::chrono::steady_clock::now());
                const auto sl = std::min(rem, std::chrono::milliseconds(50));
                if (sl.count() > 0)
                {
                    struct timespec ts{};
                    ts.tv_nsec = sl.count() * 1'000'000;
                    nanosleep(&ts, nullptr);
                }
            }
            return false;
        }
    } // namespace

    ProcessResult Run(const ProcessConfig& config)
    {
        ProcessResult result;
        if (config.Executable.empty())
        {
            result.SpawnFailed = true;
            result.StdErr = "Empty executable name";
            return result;
        }

        Pipe stdoutPipe, stderrPipe;
        if (config.CaptureOutput)
        {
            if (!stdoutPipe.Create() || !stderrPipe.Create())
            {
                result.SpawnFailed = true;
                result.StdErr = std::string("Failed to create pipes: ") + strerror(errno);
                return result;
            }
        }

        posix_spawn_file_actions_t fa;
        posix_spawn_file_actions_init(&fa);

        if (config.CaptureOutput)
        {
            posix_spawn_file_actions_addclose(&fa, stdoutPipe.ReadEnd);
            posix_spawn_file_actions_addclose(&fa, stderrPipe.ReadEnd);
            posix_spawn_file_actions_adddup2(&fa, stdoutPipe.WriteEnd, STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&fa, stderrPipe.WriteEnd, STDERR_FILENO);
            posix_spawn_file_actions_addclose(&fa, stdoutPipe.WriteEnd);
            posix_spawn_file_actions_addclose(&fa, stderrPipe.WriteEnd);
        }
        else
        {
            posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
            posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
        }

        if (!config.WorkingDirectory.empty())
        {
#if defined(__linux__) && defined(__GLIBC__) && \
    (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 29))
            posix_spawn_file_actions_addchdir_np(&fa, config.WorkingDirectory.c_str());
#else
            Log::Warn("[Process] Working directory override not supported on this platform: {}",
                      config.WorkingDirectory);
#endif
        }

        auto argv = BuildArgv(config);
        pid_t pid = 0;
        const int spawnErr = posix_spawnp(&pid,
                                          config.Executable.c_str(),
                                          &fa, nullptr,
                                          argv.data(), environ);
        posix_spawn_file_actions_destroy(&fa);

        if (spawnErr != 0)
        {
            result.SpawnFailed = true;
            result.StdErr = std::string("posix_spawnp failed: ") + strerror(spawnErr);
            return result;
        }

        if (config.CaptureOutput)
        {
            stdoutPipe.CloseWrite();
            stderrPipe.CloseWrite();
            SetNonBlocking(stdoutPipe.ReadEnd);
            SetNonBlocking(stderrPipe.ReadEnd);

            const bool hasTimeout = config.Timeout.count() > 0;
            const auto deadline   = hasTimeout
                ? std::chrono::steady_clock::now() + config.Timeout
                : std::chrono::steady_clock::time_point::max();

            std::array<struct pollfd, 2> pfds{};
            pfds[0] = {stdoutPipe.ReadEnd, POLLIN, 0};
            pfds[1] = {stderrPipe.ReadEnd, POLLIN, 0};
            int open = 2;

            while (open > 0)
            {
                int timeoutMs = -1;
                if (hasTimeout)
                {
                    const auto rem = std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now());
                    if (rem.count() <= 0)
                    {
                        result.TimedOut = true;
                        TerminateAndReap(pid);
                        DrainFd(stdoutPipe.ReadEnd, result.StdOut);
                        DrainFd(stderrPipe.ReadEnd, result.StdErr);
                        return result;
                    }
                    timeoutMs = static_cast<int>(rem.count());
                }

                const int pr = poll(pfds.data(), 2, timeoutMs);
                if (pr < 0 && errno != EINTR) break;

                for (int i = 0; i < 2; ++i)
                {
                    if (pfds[i].fd < 0) continue;
                    if (pfds[i].revents & (POLLIN | POLLHUP))
                    {
                        auto& buf = (i == 0) ? result.StdOut : result.StdErr;
                        if (DrainFd(pfds[i].fd, buf)) { pfds[i].fd = -1; --open; }
                    }
                }
            }
        }

        int status = 0;
        const auto waitTimeout = config.CaptureOutput
            ? std::chrono::milliseconds(0)   // already drained; process likely done
            : config.Timeout;
        if (!WaitForProcess(pid, status, waitTimeout))
        {
            result.TimedOut = true;
            TerminateAndReap(pid);
            return result;
        }

        if      (WIFEXITED(status))   result.ExitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) result.ExitCode = 128 + WTERMSIG(status);
        else                          result.ExitCode = -1;
        return result;
    }

    bool IsExecutableAvailable(const std::string& executable)
    {
        const ProcessResult r = Run(ProcessConfig{
            .Executable    = executable,
            .Args          = {"--version"},
            .Timeout       = std::chrono::milliseconds(5000),
            .CaptureOutput = false,
        });
        return !r.SpawnFailed && !r.TimedOut;
    }
}

