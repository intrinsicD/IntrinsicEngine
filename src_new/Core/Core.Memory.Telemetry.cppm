module;

#include <atomic>
#include <cstddef>
#include <cstdint>

export module Extrinsic.Core.Memory:Telemetry;

export namespace Extrinsic::Core::Memory::Telemetry
{
    struct Counters
    {
        uint64_t AllocCount = 0;
        uint64_t AllocBytes = 0;
    };

    void RecordAlloc(size_t bytes) noexcept;
    [[nodiscard]] Counters Snapshot() noexcept;
    void Reset() noexcept;
}
