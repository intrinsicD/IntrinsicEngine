// tests/Test_Subdivision.cpp — Loop subdivision tests.
// Covers: face count growth, vertex count, convergence toward sphere,
// boundary handling, multi-iteration, and degenerate input.

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "TestMeshBuilders.h"

TEST(Subdivision, SingleIterationQuadruplesFaces)
{
    auto input = MakeTetrahedron();
    ASSERT_EQ(input.FaceCount(), 4u);

    Geometry::Halfedge::Mesh output;
    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 1;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 1u);
    EXPECT_EQ(result->FinalFaceCount, 16u); // 4 * 4
    EXPECT_EQ(output.FaceCount(), 16u);
}

TEST(Subdivision, TwoIterations)
{
    auto input = MakeTetrahedron();

    Geometry::Halfedge::Mesh output;
    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 2;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 2u);
    EXPECT_EQ(result->FinalFaceCount, 64u); // 4 * 4^2
}

TEST(Subdivision, VertexCountFormula)
{
    // V_new = V_old + E_old for one iteration of Loop subdivision
    auto input = MakeTetrahedron();
    const auto vOld = input.VertexCount(); // 4
    // Tetrahedron has 6 edges
    const std::size_t eOld = 6;

    Geometry::Halfedge::Mesh output;
    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 1;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->FinalVertexCount, vOld + eOld); // 10
}

TEST(Subdivision, IcosahedronConvergesToSphere)
{
    auto input = MakeIcosahedron();

    Geometry::Halfedge::Mesh output;
    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 3;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());

    // All vertices should be approximately equidistant from origin
    // (icosahedron on unit sphere → subdivision converges to sphere)
    double minDist = 1e9, maxDist = 0.0;
    for (std::size_t i = 0; i < output.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        if (output.IsDeleted(v)) continue;
        double d = glm::length(output.Position(v));
        minDist = std::min(minDist, d);
        maxDist = std::max(maxDist, d);
    }
    // After 3 iterations, spread should be very small
    EXPECT_LT(maxDist - minDist, 0.02);
}

TEST(Subdivision, OutputIsClosedWhenInputIsClosed)
{
    auto input = MakeTetrahedron();

    Geometry::Halfedge::Mesh output;
    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 1;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());

    // Closed input → closed output (no boundary)
    for (std::size_t i = 0; i < output.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (!output.IsDeleted(e))
            EXPECT_FALSE(output.IsBoundary(e));
    }
}

TEST(Subdivision, ReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh input;
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::Subdivision::Subdivide(input, output);
    EXPECT_FALSE(result.has_value());
}

TEST(Subdivision, ReturnsNulloptForNonTriangleMesh)
{
    auto input = MakeSingleQuad(); // Quads, not triangles

    Geometry::Halfedge::Mesh output;
    auto result = Geometry::Subdivision::Subdivide(input, output);
    EXPECT_FALSE(result.has_value());
}

TEST(Subdivision, ZeroIterationsReturnsNullopt)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 0;

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Subdivision, InputMeshIsNotModified)
{
    auto input = MakeTetrahedron();
    const auto origVerts = input.VertexCount();
    const auto origFaces = input.FaceCount();

    Geometry::Halfedge::Mesh output;
    Geometry::Subdivision::Subdivide(input, output);

    EXPECT_EQ(input.VertexCount(), origVerts);
    EXPECT_EQ(input.FaceCount(), origFaces);
}
