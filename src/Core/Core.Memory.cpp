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
        : totalSize_((sizeBytes + CACHE_LINE - 1) & ~(CACHE_LINE - 1))
          , offset_(0)
    {
#if defined(_MSC_VER)
        start_ = static_cast<std::byte*>(_aligned_malloc(totalSize_, CACHE_LINE));
#else
        // Now safe because totalSize_ is a multiple of CACHE_LINE
        start_ = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, totalSize_));
#endif
    }

    LinearArena::~LinearArena()
    {
        if (start_)
        {
#if defined(_MSC_VER)
            _aligned_free(start_);
#else
            std::free(start_);
#endif
        }
    }

    std::expected<void*, AllocatorError> LinearArena::Alloc(size_t size, size_t align)
    {
        if (!start_) return std::unexpected(AllocatorError::OutOfMemory);

        const size_t safeAlign = align == 0 ? 1 : align;

        uintptr_t currentPtr = reinterpret_cast<uintptr_t>(start_ + offset_);
        uintptr_t alignedPtr = (currentPtr + (safeAlign - 1)) & ~(safeAlign - 1);
        size_t padding = alignedPtr - currentPtr;

        if (padding > (std::numeric_limits<size_t>::max() - offset_)) {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        if (offset_ + padding + size > totalSize_)
        {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        const size_t newOffset = offset_ + padding;
        if (size > (std::numeric_limits<size_t>::max() - newOffset) || newOffset + size > totalSize_) {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        offset_ = newOffset;
        void* ptr = start_ + offset_;
        offset_ += size;

        return ptr;
    }

    void LinearArena::Reset()
    {
        offset_ = 0;
#ifndef NDEBUG
        // Only memset if start_ is valid!
        if (start_) {
            std::memset(start_, 0xCC, totalSize_);
        }
#endif
    }
}
