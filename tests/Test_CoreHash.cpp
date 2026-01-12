#include <gtest/gtest.h>
#include <unordered_set>
#include <unordered_map>
#include <string>

import Core;

using namespace Core::Hash;

// -----------------------------------------------------------------------------
// HashString Function Tests
// -----------------------------------------------------------------------------

TEST(CoreHash, HashString_EmptyString)
{
    // FNV-1a base value XOR'd with nothing = base value
    uint32_t hash = HashString("");
    EXPECT_EQ(hash, 2166136261u);  // FNV offset basis
}

TEST(CoreHash, HashString_SingleChar)
{
    uint32_t hashA = HashString("a");
    uint32_t hashB = HashString("b");

    EXPECT_NE(hashA, hashB);
    // FNV-1a: hash = (2166136261 ^ 'a') * 16777619
    EXPECT_NE(hashA, 0u);
}

TEST(CoreHash, HashString_DifferentStrings)
{
    uint32_t h1 = HashString("Texture");
    uint32_t h2 = HashString("Material");
    uint32_t h3 = HashString("Mesh");

    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_NE(h1, h3);
}

TEST(CoreHash, HashString_SameStrings)
{
    uint32_t h1 = HashString("Backbuffer");
    uint32_t h2 = HashString("Backbuffer");

    EXPECT_EQ(h1, h2);
}

TEST(CoreHash, HashString_CaseSensitive)
{
    uint32_t lower = HashString("texture");
    uint32_t upper = HashString("TEXTURE");
    uint32_t mixed = HashString("Texture");

    EXPECT_NE(lower, upper);
    EXPECT_NE(lower, mixed);
    EXPECT_NE(upper, mixed);
}

TEST(CoreHash, HashString_Constexpr)
{
    constexpr uint32_t hash = HashString("CompileTime");
    static_assert(hash != 0);
    EXPECT_NE(hash, 0u);
}

// -----------------------------------------------------------------------------
// StringID Tests
// -----------------------------------------------------------------------------

TEST(CoreHash, StringID_DefaultConstruct)
{
    StringID id;
    EXPECT_EQ(id.Value, 0u);
}

TEST(CoreHash, StringID_FromValue)
{
    StringID id(12345u);
    EXPECT_EQ(id.Value, 12345u);
}

TEST(CoreHash, StringID_FromCharPtr)
{
    StringID id("RenderPass");
    EXPECT_EQ(id.Value, HashString("RenderPass"));
}

TEST(CoreHash, StringID_FromStringView)
{
    std::string_view sv = "DepthBuffer";
    StringID id(sv);
    EXPECT_EQ(id.Value, HashString("DepthBuffer"));
}

TEST(CoreHash, StringID_Equality)
{
    StringID a("Albedo");
    StringID b("Albedo");
    StringID c("Normal");

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(CoreHash, StringID_Ordering)
{
    StringID a("AAA");
    StringID b("ZZZ");

    // Ordering is based on hash value, not alphabetical
    // Just verify that ordering exists and is consistent
    auto cmp = a <=> b;
    EXPECT_TRUE((cmp < 0) || (cmp > 0) || (cmp == 0));

    // Same values should be equal
    StringID a2("AAA");
    EXPECT_TRUE((a <=> a2) == 0);
}

// -----------------------------------------------------------------------------
// User-Defined Literal Tests
// -----------------------------------------------------------------------------

TEST(CoreHash, StringID_Literal)
{
    StringID id = "Backbuffer"_id;
    EXPECT_EQ(id.Value, HashString("Backbuffer"));
}

TEST(CoreHash, StringID_LiteralConstexpr)
{
    constexpr StringID id = "CompileTimeID"_id;
    static_assert(id.Value != 0);
    EXPECT_NE(id.Value, 0u);
}

TEST(CoreHash, StringID_LiteralComparison)
{
    StringID a = "Forward"_id;
    StringID b = "Forward"_id;
    StringID c = "Deferred"_id;

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// -----------------------------------------------------------------------------
// Hash Support for std::unordered containers
// -----------------------------------------------------------------------------

TEST(CoreHash, StringID_UnorderedSet)
{
    std::unordered_set<StringID> idSet;

    idSet.insert("Pass1"_id);
    idSet.insert("Pass2"_id);
    idSet.insert("Pass3"_id);
    idSet.insert("Pass1"_id);  // Duplicate

    EXPECT_EQ(idSet.size(), 3u);
    EXPECT_TRUE(idSet.count("Pass1"_id) == 1);
    EXPECT_TRUE(idSet.count("Pass4"_id) == 0);
}

TEST(CoreHash, StringID_UnorderedMapKey)
{
    std::unordered_map<StringID, int> resourceMap;

    resourceMap["Texture"_id] = 100;
    resourceMap["Buffer"_id] = 200;
    resourceMap["Sampler"_id] = 300;

    EXPECT_EQ(resourceMap["Texture"_id], 100);
    EXPECT_EQ(resourceMap["Buffer"_id], 200);
    EXPECT_EQ(resourceMap.size(), 3u);
}

// -----------------------------------------------------------------------------
// Edge Cases and Collision Awareness
// -----------------------------------------------------------------------------

TEST(CoreHash, StringID_DistributionQuality)
{
    // Test that common engine strings produce reasonably distributed hashes
    std::vector<StringID> commonIds = {
        "Position"_id, "Normal"_id, "Tangent"_id, "UV"_id,
        "Color"_id, "Depth"_id, "Stencil"_id, "Shadow"_id,
        "Albedo"_id, "Metallic"_id, "Roughness"_id, "AO"_id,
        "Emissive"_id, "Height"_id, "Opacity"_id, "Mask"_id
    };

    std::unordered_set<uint32_t> hashValues;
    for (const auto& id : commonIds)
    {
        hashValues.insert(id.Value);
    }

    // No collisions among common names
    EXPECT_EQ(hashValues.size(), commonIds.size());
}

TEST(CoreHash, StringID_EmptyStringLiteral)
{
    StringID empty = ""_id;
    EXPECT_EQ(empty.Value, HashString(""));
}

TEST(CoreHash, StringID_LongString)
{
    std::string longStr(1000, 'x');  // 1000 x's
    StringID id(longStr);
    EXPECT_NE(id.Value, 0u);

    // Slightly different string should have different hash
    longStr[500] = 'y';
    StringID id2(longStr);
    EXPECT_NE(id.Value, id2.Value);
}

TEST(CoreHash, StringID_SpecialCharacters)
{
    StringID a = "path/to/resource"_id;
    StringID b = "path\\to\\resource"_id;
    StringID c = "path.to.resource"_id;

    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
}

// -----------------------------------------------------------------------------
// U64Hash Tests
// -----------------------------------------------------------------------------

TEST(CoreHash, U64Hash_Basic)
{
    U64Hash hasher;

    size_t h1 = hasher(0ull);
    size_t h2 = hasher(1ull);
    size_t h3 = hasher(std::numeric_limits<uint64_t>::max());

    // All should produce valid hashes (potentially different)
    // Zero may or may not hash to zero depending on impl
    EXPECT_NE(h2, h3);
}

TEST(CoreHash, U64Hash_InUnorderedMap)
{
    std::unordered_map<uint64_t, std::string, U64Hash> map;

    map[0x1234567890ABCDEFull] = "ResourceA";
    map[0xFEDCBA0987654321ull] = "ResourceB";

    EXPECT_EQ(map[0x1234567890ABCDEFull], "ResourceA");
    EXPECT_EQ(map.size(), 2u);
}

