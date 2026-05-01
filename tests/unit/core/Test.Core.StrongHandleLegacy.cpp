#include <gtest/gtest.h>
#include <unordered_map>

import Extrinsic.Core.StrongHandle;

using namespace Extrinsic::Core;

namespace
{
    struct ATag;
    using AHandle = StrongHandle<ATag>;
}

TEST(CoreStrongHandle, DefaultInvalidAndValueValid)
{
    AHandle invalid{};
    EXPECT_FALSE(invalid.IsValid());
    EXPECT_FALSE(static_cast<bool>(invalid));

    AHandle valid{3u, 9u};
    EXPECT_TRUE(valid.IsValid());
    EXPECT_TRUE(static_cast<bool>(valid));
    EXPECT_EQ(valid.Index, 3u);
    EXPECT_EQ(valid.Generation, 9u);
}

TEST(CoreStrongHandle, ComparisonAndHash)
{
    AHandle a{1u, 1u};
    AHandle b{1u, 2u};
    AHandle c{1u, 1u};

    EXPECT_NE(a, b);
    EXPECT_EQ(a, c);
    EXPECT_LT(a, b);

    std::unordered_map<AHandle, int, StrongHandleHash<ATag>> map;
    map.emplace(a, 10);
    map.emplace(b, 20);

    EXPECT_EQ(map[a], 10);
    EXPECT_EQ(map[b], 20);
}
