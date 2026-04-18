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

module Extrinsic.Core.Filesystem;
import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Core.Logging;
import Extrinsic.Core.Hash;
import Extrinsic.Core.Tasks;

namespace Extrinsic::Core::Filesystem
{
    std::vector<FileWatcher::Entry> FileWatcher::s_Watches;
    std::mutex FileWatcher::s_Mutex;
    std::atomic<bool> FileWatcher::s_Running = false;
    std::thread FileWatcher::s_Thread;
    std::atomic<uint64_t> FileWatcher::s_DeferredEventCount = 0;
    std::atomic<uint64_t> FileWatcher::s_DroppedEventCount = 0;
    std::atomic<uint64_t> FileWatcher::s_InlineDispatchCount = 0;
    std::atomic<uint64_t> FileWatcher::s_SchedulerDispatchCount = 0;

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
            Log::Debug("FileWatcher: Could not find file to watch '{}'", path);
            s_DroppedEventCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        s_Watches.push_back({
            .Path = path,
            .LastTime = time,
            .Callback = std::move(callback),
            .Pending = false,
            .LastDetected = {}
        });
    }

    FileWatcher::Stats FileWatcher::GetStats() noexcept
    {
        return Stats{
            .DeferredEventCount = s_DeferredEventCount.load(std::memory_order_relaxed),
            .DroppedEventCount = s_DroppedEventCount.load(std::memory_order_relaxed),
            .InlineDispatchCount = s_InlineDispatchCount.load(std::memory_order_relaxed),
            .SchedulerDispatchCount = s_SchedulerDispatchCount.load(std::memory_order_relaxed),
        };
    }

    void FileWatcher::ResetStatsForTests() noexcept
    {
        s_DeferredEventCount.store(0, std::memory_order_relaxed);
        s_DroppedEventCount.store(0, std::memory_order_relaxed);
        s_InlineDispatchCount.store(0, std::memory_order_relaxed);
        s_SchedulerDispatchCount.store(0, std::memory_order_relaxed);
    }

    void FileWatcher::ThreadFunc()
    {
        static constexpr auto kScanInterval = std::chrono::milliseconds(500);
        static constexpr auto kDebounceWindow = std::chrono::milliseconds(150);
        static constexpr std::size_t kMaxDispatchesPerTick = 32;

        while (s_Running)
        {
            std::this_thread::sleep_for(kScanInterval);
            struct DispatchItem
            {
                ChangeCallback Callback;
                std::string Path;
            };
            std::vector<DispatchItem> dispatchItems;
            dispatchItems.reserve(kMaxDispatchesPerTick);

            const auto now = std::chrono::steady_clock::now();

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
                        entry.Pending = true;
                        entry.LastDetected = now;
                        Log::Info("[HotReload] Detected change: {}", entry.Path.string());
                    }
                }

                std::size_t dispatched = 0;
                for (auto& entry : s_Watches)
                {
                    if (dispatched >= kMaxDispatchesPerTick) break;
                    if (!entry.Pending || !entry.Callback) continue;
                    if ((now - entry.LastDetected) < kDebounceWindow) continue;

                    dispatchItems.push_back(DispatchItem{
                        .Callback = entry.Callback,
                        .Path = entry.Path.string()
                    });
                    entry.Pending = false;
                    ++dispatched;
                }

                for (const auto& entry : s_Watches)
                {
                    if (entry.Pending)
                    {
                        s_DeferredEventCount.fetch_add(1, std::memory_order_relaxed);
                    }
                }
            }

            for (auto& item : dispatchItems)
            {
                if (Tasks::Scheduler::IsInitialized())
                {
                    s_SchedulerDispatchCount.fetch_add(1, std::memory_order_relaxed);
                    Tasks::Scheduler::Dispatch([cb = std::move(item.Callback), path = std::move(item.Path)]()
                    {
                        cb(path);
                    });
                }
                else
                {
                    s_InlineDispatchCount.fetch_add(1, std::memory_order_relaxed);
                    item.Callback(item.Path);
                }
            }
        }
    }
}
