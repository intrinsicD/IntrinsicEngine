module;

#include <format>
#include <string_view>

export module Core.Logging;

namespace Core::Log
{
    enum class Level
    {
        Info,
        Warning,
        Error,
        Debug
    };

    // Internal helper to print color codes
    void PrintColored(Level level, std::string_view msg);

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    export template <typename... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        PrintColored(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    export template <typename... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        PrintColored(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
    }

    export template <typename... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        PrintColored(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    // Only prints in Debug builds
    export template <typename... Args>
    void Debug([[maybe_unused]] std::format_string<Args...> fmt, [[maybe_unused]] Args&&... args)
    {
#ifndef NDEBUG
        PrintColored(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
#endif
    }
}
