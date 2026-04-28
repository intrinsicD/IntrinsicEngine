module;

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

module Core.Logging;

namespace Core::Log
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

    // -------------------------------------------------------------------------
    // Core write path
    // -------------------------------------------------------------------------

    void PrintColored(Level level, std::string_view msg)
    {
        std::lock_guard lock(s_LogMutex);

        // ANSI Color Codes
        const char* color = "\033[0m";
        const char* label = "[INFO]";

        switch (level) {
        case Level::Info:    color = "\033[32m"; label = "[INFO] "; break; // Green
        case Level::Warning: color = "\033[33m"; label = "[WARN] "; break; // Yellow
        case Level::Error:   color = "\033[31m"; label = "[ERR]  "; break; // Red
        case Level::Debug:   color = "\033[36m"; label = "[DBG]  "; break; // Cyan
        }

        std::cout << color << label << msg << "\033[0m" << std::endl;

        // Append to ring buffer
        auto& entry = s_Ring.Entries[s_Ring.WritePos % kLogCapacity];
        entry.Lvl     = level;
        entry.Message = std::string(msg);

        ++s_Ring.WritePos;
        if (s_Ring.Count < kLogCapacity)
            ++s_Ring.Count;

        s_Sequence.fetch_add(1, std::memory_order_release);
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