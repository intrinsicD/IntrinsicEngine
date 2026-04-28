module;
#include <vector>
#include <cstddef>
#include <algorithm>

export module Utils.BoundedHeap;

export namespace Utils
{
    // Keeps the k smallest values under the strict weak ordering induced by operator<.
    // top() is the current worst retained element (the maximal element under that ordering).
    // Works well with T = std::pair<float, size_t>: comparison is lexicographic, so pairs are
    // ordered by distance first, then by index. Smaller distance is better; for equal distance,
    // smaller index is better. This assumes the key values participate in a strict weak ordering.
    template <typename T>
    class BoundedHeap
    {
    public:
        explicit BoundedHeap(size_t maxSize) : m_MaxSize(maxSize)
        {
            m_Data.reserve(m_MaxSize);
        }

        // Adds an item. If the heap is full, only strictly better (smaller) items replace the
        // current worst retained item; comparator-equivalent items are rejected.
        void Push(const T& item)
        {
            if (m_MaxSize == 0) return; // handle k == 0 quietly

            if (m_Data.size() < m_MaxSize)
            {
                m_Data.push_back(item);
                std::push_heap(m_Data.begin(), m_Data.end()); // default = max-heap
            }
            else if (item < m_Data.front())
            {
                // Replace the current worst retained item with a strictly better one.
                std::pop_heap(m_Data.begin(), m_Data.end());
                m_Data.back() = item;
                std::push_heap(m_Data.begin(), m_Data.end());
            }
            // This container is not insertion-stable for comparator-equivalent values. If ties
            // need deterministic behavior, encode the tie-break policy in T's ordering.
        }

        // Returns the current worst retained item. Precondition: not empty.
        const T& top() const
        {
            return m_Data.front();
        }

        [[nodiscard]] std::size_t Size() const { return m_Data.size(); }
        [[nodiscard]] bool Empty() const { return m_Data.empty(); }
        [[nodiscard]] std::size_t Capacity() const { return m_MaxSize; }

        void Clear() { m_Data.clear(); }

        // Returns the current worst retained item. Precondition: not empty.
        // When Size() < Capacity(), this is only the worst item seen so far; treat it as a
        // pruning threshold only once Size() == Capacity().
        const T& Threshold() const
        {
            return top();
        }

        [[nodiscard]] bool IsFull() const { return m_Data.size() >= m_MaxSize; }

        // Returns items sorted ascending (best..worst) **without** destroying the heap.
        std::vector<T> GetSortedData() const
        {
            std::vector<T> out = m_Data;
            std::sort(out.begin(), out.end()); // ascending: best first
            return out;
        }

    private:
        std::size_t m_MaxSize;
        std::vector<T> m_Data; // max-heap under operator< (worst retained item at front)
    };
}
