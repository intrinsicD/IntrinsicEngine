module;

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

export module Extrinsic.Core.RingBuffer;

export namespace Extrinsic::Core
{
    // Bounded single-producer/single-consumer ring buffer.
    // Capacity is fixed at compile time. Push returns false when full; Pop returns false when empty.
    template <typename T, std::size_t Capacity>
    struct RingBuffer
    {
        static_assert(Capacity > 0, "RingBuffer capacity must be greater than zero.");
        static_assert(std::is_move_assignable_v<T>, "RingBuffer<T> requires move-assignable T.");

        std::array<T, Capacity> Buffer{};
        std::atomic<std::size_t> Head{0};
        std::atomic<std::size_t> Tail{0};

        RingBuffer() = default;
        RingBuffer(const RingBuffer&) = delete;
        RingBuffer& operator=(const RingBuffer&) = delete;

        bool Push(T item)
        {
            const std::size_t tail = Tail.load(std::memory_order_relaxed);
            const std::size_t head = Head.load(std::memory_order_acquire);
            if (tail - head >= Capacity)
                return false;

            Buffer[tail % Capacity] = std::move(item);
            Tail.store(tail + 1, std::memory_order_release);
            return true;
        }

        bool Pop(T& outItem)
        {
            const std::size_t head = Head.load(std::memory_order_relaxed);
            const std::size_t tail = Tail.load(std::memory_order_acquire);
            if (head >= tail)
                return false;

            outItem = std::move(Buffer[head % Capacity]);
            Head.store(head + 1, std::memory_order_release);
            return true;
        }

        [[nodiscard]] bool Empty() const noexcept
        {
            return Head.load(std::memory_order_acquire) >= Tail.load(std::memory_order_acquire);
        }

        [[nodiscard]] bool Full() const noexcept
        {
            const std::size_t head = Head.load(std::memory_order_acquire);
            const std::size_t tail = Tail.load(std::memory_order_acquire);
            return tail - head >= Capacity;
        }
    };
}

