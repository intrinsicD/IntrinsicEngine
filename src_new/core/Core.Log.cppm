module;

#include <cstddef>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

export module Core.Log;

namespace Extrinsic::Core::Log
{
    export enum class Level : uint8_t
    {
        Info,
        Warning,
        Error,
        Debug
    };

    export struct LogEntry
    {
        Level Lvl = Level::Info;
        std::string Message;
    };

    export void SetMinimumLevel(Level level) noexcept;
    [[nodiscard]] Level GetMinimumLevel() noexcept;
    [[nodiscard]] bool IsEnabled(Level level) noexcept;

    // Internal helper to print color codes and append to ring buffer
    void PrintColored(Level level, std::string_view msg);

    // -------------------------------------------------------------------------
    // Public API — log emission
    // -------------------------------------------------------------------------

    export template <typename... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        if (!IsEnabled(Level::Info))
            return;
        PrintColored(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    export template <typename... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        if (!IsEnabled(Level::Warning))
            return;
        PrintColored(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
    }

    export template <typename... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        if (!IsEnabled(Level::Error))
            return;
        PrintColored(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    // Only prints in Debug builds
    export template <typename... Args>
    void Debug([[maybe_unused]] std::format_string<Args...> fmt, [[maybe_unused]] Args&&... args)
    {
        if (!IsEnabled(Level::Debug))
            return;
        PrintColored(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
    }

    export [[nodiscard]] uint64_t GetSequenceNumber() noexcept;

    // Snapshot of ring-buffer entries for lock-free rendering.
    export struct LogSnapshot
    {
        std::vector<LogEntry> Entries;
        uint64_t Sequence = 0;
    };

    // Returns a copy of the current ring-buffer contents (chronological order,
    // oldest first) along with the sequence number at the time of the copy.
    // The mutex is held only for the copy — the caller may render or process
    // the snapshot without blocking logging threads.
    export [[nodiscard]] LogSnapshot TakeSnapshot();

    // Returns the current number of stored entries (up to capacity).
    export [[nodiscard]] std::size_t GetEntryCount();

    // Clears all stored entries. Does NOT reset the sequence counter
    // (preserving monotonicity for UI change detection).
    export void ClearEntries();
}
