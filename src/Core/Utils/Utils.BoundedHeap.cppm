module;
#include <vector>
#include <cstddef>
#include <algorithm>

export module Utils.BoundedHeap;

export namespace Utils
{
    // Keeps the k *smallest* T by operator<. top() = current worst (largest) in the heap.
    // Works great with T = std::pair<float,size_t> (lexicographic compare).
    template <typename T>
    class BoundedHeap
    {
    public:
        explicit BoundedHeap(size_t maxSize) : m_MaxSize(maxSize)
        {
            m_Data.reserve(m_MaxSize);
        }

        // Add an item; ignores it if the heap is full and item is not better than the current worst.
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
                // Replace current worst (largest) with better (smaller) item.
                std::pop_heap(m_Data.begin(), m_Data.end());
                m_Data.back() = item;
                std::push_heap(m_Data.begin(), m_Data.end());
            }
            // NOTE: If you want "keep first-seen on ties", change condition to:
            //   if (item < data_.front() && !(data_.front() < item)) { ... }
            // or equivalently keep strict < but normalize T's tie-break policy.
        }

        // Largest element (i.e., current worst). Precondition: not empty.
        const T& top() const
        {
            return m_Data.front();
        }

        [[nodiscard]] std::size_t Size() const { return m_Data.size(); }
        [[nodiscard]] bool Empty() const { return m_Data.empty(); }
        [[nodiscard]] std::size_t Capacity() const { return m_MaxSize; }

        void Clear() { m_Data.clear(); }

        // Convenience: Returns current worst (threshold for pruning).
        // IMPORTANT: Only call when Size() == Capacity(), otherwise threshold is undefined.
        // Caller should manage tau externally when heap is not yet full.
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
        std::vector<T> m_Data; // max-heap by operator< (largest at front)
    };
}
