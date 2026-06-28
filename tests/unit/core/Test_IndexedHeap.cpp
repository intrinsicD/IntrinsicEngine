#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <random>
#include <utility>
#include <vector>

import Extrinsic.Core.IndexedHeap;

namespace
{
    using Extrinsic::Core::IndexedHeap;

    // Drain the heap, returning keys in pop order.
    template <typename H>
    std::vector<double> DrainKeys(H& heap)
    {
        std::vector<double> out;
        std::pair<double, int> top;
        while (heap.TryPop(top))
        {
            out.push_back(top.first);
        }
        return out;
    }

    template <typename H>
    std::vector<std::pair<double, int>> DrainPairs(H heap)
    {
        std::vector<std::pair<double, int>> out;
        std::pair<double, int> top;
        while (heap.TryPop(top))
        {
            out.push_back(top);
        }
        return out;
    }

    std::vector<std::pair<double, int>> SortedReference(std::vector<std::pair<double, int>> reference)
    {
        std::sort(reference.begin(), reference.end(), [](const auto& lhs, const auto& rhs)
        {
            if (lhs.first != rhs.first) return lhs.first < rhs.first;
            return lhs.second < rhs.second;
        });
        return reference;
    }
}

TEST(CoreIndexedHeap, PopsInNonDecreasingKeyOrder)
{
    IndexedHeap<double, int> heap;
    const std::vector<std::pair<double, int>> items{
        {5.0, 0}, {1.0, 1}, {3.0, 2}, {1.0, 3}, {4.0, 4}, {2.0, 5}};
    for (auto [k, v] : items) EXPECT_TRUE(heap.Push(k, v));
    EXPECT_EQ(heap.Size(), items.size());

    const std::vector<double> drained = DrainKeys(heap);
    std::vector<double> sorted;
    for (auto [k, v] : items) sorted.push_back(k);
    std::sort(sorted.begin(), sorted.end());
    EXPECT_EQ(drained, sorted);
    EXPECT_TRUE(heap.Empty());
}

TEST(CoreIndexedHeap, DeterministicTieBreakByValue)
{
    IndexedHeap<double, int> heap;
    heap.Push(2.0, 9);
    heap.Push(2.0, 3);
    heap.Push(2.0, 7);
    // Equal keys -> ascending value token.
    std::pair<double, int> top;
    ASSERT_TRUE(heap.TryPop(top));
    EXPECT_EQ(top.second, 3);
    ASSERT_TRUE(heap.TryPop(top));
    EXPECT_EQ(top.second, 7);
    ASSERT_TRUE(heap.TryPop(top));
    EXPECT_EQ(top.second, 9);
}

TEST(CoreIndexedHeap, DuplicatePushRejected)
{
    IndexedHeap<double, int> heap;
    EXPECT_TRUE(heap.Push(1.0, 42));
    EXPECT_FALSE(heap.Push(2.0, 42)); // value already present
    EXPECT_TRUE(heap.Contains(42));
    EXPECT_EQ(heap.Size(), 1u);
}

TEST(CoreIndexedHeap, DecreaseKeySurfacesValue)
{
    IndexedHeap<double, int> heap;
    heap.Push(10.0, 0);
    heap.Push(20.0, 1);
    heap.Push(30.0, 2);
    EXPECT_TRUE(heap.DecreaseKey(2, 5.0)); // value 2 now smallest
    auto top = heap.Top();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->second, 2);
    EXPECT_DOUBLE_EQ(top->first, 5.0);

    std::pair<double, int> peek;
    ASSERT_TRUE(heap.TryTop(peek));
    EXPECT_EQ(peek.second, 2);
    EXPECT_DOUBLE_EQ(peek.first, 5.0);
}

TEST(CoreIndexedHeap, DecreaseKeyRejectsIncrease)
{
    IndexedHeap<double, int> heap;
    heap.Push(10.0, 0);
    heap.Push(20.0, 1);
    EXPECT_FALSE(heap.DecreaseKey(0, 15.0)); // would increase -> rejected
    EXPECT_FALSE(heap.DecreaseKey(7, 1.0));  // unknown value
    // Heap unchanged: 0 still smallest.
    auto top = heap.Top();
    ASSERT_TRUE(top.has_value());
    EXPECT_EQ(top->second, 0);
    EXPECT_DOUBLE_EQ(top->first, 10.0);
    // Equal key accepted as no-op.
    EXPECT_TRUE(heap.DecreaseKey(0, 10.0));
}

TEST(CoreIndexedHeap, RemoveInteriorMatchesHeapWithoutValue)
{
    const std::vector<std::pair<double, int>> items{
        {5.0, 0}, {1.0, 1}, {3.0, 2}, {8.0, 3}, {4.0, 4}, {2.0, 5}, {7.0, 6}};

    IndexedHeap<double, int> heap;
    for (auto [k, v] : items) heap.Push(k, v);
    EXPECT_TRUE(heap.Remove(3)); // remove key 8.0
    EXPECT_FALSE(heap.Contains(3));
    EXPECT_FALSE(heap.Remove(3)); // already gone
    const std::vector<double> drained = DrainKeys(heap);

    IndexedHeap<double, int> reference;
    for (auto [k, v] : items)
        if (v != 3) reference.Push(k, v);
    const std::vector<double> refDrained = DrainKeys(reference);

    EXPECT_EQ(drained, refDrained);
}

TEST(CoreIndexedHeap, RandomizedParityWithSortedReference)
{
    std::mt19937 rng(12345);
    std::uniform_real_distribution<double> keyDist(-100.0, 100.0);
    IndexedHeap<double, int> heap;
    std::vector<double> inserted;
    for (int i = 0; i < 500; ++i)
    {
        const double k = keyDist(rng);
        EXPECT_TRUE(heap.Push(k, i));
        inserted.push_back(k);
    }
    // Decrease a subset of keys; track the effective minimum per value.
    for (int i = 0; i < 500; i += 7)
    {
        const double lower = inserted[i] - 50.0;
        EXPECT_TRUE(heap.DecreaseKey(i, lower));
        inserted[i] = lower;
    }
    const std::vector<double> drained = DrainKeys(heap);
    std::vector<double> expected = inserted;
    std::sort(expected.begin(), expected.end());
    ASSERT_EQ(drained.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        EXPECT_DOUBLE_EQ(drained[i], expected[i]);
}

TEST(CoreIndexedHeap, RandomizedOperationParityWithBruteForceReference)
{
    std::mt19937 rng(67890);
    std::uniform_int_distribution<int> opDist(0, 3);
    std::uniform_int_distribution<int> keyDist(-20, 20);
    std::uniform_int_distribution<int> decrementDist(1, 8);
    IndexedHeap<double, int> heap;
    std::vector<std::pair<double, int>> reference;
    int nextValue = 0;

    for (int step = 0; step < 400; ++step)
    {
        const int op = opDist(rng);
        if (op == 0 || reference.empty())
        {
            const int value = nextValue++;
            const double key = static_cast<double>(keyDist(rng));
            ASSERT_TRUE(heap.Push(key, value));
            reference.push_back({key, value});
        }
        else if (op == 1)
        {
            const auto expected = SortedReference(reference);
            std::pair<double, int> popped;
            ASSERT_TRUE(heap.TryPop(popped));
            EXPECT_EQ(popped, expected.front());
            reference.erase(std::find_if(reference.begin(), reference.end(), [&](const auto& item)
            {
                return item.second == popped.second;
            }));
        }
        else if (op == 2)
        {
            std::uniform_int_distribution<std::size_t> liveDist(0, reference.size() - 1);
            const std::size_t index = liveDist(rng);
            const double newKey = reference[index].first - static_cast<double>(decrementDist(rng));
            ASSERT_TRUE(heap.DecreaseKey(reference[index].second, newKey));
            reference[index].first = newKey;
        }
        else
        {
            std::uniform_int_distribution<std::size_t> liveDist(0, reference.size() - 1);
            const std::size_t index = liveDist(rng);
            const int value = reference[index].second;
            ASSERT_TRUE(heap.Remove(value));
            EXPECT_FALSE(heap.Contains(value));
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(index));
        }

        const auto expected = SortedReference(reference);
        EXPECT_EQ(heap.Size(), expected.size());
        EXPECT_EQ(DrainPairs(heap), expected);
        std::pair<double, int> peek{-999.0, -999};
        if (expected.empty())
        {
            EXPECT_FALSE(heap.TryTop(peek));
            EXPECT_EQ(peek.second, -999);
        }
        else
        {
            ASSERT_TRUE(heap.TryTop(peek));
            EXPECT_EQ(peek, expected.front());
        }
    }
}

TEST(CoreIndexedHeap, FailClosedOnEmpty)
{
    IndexedHeap<double, int> heap;
    EXPECT_TRUE(heap.Empty());
    EXPECT_FALSE(heap.Top().has_value());
    std::pair<double, int> top{-1.0, -1};
    EXPECT_FALSE(heap.TryTop(top));
    EXPECT_EQ(top.second, -1);
    EXPECT_FALSE(heap.Pop());
    std::pair<double, int> out{-1.0, -1};
    EXPECT_FALSE(heap.TryPop(out));
    EXPECT_EQ(out.second, -1); // untouched
    EXPECT_FALSE(heap.Contains(0));
    EXPECT_FALSE(heap.Remove(0));
    EXPECT_FALSE(heap.DecreaseKey(0, 1.0));
}
