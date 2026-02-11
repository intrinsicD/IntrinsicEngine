module; // <--- Start Global Fragment

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <memory>
#include <expected>
#include <algorithm>
#include <thread>

module Core:Memory.Impl;
import :Memory;

namespace Core::Memory
{
    LinearArena::LinearArena(size_t sizeBytes)
        : m_TotalSize((sizeBytes + CACHE_LINE - 1) & ~(CACHE_LINE - 1))
          , m_OwningThread(std::this_thread::get_id())
          , m_Generation(Detail::g_NextArenaGeneration.fetch_add(1, std::memory_order_relaxed))
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
        // Invalidate generation so any ArenaAllocator still holding a pointer
        // to this arena will detect the lifetime violation on next allocate().
        m_Generation = 0;

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
          // The thread receiving the move is the new owner.
          m_OwningThread(std::this_thread::get_id()),
          // New generation — any ArenaAllocator holding the old generation will detect the move.
          m_Generation(Detail::g_NextArenaGeneration.fetch_add(1, std::memory_order_relaxed))
    {
        // Invalidate the moved-from arena's generation.
        other.m_Generation = 0;
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
            // Invalidate our own generation first (any allocators pointing at us are now stale).
            m_Generation = 0;

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
            // The thread receiving the move is the new owner.
            m_OwningThread = std::this_thread::get_id();
            // New generation for the new identity.
            m_Generation = Detail::g_NextArenaGeneration.fetch_add(1, std::memory_order_relaxed);

            // Nullify source
            other.m_Generation = 0;
            other.m_Start = nullptr;
            other.m_TotalSize = 0;
            other.m_Offset = 0;
            other.m_OwningThread = std::thread::id();
        }
        return *this;
    }

    std::expected<void*, AllocatorError> LinearArena::Alloc(size_t size, size_t align)
    {
        // Thread safety check - always enforced in ALL builds (not just debug).
        // This prevents silent memory corruption from cross-thread access.
        // We use fprintf instead of Core::Log to avoid circular module dependencies.
        if (m_OwningThread != std::this_thread::get_id())
        {
            std::fprintf(stderr, "[INTRINSIC] LinearArena thread violation: arena owned by different thread. "
                                 "Use a separate arena per thread.\n");
            return std::unexpected(AllocatorError::ThreadViolation);
        }
        if (!m_Start) return std::unexpected(AllocatorError::OutOfMemory);

        // Early overflow check for obviously invalid inputs
        if (size > m_TotalSize || align > m_TotalSize)
        {
            return std::unexpected(AllocatorError::Overflow);
        }

        if (align == 0 || (align & (align - 1)) != 0)
        {
            return std::unexpected(AllocatorError::InvalidAlignment);
        }

        // Always align the *current* offset, not just the pointer logic
        // This ensures GetUsed() returns an aligned watermark for subsequent saves/restores
        const size_t safeAlign = std::max(align, alignof(std::max_align_t));

        // Calculate aligned offset
        const size_t alignedOffset = (m_Offset + (safeAlign - 1)) & ~(safeAlign - 1);

        // Check for alignment calculation overflow (would wrap around on huge offsets)
        if (alignedOffset < m_Offset)
        {
            return std::unexpected(AllocatorError::Overflow);
        }

        // Check space availability
        if (alignedOffset + size > m_TotalSize || alignedOffset + size < alignedOffset)
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

    // -------------------------------------------------------------------------
    // ScopeStack Implementation
    // -------------------------------------------------------------------------
    ScopeStack::ScopeStack(ScopeStack&& other) noexcept
        : m_Arena(std::move(other.m_Arena)),
          m_Head(other.m_Head),
          m_DestructorCount(other.m_DestructorCount)
    {
        other.m_Head = nullptr;
        other.m_DestructorCount = 0;
    }

    ScopeStack& ScopeStack::operator=(ScopeStack&& other) noexcept
    {
        if (this != &other)
        {
            // First, destroy our current objects (LIFO order)
            Reset();
            m_Arena = std::move(other.m_Arena);
            m_Head = other.m_Head;
            m_DestructorCount = other.m_DestructorCount;
            other.m_Head = nullptr;
            other.m_DestructorCount = 0;
        }
        return *this;
    }

    void ScopeStack::Reset()
    {
        // Walk the intrusive linked list and call destructors (already LIFO order)
        DestructorNode* current = m_Head;
        while (current)
        {
            current->DestroyFn(current->Ptr);
            current = current->Next;
        }
        m_Head = nullptr;
        m_DestructorCount = 0;
        m_Arena.Reset(); // Reclaim all memory (objects + headers)
    }
}
