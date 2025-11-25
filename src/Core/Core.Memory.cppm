module; // Global Fragment
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <concepts>
#include <expected>
#include <span>
#include <utility>

export module Core.Memory;

export namespace Core::Memory
{
    // Constants (Implicitly internal unless exported, but usually fine as is)
    constexpr size_t DEFAULT_ALIGNMENT = 16;
    constexpr size_t CACHE_LINE = 64;

    // EXPORT THIS ENUM
    enum class AllocatorError
    {
        OutOfMemory,
        InvalidAlignment,
        Overflow
    };

    // EXPORT THIS CONCEPT
    template <typename T>
    concept Allocator = requires(T a, size_t size, size_t align)
    {
        { a.Alloc(size, align) } -> std::convertible_to<std::expected<void*, AllocatorError>>;
        { a.Reset() } -> std::same_as<void>;
    };

    // -------------------------------------------------------------------------
    // LinearArena - High-performance bump allocator for temporary allocations
    // -------------------------------------------------------------------------
    // CRITICAL DESIGN DECISION: This allocator does NOT call destructors!
    //
    // Rationale:
    // - Zero-overhead abstraction for per-frame allocations
    // - Destructors would require tracking all allocated objects (overhead)
    // - Designed for POD types and types with trivial destructors only
    //
    // Usage Requirements:
    // - ONLY allocate trivially destructible types (std::is_trivially_destructible_v)
    // - DO NOT allocate types with non-trivial destructors (std::string, std::vector, etc.)
    // - Use std::unique_ptr/std::shared_ptr from heap for complex types
    // - RenderGraph enforces this with static_assert
    //
    // Performance Benefits:
    // - O(1) allocation (just bump offset pointer)
    // - O(1) reset (just set offset to 0)
    // - Cache-friendly linear memory layout
    // - No per-allocation bookkeeping
    // -------------------------------------------------------------------------
    // EXPORT THE CLASS
    class LinearArena
    {
    public:
        LinearArena(const LinearArena&) = delete;
        LinearArena& operator=(const LinearArena&) = delete;

        // Move Constructor
        LinearArena(LinearArena&& other) noexcept
            : start_(other.start_), totalSize_(other.totalSize_), offset_(other.offset_)
        {
            other.start_ = nullptr;
            other.totalSize_ = 0;
            other.offset_ = 0;
        }

        // Move Assignment
        LinearArena& operator=(LinearArena&& other) noexcept {
            if (this != &other) {
                // Free our own memory first
                if (start_) {
#if defined(_MSC_VER)
                    _aligned_free(start_);
#else
                    std::free(start_);
#endif
                }

                // Steal resources
                start_ = other.start_;
                totalSize_ = other.totalSize_;
                offset_ = other.offset_;

                // Nullify source
                other.start_ = nullptr;
                other.totalSize_ = 0;
                other.offset_ = 0;
            }
            return *this;
        }

        explicit LinearArena(size_t sizeBytes);
        ~LinearArena();

        [[nodiscard]]
        std::expected<void*, AllocatorError> Alloc(size_t size, size_t align = DEFAULT_ALIGNMENT);

        template <typename T, typename... Args>
        [[nodiscard]]
        std::expected<T*, AllocatorError> New(Args&&... args)
        {
            auto mem = Alloc(sizeof(T), alignof(T));
            if (!mem) return std::unexpected(mem.error());

            T* ptr = static_cast<T*>(*mem);
            std::construct_at(ptr, std::forward<Args>(args)...);
            return ptr;
        }

        template <typename T>
        [[nodiscard]]
        std::expected<std::span<T>, AllocatorError> NewArray(size_t count)
        {
            auto mem = Alloc(sizeof(T) * count, alignof(T));
            if (!mem) return std::unexpected(mem.error());

            T* ptr = static_cast<T*>(*mem);
            for (size_t i = 0; i < count; ++i) std::construct_at(ptr + i);

            return std::span<T>(ptr, count);
        }

        void Reset();
        [[nodiscard]] size_t GetUsed() const { return offset_; }
        [[nodiscard]] size_t GetTotal() const { return totalSize_; }

    private:
        std::byte* start_ = nullptr;
        size_t totalSize_ = 0;
        size_t offset_ = 0;
    };
}
