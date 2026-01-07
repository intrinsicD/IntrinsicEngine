module;
#include <filesystem>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <chrono>

module Core:Filesystem.Impl;
import Core;

namespace Core::Filesystem
{
    std::filesystem::path GetRoot()
    {
        // 1. Check if "assets" exists in current working directory (Production/Binary Release)
        if (std::filesystem::exists("assets"))
        {
            return std::filesystem::current_path();
        }

        // 2. Check if we are in "bin" and need to go up (Common dev scenario)
        if (std::filesystem::exists("../assets"))
        {
            return std::filesystem::current_path().parent_path();
        }

        // 3. Fallback: Use the hardcoded CMake source path (Debug / Research only)
        // This ensures it works even if you run it from /tmp/
#ifdef ENGINE_ROOT_DIR
        return std::filesystem::path(ENGINE_ROOT_DIR);
#else
        return std::filesystem::current_path(); // Pray
#endif
    }

    std::string GetAssetPath(const std::string& relativePath)
    {
        auto path = GetRoot() / "assets" / relativePath;
        return path.string();
    }

    std::string GetShaderPath(const std::string& relativePath)
    {
        // try CWD/rel first
        if (std::filesystem::exists(relativePath))
        {
            return relativePath;
        }
        // try CWD/bin/rel (when launched from build dir)
        if (std::filesystem::exists(std::filesystem::path("bin") / relativePath))
        {
            return (std::filesystem::path("bin") / relativePath).string();
        }
        // try CWD/../bin/rel (when launched from repo root)
        if (std::filesystem::exists(std::filesystem::path("..") / "bin" / relativePath))
        {
            return (std::filesystem::path("..") / "bin" / relativePath).string();
        }
        // last resort: try assets (if you ever copy SPV there)
        auto asset = Core::Filesystem::GetAssetPath(relativePath);
        if (std::filesystem::exists(asset))
        {
            return asset;
        }
        return relativePath; // let ShaderModule print a clear error
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
