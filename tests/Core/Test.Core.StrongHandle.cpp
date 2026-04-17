#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

import Extrinsic.Core.StrongHandle;

using Extrinsic::Core::StrongHandle;
using Extrinsic::Core::INVALID_HANDLE_INDEX;

namespace
{
    struct GeometryTag
    {
    };

    struct TextureTag
    {
    };

    using GeometryHandle = StrongHandle<GeometryTag>;
    using TextureHandle = StrongHandle<TextureTag>;
}

// -----------------------------------------------------------------------------
// Default construction / validity
// -----------------------------------------------------------------------------

TEST(CoreStrongHandle, DefaultConstructionIsInvalid)
{
    GeometryHandle h{};
    EXPECT_FALSE(h.IsValid());
    EXPECT_EQ(h.Index, INVALID_HANDLE_INDEX);
    EXPECT_EQ(h.Generation, 0u);
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(CoreStrongHandle, ConstructedHandleIsValid)
{
    GeometryHandle h{5u, 1u};
    EXPECT_TRUE(h.IsValid());
    EXPECT_TRUE(static_cast<bool>(h));
    EXPECT_EQ(h.Index, 5u);
    EXPECT_EQ(h.Generation, 1u);
}

TEST(CoreStrongHandle, InvalidIndexSentinelIsMaxUint32)
{
    EXPECT_EQ(INVALID_HANDLE_INDEX, std::numeric_limits<uint32_t>::max());
}

TEST(CoreStrongHandle, HandleConstructedWithInvalidIndexIsInvalid)
{
    GeometryHandle h{INVALID_HANDLE_INDEX, 42u};
    EXPECT_FALSE(h.IsValid());
    EXPECT_FALSE(static_cast<bool>(h));
}

TEST(CoreStrongHandle, HandleWithGenerationZeroButValidIndexIsValid)
{
    // IsValid only checks Index, not Generation.
    GeometryHandle h{0u, 0u};
    EXPECT_TRUE(h.IsValid());
}

// -----------------------------------------------------------------------------
// Comparisons
// -----------------------------------------------------------------------------

TEST(CoreStrongHandle, EqualityComparesIndexAndGeneration)
{
    GeometryHandle a{1u, 1u};
    GeometryHandle b{1u, 1u};
    GeometryHandle c{1u, 2u};
    GeometryHandle d{2u, 1u};

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(CoreStrongHandle, OrderingIsTotal)
{
    GeometryHandle a{1u, 1u};
    GeometryHandle b{1u, 2u};
    GeometryHandle c{2u, 1u};

    EXPECT_TRUE((a <=> b) < 0);
    EXPECT_TRUE((a <=> c) < 0);
    EXPECT_TRUE((c <=> b) > 0);

    GeometryHandle a2{1u, 1u};
    EXPECT_TRUE((a <=> a2) == 0);
}

// -----------------------------------------------------------------------------
// Type safety across tags
// -----------------------------------------------------------------------------

TEST(CoreStrongHandle, DistinctTagsProduceDistinctTypes)
{
    static_assert(!std::is_same_v<GeometryHandle, TextureHandle>,
                  "Different tags must produce different handle types");
    SUCCEED();
}

// -----------------------------------------------------------------------------
// constexpr usage
// -----------------------------------------------------------------------------

TEST(CoreStrongHandle, ConstexprConstruction)
{
    constexpr GeometryHandle h{7u, 3u};
    static_assert(h.IsValid());
    static_assert(h.Index == 7u);
    static_assert(h.Generation == 3u);
    EXPECT_TRUE(h.IsValid());
}

// -----------------------------------------------------------------------------
// std::hash specialization
// -----------------------------------------------------------------------------

TEST(CoreStrongHandle, HashCollisionResistantForCloseValues)
{
    std::hash<GeometryHandle> hasher;

    const auto h1 = hasher(GeometryHandle{0u, 0u});
    const auto h2 = hasher(GeometryHandle{1u, 0u});
    const auto h3 = hasher(GeometryHandle{0u, 1u});
    const auto h4 = hasher(GeometryHandle{1u, 1u});

    // MurmurHash3 mix must produce distinct values for close inputs.
    EXPECT_NE(h1, h2);
    EXPECT_NE(h1, h3);
    EXPECT_NE(h1, h4);
    EXPECT_NE(h2, h3);
    EXPECT_NE(h2, h4);
    EXPECT_NE(h3, h4);
}

TEST(CoreStrongHandle, HashAllowsUnorderedSet)
{
    std::unordered_set<GeometryHandle> set;
    set.insert({0u, 1u});
    set.insert({1u, 1u});
    set.insert({0u, 1u});  // Duplicate
    set.insert({0u, 2u});  // Different generation

    EXPECT_EQ(set.size(), 3u);
    EXPECT_EQ(set.count(GeometryHandle{0u, 1u}), 1u);
    EXPECT_EQ(set.count(GeometryHandle{0u, 3u}), 0u);
}

TEST(CoreStrongHandle, HashAllowsUnorderedMap)
{
    std::unordered_map<GeometryHandle, int> map;
    map[{1u, 1u}] = 10;
    map[{2u, 1u}] = 20;
    map[{1u, 2u}] = 11;

    const GeometryHandle k11{1u, 1u};
    const GeometryHandle k21{2u, 1u};
    const GeometryHandle k12{1u, 2u};
    EXPECT_EQ(map[k11], 10);
    EXPECT_EQ(map[k21], 20);
    EXPECT_EQ(map[k12], 11);
    EXPECT_EQ(map.size(), 3u);
}

TEST(CoreStrongHandle, HashDeterministicAcrossCalls)
{
    std::hash<GeometryHandle> hasher;
    const GeometryHandle h{12345u, 42u};
    EXPECT_EQ(hasher(h), hasher(h));
}
