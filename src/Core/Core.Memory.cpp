module; // <--- Start Global Fragment

// Standard headers must go HERE, before 'module Core.Memory'
#include <cstdlib> // No semicolons!
#include <cstring>
#include <limits>
#include <memory>
#include <expected>

module Core.Memory; // <--- Enter Module Purview

namespace Core::Memory
{
    LinearArena::LinearArena(size_t sizeBytes)
        : m_TotalSize((sizeBytes + CACHE_LINE - 1) & ~(CACHE_LINE - 1))
          , m_Offset(0)
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
           : m_Start(other.m_Start), m_TotalSize(other.m_TotalSize), m_Offset(other.m_Offset)
    {
        other.m_Start = nullptr;
        other.m_TotalSize = 0;
        other.m_Offset = 0;
    }

    // Move Assignment
    LinearArena& LinearArena::operator=(LinearArena&& other) noexcept {
        if (this != &other) {
            // Free our own memory first
            if (m_Start) {
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

            // Nullify source
            other.m_Start = nullptr;
            other.m_TotalSize = 0;
            other.m_Offset = 0;
        }
        return *this;
    }

    std::expected<void*, AllocatorError> LinearArena::Alloc(size_t size, size_t align)
    {
        if (!m_Start) return std::unexpected(AllocatorError::OutOfMemory);
        // Validate align is power of two
        if ((align & (align - 1)) != 0) return std::unexpected(AllocatorError::InvalidAlignment);
        // Clamp align to at least alignof(std::max_align_t)


        const size_t safeAlign = align == 0 ? 1 : align;

        auto currentPtr = reinterpret_cast<uintptr_t>(m_Start + m_Offset);
        uintptr_t alignedPtr = (currentPtr + (safeAlign - 1)) & ~(safeAlign - 1);
        size_t padding = alignedPtr - currentPtr;

        if (padding > (std::numeric_limits<size_t>::max() - m_Offset)) {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        /*if (offset_ + padding + size > totalSize_) //TOTO remove this reduntant test.
        {
            return std::unexpected(AllocatorError::OutOfMemory);
        }*/

        const size_t newOffset = m_Offset + padding;
        if (size > (std::numeric_limits<size_t>::max() - newOffset) || newOffset + size > m_TotalSize) {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        m_Offset = newOffset;
        void* ptr = m_Start + m_Offset;
        m_Offset += size;

        return ptr;
    }

    void LinearArena::Reset()
    {
        m_Offset = 0;
#ifndef NDEBUG
        // Only memset if start_ is valid!
        if (m_Start) {
            std::memset(m_Start, 0xCC, m_TotalSize);
        }
#endif
    }
}
