module;

#include <atomic>
#include <cstddef>
#include <cstdint>

export module Extrinsic.Core.Memory:Telemetry;

export namespace Extrinsic::Core::Memory::Telemetry
{
    export struct Counters
    {
        uint64_t AllocCount = 0;
        uint64_t AllocBytes = 0;
    };

    export void RecordAlloc(size_t bytes) noexcept;
    export [[nodiscard]] Counters Snapshot() noexcept;
    export void Reset() noexcept;
}
