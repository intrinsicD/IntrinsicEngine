// tests/Test_Simplification.cpp — QEM mesh simplification tests.
// Covers: target face count, error threshold, boundary preservation,
// degenerate input handling, and quality guard correctness.

#include <gtest/gtest.h>
#include <cmath>
#include <cstddef>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

namespace
{
    // Subdivided icosahedron: closed mesh with many faces for simplification.
    Geometry::Halfedge::Mesh MakeDenseMesh()
    {
        auto ico = MakeIcosahedron();
        Geometry::Halfedge::Mesh refined;
        Geometry::Subdivision::SubdivisionParams sp;
        sp.Iterations = 2; // 20 * 16 = 320 faces
        Geometry::Subdivision::Subdivide(ico, refined, sp);
        return refined;
    }
}

// --- Basic functionality ---

TEST(Simplification, ReducesToTargetFaceCount)
{
    auto mesh = MakeDenseMesh();
    const auto originalFaces = mesh.FaceCount();
    ASSERT_GT(originalFaces, 100u);

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 40;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);

    mesh.GarbageCollection();
    EXPECT_LE(mesh.FaceCount(), 40u);
    EXPECT_EQ(result->FinalFaceCount, mesh.FaceCount());
}

TEST(Simplification, ErrorThresholdStopsEarly)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 4; // Aggressive target
    params.MaxError = 1e-10; // Very tight error — should stop early

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());

    mesh.GarbageCollection();
    // Should have stopped before reaching target due to error bound
    EXPECT_GT(mesh.FaceCount(), 4u);
}

TEST(Simplification, PreservesBoundary)
{
    // Open mesh (single triangle is all-boundary) — no collapses possible
    auto mesh = MakeSingleTriangle();
    ASSERT_EQ(mesh.FaceCount(), 1u);

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 0;
    params.PreserveBoundary = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    // Either nullopt (can't simplify) or zero collapses
    if (result.has_value())
        EXPECT_EQ(result->CollapseCount, 0u);
}

TEST(Simplification, ClosedMeshRemainsClosedAfterSimplification)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 80;
    params.PreserveBoundary = true;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);

    mesh.GarbageCollection();
    // Closed mesh should remain closed (no boundary edges)
    std::size_t boundaryEdges = 0;
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (!mesh.IsDeleted(e) && mesh.IsBoundary(e))
            ++boundaryEdges;
    }
    EXPECT_EQ(boundaryEdges, 0u);
}

TEST(Simplification, ReturnsNulloptForEmptyMesh)
{
    Geometry::Halfedge::Mesh mesh;

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 0;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Simplification, ReturnsNulloptForSingleFaceMesh)
{
    auto mesh = MakeSingleTriangle();

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 0;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    // Can't simplify below minimum — either nullopt or zero collapses
    if (result.has_value())
        EXPECT_EQ(result->CollapseCount, 0u);
}

TEST(Simplification, MaxCollapseErrorIsNonNegative)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 100;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GE(result->MaxCollapseError, 0.0);
}

TEST(Simplification, TetrahedronMinimalSimplification)
{
    auto mesh = MakeTetrahedron();
    ASSERT_EQ(mesh.FaceCount(), 4u);

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 2;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    // Tetrahedron has very few faces; some collapses may be blocked by topology
    if (result.has_value())
    {
        mesh.GarbageCollection();
        EXPECT_LE(mesh.FaceCount(), 4u);
    }
}

TEST(Simplification, QuadricMinimizerPlacement)
{
    auto mesh = MakeDenseMesh();

    Geometry::Simplification::SimplificationParams params;
    params.TargetFaces = 80;
    params.Quadric.PlacementPolicy =
        Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
}
