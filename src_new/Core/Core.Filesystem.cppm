module;

#include <filesystem>
#include <string>
#include <functional>
#include <thread>
#include <vector>
#include <mutex>
#include <optional>
#include <atomic>

export module Extrinsic.Core.Filesystem;

import Extrinsic.Core.Hash;

namespace Extrinsic::Core::Filesystem
{
    export std::filesystem::path GetRoot();

    export std::string GetAssetPath(const std::string& relativePath);

    export std::string GetShaderPath(const std::string& relativePath);

    export std::string GetAbsolutePath(const std::string& relativePath);

    //TODO: Is this here really the right place to resolve a shader path? maybe, maybe not?
    using ShaderPathLookup = std::function<std::optional<std::string>(Hash::StringID)>;
    export [[nodiscard]] std::string ResolveShaderPathOrExit(ShaderPathLookup lookup, Hash::StringID name);

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
        };

        static void ThreadFunc();

        static std::vector<Entry> s_Watches;
        static std::mutex s_Mutex;
        static std::atomic<bool> s_Running;
        static std::thread s_Thread;
    };
}