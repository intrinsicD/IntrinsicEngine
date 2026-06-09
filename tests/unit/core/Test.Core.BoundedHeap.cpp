#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

import Extrinsic.Core.BoundedHeap;

namespace
{
    using Extrinsic::Core::BoundedHeap;
}

TEST(CoreBoundedHeap, ZeroCapacityIgnoresPushes)
{
    BoundedHeap<int> heap{0};

    heap.Push(42);

    EXPECT_TRUE(heap.Empty());
    EXPECT_EQ(heap.Size(), 0u);
    EXPECT_EQ(heap.Capacity(), 0u);
}

TEST(CoreBoundedHeap, KeepsSmallestValuesAndReportsWorstRetainedThreshold)
{
    BoundedHeap<int> heap{3};

    for (const int value : {30, 10, 50, 20, 40})
        heap.Push(value);

    EXPECT_TRUE(heap.IsFull());
    EXPECT_EQ(heap.Threshold(), 30);

    const std::vector<int> sorted = heap.GetSortedData();
    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0], 10);
    EXPECT_EQ(sorted[1], 20);
    EXPECT_EQ(sorted[2], 30);
}

TEST(CoreBoundedHeap, UsesTotalOrderingForDeterministicTieBreaks)
{
    using Entry = std::pair<float, std::string>;
    BoundedHeap<Entry> heap{2};

    heap.Push({1.0f, "b"});
    heap.Push({1.0f, "c"});
    heap.Push({1.0f, "a"});

    const std::vector<Entry> sorted = heap.GetSortedData();
    ASSERT_EQ(sorted.size(), 2u);
    EXPECT_EQ(sorted[0], Entry(1.0f, "a"));
    EXPECT_EQ(sorted[1], Entry(1.0f, "b"));
}
