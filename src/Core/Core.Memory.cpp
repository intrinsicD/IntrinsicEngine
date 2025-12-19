module; // <--- Start Global Fragment

#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <limits>
#include <memory>
#include <expected>
#include <algorithm>
#include <cassert>
#include <thread>

module Core.Memory; // <--- Enter Module Purview

namespace Core::Memory
{
    LinearArena::LinearArena(size_t sizeBytes)
        : m_TotalSize((sizeBytes + CACHE_LINE - 1) & ~(CACHE_LINE - 1))
          , m_OwningThread(std::this_thread::get_id())
    {
        if (sizeBytes == 0)
        {
            m_Start = nullptr;
            m_TotalSize = 0;
            return;
        }
#if defined(_MSC_VER)
        m_Start = static_cast<std::byte*>(_aligned_malloc(m_TotalSize, CACHE_LINE));
#else
        // Now safe because totalSize_ is a multiple of CACHE_LINE
        m_Start = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, m_TotalSize));
#endif

        if (!m_Start)
        {
            m_TotalSize = 0;
            m_Offset = 0;
            // Note: Without exceptions, we can't fail in constructor.
            // Caller must check via GetTotal() == 0 or first Alloc() will fail with OutOfMemory.
        }
    }

    LinearArena::~LinearArena()
    {
        if (m_Start)
        {
#if defined(_MSC_VER)
            _aligned_free(m_Start);
#else
            std::free(m_Start);
#endif
        }
    }

    LinearArena::LinearArena(LinearArena&& other) noexcept
        : m_Start(other.m_Start),
          m_TotalSize(other.m_TotalSize),
          m_Offset(other.m_Offset),
          m_OwningThread(other.m_OwningThread)
    {
        other.m_Start = nullptr;
        other.m_TotalSize = 0;
        other.m_Offset = 0;
        other.m_OwningThread = std::thread::id();
    }

    // Move Assignment
    LinearArena& LinearArena::operator=(LinearArena&& other) noexcept
    {
        if (this != &other)
        {
            // Free our own memory first
            if (m_Start)
            {
#if defined(_MSC_VER)
                _aligned_free(m_Start);
#else
                std::free(m_Start);
#endif
            }

            // Steal resources
            m_Start = other.m_Start;
            m_TotalSize = other.m_TotalSize;
            m_Offset = other.m_Offset;
            m_OwningThread = other.m_OwningThread;

            // Nullify source
            other.m_Start = nullptr;
            other.m_TotalSize = 0;
            other.m_Offset = 0;
            other.m_OwningThread = std::thread::id();
        }
        return *this;
    }

    std::expected<void*, AllocatorError> LinearArena::Alloc(size_t size, size_t align)
    {
        assert(m_OwningThread == std::this_thread::get_id() && "LinearArena is not thread-safe; use a separate arena per thread.");
        if (!m_Start) return std::unexpected(AllocatorError::OutOfMemory);

        // Always align the *current* offset, not just the pointer logic
        // This ensures GetUsed() returns an aligned watermark for subsequent saves/restores
        const size_t safeAlign = std::max(align, alignof(std::max_align_t));

        // Calculate aligned offset
        const size_t alignedOffset = (m_Offset + (safeAlign - 1)) & ~(safeAlign - 1);

        // Check overflow
        if (alignedOffset + size > m_TotalSize)
        {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        void* ptr = m_Start + alignedOffset;
        m_Offset = alignedOffset + size;

        return ptr;
    }

    void LinearArena::Reset()
    {
        m_Offset = 0;
#ifndef NDEBUG
        static constexpr size_t DEBUG_FILL_THRESHOLD_BYTES = 8 * 1024 * 1024; // 8 MB
        // Only memset if start_ is valid!
        if (m_Start && m_TotalSize <= DEBUG_FILL_THRESHOLD_BYTES)
        {
            std::memset(m_Start, 0xCC, m_TotalSize);
        }
#endif
    }
}
