module;
#include <source_location>
#include <chrono>

export module Core:Profiling;

import :Logging;

export namespace Core::Profiling
{
    struct ScopedTimer
    {
        const char* Name;
        std::source_location Loc;
        std::chrono::high_resolution_clock::time_point Start;

        ScopedTimer(const char* name, std::source_location loc = std::source_location::current())
            : Name(name),
              Loc(loc),
              Start(std::chrono::high_resolution_clock::now())
        {
        }

        ~ScopedTimer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - Start).count();
            // In real engine: Submit to Telemetry System
            Log::Info("[PROFILE] {} - {} ({}:{}) took {} us",
                      Name, Loc.file_name(), Loc.line(), Loc.column(), dur);
        }
    };
}

// Macro for easy usage
#define PROFILE_SCOPE(name) Core::Profiling::ScopedTimer timer##__LINE__(name)
#define PROFILE_FUNCTION() PROFILE_SCOPE(__func__)
