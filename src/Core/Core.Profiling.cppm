module;
#include <source_location>
#include <chrono>

export module Core:Profiling;

import :Telemetry;

export namespace Core::Profiling
{
    // Compile-time FNV-1a hash for scope names
    constexpr uint32_t HashString(const char* str)
    {
        uint32_t hash = 2166136261u;
        while (*str)
        {
            hash ^= static_cast<uint8_t>(*str++);
            hash *= 16777619u;
        }
        return hash;
    }

    struct ScopedTimer
    {
        const char* Name;
        uint32_t NameHash;
        std::source_location Loc;
        std::chrono::high_resolution_clock::time_point Start;

        ScopedTimer(const char* name, std::source_location loc = std::source_location::current())
            : Name(name),
              NameHash(HashString(name)),
              Loc(loc),
              Start(std::chrono::high_resolution_clock::now())
        {
        }

        ~ScopedTimer()
        {
            auto end = std::chrono::high_resolution_clock::now();
            auto durationNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - Start).count();
            // Submit to Telemetry System (no console output)
            Telemetry::TelemetrySystem::Get().RecordSample(NameHash, Name, durationNs);
        }
    };
}


