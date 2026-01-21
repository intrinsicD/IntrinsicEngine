#include <gtest/gtest.h>

import Core;

namespace
{
    struct TestTag {};
    using Pool = Core::ResourcePool<int, TestTag>;
    using Handle = Pool::Handle;
}

TEST(ResourcePool, AddGetRemoveDeferredRecycle)
{
    Pool pool;
    pool.Initialize(2);

    const Handle h0 = pool.Add(123);
    ASSERT_TRUE(h0.IsValid());

    int* p0 = pool.Get(h0);
    ASSERT_NE(p0, nullptr);
    EXPECT_EQ(*p0, 123);

    // Defer deletion at frame 10.
    pool.Remove(h0, 10);
    EXPECT_EQ(pool.Get(h0), nullptr);
    EXPECT_EQ(pool.GetPendingDeletionCount(), 1u);

    // Not yet safe: need currentFrame > enqueued + framesInFlight.
    pool.ProcessDeletions(12);
    EXPECT_EQ(pool.GetPendingDeletionCount(), 1u);

    // Now safe.
    pool.ProcessDeletions(13);
    EXPECT_EQ(pool.GetPendingDeletionCount(), 0u);

    // Slot should recycle and bump generation.
    const Handle h1 = pool.Add(456);
    EXPECT_EQ(h1.Index, h0.Index);
    EXPECT_NE(h1.Generation, h0.Generation);

    EXPECT_EQ(pool.Get(h0), nullptr); // stale handle

    int* p1 = pool.Get(h1);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(*p1, 456);
}
