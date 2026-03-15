// tests/Test_ImporterUtils.cpp — Tests for shared importer utilities:
// SpatialVertexKey deduplication and polygon fan triangulation.

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>

// Include the headers under test directly (they are non-module headers).
#include "../src/Runtime/Graphics/Importers/Graphics.Importers.VertexDedup.hpp"
#include "../src/Runtime/Graphics/Importers/Graphics.Importers.TriangulationUtils.hpp"

using namespace Graphics::Importers;

// =============================================================================
// SpatialVertexKey Tests
// =============================================================================

TEST(SpatialVertexDedup, CoincidentVerticesAreEqual)
{
    SpatialVertexKey a{glm::vec3(1.0f, 2.0f, 3.0f)};
    SpatialVertexKey b{glm::vec3(1.0f, 2.0f, 3.0f)};
    EXPECT_EQ(a, b);
}

TEST(SpatialVertexDedup, NearThresholdVerticesAreEqual)
{
    // Within the ~10 micron quantization tolerance
    SpatialVertexKey a{glm::vec3(1.0f, 2.0f, 3.0f)};
    SpatialVertexKey b{glm::vec3(1.000005f, 2.000005f, 3.000005f)};
    EXPECT_EQ(a, b);
}

TEST(SpatialVertexDedup, DistinctVerticesAreNotEqual)
{
    SpatialVertexKey a{glm::vec3(1.0f, 2.0f, 3.0f)};
    SpatialVertexKey b{glm::vec3(1.001f, 2.0f, 3.0f)};
    EXPECT_NE(a, b);
}

TEST(SpatialVertexDedup, HashConsistentWithEquality)
{
    SpatialVertexKey a{glm::vec3(1.0f, 2.0f, 3.0f)};
    SpatialVertexKey b{glm::vec3(1.0f, 2.0f, 3.0f)};
    SpatialVertexKeyHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

TEST(SpatialVertexDedup, DedupMapWorks)
{
    std::unordered_map<SpatialVertexKey, uint32_t, SpatialVertexKeyHash> uniqueVerts;

    glm::vec3 positions[] = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f},  // duplicate of [0]
        {1.0f, 0.0f, 0.0f},  // duplicate of [1]
    };

    std::vector<glm::vec3> uniquePositions;
    std::vector<uint32_t> indices;

    for (const auto& pos : positions)
    {
        SpatialVertexKey key{pos};
        auto [it, inserted] = uniqueVerts.try_emplace(
            key, static_cast<uint32_t>(uniquePositions.size()));
        if (inserted)
            uniquePositions.push_back(pos);
        indices.push_back(it->second);
    }

    EXPECT_EQ(uniquePositions.size(), 3u);
    EXPECT_EQ(indices.size(), 5u);
    EXPECT_EQ(indices[0], indices[3]);  // same vertex
    EXPECT_EQ(indices[1], indices[4]);  // same vertex
}

TEST(SpatialVertexDedup, NegativeCoordinates)
{
    SpatialVertexKey a{glm::vec3(-1.0f, -2.0f, -3.0f)};
    SpatialVertexKey b{glm::vec3(-1.0f, -2.0f, -3.0f)};
    EXPECT_EQ(a, b);

    SpatialVertexKey c{glm::vec3(-1.001f, -2.0f, -3.0f)};
    EXPECT_NE(a, c);
}

TEST(SpatialVertexDedup, OriginVertex)
{
    SpatialVertexKey a{glm::vec3(0.0f, 0.0f, 0.0f)};
    SpatialVertexKey b{glm::vec3(0.0f, 0.0f, 0.0f)};
    EXPECT_EQ(a, b);
    SpatialVertexKeyHash hasher;
    EXPECT_EQ(hasher(a), hasher(b));
}

// =============================================================================
// TriangulateFan Tests
// =============================================================================

TEST(TriangulateFan, Triangle)
{
    std::vector<uint32_t> face = {0, 1, 2};
    std::vector<uint32_t> indices;
    TriangulateFan(face, indices);
    ASSERT_EQ(indices.size(), 3u);
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);
    EXPECT_EQ(indices[2], 2u);
}

TEST(TriangulateFan, Quad)
{
    std::vector<uint32_t> face = {0, 1, 2, 3};
    std::vector<uint32_t> indices;
    TriangulateFan(face, indices);
    ASSERT_EQ(indices.size(), 6u);
    // Triangle 1: 0, 1, 2
    EXPECT_EQ(indices[0], 0u);
    EXPECT_EQ(indices[1], 1u);
    EXPECT_EQ(indices[2], 2u);
    // Triangle 2: 0, 2, 3
    EXPECT_EQ(indices[3], 0u);
    EXPECT_EQ(indices[4], 2u);
    EXPECT_EQ(indices[5], 3u);
}

TEST(TriangulateFan, Pentagon)
{
    std::vector<uint32_t> face = {10, 11, 12, 13, 14};
    std::vector<uint32_t> indices;
    TriangulateFan(face, indices);
    ASSERT_EQ(indices.size(), 9u);  // 3 triangles * 3 indices
}

TEST(TriangulateFan, DegenerateSingleVertex)
{
    std::vector<uint32_t> face = {0};
    std::vector<uint32_t> indices;
    TriangulateFan(face, indices);
    EXPECT_TRUE(indices.empty());
}

TEST(TriangulateFan, DegenerateTwoVertices)
{
    std::vector<uint32_t> face = {0, 1};
    std::vector<uint32_t> indices;
    TriangulateFan(face, indices);
    EXPECT_TRUE(indices.empty());
}

TEST(TriangulateFan, EmptyFace)
{
    std::vector<uint32_t> face;
    std::vector<uint32_t> indices;
    TriangulateFan(face, indices);
    EXPECT_TRUE(indices.empty());
}

TEST(TriangulateFan, AppendsToExistingIndices)
{
    std::vector<uint32_t> indices = {100, 200, 300};
    std::vector<uint32_t> face = {0, 1, 2};
    TriangulateFan(face, indices);
    ASSERT_EQ(indices.size(), 6u);
    EXPECT_EQ(indices[0], 100u);  // Pre-existing
    EXPECT_EQ(indices[3], 0u);    // Newly appended
}
