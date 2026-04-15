module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <deque>
#include <condition_variable>
#include <vector>
#include <utility>

module Extrinsic.Core.Logging;

namespace Extrinsic::Core::Log
{
 // -------------------------------------------------------------------------
    // Ring-buffer log sink (fixed capacity, overwrites oldest on full)
    // -------------------------------------------------------------------------

    static constexpr std::size_t kLogCapacity = 2048;

    struct LogRingBuffer
    {
        std::array<LogEntry, kLogCapacity> Entries{};
        std::size_t WritePos  = 0;   // Next write slot (unbounded, modulo on access)
        std::size_t Count     = 0;   // Current number of stored entries (≤ kLogCapacity)
    };

    static LogRingBuffer      s_Ring;
    static std::mutex         s_LogMutex;
    static std::atomic<uint64_t> s_Sequence{0};

    struct AsyncConsoleSink
    {
        std::mutex Mutex;
        std::condition_variable Cv;
        std::deque<LogEntry> Queue;
        bool Running = true;
        std::thread Worker;
    };

    static AsyncConsoleSink s_ConsoleSink;
    static std::once_flag s_ConsoleSinkInitOnce;

    [[nodiscard]] static auto ColoredLabel(const Level level) noexcept -> std::pair<const char*, const char*>
    {
        switch (level)
        {
        case Level::Info: return {"\033[32m", "[INFO] "}; // Green
        case Level::Warning: return {"\033[33m", "[WARN] "}; // Yellow
        case Level::Error: return {"\033[31m", "[ERR]  "}; // Red
        case Level::Debug: return {"\033[36m", "[DBG]  "}; // Cyan
        }

        return {"\033[0m", "[INFO] "};
    }

    static void ConsoleSinkWorker()
    {
        for (;;)
        {
            std::vector<LogEntry> batch;
            {
                std::unique_lock lock(s_ConsoleSink.Mutex);
                s_ConsoleSink.Cv.wait(lock, []
                {
                    return !s_ConsoleSink.Running || !s_ConsoleSink.Queue.empty();
                });

                if (!s_ConsoleSink.Running && s_ConsoleSink.Queue.empty())
                    return;

                batch.reserve(s_ConsoleSink.Queue.size());
                while (!s_ConsoleSink.Queue.empty())
                {
                    batch.push_back(std::move(s_ConsoleSink.Queue.front()));
                    s_ConsoleSink.Queue.pop_front();
                }
            }

            for (const auto& entry : batch)
            {
                const auto [color, label] = ColoredLabel(entry.Lvl);
                std::cout << color << label << entry.Message << "\033[0m" << std::endl;
            }
        }
    }

    static void EnsureConsoleSinkInitialized()
    {
        std::call_once(s_ConsoleSinkInitOnce, []
        {
            s_ConsoleSink.Worker = std::thread(ConsoleSinkWorker);
        });
    }

    struct ConsoleSinkShutdown
    {
        ~ConsoleSinkShutdown()
        {
            {
                std::lock_guard lock(s_ConsoleSink.Mutex);
                s_ConsoleSink.Running = false;
            }
            s_ConsoleSink.Cv.notify_all();
            if (s_ConsoleSink.Worker.joinable())
                s_ConsoleSink.Worker.join();
        }
    };

    static ConsoleSinkShutdown s_ConsoleSinkShutdown;

    // -------------------------------------------------------------------------
    // Core write path
    // -------------------------------------------------------------------------

    void PrintColored(const Level level, const std::string_view msg)
    {
        EnsureConsoleSinkInitialized();

        // Append to ring buffer (snapshot data path)
        std::lock_guard lock(s_LogMutex);
        LogEntry pendingEntry{
            .Lvl = level,
            .Message = std::string(msg)
        };

        // Append to ring buffer
        auto& entry = s_Ring.Entries[s_Ring.WritePos % kLogCapacity];
        entry = pendingEntry;

        ++s_Ring.WritePos;
        if (s_Ring.Count < kLogCapacity)
            ++s_Ring.Count;

        s_Sequence.fetch_add(1, std::memory_order_release);

        // Queue async console output (I/O path)
        {
            std::lock_guard qLock(s_ConsoleSink.Mutex);
            s_ConsoleSink.Queue.push_back(std::move(pendingEntry));
        }
        s_ConsoleSink.Cv.notify_one();
    }

    // -------------------------------------------------------------------------
    // Read API
    // -------------------------------------------------------------------------

    uint64_t GetSequenceNumber() noexcept
    {
        return s_Sequence.load(std::memory_order_acquire);
    }

    LogSnapshot TakeSnapshot()
    {
        LogSnapshot snap;

        {
            std::lock_guard lock(s_LogMutex);

            snap.Sequence = s_Sequence.load(std::memory_order_relaxed);

            if (s_Ring.Count == 0)
                return snap;

            snap.Entries.reserve(s_Ring.Count);

            const std::size_t oldest = s_Ring.WritePos - s_Ring.Count;
            for (std::size_t i = oldest; i < s_Ring.WritePos; ++i)
                snap.Entries.push_back(s_Ring.Entries[i % kLogCapacity]);
        }

        return snap;
    }

    std::size_t GetEntryCount()
    {
        std::lock_guard lock(s_LogMutex);
        return s_Ring.Count;
    }

    void ClearEntries()
    {
        std::lock_guard lock(s_LogMutex);
        s_Ring.WritePos = 0;
        s_Ring.Count    = 0;
        // Sequence counter is NOT reset — preserves monotonicity for UI
        // change detection (see CLAUDE.md log sink contract).
    }
}
