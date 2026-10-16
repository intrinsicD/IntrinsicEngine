// Backend-private offset allocator shared by the real staging belt and CPU tests.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace Extrinsic::Backends::Vulkan::Detail
{
    class StagingRing
    {
    public:
        explicit StagingRing(std::size_t capacity) : m_Capacity(capacity) {}

        [[nodiscard]] std::optional<std::size_t> Allocate(std::size_t sizeBytes,
                                                        std::size_t alignment)
        {
            if (sizeBytes == 0 || sizeBytes > m_Capacity)
                return std::nullopt;
            if (m_Used == 0) m_Tail = 0;
            if (alignment == 0) alignment = 1;
            const std::size_t remainder = m_Tail % alignment;
            std::size_t padding = remainder == 0 ? 0 : alignment - remainder;
            std::size_t offset;
            const std::size_t untilEnd = m_Capacity - m_Tail;
            if (padding > untilEnd || sizeBytes > untilEnd - padding)
            {
                // Wrap padding stays occupied until this allocation retires.
                padding = untilEnd;
                offset = 0;
            }
            else
                offset = m_Tail + padding;
            const std::size_t available = m_Capacity - m_Used;
            if (padding > available || sizeBytes > available - padding)
                return std::nullopt;
            if (m_PendingBytes == 0) m_PendingStartTail = m_Tail;
            const std::size_t consumed = padding + sizeBytes;
            m_Tail = offset + sizeBytes;
            m_Used += consumed;
            m_PendingBytes += consumed;
            return offset;
        }

        void Retire(std::uint64_t retireValue)
        {
            if (m_PendingBytes != 0)
            {
                m_InFlight.push_back({m_PendingBytes, retireValue});
                m_PendingBytes = 0;
            }
        }

        void GarbageCollect(std::uint64_t completedValue)
        {
            while (!m_InFlight.empty() && m_InFlight.front().RetireValue <= completedValue)
            {
                m_Used -= m_InFlight.front().Bytes;
                m_InFlight.pop_front();
            }
        }

        void CancelPending()
        {
            if (m_PendingBytes == 0) return;
            m_Tail = m_PendingStartTail;
            m_Used -= m_PendingBytes;
            m_PendingBytes = 0;
        }

    private:
        struct Region { std::size_t Bytes; std::uint64_t RetireValue; };
        std::size_t m_Capacity;
        std::size_t m_Tail = 0;
        std::size_t m_Used = 0;
        std::size_t m_PendingStartTail = 0;
        std::size_t m_PendingBytes = 0;
        std::deque<Region> m_InFlight;
    };
}
