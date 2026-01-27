module; // Global Fragment
#include <cstddef>
#include <cstdlib>
#include <concepts>
#include <expected>
#include <span>
#include <utility>
#include <thread>
#include <type_traits>
#include <new>
#include <limits>

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
    // - Tracks destructors via intrusive linked list stored IN the arena
    // - Zero heap allocations - all metadata lives in the LinearArena
    // - Destroys objects in LIFO order on Reset() (stack semantics)
    //
    // Use Cases:
    // - RenderGraph passes that capture std::shared_ptr<Texture>
    // - Debug passes that capture std::string names
    // - Callbacks/closures with complex captured state
    //
    // Performance Notes:
    // - ~24 bytes overhead per non-trivial allocation (DestructorNode in arena)
    // - Strictly O(1) allocation with zero system heap allocations
    // - O(n) reset where n = non-trivial allocations
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
            // 1. Allocate the object
            auto result = m_Arena.Alloc(sizeof(T), alignof(T));
            if (!result) return std::unexpected(result.error());

            T* ptr = static_cast<T*>(*result);
            std::construct_at(ptr, std::forward<Args>(args)...);

            // 2. If non-trivially destructible, allocate DestructorNode in arena and link
            if constexpr (!std::is_trivially_destructible_v<T>)
            {
                auto headRes = m_Arena.Alloc(sizeof(DestructorNode), alignof(DestructorNode));
                if (!headRes)
                {
                    // Critical: allocated object but can't track destructor - must cleanup
                    std::destroy_at(ptr);
                    return std::unexpected(headRes.error());
                }

                auto* node = static_cast<DestructorNode*>(*headRes);
                node->Ptr = ptr;
                node->DestroyFn = [](void* p) { std::destroy_at(static_cast<T*>(p)); };
                node->Next = m_Head;
                m_Head = node;
                ++m_DestructorCount;
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
                // Allocate ArrayMetadata to store count alongside pointer
                struct ArrayMetadata
                {
                    T* Ptr;
                    size_t Count;
                };

                auto metaRes = m_Arena.Alloc(sizeof(ArrayMetadata), alignof(ArrayMetadata));
                if (!metaRes)
                {
                    // Cleanup: destroy constructed elements
                    for (size_t i = count; i > 0; --i)
                    {
                        std::destroy_at(ptr + i - 1);
                    }
                    return std::unexpected(metaRes.error());
                }

                auto* meta = static_cast<ArrayMetadata*>(*metaRes);
                meta->Ptr = ptr;
                meta->Count = count;

                // Allocate DestructorNode
                auto headRes = m_Arena.Alloc(sizeof(DestructorNode), alignof(DestructorNode));
                if (!headRes)
                {
                    // Cleanup: destroy constructed elements
                    for (size_t i = count; i > 0; --i)
                    {
                        std::destroy_at(ptr + i - 1);
                    }
                    return std::unexpected(headRes.error());
                }

                auto* node = static_cast<DestructorNode*>(*headRes);
                node->Ptr = meta;
                node->DestroyFn = [](void* p)
                {
                    auto* m = static_cast<ArrayMetadata*>(p);
                    // Destroy in reverse order within the array
                    for (size_t i = m->Count; i > 0; --i)
                    {
                        std::destroy_at(m->Ptr + i - 1);
                    }
                };
                node->Next = m_Head;
                m_Head = node;
                ++m_DestructorCount;
            }
            return std::span<T>(ptr, count);
        }

        void Reset();

        // Diagnostics
        [[nodiscard]] size_t GetUsed() const { return m_Arena.GetUsed(); }
        [[nodiscard]] size_t GetTotal() const { return m_Arena.GetTotal(); }
        [[nodiscard]] size_t GetDestructorCount() const { return m_DestructorCount; }

        // Direct arena access for POD allocations (bypasses destructor tracking)
        LinearArena& GetArena() { return m_Arena; }
        [[nodiscard]] const LinearArena& GetArena() const { return m_Arena; }

    private:
        // Intrusive linked list node stored in the arena itself
        struct DestructorNode
        {
            void (*DestroyFn)(void*);  // Function pointer to type-erased destructor
            void* Ptr;                  // Pointer to object (or ArrayMetadata for arrays)
            DestructorNode* Next;       // Next node in LIFO chain
        };

        LinearArena m_Arena;
        DestructorNode* m_Head = nullptr;  // Head of intrusive linked list
        size_t m_DestructorCount = 0;      // Track count for diagnostics
    };

    // -------------------------------------------------------------------------
    // ArenaAllocator - STL-compatible adapter for LinearArena (monotonic)
    // -------------------------------------------------------------------------
    // Notes:
    // - allocate() bumps the LinearArena; deallocate() is a no-op.
    // - Containers using this allocator must not outlive the arena they reference.
    // - OOM is treated as fatal for the container contract: we throw std::bad_alloc.
    template <typename T>
    class ArenaAllocator
    {
    public:
        using value_type = T;

        LinearArena* m_Arena = nullptr;

        explicit ArenaAllocator(LinearArena& arena) noexcept : m_Arena(&arena) {}

        template <typename U>
        ArenaAllocator(const ArenaAllocator<U>& other) noexcept : m_Arena(other.m_Arena) {}

        [[nodiscard]] T* allocate(std::size_t n)
        {
            if (n == 0) return nullptr;
            if (n > std::numeric_limits<std::size_t>::max() / sizeof(T))
            {
                std::terminate();
            }

            auto res = m_Arena->Alloc(n * sizeof(T), alignof(T));
            if (!res)
            {
                // Exceptions are disabled in this codebase; monotonic scratch OOM is fatal.
                std::terminate();
            }
            return static_cast<T*>(*res);
        }

        void deallocate(T*, std::size_t) noexcept
        {
            // No-op: LinearArena is monotonic and resets en-masse.
        }

        template <typename U>
        struct rebind
        {
            using other = ArenaAllocator<U>;
        };

        template <typename U>
        friend class ArenaAllocator;

        friend bool operator==(const ArenaAllocator& a, const ArenaAllocator& b) noexcept
        {
            return a.m_Arena == b.m_Arena;
        }
    };
}
