module;

#include <iostream>
#include <mutex>
#include <string_view>

module Core.Logging;

namespace Core::Log
{
    // Global lock to prevent scrambled output from multiple threads
    std::mutex s_LogMutex;

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
    }
}