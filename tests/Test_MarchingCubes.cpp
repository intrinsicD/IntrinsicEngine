// tests/Test_MarchingCubes.cpp — Isosurface extraction tests.
#include <gtest/gtest.h>
#include <cmath>
#include <optional>
#include <glm/glm.hpp>

import Geometry;

using namespace Geometry::MarchingCubes;

// ============================================================================
// Helper: create a scalar grid filled with a sphere SDF.
// ============================================================================
static ScalarGrid MakeSphereGrid(std::size_t N, float radius, glm::vec3 center = glm::vec3(0))
{
    ScalarGrid grid;
    grid.NX = grid.NY = grid.NZ = N;
    const float extent = radius * 2.0f;
    grid.Origin = center - glm::vec3(extent);
    grid.Spacing = glm::vec3(2.0f * extent / static_cast<float>(N));
    grid.Values.resize((N + 1) * (N + 1) * (N + 1));

    for (std::size_t z = 0; z <= N; ++z)
        for (std::size_t y = 0; y <= N; ++y)
            for (std::size_t x = 0; x <= N; ++x)
            {
                const glm::vec3 pos = grid.VertexPosition(x, y, z);
                grid.Set(x, y, z, glm::length(pos - center) - radius);
            }
    return grid;
}

// ============================================================================
// Invalid / degenerate grids
// ============================================================================

TEST(MarchingCubes, InvalidGridReturnsNullopt)
{
    ScalarGrid grid;  // Zero dimensions.
    auto result = Extract(grid);
    EXPECT_FALSE(result.has_value());
}

TEST(MarchingCubes, EmptyIsosurfaceReturnsNullopt)
{
    // All values positive — entirely "outside".
    ScalarGrid grid;
    grid.NX = grid.NY = grid.NZ = 2;
    grid.Origin = glm::vec3(0);
    grid.Spacing = glm::vec3(1);
    grid.Values.assign((2 + 1) * (2 + 1) * (2 + 1), 10.0f);

    auto result = Extract(grid);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Basic sphere extraction
// ============================================================================

TEST(MarchingCubes, SphereExtractionProducesTriangles)
{
    auto grid = MakeSphereGrid(10, 1.0f);
    auto result = Extract(grid);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->VertexCount, 0u);
    EXPECT_GT(result->TriangleCount, 0u);
    EXPECT_EQ(result->Vertices.size(), result->VertexCount);
    EXPECT_EQ(result->Triangles.size(), result->TriangleCount);
}

TEST(MarchingCubes, SphereExtractionWithNormals)
{
    auto grid = MakeSphereGrid(10, 1.0f);
    MarchingCubesParams params;
    params.ComputeNormals = true;

    auto result = Extract(grid, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Normals.size(), result->Vertices.size());

    // Normals should be approximately unit length.
    for (const auto& n : result->Normals)
    {
        const float len = glm::length(n);
        EXPECT_NEAR(len, 1.0f, 0.1f);
    }
}

TEST(MarchingCubes, SphereVerticesNearSurface)
{
    const float radius = 1.0f;
    auto grid = MakeSphereGrid(20, radius);
    auto result = Extract(grid);
    ASSERT_TRUE(result.has_value());

    // All vertices should lie approximately on the sphere surface.
    for (const auto& v : result->Vertices)
    {
        const float dist = glm::length(v);
        // Allow tolerance proportional to grid spacing.
        EXPECT_NEAR(dist, radius, 0.15f) << "Vertex " << v.x << "," << v.y << "," << v.z
                                          << " is off the sphere surface";
    }
}

// ============================================================================
// Vertex welding
// ============================================================================

TEST(MarchingCubes, VertexWeldingProducesSharedVertices)
{
    auto grid = MakeSphereGrid(10, 1.0f);
    auto result = Extract(grid);
    ASSERT_TRUE(result.has_value());

    // With proper vertex welding, vertex count should be significantly less
    // than 3 * triangle count (which would be the unwelded case).
    EXPECT_LT(result->VertexCount, result->TriangleCount * 3);
}

// ============================================================================
// ToMesh conversion
// ============================================================================

TEST(MarchingCubes, ToMeshProducesValidHalfedgeMesh)
{
    auto grid = MakeSphereGrid(8, 1.0f);
    auto result = Extract(grid);
    ASSERT_TRUE(result.has_value());

    auto mesh = ToMesh(*result);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_GT(mesh->NumVertices(), 0u);
    EXPECT_GT(mesh->NumFaces(), 0u);
}

TEST(MarchingCubes, ToMeshEmptyResultReturnsNullopt)
{
    MarchingCubesResult empty;
    auto mesh = ToMesh(empty);
    EXPECT_FALSE(mesh.has_value());
}

// ============================================================================
// ScalarGrid accessors
// ============================================================================

TEST(MarchingCubes, ScalarGridIsValid)
{
    ScalarGrid grid;
    grid.NX = grid.NY = grid.NZ = 2;
    grid.Origin = glm::vec3(0);
    grid.Spacing = glm::vec3(1);
    grid.Values.resize(3 * 3 * 3, 0.0f);
    EXPECT_TRUE(grid.IsValid());
}

TEST(MarchingCubes, ScalarGridIsInvalidWhenSizeMismatch)
{
    ScalarGrid grid;
    grid.NX = grid.NY = grid.NZ = 2;
    grid.Values.resize(10, 0.0f);  // Wrong size.
    EXPECT_FALSE(grid.IsValid());
}

TEST(MarchingCubes, ScalarGridLinearIndex)
{
    ScalarGrid grid;
    grid.NX = 3; grid.NY = 4; grid.NZ = 5;
    // Index should be z*(NY+1)*(NX+1) + y*(NX+1) + x
    EXPECT_EQ(grid.LinearIndex(2, 3, 4), 4u * 5u * 4u + 3u * 4u + 2u);
}

// ============================================================================
// Non-zero isovalue
// ============================================================================

TEST(MarchingCubes, NonZeroIsovalue)
{
    auto grid = MakeSphereGrid(10, 2.0f);
    MarchingCubesParams params;
    params.Isovalue = -0.5f;  // Extract a slightly larger sphere.

    auto result = Extract(grid, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->TriangleCount, 0u);
}
