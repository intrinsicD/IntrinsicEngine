module; // <--- Start Global Fragment

// Standard headers must go HERE, before 'module Core.Memory'
#include <cstdlib> // No semicolons!
#include <cstring>
#include <new>
#include <memory>
#include <expected>

module Core.Memory; // <--- Enter Module Purview

namespace Core::Memory {

    LinearArena::LinearArena(size_t sizeBytes)
        : totalSize_(sizeBytes)
        , offset_(0)
    {
#if defined(_MSC_VER)
        start_ = static_cast<std::byte*>(_aligned_malloc(sizeBytes, CACHE_LINE));
#else
        start_ = static_cast<std::byte*>(std::aligned_alloc(CACHE_LINE, sizeBytes));
#endif
    }

    LinearArena::~LinearArena() {
        if (start_) {
#if defined(_MSC_VER)
            _aligned_free(start_);
#else
            std::free(start_);
#endif
        }
    }

    std::expected<void*, AllocatorError> LinearArena::Alloc(size_t size, size_t align) {
        uintptr_t currentPtr = reinterpret_cast<uintptr_t>(start_ + offset_);
        uintptr_t alignedPtr = (currentPtr + (align - 1)) & ~(align - 1);
        size_t padding = alignedPtr - currentPtr;

        if (offset_ + padding + size > totalSize_) {
            return std::unexpected(AllocatorError::OutOfMemory);
        }

        offset_ += padding;
        void* ptr = start_ + offset_;
        offset_ += size;

        return ptr;
    }

    void LinearArena::Reset() {
        offset_ = 0;
#ifdef _DEBUG
        std::memset(start_, 0xCC, totalSize_);
#endif
    }
}