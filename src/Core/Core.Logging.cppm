module;
#include <iostream>
#include <format>
#include <source_location>
#include <string_view>
#include <mutex>

export module Core.Logging;

namespace Core::Log {

    // Global lock to prevent scrambled output from multiple threads
    std::mutex s_LogMutex;

    enum class Level {
        Info,
        Warning,
        Error,
        Debug
    };

    // Internal helper to print color codes
    void PrintColored(Level level, std::string_view msg) {
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
    }

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------
    
    export template<typename... Args>
    void Info(std::format_string<Args...> fmt, Args&&... args) {
        PrintColored(Level::Info, std::format(fmt, std::forward<Args>(args)...));
    }

    export template<typename... Args>
    void Warn(std::format_string<Args...> fmt, Args&&... args) {
        PrintColored(Level::Warning, std::format(fmt, std::forward<Args>(args)...));
    }

    export template<typename... Args>
    void Error(std::format_string<Args...> fmt, Args&&... args) {
        PrintColored(Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    // Only prints in Debug builds
    export template<typename... Args>
    void Debug(std::format_string<Args...> fmt, Args&&... args) {
#ifndef NDEBUG
        PrintColored(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
#endif
    }
}