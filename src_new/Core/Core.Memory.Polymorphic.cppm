module;

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory_resource>

export module Extrinsic.Core.Memory:Polymorphic;
import :Common;
import :LinearArena;

export namespace Extrinsic::Core::Memory
{
    template <typename T>
    class ArenaAllocator
    {
    public:
        using value_type = T;

        explicit ArenaAllocator(LinearArena& arena) noexcept : m_Arena(&arena) {}

        template <typename U>
        ArenaAllocator(const ArenaAllocator<U>& other) noexcept : m_Arena(other.m_Arena) {}

        [[nodiscard]] T* allocate(const std::size_t n)
        {
            if (n == 0)
                return nullptr;

            if (!m_Arena || n > std::numeric_limits<size_t>::max() / sizeof(T))
                std::abort();

            auto mem = m_Arena->AllocBytes(n * sizeof(T), alignof(T));
            if (!mem)
                std::abort();

            return reinterpret_cast<T*>(mem->data());
        }

        void deallocate(T*, std::size_t) noexcept {}

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

    private:
        LinearArena* m_Arena = nullptr;
    };

    class ArenaMemoryResource final : public std::pmr::memory_resource
    {
    public:
        explicit ArenaMemoryResource(LinearArena& arena) noexcept : m_Arena(arena) {}

        [[nodiscard]] void* do_allocate(size_t bytes, size_t alignment) override; // NOLINT
        void do_deallocate(void*, size_t, size_t) override {} // NOLINT
        [[nodiscard]] bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override // NOLINT
        {
            return this == &other;
        }

    private:
        LinearArena& m_Arena;
    };
}
