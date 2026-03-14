module;
#include <source_location>
#include <chrono>

export module Core.Profiling;

import Core.Hash;
import Core.Telemetry;

export namespace Core::Profiling
{
    // FNV-1a hash — delegates to Core::Hash::HashString.
    using Core::Hash::HashString;

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


