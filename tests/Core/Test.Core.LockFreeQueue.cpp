#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

import Extrinsic.Core.LockFreeQueue;

using Extrinsic::Core::LockFreeQueue;

// -----------------------------------------------------------------------------
// Basic contract
// -----------------------------------------------------------------------------

TEST(CoreLockFreeQueue, PopOnEmptyReturnsFalse)
{
    LockFreeQueue<int> q(16);
    int out = -1;
    EXPECT_FALSE(q.Pop(out));
    EXPECT_EQ(out, -1);  // Must not be overwritten on empty
}

TEST(CoreLockFreeQueue, PushThenPopSingleItem)
{
    LockFreeQueue<int> q(4);
    EXPECT_TRUE(q.Push(42));
    int out = 0;
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 42);
}

TEST(CoreLockFreeQueue, FifoOrder)
{
    LockFreeQueue<int> q(8);
    for (int i = 0; i < 5; ++i)
        EXPECT_TRUE(q.Push(int(i)));

    for (int i = 0; i < 5; ++i)
    {
        int out = -1;
        EXPECT_TRUE(q.Pop(out));
        EXPECT_EQ(out, i);
    }
    int spare = 0;
    EXPECT_FALSE(q.Pop(spare));
}

TEST(CoreLockFreeQueue, PushReturnsFalseOnFull)
{
    LockFreeQueue<int> q(4);  // capacity 4
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(q.Push(int(i)));

    EXPECT_FALSE(q.Push(100));

    int out = 0;
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 0);

    // After pop, room opens up again.
    EXPECT_TRUE(q.Push(100));
}

TEST(CoreLockFreeQueue, CapacityRoundsUpToPowerOfTwo)
{
    // Capacity 3 → 4, 5 → 8, 17 → 32. We cannot observe capacity directly but
    // we can verify the queue accepts the rounded-up count of elements.
    LockFreeQueue<int> q5(5);  // rounds to 8
    for (int i = 0; i < 8; ++i)
        EXPECT_TRUE(q5.Push(int(i)));
    EXPECT_FALSE(q5.Push(99));
}

TEST(CoreLockFreeQueue, MinimumCapacityTwo)
{
    LockFreeQueue<int> q(0);  // clamps to 2
    EXPECT_TRUE(q.Push(1));
    EXPECT_TRUE(q.Push(2));
    EXPECT_FALSE(q.Push(3));

    int out = 0;
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 1);
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 2);
    EXPECT_FALSE(q.Pop(out));
}

TEST(CoreLockFreeQueue, WrapAroundReuseSlots)
{
    LockFreeQueue<int> q(4);
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        for (int i = 0; i < 4; ++i)
            EXPECT_TRUE(q.Push(cycle * 100 + i));
        for (int i = 0; i < 4; ++i)
        {
            int out = -1;
            EXPECT_TRUE(q.Pop(out));
            EXPECT_EQ(out, cycle * 100 + i);
        }
    }
}

// -----------------------------------------------------------------------------
// Move-only payload (unique_ptr)
// -----------------------------------------------------------------------------

TEST(CoreLockFreeQueue, WorksWithMoveOnlyPayload)
{
    LockFreeQueue<std::unique_ptr<int>> q(4);
    for (int i = 0; i < 3; ++i)
        EXPECT_TRUE(q.Push(std::make_unique<int>(i * 10)));

    for (int i = 0; i < 3; ++i)
    {
        std::unique_ptr<int> out;
        EXPECT_TRUE(q.Pop(out));
        ASSERT_NE(out, nullptr);
        EXPECT_EQ(*out, i * 10);
    }
}

// -----------------------------------------------------------------------------
// Concurrency: single producer / single consumer must not drop items.
// -----------------------------------------------------------------------------

TEST(CoreLockFreeQueue, SingleProducerSingleConsumerNoLoss)
{
    constexpr int kItems = 5000;
    LockFreeQueue<int> q(1024);

    std::vector<int> consumed;
    consumed.reserve(kItems);

    std::thread producer([&] {
        for (int i = 0; i < kItems; ++i)
        {
            while (!q.Push(int(i)))
                std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        int out = 0;
        int pulled = 0;
        while (pulled < kItems)
        {
            if (q.Pop(out))
            {
                consumed.push_back(out);
                ++pulled;
            }
            else
            {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(consumed.size()), kItems);
    for (int i = 0; i < kItems; ++i)
        EXPECT_EQ(consumed[i], i);
}

// -----------------------------------------------------------------------------
// Multi-producer / multi-consumer: items pushed == items popped; no duplicates.
// -----------------------------------------------------------------------------

TEST(CoreLockFreeQueue, MultiProducerMultiConsumerNoLossOrDuplicates)
{
    constexpr int kProducers = 4;
    constexpr int kConsumers = 4;
    constexpr int kPerProducer = 2000;
    constexpr int kTotal = kProducers * kPerProducer;

    LockFreeQueue<int> q(4096);
    std::atomic<int> produced{0};
    std::atomic<int> consumed{0};
    std::mutex sinkMutex;
    std::vector<int> sink;
    sink.reserve(kTotal);

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
    {
        producers.emplace_back([&, p] {
            for (int i = 0; i < kPerProducer; ++i)
            {
                const int value = p * kPerProducer + i;
                while (!q.Push(int(value)))
                    std::this_thread::yield();
                produced.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::vector<std::thread> consumers;
    std::atomic<bool> stopConsumers{false};
    for (int c = 0; c < kConsumers; ++c)
    {
        consumers.emplace_back([&] {
            int out = 0;
            while (!stopConsumers.load(std::memory_order_acquire) ||
                   consumed.load(std::memory_order_acquire) < kTotal)
            {
                if (q.Pop(out))
                {
                    std::lock_guard lock(sinkMutex);
                    sink.push_back(out);
                    consumed.fetch_add(1, std::memory_order_release);
                }
                else
                {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    stopConsumers.store(true, std::memory_order_release);
    for (auto& t : consumers) t.join();

    ASSERT_EQ(produced.load(), kTotal);
    ASSERT_EQ(consumed.load(), kTotal);
    ASSERT_EQ(static_cast<int>(sink.size()), kTotal);

    // Every unique value [0, kTotal) must appear exactly once.
    std::unordered_set<int> unique(sink.begin(), sink.end());
    EXPECT_EQ(static_cast<int>(unique.size()), kTotal);
    EXPECT_EQ(*std::min_element(sink.begin(), sink.end()), 0);
    EXPECT_EQ(*std::max_element(sink.begin(), sink.end()), kTotal - 1);
}
