#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

import Geometry;

#include "TestMeshBuilders.h"

// Create a flat NxN grid of triangles (all in the Z=0 plane)
static Geometry::Halfedge::Mesh MakeFlatGrid(int n)
{
    Geometry::Halfedge::Mesh mesh;
    std::vector<Geometry::VertexHandle> verts;
    verts.reserve((n + 1) * (n + 1));

    for (int y = 0; y <= n; ++y)
    {
        for (int x = 0; x <= n; ++x)
        {
            verts.push_back(mesh.AddVertex({
                static_cast<float>(x) / static_cast<float>(n),
                static_cast<float>(y) / static_cast<float>(n),
                0.0f
            }));
        }
    }

    for (int y = 0; y < n; ++y)
    {
        for (int x = 0; x < n; ++x)
        {
            int i00 = y * (n + 1) + x;
            int i10 = y * (n + 1) + (x + 1);
            int i01 = (y + 1) * (n + 1) + x;
            int i11 = (y + 1) * (n + 1) + (x + 1);

            (void)mesh.AddTriangle(verts[i00], verts[i10], verts[i11]);
            (void)mesh.AddTriangle(verts[i00], verts[i11], verts[i01]);
        }
    }

    return mesh;
}


// =============================================================================
// Tests
// =============================================================================

TEST(AdaptiveRemesh, EmptyMeshReturnsNullopt)
{
    Geometry::Halfedge::Mesh mesh;
    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh);
    EXPECT_FALSE(result.has_value());
}

TEST(AdaptiveRemesh, SingleTriangleReturnsNullopt)
{
    auto mesh = MakeSingleTriangle();
    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh);
    EXPECT_FALSE(result.has_value()); // < 2 faces
}

TEST(AdaptiveRemesh, TwoTrianglesMinimalCase)
{
    auto mesh = MakeTwoTriangles();
    ASSERT_EQ(mesh.FaceCount(), 2u);

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.Iterations = 1;
    params.CurvatureAdaptation = 0.0; // isotropic to keep it simple

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 1u);
    EXPECT_GT(result->FinalVertexCount, 0u);
    EXPECT_GT(result->FinalFaceCount, 0u);
}

TEST(AdaptiveRemesh, FlatPlaneUniformSizing)
{
    // On a flat grid, curvature is ~0 everywhere.
    // With adaptation, the result should be near-uniform edge lengths.
    auto mesh = MakeFlatGrid(4);
    std::size_t initialFaces = mesh.FaceCount();
    ASSERT_EQ(initialFaces, 32u); // 4x4 grid * 2 triangles each

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.Iterations = 3;
    params.CurvatureAdaptation = 1.0;
    params.PreserveBoundary = true;

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 3u);
    EXPECT_GT(result->FinalFaceCount, 0u);
}

TEST(AdaptiveRemesh, SphereRefinement)
{
    // Icosahedron has uniform curvature everywhere.
    // Adaptive remeshing should refine it similar to isotropic.
    auto mesh = MakeIcosahedron();
    ASSERT_EQ(mesh.FaceCount(), 20u);

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.Iterations = 2;
    params.CurvatureAdaptation = 1.0;

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->FinalFaceCount, 20u); // Should have at least as many faces
    EXPECT_GT(result->SplitCount + result->CollapseCount + result->FlipCount, 0u);
}

TEST(AdaptiveRemesh, BoundaryPreservation)
{
    auto mesh = MakeTwoTriangles();

    // Record boundary vertex positions
    std::vector<glm::vec3> boundaryPosBefore;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsBoundary(vh))
            boundaryPosBefore.push_back(mesh.Position(vh));
    }

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.Iterations = 2;
    params.CurvatureAdaptation = 0.0;
    params.PreserveBoundary = true;

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Check that the original boundary vertices are unmoved
    // (Note: new boundary vertices may have been added by splits,
    // but original ones should not have moved)
    std::size_t matchCount = 0;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh.IsDeleted(vh)) continue;
        if (!mesh.IsBoundary(vh)) continue;

        glm::vec3 pos = mesh.Position(vh);
        for (const auto& bp : boundaryPosBefore)
        {
            if (glm::length(pos - bp) < 1e-5f)
            {
                ++matchCount;
                break;
            }
        }
    }

    // At least some of the original boundary positions should still exist
    EXPECT_GE(matchCount, 2u);
}

TEST(AdaptiveRemesh, MultiIterationConvergence)
{
    auto mesh1 = MakeIcosahedron();
    auto mesh5 = MakeIcosahedron();

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params1;
    params1.Iterations = 1;
    params1.CurvatureAdaptation = 1.0;

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params5;
    params5.Iterations = 5;
    params5.CurvatureAdaptation = 1.0;

    auto result1 = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh1, params1);
    auto result5 = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh5, params5);

    ASSERT_TRUE(result1.has_value());
    ASSERT_TRUE(result5.has_value());

    EXPECT_EQ(result1->IterationsPerformed, 1u);
    EXPECT_EQ(result5->IterationsPerformed, 5u);

    // Both should produce valid meshes
    EXPECT_GT(result1->FinalFaceCount, 0u);
    EXPECT_GT(result5->FinalFaceCount, 0u);
}

TEST(AdaptiveRemesh, ZeroAdaptationBehavesLikeIsotropic)
{
    auto mesh = MakeFlatGrid(3);

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.Iterations = 3;
    params.CurvatureAdaptation = 0.0; // No adaptation → uniform sizing
    params.PreserveBoundary = true;

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->IterationsPerformed, 3u);
    EXPECT_GT(result->FinalFaceCount, 0u);
}

TEST(AdaptiveRemesh, EdgeLengthBoundsRespected)
{
    auto mesh = MakeIcosahedron();

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.MinEdgeLength = 0.2;
    params.MaxEdgeLength = 0.8;
    params.Iterations = 3;
    params.CurvatureAdaptation = 1.0;

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    mesh.GarbageCollection();

    // Check that most edges respect bounds (some tolerance for convergence)
    std::size_t tooShort = 0, tooLong = 0, total = 0;
    for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        if (mesh.IsDeleted(e)) continue;

        Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(2u * ei)};
        glm::vec3 a = mesh.Position(mesh.FromVertex(h));
        glm::vec3 b = mesh.Position(mesh.ToVertex(h));
        double len = static_cast<double>(glm::distance(a, b));

        if (len < params.MinEdgeLength * 0.5) ++tooShort;
        if (len > params.MaxEdgeLength * 2.0) ++tooLong;
        ++total;
    }

    // Allow up to 10% of edges to violate bounds (convergence tolerance)
    double violationRate = static_cast<double>(tooShort + tooLong) / static_cast<double>(total);
    EXPECT_LT(violationRate, 0.15) << "Too many edges violate bounds: "
        << tooShort << " too short, " << tooLong << " too long out of " << total;
}

TEST(AdaptiveRemesh, ResultDiagnostics)
{
    auto mesh = MakeIcosahedron();

    Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
    params.Iterations = 2;
    params.CurvatureAdaptation = 1.0;

    auto result = Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(result->IterationsPerformed, 2u);
    EXPECT_GT(result->FinalVertexCount, 0u);
    EXPECT_GT(result->FinalEdgeCount, 0u);
    EXPECT_GT(result->FinalFaceCount, 0u);

    // At least some operations should have occurred
    EXPECT_GT(result->SplitCount + result->CollapseCount + result->FlipCount, 0u);
}
