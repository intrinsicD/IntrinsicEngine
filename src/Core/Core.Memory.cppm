module; // Global Fragment
#include <cstddef>
#include <cstdlib>
#include <concepts>
#include <expected>
#include <span>
#include <utility>
#include <thread>
#include <vector>
#include <functional>

export module Core:Memory;

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
        Overflow,
        ThreadViolation // Cross-thread access attempted on thread-local allocator
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
        LinearArena(LinearArena&& other) noexcept;

        // Move Assignment
        LinearArena& operator=(LinearArena&& other) noexcept;

        explicit LinearArena(size_t sizeBytes);
        ~LinearArena();

        [[nodiscard]]
        std::expected<void*, AllocatorError> Alloc(size_t size, size_t align = DEFAULT_ALIGNMENT);

        template <typename T, typename... Args>
        [[nodiscard]]
        std::expected<T*, AllocatorError> New(Args&&... args)
        {
            static_assert(std::is_trivially_destructible_v<T>,
                          "LinearArena cannot manage types with non-trivial destructors. Use a PoolAllocator or standard heap.")
                ;
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
            static_assert(std::is_trivially_destructible_v<T>,
                          "LinearArena cannot manage arrays of non-trivially-destructible types.");
            auto mem = Alloc(sizeof(T) * count, alignof(T));
            if (!mem) return std::unexpected(mem.error());

            T* ptr = static_cast<T*>(*mem);
            for (size_t i = 0; i < count; ++i) std::construct_at(ptr + i);

            return std::span<T>(ptr, count);
        }

        void Reset();
        [[nodiscard]] size_t GetUsed() const { return m_Offset; }
        [[nodiscard]] size_t GetTotal() const { return m_TotalSize; }

    private:
        std::byte* m_Start = nullptr;
        size_t m_TotalSize = 0;
        size_t m_Offset = 0;
        std::thread::id m_OwningThread;
    };

    // -------------------------------------------------------------------------
    // ScopeStack - Frame allocator with destructor support
    // -------------------------------------------------------------------------
    // PURPOSE: A hybrid allocator for render passes and per-frame allocations
    // that need to capture non-trivially-destructible types (e.g., std::shared_ptr,
    // std::string, std::function).
    //
    // Design Tradeoffs:
    // - Uses LinearArena for O(1) bump allocation (fast path)
    // - Tracks destructors in a separate vector (heap allocation overhead)
    // - Destroys objects in LIFO order on Reset() (stack semantics)
    //
    // Use Cases:
    // - RenderGraph passes that capture std::shared_ptr<Texture>
    // - Debug passes that capture std::string names
    // - Callbacks/closures with complex captured state
    //
    // Performance Notes:
    // - ~10-20 bytes overhead per non-trivial allocation (destructor storage)
    // - Still O(1) allocation, O(n) reset where n = non-trivial allocations
    // - For POD-only hot paths, prefer LinearArena directly
    // -------------------------------------------------------------------------
    class ScopeStack
    {
    public:
        ScopeStack(const ScopeStack&) = delete;
        ScopeStack& operator=(const ScopeStack&) = delete;

        ScopeStack(ScopeStack&& other) noexcept;
        ScopeStack& operator=(ScopeStack&& other) noexcept;

        explicit ScopeStack(size_t size) : m_Arena(size)
        {
        }

        ~ScopeStack() { Reset(); } // RAII - destructors called in reverse order

        template <typename T, typename... Args>
        [[nodiscard]] std::expected<T*, AllocatorError> New(Args&&... args)
        {
            auto result = m_Arena.Alloc(sizeof(T), alignof(T));
            if (!result) return std::unexpected(result.error());

            T* ptr = static_cast<T*>(*result);
            std::construct_at(ptr, std::forward<Args>(args)...);

            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                m_Destructors.push_back([ptr]() { std::destroy_at(ptr); });
            }
            return ptr;
        }

        template <typename T>
        [[nodiscard]] std::expected<std::span<T>, AllocatorError> NewArray(size_t count)
        {
            static_assert(std::is_default_constructible_v<T>,
                          "ScopeStack::NewArray requires T to be default-constructible. "
                          "Use New<T>(args...) in a loop or add a NewArray(count, ctorArgs...) overload if needed.");

            if (count == 0) return std::span<T>{};

            auto mem = m_Arena.Alloc(sizeof(T) * count, alignof(T));
            if (!mem) return std::unexpected(mem.error());

            T* ptr = static_cast<T*>(*mem);
            for (size_t i = 0; i < count; ++i)
            {
                std::construct_at(ptr + i);
            }

            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                // Capture both pointer and count for array destruction
                m_Destructors.push_back([ptr, count]()
                {
                    // Destroy in reverse order within the array
                    for (size_t i = count; i > 0; --i)
                    {
                        std::destroy_at(ptr + i - 1);
                    }
                });
            }
            return std::span<T>(ptr, count);
        }

        void Reset();

        // Diagnostics
        [[nodiscard]] size_t GetUsed() const { return m_Arena.GetUsed(); }
        [[nodiscard]] size_t GetTotal() const { return m_Arena.GetTotal(); }
        [[nodiscard]] size_t GetDestructorCount() const { return m_Destructors.size(); }

        // Direct arena access for POD allocations (bypasses destructor tracking)
        LinearArena& GetArena() { return m_Arena; }
        [[nodiscard]] const LinearArena& GetArena() const { return m_Arena; }

    private:
        LinearArena m_Arena;
        std::vector<std::function<void()>> m_Destructors;
    };
}
