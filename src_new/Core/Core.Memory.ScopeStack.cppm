module;

#include <cstddef>
#include <expected>
#include <limits>
#include <new>
#include <span>
#include <type_traits>
#include <utility>

export module Extrinsic.Core.Memory:ScopeStack;
import :Common;
import :LinearArena;

export namespace Extrinsic::Core::Memory
{
    export class ScopeStack final
    {
    public:
        ScopeStack(const ScopeStack&) = delete;
        ScopeStack& operator=(const ScopeStack&) = delete;

        explicit ScopeStack(size_t bytes) noexcept : m_Arena(bytes) {}
        ScopeStack(ScopeStack&& other) noexcept;
        ScopeStack& operator=(ScopeStack&& other) noexcept;
        ~ScopeStack() { Reset(); }

        template <typename T, typename... Args>
        [[nodiscard]] std::expected<T*, AllocError> New(Args&&... args) noexcept
        {
            return m_Arena.AllocBytes(sizeof(T), alignof(T)).and_then([&](std::span<std::byte> mem) -> std::expected<T*, AllocError>
            {
                T* ptr = std::construct_at(reinterpret_cast<T*>(mem.data()), std::forward<Args>(args)...);

                if constexpr (!std::is_trivially_destructible_v<T>)
                {
                    auto nodeMem = m_Arena.AllocBytes(sizeof(DestructorNode), alignof(DestructorNode));
                    if (!nodeMem)
                    {
                        std::destroy_at(ptr);
                        return std::unexpected(nodeMem.error());
                    }

                    auto* node = reinterpret_cast<DestructorNode*>(nodeMem->data());
                    node->DestroyFn = [](void* p) { std::destroy_at(static_cast<T*>(p)); };
                    node->Ptr = ptr;
                    node->Next = m_Head;
                    m_Head = node;
                    ++m_DestructorCount;
                }

                return ptr;
            });
        }

        template <typename T>
        [[nodiscard]] std::expected<std::span<T>, AllocError> NewArray(size_t count) noexcept
        {
            static_assert(std::is_default_constructible_v<T>,
                          "ScopeStack::NewArray requires default constructible T.");

            if (count == 0)
                return std::span<T>{};

            if (count > std::numeric_limits<size_t>::max() / sizeof(T))
                return std::unexpected(AllocError::Overflow);

            return m_Arena.AllocBytes(sizeof(T) * count, alignof(T)).and_then(
                [&](std::span<std::byte> mem) -> std::expected<std::span<T>, AllocError>
                {
                    T* ptr = reinterpret_cast<T*>(mem.data());
                    for (size_t i = 0; i < count; ++i)
                        std::construct_at(ptr + i);

                    if constexpr (!std::is_trivially_destructible_v<T>)
                    {
                        struct ArrayMeta
                        {
                            T* Ptr;
                            size_t Count;
                        };

                        auto metaMem = m_Arena.AllocBytes(sizeof(ArrayMeta), alignof(ArrayMeta));
                        if (!metaMem)
                        {
                            for (size_t i = count; i > 0; --i)
                                std::destroy_at(ptr + i - 1);
                            return std::unexpected(metaMem.error());
                        }

                        auto* meta = reinterpret_cast<ArrayMeta*>(metaMem->data());
                        meta->Ptr = ptr;
                        meta->Count = count;

                        auto nodeMem = m_Arena.AllocBytes(sizeof(DestructorNode), alignof(DestructorNode));
                        if (!nodeMem)
                        {
                            for (size_t i = count; i > 0; --i)
                                std::destroy_at(ptr + i - 1);
                            return std::unexpected(nodeMem.error());
                        }

                        auto* node = reinterpret_cast<DestructorNode*>(nodeMem->data());
                        node->DestroyFn = [](void* p)
                        {
                            auto* m = static_cast<ArrayMeta*>(p);
                            for (size_t i = m->Count; i > 0; --i)
                                std::destroy_at(m->Ptr + i - 1);
                        };
                        node->Ptr = meta;
                        node->Next = m_Head;
                        m_Head = node;
                        ++m_DestructorCount;
                    }

                    return std::span<T>{ptr, count};
                });
        }

        void Reset() noexcept;

        [[nodiscard]] size_t Used() const noexcept { return m_Arena.Used(); }
        [[nodiscard]] size_t Capacity() const noexcept { return m_Arena.Capacity(); }
        [[nodiscard]] size_t DestructorCount() const noexcept { return m_DestructorCount; }

        [[nodiscard]] LinearArena& Arena() noexcept { return m_Arena; }
        [[nodiscard]] const LinearArena& Arena() const noexcept { return m_Arena; }

    private:
        struct DestructorNode
        {
            void (*DestroyFn)(void*);
            void* Ptr;
            DestructorNode* Next;
        };

        LinearArena m_Arena;
        DestructorNode* m_Head = nullptr;
        size_t m_DestructorCount = 0;
    };
}
