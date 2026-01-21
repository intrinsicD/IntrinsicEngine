module;
#include <array>
#include <atomic>

export module Utils.RingBuffer;

export namespace Utils
{
    template <typename T, size_t Capacity>
    struct RingBuffer
    {
        std::array<T, Capacity> Buffer;
        std::atomic<size_t> Head{0};
        std::atomic<size_t> Tail{0};

        bool Push(T&& item)
        {
            size_t tail = Tail.load(std::memory_order_relaxed);
            size_t head = Head.load(std::memory_order_acquire);
            if (tail - head >= Capacity) return false; // Full

            Buffer[tail % Capacity] = std::move(item);
            Tail.store(tail + 1, std::memory_order_release);
            return true;
        }

        bool Pop(T& outItem)
        {
            size_t head = Head.load(std::memory_order_relaxed);
            size_t tail = Tail.load(std::memory_order_acquire);
            if (head >= tail) return false; // Empty

            outItem = std::move(Buffer[head % Capacity]);
            Head.store(head + 1, std::memory_order_release);
            return true;
        }
    };
}
