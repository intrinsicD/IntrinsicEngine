module;
#include <filesystem>
#include <thread>
#include <mutex>
#include <vector>
#include <algorithm>
#include <chrono>

module Core.Filesystem;
import Core.Logging;

namespace Core::Filesystem
{
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
        if (ec) {
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

            std::lock_guard lock(s_Mutex);
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
                    if (entry.Callback) {
                        entry.Callback(entry.Path.string());
                    }
                    
                    Log::Info("[HotReload] Detected change: {}", entry.Path.string());
                }
            }
        }
    }
}