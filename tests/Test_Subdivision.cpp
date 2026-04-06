// tests/Test_Subdivision.cpp — Loop and Catmull-Clark subdivision tests.
// Covers: face count growth, vertex count, convergence toward sphere,
// boundary handling, multi-iteration, and degenerate input.

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <utility>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

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
    constexpr double kSphereSpreadTolerance = 0.025;

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
    EXPECT_LT(maxDist - minDist, kSphereSpreadTolerance);
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
    (void)Geometry::Subdivision::Subdivide(input, output);

    EXPECT_EQ(input.VertexCount(), origVerts);
    EXPECT_EQ(input.FaceCount(), origFaces);
}

TEST(Subdivision, RespectsMaxOutputFaceBudget)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 5;
    params.MaxOutputFaces = 70; // tetrahedron: 4 -> 16 -> 64 -> 256 ...

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 2u);
    EXPECT_EQ(result->FinalFaceCount, 64u);
}

TEST(Subdivision, ReturnsNulloptWhenBudgetBlocksFirstIteration)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::Subdivision::SubdivisionParams params;
    params.Iterations = 1;
    params.MaxOutputFaces = 8; // first iteration needs 16 faces

    auto result = Geometry::Subdivision::Subdivide(input, output, params);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Catmull-Clark Subdivision tests
// =============================================================================

namespace
{

[[nodiscard]] Geometry::Halfedge::Mesh MakeQuadCube()
{
    auto mesh = Geometry::Halfedge::MakeMesh(Geometry::AABB{
        .Min = {-1.0f, -1.0f, -1.0f},
        .Max = { 1.0f,  1.0f,  1.0f},
    });
    return mesh ? std::move(*mesh) : Geometry::Halfedge::Mesh{};
}

} // namespace

TEST(CatmullClark, SingleTriangleProducesQuads)
{
    auto input = MakeSingleTriangle();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // A single triangle has 3 edges, so produces 3 quad faces
    EXPECT_EQ(result->FinalFaceCount, 3u);
    EXPECT_EQ(result->IterationsPerformed, 1u);

    // All faces should be quads
    for (std::size_t fi = 0; fi < output.FacesSize(); ++fi)
    {
        Geometry::FaceHandle fh{static_cast<Geometry::PropertyIndex>(fi)};
        if (output.IsDeleted(fh)) continue;
        EXPECT_EQ(output.Valence(fh), 4u) << "Face " << fi << " should be a quad";
    }
}

TEST(CatmullClark, TetrahedronProducesAllQuads)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->AllQuads);
    EXPECT_EQ(result->IterationsPerformed, 1u);

    // Tetrahedron: 4 faces with 3 edges each -> 12 quads
    // (each face has 3 halfedges, each produces 1 quad = 3 quads/face * 4 faces = 12)
    EXPECT_EQ(result->FinalFaceCount, 12u);
}

TEST(CatmullClark, CubeProducesAllQuads)
{
    auto input = MakeQuadCube();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->AllQuads);
    // Cube: 6 quad faces, each with 4 edges -> 24 quad faces after one iteration
    EXPECT_EQ(result->FinalFaceCount, 24u);
}

TEST(CatmullClark, PreservesClosedMeshEulerCharacteristic)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    // Euler characteristic: V - E + F = 2 for closed mesh
    std::size_t V = output.VertexCount();
    std::size_t E = output.EdgeCount();
    std::size_t F = output.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "V=" << V << " E=" << E << " F=" << F;
}

TEST(CatmullClark, CubePreservesEulerCharacteristic)
{
    auto input = MakeQuadCube();
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    std::size_t V = output.VertexCount();
    std::size_t E = output.EdgeCount();
    std::size_t F = output.FaceCount();
    EXPECT_EQ(static_cast<int>(V) - static_cast<int>(E) + static_cast<int>(F), 2)
        << "V=" << V << " E=" << E << " F=" << F;
}

TEST(CatmullClark, TwoIterationsWork)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::CatmullClark::SubdivisionParams params;
    params.Iterations = 2;

    auto result = Geometry::CatmullClark::Subdivide(input, output, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->IterationsPerformed, 2u);
    EXPECT_TRUE(result->AllQuads);

    // After two iterations, all faces should still be quads
    // Face count: 12 (iter 1) -> 12*4 = 48 (iter 2 — each quad -> 4 quads)
    EXPECT_EQ(result->FinalFaceCount, 48u);
}

TEST(CatmullClark, CubeConvergesToSphere)
{
    // Catmull-Clark subdivision of a cube should converge toward a sphere.
    // We verify that the variance of vertex distances from the origin
    // strictly decreases with each subdivision iteration.
    auto input = MakeQuadCube();

    auto computeRadiusVariance = [](const Geometry::Halfedge::Mesh& mesh) -> double
    {
        double sumR = 0.0, sumR2 = 0.0;
        std::size_t count = 0;
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh)) continue;
            double r = static_cast<double>(glm::length(mesh.Position(vh)));
            sumR += r;
            sumR2 += r * r;
            ++count;
        }
        double meanR = sumR / static_cast<double>(count);
        return sumR2 / static_cast<double>(count) - meanR * meanR;
    };

    // Variance after 1 iteration
    Geometry::Halfedge::Mesh output1;
    Geometry::CatmullClark::SubdivisionParams p1;
    p1.Iterations = 1;
    auto r1 = Geometry::CatmullClark::Subdivide(input, output1, p1);
    ASSERT_TRUE(r1.has_value());
    double var1 = computeRadiusVariance(output1);

    // Variance after 2 iterations
    Geometry::Halfedge::Mesh output2;
    Geometry::CatmullClark::SubdivisionParams p2;
    p2.Iterations = 2;
    auto r2 = Geometry::CatmullClark::Subdivide(input, output2, p2);
    ASSERT_TRUE(r2.has_value());
    double var2 = computeRadiusVariance(output2);

    // Variance should decrease with more iterations
    EXPECT_LT(var2, var1)
        << "Radius variance should decrease: iter1=" << var1 << " iter2=" << var2;
}

TEST(CatmullClark, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh input;
    Geometry::Halfedge::Mesh output;

    auto result = Geometry::CatmullClark::Subdivide(input, output);
    EXPECT_FALSE(result.has_value());
}

TEST(CatmullClark, ZeroIterationsReturnsNullopt)
{
    auto input = MakeTetrahedron();
    Geometry::Halfedge::Mesh output;

    Geometry::CatmullClark::SubdivisionParams params;
    params.Iterations = 0;
    auto result = Geometry::CatmullClark::Subdivide(input, output, params);
    EXPECT_FALSE(result.has_value());
}

TEST(CatmullClark, VertexCountFormula)
{
    // After one CC iteration:
    // V_new = V_old + E_old + F_old
    auto input = MakeTetrahedron();
    std::size_t Vold = input.VertexCount();
    std::size_t Eold = input.EdgeCount();
    std::size_t Fold = input.FaceCount();

    Geometry::Halfedge::Mesh output;
    auto result = Geometry::CatmullClark::Subdivide(input, output);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->FinalVertexCount, Vold + Eold + Fold)
        << "V=" << Vold << " E=" << Eold << " F=" << Fold;
}
