// tests/Test_Grid.cpp — Grid data structure tests.
#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <optional>
#include <set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

using namespace Geometry::Grid;

// =============================================================================
// GridDimensions
// =============================================================================

TEST(GridDimensions, VertexCount)
{
    GridDimensions dims;
    dims.NX = 3; dims.NY = 4; dims.NZ = 5;
    EXPECT_EQ(dims.VertexCount(), 4u * 5u * 6u);
}

TEST(GridDimensions, CellCount)
{
    GridDimensions dims;
    dims.NX = 3; dims.NY = 4; dims.NZ = 5;
    EXPECT_EQ(dims.CellCount(), 3u * 4u * 5u);
}

TEST(GridDimensions, LinearIndexRoundTrip)
{
    GridDimensions dims;
    dims.NX = 5; dims.NY = 7; dims.NZ = 3;

    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                const auto idx = dims.LinearIndex(x, y, z);
                const auto coord = dims.GridCoord(idx);
                EXPECT_EQ(coord.x, static_cast<int>(x));
                EXPECT_EQ(coord.y, static_cast<int>(y));
                EXPECT_EQ(coord.z, static_cast<int>(z));
            }
}

TEST(GridDimensions, LinearIndexFormula)
{
    GridDimensions dims;
    dims.NX = 3; dims.NY = 4; dims.NZ = 5;
    // z*(NY+1)*(NX+1) + y*(NX+1) + x
    EXPECT_EQ(dims.LinearIndex(2, 3, 4), 4u * 5u * 4u + 3u * 4u + 2u);
}

TEST(GridDimensions, WorldPosition)
{
    GridDimensions dims;
    dims.NX = 10; dims.NY = 10; dims.NZ = 10;
    dims.Origin = glm::vec3(-1.0f);
    dims.Spacing = glm::vec3(0.2f);

    auto pos = dims.WorldPosition(5, 5, 5);
    EXPECT_NEAR(pos.x, 0.0f, 1e-5f);
    EXPECT_NEAR(pos.y, 0.0f, 1e-5f);
    EXPECT_NEAR(pos.z, 0.0f, 1e-5f);
}

TEST(GridDimensions, CellCenter)
{
    GridDimensions dims;
    dims.NX = 10; dims.NY = 10; dims.NZ = 10;
    dims.Origin = glm::vec3(0.0f);
    dims.Spacing = glm::vec3(1.0f);

    auto center = dims.CellCenter(0, 0, 0);
    EXPECT_NEAR(center.x, 0.5f, 1e-5f);
    EXPECT_NEAR(center.y, 0.5f, 1e-5f);
    EXPECT_NEAR(center.z, 0.5f, 1e-5f);
}

TEST(GridDimensions, InBounds)
{
    GridDimensions dims;
    dims.NX = 3; dims.NY = 4; dims.NZ = 5;

    EXPECT_TRUE(dims.InBounds(0, 0, 0));
    EXPECT_TRUE(dims.InBounds(3, 4, 5));  // vertex grid goes up to NX/NY/NZ
    EXPECT_FALSE(dims.InBounds(-1, 0, 0));
    EXPECT_FALSE(dims.InBounds(0, -1, 0));
    EXPECT_FALSE(dims.InBounds(4, 0, 0));
    EXPECT_FALSE(dims.InBounds(0, 5, 0));
    EXPECT_FALSE(dims.InBounds(0, 0, 6));
}

TEST(GridDimensions, IsValid)
{
    GridDimensions dims;
    EXPECT_FALSE(dims.IsValid());  // NX=NY=NZ=0

    dims.NX = 1; dims.NY = 1; dims.NZ = 1;
    EXPECT_TRUE(dims.IsValid());

    dims.NY = 0;
    EXPECT_FALSE(dims.IsValid());
}

// =============================================================================
// DenseGrid — construction and property management
// =============================================================================

TEST(DenseGrid, ConstructionAllocatesCorrectSize)
{
    GridDimensions dims;
    dims.NX = 3; dims.NY = 4; dims.NZ = 5;

    DenseGrid grid(dims);
    EXPECT_EQ(grid.VertexCount(), dims.VertexCount());
    EXPECT_EQ(grid.Cells().Size(), dims.VertexCount());
}

TEST(DenseGrid, AddAndGetProperty)
{
    GridDimensions dims;
    dims.NX = 2; dims.NY = 2; dims.NZ = 2;

    DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("density", 0.0f);
    EXPECT_TRUE(scalar.IsValid());
    EXPECT_TRUE(grid.HasProperty("density"));

    auto retrieved = grid.GetProperty<float>("density");
    EXPECT_TRUE(retrieved.IsValid());
}

TEST(DenseGrid, GetOrAddProperty)
{
    GridDimensions dims;
    dims.NX = 2; dims.NY = 2; dims.NZ = 2;

    DenseGrid grid(dims);
    auto p1 = grid.GetOrAddProperty<float>("scalar", 1.0f);
    auto p2 = grid.GetOrAddProperty<float>("scalar", 2.0f);

    // Should return the same property, not create a duplicate.
    EXPECT_TRUE(p1.IsValid());
    EXPECT_TRUE(p2.IsValid());
    // Default from first creation
    EXPECT_NEAR(p1[0], 1.0f, 1e-9f);
}

TEST(DenseGrid, MultipleProperties)
{
    GridDimensions dims;
    dims.NX = 4; dims.NY = 4; dims.NZ = 4;

    DenseGrid grid(dims);
    auto density = grid.AddProperty<float>("density", 0.0f);
    auto velocity = grid.AddProperty<glm::vec3>("velocity", glm::vec3(0.0f));
    auto material = grid.AddProperty<int>("material", -1);

    EXPECT_TRUE(density.IsValid());
    EXPECT_TRUE(velocity.IsValid());
    EXPECT_TRUE(material.IsValid());
    EXPECT_EQ(density.Vector().size(), grid.VertexCount());
    EXPECT_EQ(velocity.Vector().size(), grid.VertexCount());
    EXPECT_EQ(material.Vector().size(), grid.VertexCount());
}

TEST(DenseGrid, ScalarFieldReadWrite)
{
    GridDimensions dims;
    dims.NX = 3; dims.NY = 3; dims.NZ = 3;

    DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    // Write a sphere SDF
    const glm::vec3 center(1.5f, 1.5f, 1.5f);
    const float radius = 1.0f;

    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                float sdf = glm::length(pos - center) - radius;
                grid.Set(scalar, x, y, z, sdf);
            }

    // Read back and verify
    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                float expected = glm::length(pos - center) - radius;
                float actual = grid.At(scalar, x, y, z);
                EXPECT_NEAR(actual, expected, 1e-6f);
            }
}

TEST(DenseGrid, VertexIndexMatchesDimensions)
{
    GridDimensions dims;
    dims.NX = 5; dims.NY = 7; dims.NZ = 3;

    DenseGrid grid(dims);
    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                EXPECT_EQ(grid.VertexIndex(x, y, z), dims.LinearIndex(x, y, z));
            }
}

TEST(DenseGrid, Reset)
{
    GridDimensions dims;
    dims.NX = 2; dims.NY = 2; dims.NZ = 2;

    DenseGrid grid(dims);
    (void)grid.AddProperty<float>("temp");

    GridDimensions newDims;
    newDims.NX = 5; newDims.NY = 5; newDims.NZ = 5;
    grid.Reset(newDims);

    EXPECT_EQ(grid.VertexCount(), newDims.VertexCount());
    EXPECT_FALSE(grid.HasProperty("temp"));
}

// =============================================================================
// DenseGrid — MarchingCubes integration
// =============================================================================

TEST(DenseGrid, MarchingCubesExtraction)
{
    GridDimensions dims;
    dims.NX = dims.NY = dims.NZ = 10;
    dims.Origin = glm::vec3(-2.0f);
    dims.Spacing = glm::vec3(4.0f / 10.0f);

    DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    // Fill with sphere SDF
    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                grid.Set(scalar, x, y, z, glm::length(pos) - 1.0f);
            }

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->VertexCount, 0u);
    EXPECT_GT(result->TriangleCount, 0u);

    // Vertices should be near the unit sphere surface
    for (const auto& v : result->Vertices)
    {
        float dist = glm::length(v);
        EXPECT_NEAR(dist, 1.0f, 0.25f);
    }
}

TEST(DenseGrid, MarchingCubesCustomPropertyName)
{
    GridDimensions dims;
    dims.NX = dims.NY = dims.NZ = 8;
    dims.Origin = glm::vec3(-2.0f);
    dims.Spacing = glm::vec3(4.0f / 8.0f);

    DenseGrid grid(dims);
    auto sdf = grid.AddProperty<float>("sdf", 0.0f);

    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                grid.Set(sdf, x, y, z, glm::length(pos) - 1.0f);
            }

    // Wrong property name → nullopt
    auto bad = Geometry::MarchingCubes::Extract(grid, {}, "nonexistent");
    EXPECT_FALSE(bad.has_value());

    // Correct property name → success
    auto good = Geometry::MarchingCubes::Extract(grid, {}, "sdf");
    ASSERT_TRUE(good.has_value());
    EXPECT_GT(good->TriangleCount, 0u);
}

TEST(DenseGrid, MarchingCubesInvalidGridReturnsNullopt)
{
    DenseGrid grid;  // Default: dims are all zero
    auto result = Geometry::MarchingCubes::Extract(grid);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// DenseGrid — scalar property round-trip
// =============================================================================

TEST(DenseGrid, ScalarPropertyRoundTrip)
{
    GridDimensions dims;
    dims.NX = dims.NY = dims.NZ = 4;
    dims.Origin = glm::vec3(-1.0f);
    dims.Spacing = glm::vec3(0.5f);

    DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);
    EXPECT_EQ(grid.VertexCount(), 5u * 5u * 5u);

    // Fill with sequential values
    for (std::size_t i = 0; i < grid.VertexCount(); ++i)
        scalar[i] = static_cast<float>(i) * 0.1f;

    // Verify round-trip through At/Set accessors
    auto retrieved = grid.GetProperty<float>("scalar");
    ASSERT_TRUE(retrieved.IsValid());

    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                float expected = static_cast<float>(dims.LinearIndex(x, y, z)) * 0.1f;
                float actual = grid.At(retrieved, x, y, z);
                EXPECT_NEAR(actual, expected, 1e-6f);
            }
}

// =============================================================================
// SparseGrid — basic functionality
// =============================================================================

TEST(SparseGrid, EmptyGridHasNoBlocks)
{
    GridDimensions dims;
    dims.NX = 64; dims.NY = 64; dims.NZ = 64;

    SparseGrid grid(dims);
    EXPECT_EQ(grid.AllocatedBlockCount(), 0u);
    EXPECT_EQ(grid.AllocatedVertexCount(), 0u);
}

TEST(SparseGrid, TouchVertexAllocatesBlock)
{
    GridDimensions dims;
    dims.NX = 64; dims.NY = 64; dims.NZ = 64;

    SparseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    auto idx = grid.TouchVertex(5, 5, 5);
    EXPECT_EQ(grid.AllocatedBlockCount(), 1u);
    EXPECT_EQ(grid.AllocatedVertexCount(), SparseGrid::BlockVolume);

    // Writing via returned index should work
    scalar[idx] = 42.0f;
    EXPECT_NEAR(scalar[idx], 42.0f, 1e-9f);
}

TEST(SparseGrid, VertexIndexReturnsNulloptForUnallocated)
{
    GridDimensions dims;
    dims.NX = 64; dims.NY = 64; dims.NZ = 64;

    SparseGrid grid(dims);
    EXPECT_FALSE(grid.VertexIndex(10, 10, 10).has_value());
}

TEST(SparseGrid, VertexIndexAfterTouch)
{
    GridDimensions dims;
    dims.NX = 64; dims.NY = 64; dims.NZ = 64;

    SparseGrid grid(dims);
    auto expected = grid.TouchVertex(10, 10, 10);
    auto actual = grid.VertexIndex(10, 10, 10);
    ASSERT_TRUE(actual.has_value());
    EXPECT_EQ(*actual, expected);
}

TEST(SparseGrid, SameBlockForNearbyVertices)
{
    GridDimensions dims;
    dims.NX = 64; dims.NY = 64; dims.NZ = 64;

    SparseGrid grid(dims);

    // Vertices (0,0,0) and (7,7,7) are in the same 8^3 block.
    (void)grid.TouchVertex(0, 0, 0);
    EXPECT_EQ(grid.AllocatedBlockCount(), 1u);

    (void)grid.TouchVertex(7, 7, 7);
    EXPECT_EQ(grid.AllocatedBlockCount(), 1u);  // Same block!

    // Vertex (8,0,0) is in a different block.
    (void)grid.TouchVertex(8, 0, 0);
    EXPECT_EQ(grid.AllocatedBlockCount(), 2u);
}

TEST(SparseGrid, IsAllocated)
{
    GridDimensions dims;
    dims.NX = 64; dims.NY = 64; dims.NZ = 64;

    SparseGrid grid(dims);
    EXPECT_FALSE(grid.IsAllocated(5, 5, 5));

    (void)grid.TouchVertex(5, 5, 5);
    EXPECT_TRUE(grid.IsAllocated(5, 5, 5));
    // All vertices in the same block should also be "allocated"
    EXPECT_TRUE(grid.IsAllocated(0, 0, 0));
    EXPECT_TRUE(grid.IsAllocated(7, 7, 7));
    // Different block
    EXPECT_FALSE(grid.IsAllocated(8, 0, 0));
}

TEST(SparseGrid, ScalarFieldWriteRead)
{
    GridDimensions dims;
    dims.NX = 32; dims.NY = 32; dims.NZ = 32;
    dims.Origin = glm::vec3(-1.0f);
    dims.Spacing = glm::vec3(2.0f / 32.0f);

    SparseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    // Only populate the narrow-band around a sphere SDF
    const float radius = 0.5f;
    const float bandwidth = 0.15f;
    std::size_t touchedCount = 0;

    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                float sdf = glm::length(pos) - radius;
                if (std::abs(sdf) <= bandwidth)
                {
                    auto idx = grid.TouchVertex(x, y, z);
                    scalar[idx] = sdf;
                    ++touchedCount;
                }
            }

    // Should have allocated far fewer than the full grid
    EXPECT_GT(touchedCount, 0u);
    EXPECT_LT(grid.AllocatedVertexCount(), dims.VertexCount());

    // Read back narrow-band values
    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                float sdf = glm::length(pos) - radius;
                if (std::abs(sdf) <= bandwidth)
                {
                    auto idx = grid.VertexIndex(x, y, z);
                    ASSERT_TRUE(idx.has_value());
                    EXPECT_NEAR(scalar[*idx], sdf, 1e-6f);
                }
            }
}

TEST(SparseGrid, MultipleProperties)
{
    GridDimensions dims;
    dims.NX = 16; dims.NY = 16; dims.NZ = 16;

    SparseGrid grid(dims);
    auto density = grid.AddProperty<float>("density", 0.0f);
    auto velocity = grid.AddProperty<glm::vec3>("velocity", glm::vec3(0.0f));

    auto idx = grid.TouchVertex(4, 4, 4);
    density[idx] = 1.5f;
    velocity[idx] = glm::vec3(1.0f, 2.0f, 3.0f);

    EXPECT_NEAR(density[idx], 1.5f, 1e-9f);
    EXPECT_NEAR(velocity[idx].x, 1.0f, 1e-9f);
    EXPECT_NEAR(velocity[idx].y, 2.0f, 1e-9f);
    EXPECT_NEAR(velocity[idx].z, 3.0f, 1e-9f);
}

TEST(SparseGrid, ForEachBlock)
{
    GridDimensions dims;
    dims.NX = 32; dims.NY = 32; dims.NZ = 32;

    SparseGrid grid(dims);
    (void)grid.TouchVertex(0, 0, 0);   // Block (0,0,0)
    (void)grid.TouchVertex(8, 0, 0);   // Block (1,0,0)
    (void)grid.TouchVertex(0, 8, 0);   // Block (0,1,0)

    std::set<std::size_t> bases;
    grid.ForEachBlock([&](std::size_t, std::size_t, std::size_t, std::size_t base)
    {
        bases.insert(base);
    });

    EXPECT_EQ(bases.size(), 3u);
}

TEST(SparseGrid, ForEachAllocatedVertex)
{
    GridDimensions dims;
    dims.NX = 16; dims.NY = 16; dims.NZ = 16;

    SparseGrid grid(dims);
    (void)grid.TouchVertex(0, 0, 0);  // Allocates block (0,0,0)

    std::size_t count = 0;
    grid.ForEachAllocatedVertex([&](std::size_t x, std::size_t y, std::size_t z, std::size_t)
    {
        EXPECT_TRUE(dims.InBounds(static_cast<int>(x), static_cast<int>(y), static_cast<int>(z)));
        ++count;
    });

    // Block has 8^3 = 512 vertices, but InBounds check may clip at grid boundary
    // Block (0,0,0) covers vertices 0..7 in each axis, all of which are in-bounds
    // for a 16^3 grid.
    EXPECT_EQ(count, SparseGrid::BlockVolume);
}

TEST(SparseGrid, Reset)
{
    GridDimensions dims;
    dims.NX = 16; dims.NY = 16; dims.NZ = 16;

    SparseGrid grid(dims);
    (void)grid.AddProperty<float>("temp");
    (void)grid.TouchVertex(0, 0, 0);
    EXPECT_EQ(grid.AllocatedBlockCount(), 1u);

    GridDimensions newDims;
    newDims.NX = 32; newDims.NY = 32; newDims.NZ = 32;
    grid.Reset(newDims);

    EXPECT_EQ(grid.AllocatedBlockCount(), 0u);
    EXPECT_EQ(grid.AllocatedVertexCount(), 0u);
    EXPECT_FALSE(grid.HasProperty("temp"));
}

TEST(SparseGrid, MemoryEfficiency)
{
    // For a large grid with sparse narrow-band, the sparse grid should
    // use far less memory than a dense grid would.
    GridDimensions dims;
    dims.NX = dims.NY = dims.NZ = 128;
    dims.Origin = glm::vec3(-1.0f);
    dims.Spacing = glm::vec3(2.0f / 128.0f);

    SparseGrid grid(dims);
    (void)grid.AddProperty<float>("scalar", 0.0f);

    // Only allocate the thin band around a sphere
    for (std::size_t z = 0; z <= dims.NZ; ++z)
        for (std::size_t y = 0; y <= dims.NY; ++y)
            for (std::size_t x = 0; x <= dims.NX; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                float sdf = glm::length(pos) - 0.5f;
                if (std::abs(sdf) < 0.05f)
                    (void)grid.TouchVertex(x, y, z);
            }

    const std::size_t denseTotal = dims.VertexCount();
    const std::size_t sparseTotal = grid.AllocatedVertexCount();

    // Sparse should be a small fraction of dense
    EXPECT_LT(sparseTotal, denseTotal / 4);
}

// =============================================================================
// SparseGrid — block-local spatial locality
// =============================================================================

TEST(SparseGrid, IntraBlockLocalityConsecutiveIndices)
{
    GridDimensions dims;
    dims.NX = 32; dims.NY = 32; dims.NZ = 32;

    SparseGrid grid(dims);

    // Touch two adjacent vertices in the same block
    auto idx0 = grid.TouchVertex(0, 0, 0);
    auto idx1 = grid.TouchVertex(1, 0, 0);

    // They should be exactly 1 apart (consecutive in the local linear layout)
    EXPECT_EQ(idx1, idx0 + 1);
}

// =============================================================================
// DenseGrid — PropertySet span access for GPU upload
// =============================================================================

TEST(DenseGrid, SpanAccessForGPUUpload)
{
    GridDimensions dims;
    dims.NX = dims.NY = dims.NZ = 4;

    DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 1.0f);

    auto span = scalar.Span();
    EXPECT_EQ(span.size(), dims.VertexCount());

    // All default values
    for (float val : span)
        EXPECT_NEAR(val, 1.0f, 1e-9f);

    // Modify via span
    span[0] = 42.0f;
    EXPECT_NEAR(scalar[0], 42.0f, 1e-9f);
}

// =============================================================================
// DenseGrid — MarchingCubes extraction integration
// =============================================================================

TEST(DenseGrid, MarchingCubesExtractSphereSDF)
{
    GridDimensions dims;
    dims.NX = dims.NY = dims.NZ = 10;
    const float radius = 1.0f;
    const float extent = radius * 2.0f;
    dims.Origin = glm::vec3(-extent);
    dims.Spacing = glm::vec3(2.0f * extent / 10.0f);

    DenseGrid grid(dims);
    auto scalar = grid.AddProperty<float>("scalar", 0.0f);

    for (std::size_t z = 0; z <= 10; ++z)
        for (std::size_t y = 0; y <= 10; ++y)
            for (std::size_t x = 0; x <= 10; ++x)
            {
                auto pos = grid.WorldPosition(x, y, z);
                grid.Set(scalar, x, y, z, glm::length(pos) - radius);
            }

    auto result = Geometry::MarchingCubes::Extract(grid);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->VertexCount, 0u);
    EXPECT_GT(result->TriangleCount, 0u);
}
