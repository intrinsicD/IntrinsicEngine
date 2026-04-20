module;

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <limits>
#include <span>
#include <thread>
#include <type_traits>
#include <utility>

export module Extrinsic.Core.Memory:LinearArena;
import :Common;
import Extrinsic.Core.Error;

export namespace Extrinsic::Core::Memory
{
    struct ArenaMarker
    {
        size_t Offset = 0;
        uint64_t Epoch = 0;
    };

    class LinearArena final
    {
    public:
        LinearArena(const LinearArena&) = delete;
        LinearArena& operator=(const LinearArena&) = delete;

        explicit LinearArena(size_t sizeBytes) noexcept;
        LinearArena(LinearArena&& other) noexcept;
        LinearArena& operator=(LinearArena&& other) noexcept;
        ~LinearArena();

        [[nodiscard]] std::expected<std::span<std::byte>, ErrorCode>
        AllocBytes(size_t size, size_t align = kDefaultAlignment) noexcept;

        template <typename T, typename... Args>
        [[nodiscard]] std::expected<T*, ErrorCode> New(Args&&... args) noexcept
        {
            static_assert(std::is_trivially_destructible_v<T>,
                          "LinearArena::New<T> only supports trivially destructible types.");

            return AllocBytes(sizeof(T), alignof(T))
                .transform([&](std::span<std::byte> mem)
                {
                    return std::construct_at(reinterpret_cast<T*>(mem.data()), std::forward<Args>(args)...);
                });
        }

        template <typename T>
        [[nodiscard]] std::expected<std::span<T>, ErrorCode> NewArray(size_t count) noexcept
        {
            static_assert(std::is_trivially_destructible_v<T>,
                          "LinearArena::NewArray<T> only supports trivially destructible types.");

            if (count == 0)
                return std::span<T>{};

            if (count > std::numeric_limits<size_t>::max() / sizeof(T))
                return std::unexpected(Core::ErrorCode::OutOfRange);

            return AllocBytes(count * sizeof(T), alignof(T))
                .transform([&](std::span<std::byte> mem)
                {
                    T* base = reinterpret_cast<T*>(mem.data());
                    for (size_t i = 0; i < count; ++i)
                        std::construct_at(base + i);
                    return std::span<T>{base, count};
                });
        }

        [[nodiscard]] ArenaMarker Mark() const noexcept { return ArenaMarker{m_Offset, m_Epoch}; }
        [[nodiscard]] std::expected<void, ErrorCode> Rewind(ArenaMarker marker) noexcept;
        void Reset() noexcept;

        [[nodiscard]] size_t Used() const noexcept { return m_Offset; }
        [[nodiscard]] size_t Capacity() const noexcept { return m_Capacity; }
        [[nodiscard]] uint64_t Epoch() const noexcept { return m_Epoch; }

    private:
        std::byte* m_Start = nullptr;
        size_t m_Capacity = 0;
        size_t m_Offset = 0;
        std::thread::id m_OwningThread{};
        uint64_t m_Epoch = 1;
    };
}
