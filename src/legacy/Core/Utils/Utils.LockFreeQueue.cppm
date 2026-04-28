module;
#include <atomic>
#include <memory>

export module Utils.LockFreeQueue;

export namespace Utils
{
    // Hardware destructive interference size (cache line size)
    // Standard on x64/ARM64 is usually 64 bytes.
    constexpr size_t CACHE_LINE_SIZE = 64;

    template <typename T>
    class LockFreeQueue
    {
    public:
        explicit LockFreeQueue(size_t capacity)
        {
            // Capacity must be a power of 2 for bitwise masking
            if (capacity < 2) capacity = 2;
            
            // Round up to next power of 2
            size_t cap = 1;
            while (cap < capacity) cap *= 2;
            
            m_Capacity = cap;
            m_Mask = m_Capacity - 1;

            m_Slots = std::make_unique<Slot[]>(m_Capacity);

            for (size_t i = 0; i < m_Capacity; ++i)
            {
                // Initial turn: 0 for slot 0, 1 for slot 1, etc.
                m_Slots[i].Turn.store(i, std::memory_order_relaxed);
            }
        }

        // Returns true if item was pushed, false if queue is full
        bool Push(T&& item)
        {
            size_t tail = m_Tail.load(std::memory_order_relaxed);
            while (true)
            {
                // Map logical index to physical slot
                Slot& slot = m_Slots[tail & m_Mask];
                
                // Acquire ensures we see the slot state published by the consumer
                size_t turn = slot.Turn.load(std::memory_order_acquire);
                
                // diff == 0 means the slot is free and waiting for this specific tail index
                long long diff = static_cast<long long>(turn) - static_cast<long long>(tail);

                if (diff == 0)
                {
                    // Try to claim this slot
                    if (m_Tail.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed))
                    {
                        // Critical: Move data *before* publishing turn
                        slot.Storage = std::move(item);
                        
                        // Publish: Set turn to (tail + 1) so Consumer can see it
                        // Release ensures 'Storage' write is visible before 'Turn' update
                        slot.Turn.store(tail + 1, std::memory_order_release);
                        return true;
                    }
                    // CAS failed: 'tail' was updated by another thread, loop again with new 'tail'
                }
                else if (diff < 0)
                {
                    // diff < 0 means turn < tail. 
                    // This implies the slot is full (turn == tail + 1) or being written to.
                    // Queue is effectively full.
                    return false;
                }
                else
                {
                    // diff > 0 means turn > tail.
                    // This implies 'tail' is lagging behind the actual queue state 
                    // (another producer moved it way forward). Reload tail.
                    tail = m_Tail.load(std::memory_order_relaxed);
                }
            }
        }

        // Returns true if item was popped, false if queue is empty
        bool Pop(T& outItem)
        {
            size_t head = m_Head.load(std::memory_order_relaxed);
            while (true)
            {
                Slot& slot = m_Slots[head & m_Mask];
                
                // Acquire ensures we see the data written by the producer
                size_t turn = slot.Turn.load(std::memory_order_acquire);
                
                // diff == 0 means the slot is filled and waiting for this specific head index
                // Producer sets turn to (tail + 1). Consumer expects (head + 1).
                long long diff = static_cast<long long>(turn) - static_cast<long long>(head + 1);

                if (diff == 0)
                {
                    if (m_Head.compare_exchange_weak(head, head + 1, std::memory_order_relaxed))
                    {
                        outItem = std::move(slot.Storage);
                        
                        // Reset turn for the next generation of producers.
                        // We add capacity to the current head to wrap around the generation count.
                        slot.Turn.store(head + m_Mask + 1, std::memory_order_release);
                        return true;
                    }
                }
                else if (diff < 0)
                {
                    // Slot is empty (turn == head) or lagging. Queue empty.
                    return false;
                }
                else
                {
                    head = m_Head.load(std::memory_order_relaxed);
                }
            }
        }

    private:
        struct Slot
        {
            std::atomic<size_t> Turn;
            T Storage;
        };

        // Padding to prevent false sharing between Head and Tail
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_Head{0};
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_Tail{0};

        std::unique_ptr<Slot[]> m_Slots;
        size_t m_Capacity;
        size_t m_Mask;
    };
}