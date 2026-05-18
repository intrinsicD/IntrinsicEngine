#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <unordered_map>

import Extrinsic.ECS.Component.StableId;

using Extrinsic::ECS::Components::IsValid;
using Extrinsic::ECS::Components::kInvalidStableId;
using Extrinsic::ECS::Components::StableId;
using Extrinsic::ECS::Components::StableIdHash;

TEST(ECSStableIdentity, DefaultConstructedEqualsInvalidSentinel)
{
    constexpr StableId id{};
    EXPECT_EQ(id, kInvalidStableId);
    EXPECT_FALSE(IsValid(id));
}

TEST(ECSStableIdentity, InvalidSentinelHasBothHalvesZero)
{
    // The serializer / hot-reload contract depends on `kInvalidStableId`
    // being all-zero so a default-zeroed payload reads as "no identity".
    static_assert(kInvalidStableId.High == 0u);
    static_assert(kInvalidStableId.Low == 0u);
    SUCCEED();
}

TEST(ECSStableIdentity, IsValidIsFalseOnlyForSentinel)
{
    EXPECT_FALSE(IsValid(StableId{0u, 0u}));
    EXPECT_TRUE(IsValid(StableId{1u, 0u}));
    EXPECT_TRUE(IsValid(StableId{0u, 1u}));
    EXPECT_TRUE(IsValid(StableId{1u, 1u}));
    EXPECT_TRUE(IsValid(StableId{0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull}));
}

TEST(ECSStableIdentity, EqualityComparesBothHalves)
{
    constexpr StableId a{0x0123456789abcdefull, 0xfedcba9876543210ull};
    constexpr StableId b{0x0123456789abcdefull, 0xfedcba9876543210ull};
    constexpr StableId c{0x0123456789abcdefull, 0x0u};
    constexpr StableId d{0x0u, 0xfedcba9876543210ull};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
    EXPECT_NE(c, d);
}

TEST(ECSStableIdentity, ThreeWayComparesHighThenLow)
{
    // operator<=> on the aggregate orders by `High` first, then `Low`,
    // matching the standard memberwise default. Persisted indexes that
    // sort by `StableId` (e.g. a serializer's deterministic order)
    // depend on this lexicographic shape.
    constexpr StableId low_high{1u, 0xFFFFFFFFFFFFFFFFull};
    constexpr StableId high_low{2u, 0u};

    EXPECT_TRUE(low_high < high_low);
    EXPECT_TRUE(high_low > low_high);
    EXPECT_TRUE((low_high <=> high_low) < 0);

    constexpr StableId equal_high_low_a{5u, 10u};
    constexpr StableId equal_high_low_b{5u, 11u};
    EXPECT_TRUE(equal_high_low_a < equal_high_low_b);
    EXPECT_TRUE((equal_high_low_a <=> equal_high_low_b) < 0);
}

TEST(ECSStableIdentity, StableIdHashDistinguishesSwappedHalves)
{
    constexpr StableId a{0xAAAAAAAAAAAAAAAAull, 0xBBBBBBBBBBBBBBBBull};
    constexpr StableId b{0xBBBBBBBBBBBBBBBBull, 0xAAAAAAAAAAAAAAAAull};
    ASSERT_NE(a, b);

    const std::size_t ha = StableIdHash{}(a);
    const std::size_t hb = StableIdHash{}(b);
    EXPECT_NE(ha, hb)
        << "StableIdHash must not collapse swapped halves to the same digest";
}

TEST(ECSStableIdentity, StableIdHashOfInvalidSentinelDoesNotCollideWithCommonValues)
{
    // The sentinel is the all-zero pair; if the hash returned 0 for the
    // sentinel, an unordered_map populated with valid ids that happened
    // to mix to 0 would alias the "no identity" bucket. Spot-check that
    // the sentinel hashes distinctly from a handful of nearby ids.
    const std::size_t sentinel = StableIdHash{}(kInvalidStableId);
    EXPECT_NE(sentinel, StableIdHash{}(StableId{0u, 1u}));
    EXPECT_NE(sentinel, StableIdHash{}(StableId{1u, 0u}));
    EXPECT_NE(sentinel, StableIdHash{}(StableId{1u, 1u}));
}

TEST(ECSStableIdentity, StableIdHashIsUsableWithUnorderedMapAcrossModuleBoundary)
{
    // Mirrors the `Extrinsic.Core.StrongHandle` exported-hasher contract:
    // an unordered container instantiated in the test's GMF must be able
    // to use the module-exported hasher functor directly.
    std::unordered_map<StableId, int, StableIdHash> by_id;
    const StableId a{1u, 2u};
    const StableId b{3u, 4u};
    by_id[a] = 11;
    by_id[b] = 22;

    ASSERT_EQ(by_id.size(), 2u);
    EXPECT_EQ(by_id.at(a), 11);
    EXPECT_EQ(by_id.at(b), 22);
    EXPECT_EQ(by_id.find(kInvalidStableId), by_id.end());
}

TEST(ECSStableIdentity, StableIdIsTriviallyCopyableAndStandardLayout)
{
    // CPU-only payload contract: the value type must be memcpy-safe and
    // ABI-stable so serializers, hot-reload, and cross-module containers
    // can move it without observability concerns.
    static_assert(std::is_trivially_copyable_v<StableId>);
    static_assert(std::is_standard_layout_v<StableId>);
    static_assert(std::is_trivially_destructible_v<StableId>);
    static_assert(sizeof(StableId) == sizeof(std::uint64_t) * 2u);
    static_assert(alignof(StableId) >= alignof(std::uint64_t));
    SUCCEED();
}
