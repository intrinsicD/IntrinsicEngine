module;

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <limits>

module Extrinsic.Core.Memory:LinearArena.Impl;
import :LinearArena;
import :Telemetry;

namespace Extrinsic::Core::Memory
{
    namespace
    {
        [[nodiscard]] constexpr bool IsPowerOfTwo(const size_t x) noexcept
        {
            return x != 0 && (x & (x - 1)) == 0;
        }

        [[nodiscard]] MemoryExpected<size_t> AlignUp(const size_t x, const size_t align) noexcept
        {
            if (!IsPowerOfTwo(align))
                return std::unexpected(MemoryError::InvalidArgument);

            if (x > std::numeric_limits<size_t>::max() - (align - 1))
                return std::unexpected(MemoryError::OutOfRange);

            return (x + (align - 1)) & ~(align - 1);
        }
    }

    LinearArena::LinearArena(const size_t sizeBytes) noexcept
        : m_OwningThread(std::this_thread::get_id())
    {
        if (sizeBytes == 0)
            return;

        auto rounded = AlignUp(sizeBytes, kCacheLineSize);
        if (!rounded)
            return;

        m_Capacity = *rounded;

#if defined(_MSC_VER)
        m_Start = static_cast<std::byte*>(_aligned_malloc(m_Capacity, kCacheLineSize));
#else
        m_Start = static_cast<std::byte*>(std::aligned_alloc(kCacheLineSize, m_Capacity));
#endif

        if (!m_Start)
            m_Capacity = 0;
    }

    LinearArena::~LinearArena()
    {
        if (!m_Start)
            return;

#if defined(_MSC_VER)
        _aligned_free(m_Start);
#else
        std::free(m_Start);
#endif
    }

    LinearArena::LinearArena(LinearArena&& other) noexcept
        : m_Start(other.m_Start)
          , m_Capacity(other.m_Capacity)
          , m_Offset(other.m_Offset)
          , m_OwningThread(other.m_OwningThread)
          , m_Epoch(other.m_Epoch + 1)
    {
        other.m_Start = nullptr;
        other.m_Capacity = 0;
        other.m_Offset = 0;
        other.m_OwningThread = std::thread::id{};
        ++other.m_Epoch;
    }

    LinearArena& LinearArena::operator=(LinearArena&& other) noexcept
    {
        if (this == &other)
            return *this;

        if (m_Start)
        {
#if defined(_MSC_VER)
            _aligned_free(m_Start);
#else
            std::free(m_Start);
#endif
        }

        m_Start = other.m_Start;
        m_Capacity = other.m_Capacity;
        m_Offset = other.m_Offset;
        m_OwningThread = other.m_OwningThread;
        m_Epoch = other.m_Epoch + 1;

        other.m_Start = nullptr;
        other.m_Capacity = 0;
        other.m_Offset = 0;
        other.m_OwningThread = std::thread::id{};
        ++other.m_Epoch;
        return *this;
    }

    MemoryExpected<std::span<std::byte>> LinearArena::AllocBytes(const size_t size, const size_t align) noexcept
    {
        if (m_OwningThread != std::this_thread::get_id())
        {
            std::fprintf(stderr,
                         "[EXTRINSIC] LinearArena thread violation: allocation from non-owning thread.\n");
            return std::unexpected(MemoryError::ThreadViolation);
        }

        if (!m_Start)
            return std::unexpected(MemoryError::OutOfMemory);

        if (size > m_Capacity || align > m_Capacity)
            return std::unexpected(MemoryError::OutOfRange);

        const size_t safeAlign = std::max(align, alignof(std::max_align_t));
        auto alignedOffset = AlignUp(m_Offset, safeAlign);
        if (!alignedOffset)
            return std::unexpected(alignedOffset.error());

        const size_t offset = *alignedOffset;
        if (offset > m_Capacity || size > (m_Capacity - offset))
            return std::unexpected(MemoryError::OutOfMemory);

        m_Offset = offset + size;
        Telemetry::RecordAlloc(size);
        return std::span<std::byte>(m_Start + offset, size);
    }

    MemoryExpected<Extrinsic::Core::Unit> LinearArena::Rewind(const ArenaMarker marker) noexcept
    {
        if (marker.Epoch != m_Epoch || marker.Offset > m_Offset)
            return std::unexpected(MemoryError::InvalidState);

        m_Offset = marker.Offset;
        return Extrinsic::Core::Ok();
    }

    void LinearArena::Reset() noexcept
    {
        m_Offset = 0;
#ifndef NDEBUG
        static constexpr size_t kDebugFillThreshold = 8 * 1024 * 1024;
        if (m_Start && m_Capacity <= kDebugFillThreshold)
            std::memset(m_Start, 0xCC, m_Capacity);
#endif
    }
}
