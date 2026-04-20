module;

#include <atomic>

module Extrinsic.Core.Memory:Telemetry.Impl;
import :Telemetry;

namespace Extrinsic::Core::Memory::Telemetry
{
    namespace
    {
        std::atomic<uint64_t> g_AllocCount{0};
        std::atomic<uint64_t> g_AllocBytes{0};
    }

    void RecordAlloc(const size_t bytes) noexcept
    {
        g_AllocCount.fetch_add(1, std::memory_order_relaxed);
        g_AllocBytes.fetch_add(static_cast<uint64_t>(bytes), std::memory_order_relaxed);
    }

    Counters Snapshot() noexcept
    {
        return Counters{
            .AllocCount = g_AllocCount.load(std::memory_order_relaxed),
            .AllocBytes = g_AllocBytes.load(std::memory_order_relaxed)
        };
    }

    void Reset() noexcept
    {
        g_AllocCount.store(0, std::memory_order_relaxed);
        g_AllocBytes.store(0, std::memory_order_relaxed);
    }
}
