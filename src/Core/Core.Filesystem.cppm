module;
#include <filesystem>
#include <string>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>

export module Core:Filesystem;

import :Logging;

export namespace Core::Filesystem
{
    std::filesystem::path GetRoot();

    std::string GetAssetPath(const std::string& relativePath);

    std::string GetShaderPath(const std::string& relativePath);

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
