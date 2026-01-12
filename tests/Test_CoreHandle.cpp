#include <gtest/gtest.h>
#include <unordered_set>
#include <unordered_map>

import Core;

// Define test tag types for StrongHandle
struct GeometryTag {};
struct TextureTag {};
struct MaterialTag {};

using GeometryHandle = Core::StrongHandle<GeometryTag>;
using TextureHandle = Core::StrongHandle<TextureTag>;
using MaterialHandle = Core::StrongHandle<MaterialTag>;

// -----------------------------------------------------------------------------
// Basic Functionality
// -----------------------------------------------------------------------------

TEST(StrongHandle, DefaultConstructor_Invalid)
{
    GeometryHandle h;
    EXPECT_FALSE(h.IsValid());
    EXPECT_FALSE(static_cast<bool>(h));
    EXPECT_EQ(h.Index, GeometryHandle::INVALID_INDEX);
    EXPECT_EQ(h.Generation, 0u);
}

TEST(StrongHandle, ParameterizedConstructor_Valid)
{
    GeometryHandle h(42, 1);
    EXPECT_TRUE(h.IsValid());
    EXPECT_TRUE(static_cast<bool>(h));
    EXPECT_EQ(h.Index, 42u);
    EXPECT_EQ(h.Generation, 1u);
}

TEST(StrongHandle, ZeroIndexIsValid)
{
    // Index 0 should be valid (common slot)
    GeometryHandle h(0, 1);
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.Index, 0u);
}

// -----------------------------------------------------------------------------
// Comparison Operators
// -----------------------------------------------------------------------------

TEST(StrongHandle, Equality_SameValues)
{
    GeometryHandle h1(10, 5);
    GeometryHandle h2(10, 5);
    EXPECT_EQ(h1, h2);
}

TEST(StrongHandle, Equality_DifferentIndex)
{
    GeometryHandle h1(10, 5);
    GeometryHandle h2(11, 5);
    EXPECT_NE(h1, h2);
}

TEST(StrongHandle, Equality_DifferentGeneration)
{
    GeometryHandle h1(10, 5);
    GeometryHandle h2(10, 6);
    EXPECT_NE(h1, h2);
}

TEST(StrongHandle, Ordering_SpaceshipOperator)
{
    GeometryHandle h1(5, 1);
    GeometryHandle h2(10, 1);
    GeometryHandle h3(5, 2);

    // Index takes precedence in lexicographic comparison
    EXPECT_LT(h1, h2);
    EXPECT_LT(h1, h3);  // Same index, but generation 1 < 2
}

// -----------------------------------------------------------------------------
// Type Safety (Compile-Time)
// -----------------------------------------------------------------------------

TEST(StrongHandle, TypeSafety_DifferentTagsAreDistinctTypes)
{
    // This test verifies that different tag types create incompatible handle types
    // The real test is that this compiles - assignment between different types would fail
    GeometryHandle geoH(1, 1);
    TextureHandle texH(1, 1);
    MaterialHandle matH(1, 1);

    // Same index/generation, but different types - should NOT be comparable
    // (This would fail to compile if uncommented):
    // EXPECT_EQ(geoH, texH);  // Should not compile!

    // Verify they are indeed separate types with their own validity
    EXPECT_TRUE(geoH.IsValid());
    EXPECT_TRUE(texH.IsValid());
    EXPECT_TRUE(matH.IsValid());
}

// -----------------------------------------------------------------------------
// Hash Support (for unordered containers)
// -----------------------------------------------------------------------------

TEST(StrongHandle, Hashable_UnorderedSet)
{
    std::unordered_set<GeometryHandle> handleSet;

    GeometryHandle h1(1, 1);
    GeometryHandle h2(2, 1);
    GeometryHandle h3(1, 2);  // Same index, different generation
    GeometryHandle h1Dup(1, 1);  // Duplicate of h1

    handleSet.insert(h1);
    handleSet.insert(h2);
    handleSet.insert(h3);
    handleSet.insert(h1Dup);  // Should not increase size

    EXPECT_EQ(handleSet.size(), 3u);
    EXPECT_TRUE(handleSet.count(h1) == 1);
    EXPECT_TRUE(handleSet.count(h2) == 1);
    EXPECT_TRUE(handleSet.count(h3) == 1);
}

TEST(StrongHandle, Hashable_UnorderedMap)
{
    std::unordered_map<TextureHandle, std::string> textureNames;

    TextureHandle t1(0, 1);
    TextureHandle t2(1, 1);

    textureNames[t1] = "Diffuse";
    textureNames[t2] = "Normal";

    EXPECT_EQ(textureNames[t1], "Diffuse");
    EXPECT_EQ(textureNames[t2], "Normal");

    // Overwrite
    textureNames[t1] = "Albedo";
    EXPECT_EQ(textureNames[t1], "Albedo");
    EXPECT_EQ(textureNames.size(), 2u);
}

TEST(StrongHandle, Hash_DifferentValuesProduceDifferentHashes)
{
    std::hash<GeometryHandle> hasher;

    GeometryHandle h1(1, 1);
    GeometryHandle h2(2, 1);
    GeometryHandle h3(1, 2);

    // While hash collisions are allowed, distinct handles should ideally have different hashes
    size_t hash1 = hasher(h1);
    size_t hash2 = hasher(h2);
    size_t hash3 = hasher(h3);

    // At minimum, different index/generation should affect the hash
    EXPECT_NE(hash1, hash2);
    EXPECT_NE(hash1, hash3);
}

// -----------------------------------------------------------------------------
// Edge Cases
// -----------------------------------------------------------------------------

TEST(StrongHandle, MaxGeneration)
{
    GeometryHandle h(0, std::numeric_limits<uint32_t>::max());
    EXPECT_TRUE(h.IsValid());
    EXPECT_EQ(h.Generation, std::numeric_limits<uint32_t>::max());
}

TEST(StrongHandle, MaxValidIndex)
{
    // INVALID_INDEX is max uint32, so max-1 should be valid
    GeometryHandle h(GeometryHandle::INVALID_INDEX - 1, 0);
    EXPECT_TRUE(h.IsValid());
}

TEST(StrongHandle, CopySemantics)
{
    GeometryHandle original(42, 7);
    GeometryHandle copy = original;

    EXPECT_EQ(copy.Index, original.Index);
    EXPECT_EQ(copy.Generation, original.Generation);
    EXPECT_EQ(copy, original);
}

TEST(StrongHandle, ConstexprDefaultConstruction)
{
    constexpr GeometryHandle h;
    static_assert(!h.IsValid());
    EXPECT_FALSE(h.IsValid());
}

TEST(StrongHandle, ConstexprValueConstruction)
{
    constexpr GeometryHandle h(100, 50);
    static_assert(h.IsValid());
    static_assert(h.Index == 100);
    static_assert(h.Generation == 50);
    EXPECT_TRUE(h.IsValid());
}

