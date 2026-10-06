#include <gtest/gtest.h>
#include <cstdint>
#include <limits>

import Extrinsic.Core.IntegerMath;

using Extrinsic::Core::CeilDiv;

static_assert(CeilDiv(10u, 4u) == 3u);
static_assert(noexcept(CeilDiv(1u, 1u)));

TEST(CoreIntegerMath, CeilDivRoundsUpAndHandlesZero)
{
    constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
    EXPECT_EQ(CeilDiv(0u, 8u), 0u);
    EXPECT_EQ(CeilDiv(16u, 8u), 2u);
    EXPECT_EQ(CeilDiv(17u, 8u), 3u);
    EXPECT_EQ(CeilDiv(5u, 0u), 0u);
    EXPECT_EQ(CeilDiv(kMax, 1u), kMax);
    EXPECT_EQ(CeilDiv(kMax, 2u), 0u); // documented uint32_t wrap
}
