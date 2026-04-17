module;

#include <filesystem>
#include <string>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <optional>
#include <atomic>
#include <chrono>

export module Extrinsic.Core.Filesystem;

import Extrinsic.Core.Hash;
//import Extrinsic.Core.CallbackRegistry; //TODO: Should the Filewatcher use the CallbaclRegistry? own one itself, or dependency injection? Im unsure...

namespace Extrinsic::Core::Filesystem
{

    export class FileWatcher
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
            bool Pending = false;
            std::chrono::steady_clock::time_point LastDetected{};
        };

        static void ThreadFunc();

        static std::vector<Entry> s_Watches;
        static std::mutex s_Mutex;
        static std::atomic<bool> s_Running;
        static std::thread s_Thread;
    };
}
