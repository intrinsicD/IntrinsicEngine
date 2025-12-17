module;
#include <filesystem>
#include <string>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>

export module Core.Filesystem;

import Core.Logging;

export namespace Core::Filesystem {
    
    std::filesystem::path GetRoot() {
        // 1. Check if "assets" exists in current working directory (Production/Binary Release)
        if (std::filesystem::exists("assets")) {
            return std::filesystem::current_path();
        }

        // 2. Check if we are in "bin" and need to go up (Common dev scenario)
        if (std::filesystem::exists("../assets")) {
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

    std::string GetAssetPath(const std::string& relativePath) {
        auto path = GetRoot() / relativePath;
        return path.string();
    }

    class FileWatcher
    {
    public:
        using ChangeCallback = std::function<void(const std::string&)>;

        static void Initialize();
        static void Shutdown();

        // Register a file to be watched. Callback runs on the Watcher Thread!
        static void Watch(const std::string& path, ChangeCallback callback);

    private:
        struct Entry
        {
            std::filesystem::path Path;
            std::filesystem::file_time_type LastTime;
            ChangeCallback Callback;
        };

        static void ThreadFunc();

        static std::vector<Entry> s_Watches;
        static std::mutex s_Mutex;
        static std::atomic<bool> s_Running;
        static std::thread s_Thread;
    };
}