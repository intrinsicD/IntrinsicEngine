#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <utility>

import Utils.BoundedHeap;

using namespace Utils;

// -----------------------------------------------------------------------------
// Basic Functionality
// -----------------------------------------------------------------------------

TEST(BoundedHeap, Constructor_EmptyHeap)
{
    BoundedHeap<int> heap(10);

    EXPECT_TRUE(heap.Empty());
    EXPECT_EQ(heap.Size(), 0u);
    EXPECT_EQ(heap.Capacity(), 10u);
}

TEST(BoundedHeap, Constructor_ZeroCapacity)
{
    BoundedHeap<int> heap(0);

    EXPECT_EQ(heap.Capacity(), 0u);

    // Push should be silently ignored
    heap.Push(42);
    EXPECT_TRUE(heap.Empty());
}

TEST(BoundedHeap, Push_SingleElement)
{
    BoundedHeap<int> heap(5);

    heap.Push(10);

    EXPECT_FALSE(heap.Empty());
    EXPECT_EQ(heap.Size(), 1u);
    EXPECT_EQ(heap.top(), 10);
}

TEST(BoundedHeap, Push_FillToCapacity)
{
    BoundedHeap<int> heap(3);

    heap.Push(5);
    heap.Push(3);
    heap.Push(7);

    EXPECT_EQ(heap.Size(), 3u);
    // Max-heap: top() returns largest (worst) element
    EXPECT_EQ(heap.top(), 7);
}

TEST(BoundedHeap, Push_ExceedCapacity_ReplacesWorst)
{
    BoundedHeap<int> heap(3);

    heap.Push(10);
    heap.Push(20);
    heap.Push(30);  // Heap: [10, 20, 30], top = 30

    // Push smaller value - should replace 30
    heap.Push(5);

    EXPECT_EQ(heap.Size(), 3u);

    auto sorted = heap.GetSortedData();
    EXPECT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0], 5);   // Best (smallest)
    EXPECT_EQ(sorted[1], 10);
    EXPECT_EQ(sorted[2], 20);  // 30 was evicted
}

TEST(BoundedHeap, Push_ExceedCapacity_IgnoresWorse)
{
    BoundedHeap<int> heap(3);

    heap.Push(10);
    heap.Push(20);
    heap.Push(30);

    // Push larger value - should be ignored
    heap.Push(40);

    EXPECT_EQ(heap.Size(), 3u);
    EXPECT_EQ(heap.top(), 30);  // 30 still worst
}

// -----------------------------------------------------------------------------
// GetSortedData Tests
// -----------------------------------------------------------------------------

TEST(BoundedHeap, GetSortedData_AscendingOrder)
{
    BoundedHeap<int> heap(5);

    heap.Push(50);
    heap.Push(10);
    heap.Push(30);
    heap.Push(20);
    heap.Push(40);

    auto sorted = heap.GetSortedData();

    ASSERT_EQ(sorted.size(), 5u);
    EXPECT_EQ(sorted[0], 10);
    EXPECT_EQ(sorted[1], 20);
    EXPECT_EQ(sorted[2], 30);
    EXPECT_EQ(sorted[3], 40);
    EXPECT_EQ(sorted[4], 50);
}

TEST(BoundedHeap, GetSortedData_DoesNotDestroyHeap)
{
    BoundedHeap<int> heap(3);

    heap.Push(30);
    heap.Push(10);
    heap.Push(20);

    auto sorted1 = heap.GetSortedData();
    auto sorted2 = heap.GetSortedData();

    EXPECT_EQ(sorted1, sorted2);
    EXPECT_EQ(heap.Size(), 3u);  // Heap unchanged
}

// -----------------------------------------------------------------------------
// Clear Tests
// -----------------------------------------------------------------------------

TEST(BoundedHeap, Clear)
{
    BoundedHeap<int> heap(5);

    heap.Push(1);
    heap.Push(2);
    heap.Push(3);

    EXPECT_EQ(heap.Size(), 3u);

    heap.Clear();

    EXPECT_TRUE(heap.Empty());
    EXPECT_EQ(heap.Size(), 0u);
    EXPECT_EQ(heap.Capacity(), 5u);  // Capacity unchanged
}

// -----------------------------------------------------------------------------
// Threshold Tests
// -----------------------------------------------------------------------------

TEST(BoundedHeap, IsFull_NotFull)
{
    BoundedHeap<int> heap(5);

    heap.Push(10);
    heap.Push(20);

    // Not full yet
    EXPECT_FALSE(heap.IsFull());
    EXPECT_EQ(heap.Size(), 2u);
}

TEST(BoundedHeap, IsFull_Full)
{
    BoundedHeap<int> heap(3);

    heap.Push(10);
    heap.Push(30);
    heap.Push(20);

    // Now full
    EXPECT_TRUE(heap.IsFull());
    EXPECT_EQ(heap.Size(), 3u);
}

TEST(BoundedHeap, Threshold_Full)
{
    BoundedHeap<int> heap(3);

    heap.Push(10);
    heap.Push(30);
    heap.Push(20);

    // Full, returns current worst
    EXPECT_TRUE(heap.IsFull());
    EXPECT_EQ(heap.Threshold(), 30);
}

// -----------------------------------------------------------------------------
// Pair Type Tests (Common Use Case for KNN)
// -----------------------------------------------------------------------------

TEST(BoundedHeap, PairType_DistanceIndex)
{
    using DistIdx = std::pair<float, size_t>;
    BoundedHeap<DistIdx> heap(3);  // Keep 3 nearest neighbors

    // Simulate KNN: points with distances
    heap.Push({5.0f, 0});   // Point 0 at distance 5
    heap.Push({2.0f, 1});   // Point 1 at distance 2
    heap.Push({8.0f, 2});   // Point 2 at distance 8 (worst so far)

    EXPECT_EQ(heap.top().first, 8.0f);  // Current worst distance

    // Found closer point
    heap.Push({1.0f, 3});   // Point 3 at distance 1 - should evict point 2

    auto sorted = heap.GetSortedData();

    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_FLOAT_EQ(sorted[0].first, 1.0f);  // Closest
    EXPECT_EQ(sorted[0].second, 3u);
    EXPECT_FLOAT_EQ(sorted[1].first, 2.0f);
    EXPECT_EQ(sorted[1].second, 1u);
    EXPECT_FLOAT_EQ(sorted[2].first, 5.0f);
    EXPECT_EQ(sorted[2].second, 0u);
}

TEST(BoundedHeap, PairType_Threshold_UsableForPruning)
{
    using DistIdx = std::pair<float, size_t>;
    BoundedHeap<DistIdx> heap(3);

    heap.Push({1.0f, 0});
    heap.Push({2.0f, 1});
    heap.Push({3.0f, 2});

    // For KNN: any point farther than threshold can be skipped
    float pruningThreshold = heap.Threshold().first;
    EXPECT_FLOAT_EQ(pruningThreshold, 3.0f);

    // Point at distance 4.0 would not improve the result
    // (pruning optimization in real Octree queries)
}

// -----------------------------------------------------------------------------
// Edge Cases
// -----------------------------------------------------------------------------

TEST(BoundedHeap, DuplicateValues)
{
    BoundedHeap<int> heap(5);

    heap.Push(10);
    heap.Push(10);
    heap.Push(10);
    heap.Push(5);
    heap.Push(5);

    EXPECT_EQ(heap.Size(), 5u);

    auto sorted = heap.GetSortedData();
    EXPECT_EQ(sorted[0], 5);
    EXPECT_EQ(sorted[1], 5);
    EXPECT_EQ(sorted[2], 10);
}

TEST(BoundedHeap, NegativeValues)
{
    BoundedHeap<int> heap(3);

    heap.Push(-10);
    heap.Push(-5);
    heap.Push(-20);

    // -5 is the "worst" (largest)
    EXPECT_EQ(heap.top(), -5);

    heap.Push(-30);  // Better than all

    auto sorted = heap.GetSortedData();
    EXPECT_EQ(sorted[0], -30);
    EXPECT_EQ(sorted[1], -20);
    EXPECT_EQ(sorted[2], -10);
}

TEST(BoundedHeap, FloatValues)
{
    BoundedHeap<float> heap(3);

    heap.Push(3.14f);
    heap.Push(2.71f);
    heap.Push(1.41f);

    EXPECT_FLOAT_EQ(heap.top(), 3.14f);

    heap.Push(0.5f);  // Better

    auto sorted = heap.GetSortedData();
    EXPECT_FLOAT_EQ(sorted[0], 0.5f);
    EXPECT_FLOAT_EQ(sorted[1], 1.41f);
    EXPECT_FLOAT_EQ(sorted[2], 2.71f);
}

TEST(BoundedHeap, CapacityOne)
{
    BoundedHeap<int> heap(1);

    heap.Push(100);
    EXPECT_EQ(heap.top(), 100);

    heap.Push(50);  // Better, replaces 100
    EXPECT_EQ(heap.top(), 50);

    heap.Push(75);  // Worse, ignored
    EXPECT_EQ(heap.top(), 50);

    heap.Push(25);  // Better, replaces 50
    EXPECT_EQ(heap.top(), 25);

    EXPECT_EQ(heap.Size(), 1u);
}

TEST(BoundedHeap, LargeCapacity)
{
    BoundedHeap<int> heap(1000);

    // Fill with descending values
    for (int i = 999; i >= 0; --i)
    {
        heap.Push(i);
    }

    EXPECT_EQ(heap.Size(), 1000u);
    EXPECT_EQ(heap.top(), 999);  // Largest value

    auto sorted = heap.GetSortedData();
    EXPECT_EQ(sorted[0], 0);
    EXPECT_EQ(sorted[999], 999);
}

// -----------------------------------------------------------------------------
// Stability and Ordering Consistency
// -----------------------------------------------------------------------------

TEST(BoundedHeap, ConsistentAfterMultipleOperations)
{
    BoundedHeap<int> heap(5);

    // Series of operations
    for (int i = 0; i < 100; ++i)
    {
        heap.Push(i);
    }

    // Should contain the 5 smallest: 0, 1, 2, 3, 4
    EXPECT_EQ(heap.Size(), 5u);

    auto sorted = heap.GetSortedData();
    for (int i = 0; i < 5; ++i)
    {
        EXPECT_EQ(sorted[i], i);
    }
}

