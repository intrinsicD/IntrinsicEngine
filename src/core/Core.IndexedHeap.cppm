module;

#include <cstddef>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

export module Extrinsic.Core.IndexedHeap;

namespace Extrinsic::Core
{
    // Indexed binary min-heap with O(log n) decrease-key and remove.
    //
    // Key      — priority; smaller (under Compare) is popped first.
    // Value    — externally meaningful identity / token used for Contains /
    //            DecreaseKey / Remove. Must be hashable and LessThanComparable.
    // Compare  — strict-weak "less" on Key (default std::less<Key>); the heap is
    //            a min-heap under it. Ties on Key break deterministically by the
    //            Value token so pop order is reproducible run-to-run.
    //
    // Fail-closed contract: Top/Pop on an empty heap report failure (nullopt /
    // false) rather than UB; Contains/DecreaseKey/Remove on an unknown Value
    // report not-found; a DecreaseKey that would *increase* the key is rejected
    // and leaves the heap unchanged. No asserts, no exceptions for these paths.
    //
    // This is a class template: its definitions live in the interface so any
    // importer can instantiate it for its own Key/Value/Compare (no explicit
    // instantiation list, no private implementation unit).
    export template <typename Key, typename Value, typename Compare = std::less<Key>>
    class IndexedHeap
    {
    public:
        IndexedHeap() = default;
        explicit IndexedHeap(Compare compare) : less_(std::move(compare)) {}

        [[nodiscard]] bool Empty() const noexcept { return nodes_.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept { return nodes_.size(); }
        [[nodiscard]] bool Contains(const Value& value) const { return positions_.find(value) != positions_.end(); }

        // Insert a new (key, value). Returns false if the value is already
        // present (use DecreaseKey to update an existing entry).
        bool Push(const Key& key, const Value& value)
        {
            if (positions_.find(value) != positions_.end())
            {
                return false;
            }
            nodes_.push_back(Node{key, value});
            const std::size_t index = nodes_.size() - 1;
            positions_[value] = index;
            SiftUp(index);
            return true;
        }

        // The current minimum (key, value), or nullopt when empty.
        [[nodiscard]] std::optional<std::pair<Key, Value>> Top() const
        {
            if (nodes_.empty())
            {
                return std::nullopt;
            }
            return std::make_pair(nodes_.front().key, nodes_.front().value);
        }

        // Pop the minimum into out; false (out untouched) when empty.
        bool TryPop(std::pair<Key, Value>& out)
        {
            if (nodes_.empty())
            {
                return false;
            }
            out = std::make_pair(nodes_.front().key, nodes_.front().value);
            RemoveAt(0);
            return true;
        }

        // Pop and discard the minimum; false when empty.
        bool Pop()
        {
            if (nodes_.empty())
            {
                return false;
            }
            RemoveAt(0);
            return true;
        }

        // Lower the key of an existing value. Rejected (false, no change) if the
        // value is absent or if newKey is not strictly less than the current key
        // (i.e. would increase it). Equal keys are accepted as a no-op.
        bool DecreaseKey(const Value& value, const Key& newKey)
        {
            const auto it = positions_.find(value);
            if (it == positions_.end())
            {
                return false;
            }
            const std::size_t index = it->second;
            // Reject an increase: oldKey < newKey under Compare.
            if (less_(nodes_[index].key, newKey))
            {
                return false;
            }
            nodes_[index].key = newKey;
            SiftUp(index);
            return true;
        }

        // Remove an arbitrary value; false if not present.
        bool Remove(const Value& value)
        {
            const auto it = positions_.find(value);
            if (it == positions_.end())
            {
                return false;
            }
            RemoveAt(it->second);
            return true;
        }

        void Clear() noexcept
        {
            nodes_.clear();
            positions_.clear();
        }

    private:
        struct Node
        {
            Key key;
            Value value;
        };

        // True if a should sit above b (a is "smaller"). Deterministic tie-break
        // on the Value token keeps pop order reproducible.
        [[nodiscard]] bool Before(const Node& a, const Node& b) const
        {
            if (less_(a.key, b.key)) return true;
            if (less_(b.key, a.key)) return false;
            return a.value < b.value;
        }

        void SwapNodes(std::size_t i, std::size_t j)
        {
            std::swap(nodes_[i], nodes_[j]);
            positions_[nodes_[i].value] = i;
            positions_[nodes_[j].value] = j;
        }

        void SiftUp(std::size_t index)
        {
            while (index > 0)
            {
                const std::size_t parent = (index - 1) / 2;
                if (Before(nodes_[index], nodes_[parent]))
                {
                    SwapNodes(index, parent);
                    index = parent;
                }
                else
                {
                    break;
                }
            }
        }

        void SiftDown(std::size_t index)
        {
            const std::size_t count = nodes_.size();
            while (true)
            {
                const std::size_t left = 2 * index + 1;
                const std::size_t right = 2 * index + 2;
                std::size_t best = index;
                if (left < count && Before(nodes_[left], nodes_[best])) best = left;
                if (right < count && Before(nodes_[right], nodes_[best])) best = right;
                if (best == index) break;
                SwapNodes(index, best);
                index = best;
            }
        }

        void RemoveAt(std::size_t index)
        {
            const std::size_t last = nodes_.size() - 1;
            positions_.erase(nodes_[index].value);
            if (index != last)
            {
                nodes_[index] = nodes_[last];
                positions_[nodes_[index].value] = index;
                nodes_.pop_back();
                // The moved element may belong either higher or lower.
                SiftDown(index);
                SiftUp(index);
            }
            else
            {
                nodes_.pop_back();
            }
        }

        std::vector<Node> nodes_;
        std::unordered_map<Value, std::size_t> positions_;
        Compare less_{};
    };
}
