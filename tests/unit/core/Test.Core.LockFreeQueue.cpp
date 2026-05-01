#include <gtest/gtest.h>

import Extrinsic.Core.LockFreeQueue;

using namespace Extrinsic::Core;

TEST(CoreLockFreeQueue, PushPopAndEmpty)
{
    LockFreeQueue<int> q(4);

    int out = 0;
    EXPECT_FALSE(q.Pop(out));

    EXPECT_TRUE(q.Push(1));
    EXPECT_TRUE(q.Push(2));

    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 1);
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 2);
    EXPECT_FALSE(q.Pop(out));
}

TEST(CoreLockFreeQueue, FullAndWrapAround)
{
    LockFreeQueue<int> q(2);

    EXPECT_TRUE(q.Push(10));
    EXPECT_TRUE(q.Push(11));
    EXPECT_FALSE(q.Push(12));

    int out = 0;
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 10);

    EXPECT_TRUE(q.Push(12));
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 11);
    EXPECT_TRUE(q.Pop(out));
    EXPECT_EQ(out, 12);
}
