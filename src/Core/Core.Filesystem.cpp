module;
#include <filesystem>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <chrono>

#if defined(__linux__)
    // (no Linux-specific includes needed here)
#elif defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

module Core:Filesystem.Impl;
import Core;

namespace Core::Filesystem
{
    namespace
    {
        [[nodiscard]] bool HasAssetsDir(const std::filesystem::path& root)
        {
            std::error_code ec;
            const auto p = root / "assets";
            return std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec);
        }

        [[nodiscard]] std::filesystem::path CanonicalOrAbsolute(const std::filesystem::path& p)
        {
            std::error_code ec;
            auto c = std::filesystem::weakly_canonical(p, ec);
            if (!ec) return c;
            return std::filesystem::absolute(p, ec);
        }

        [[nodiscard]] std::filesystem::path GetExecutablePath()
        {
#if defined(__linux__)
            std::error_code ec;
            auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
            if (!ec && !p.empty())
                return CanonicalOrAbsolute(p);
            return {};
#elif defined(_WIN32)
            wchar_t buffer[MAX_PATH] = {};
            const DWORD len = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
            if (len == 0 || len >= std::size(buffer)) return {};
            return CanonicalOrAbsolute(std::filesystem::path(buffer));
#elif defined(__APPLE__)
            uint32_t size = 0;
            (void)_NSGetExecutablePath(nullptr, &size);
            if (size == 0) return {};

            std::string buf(size, '\0');
            if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
            return CanonicalOrAbsolute(std::filesystem::path(buf));
#else
            return {};
#endif
        }

        [[nodiscard]] std::filesystem::path ResolveRoot()
        {
            // 1) Dev mode: Prefer the source root from CMake, but only if it actually contains assets/.
#ifdef ENGINE_ROOT_DIR
            {
                const std::filesystem::path macroRoot = CanonicalOrAbsolute(std::filesystem::path(ENGINE_ROOT_DIR));
                if (!macroRoot.empty() && HasAssetsDir(macroRoot))
                    return macroRoot;
            }
#endif

            // 2) Installed/packaged mode: Search relative to the executable.
            //    Candidate order: <exe/..>, <exe/../..>, <exe>, plus a couple common layouts.
            //    (Bounded search to keep this deterministic.)
            {
                const auto exe = GetExecutablePath();
                const auto exeDir = exe.empty() ? std::filesystem::path{} : exe.parent_path();
                if (!exeDir.empty())
                {
                    const std::vector<std::filesystem::path> candidates = {
                        exeDir.parent_path(),
                        exeDir.parent_path().parent_path(),
                        exeDir,
                        exeDir / ".." / "share" / "IntrinsicEngine",
                        exeDir.parent_path() / "share" / "IntrinsicEngine",
                    };

                    for (const auto& c : candidates)
                    {
                        const auto root = CanonicalOrAbsolute(c);
                        if (!root.empty() && HasAssetsDir(root))
                            return root;
                    }
                }
            }

            // 3) Legacy behavior: Look in/around the current working directory.
            {
                const auto cwd = std::filesystem::current_path();
                if (HasAssetsDir(cwd)) return cwd;
                if (HasAssetsDir(cwd.parent_path())) return cwd.parent_path();
            }

            // 4) Final fallback: return CWD (callers will log missing file errors)
            return std::filesystem::current_path();
        }
    }

    std::filesystem::path GetRoot()
    {
        // Cache result; root doesn't change during process lifetime.
        static std::once_flag s_Once;
        static std::filesystem::path s_Root;

        std::call_once(s_Once, []
        {
            s_Root = ResolveRoot();
        });

        return s_Root;
    }

    std::string GetAssetPath(const std::string& relativePath)
    {
        auto path = GetRoot() / "assets" / relativePath;
        return path.string();
    }

    std::string GetShaderPath(const std::string& relativePath)
    {
        // Normalize input like "shaders/foo.spv".
        const std::filesystem::path rel = relativePath;

        // 1) Try as-given relative to CWD.
        if (std::filesystem::exists(rel))
        {
            return rel.string();
        }

        // 2) Common dev: launched from build dir (e.g. cmake-build-debug/)
        //    shaders are copied to <build>/bin/shaders/*.
        {
            const auto p = std::filesystem::path("bin") / rel;
            if (std::filesystem::exists(p)) return p.string();
        }

        // 3) Common dev: launched from repo root, shaders live in cmake-build-*/bin/shaders.
        //    Search a couple of typical build folders.
        {
            static const std::vector<std::filesystem::path> buildDirs = {
                "cmake-build-debug", "cmake-build-release", "build", "out/build"
            };

            for (const auto& buildDir : buildDirs)
            {
                const auto p = std::filesystem::path(buildDir) / "bin" / rel;
                if (std::filesystem::exists(p)) return p.string();
            }
        }

        // 4) When launched from bin/, go up.
        {
            const auto p = std::filesystem::path("..") / "bin" / rel;
            if (std::filesystem::exists(p)) return p.string();
        }

        // 5) Last resort: allow SPV to be placed in assets/ (not typical, but convenient).
        {
            auto asset = Core::Filesystem::GetAssetPath(relativePath);
            if (std::filesystem::exists(asset)) return asset;
        }

        return relativePath; // let ShaderModule print a clear error
    }

    std::string ResolveShaderPathOrExit(ShaderPathLookup lookup, Core::Hash::StringID name)
    {
        auto path = lookup(name);
        if (!path)
        {
            Core::Log::Error("CRITICAL: Missing shader configuration for ID: 0x{:08X}", name.Value);
            std::exit(-1);
        }
        return Core::Filesystem::GetShaderPath(*path);
    }

    std::vector<FileWatcher::Entry> FileWatcher::s_Watches;
    std::mutex FileWatcher::s_Mutex;
    std::atomic<bool> FileWatcher::s_Running = false;
    std::thread FileWatcher::s_Thread;

    void FileWatcher::Initialize()
    {
        if (s_Running) return;
        s_Running = true;
        s_Thread = std::thread(ThreadFunc);
        Log::Info("FileWatcher initialized.");
    }

    void FileWatcher::Shutdown()
    {
        s_Running = false;
        if (s_Thread.joinable()) s_Thread.join();

        std::lock_guard lock(s_Mutex);
        s_Watches.clear();
    }

    void FileWatcher::Watch(const std::string& path, ChangeCallback callback)
    {
        std::lock_guard lock(s_Mutex);

        std::error_code ec;
        auto time = std::filesystem::last_write_time(path, ec);
        if (ec)
        {
            Log::Warn("FileWatcher: Could not find file to watch '{}'", path);
            return;
        }

        s_Watches.push_back({path, time, std::move(callback)});
    }

    void FileWatcher::ThreadFunc()
    {
        while (s_Running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::vector<Entry> change_entries;

            {
                std::scoped_lock lock(s_Mutex);
                for (auto& entry : s_Watches)
                {
                    std::error_code ec;
                    if (!std::filesystem::exists(entry.Path, ec)) continue;

                    auto currentTime = std::filesystem::last_write_time(entry.Path, ec);
                    if (ec) continue;

                    if (currentTime > entry.LastTime)
                    {
                        entry.LastTime = currentTime;

                        // Fixed: No try-catch block.
                        // We assume the callback is safe.
                        if (entry.Callback)
                        {
                            change_entries.push_back(entry);
                            //entry.Callback(entry.Path.string());
                        }

                        Log::Info("[HotReload] Detected change: {}", entry.Path.string());
                    }
                }
            }

            for (auto& entry : change_entries)
            {
                if (entry.Callback)
                {
                    entry.Callback(entry.Path.string());
                }
            }
        }
    }
}
