#include <gtest/gtest.h>
#include <cstddef>

import Geometry;

#include "Test_MeshBuilders.h"

// =============================================================================
// MeshBoundary Tests
// =============================================================================

TEST(MeshBoundary, EmptyMeshReturnsEmptyResult)
{
    Geometry::Halfedge::Mesh mesh;
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->BoundaryVertices.empty());
    EXPECT_TRUE(result->BoundaryEdges.empty());
    EXPECT_TRUE(result->BoundaryFaces.empty());
}

TEST(MeshBoundary, ClosedMeshHasNoBoundary)
{
    auto mesh = MakeTetrahedron();
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->BoundaryVertices.empty());
    EXPECT_TRUE(result->BoundaryEdges.empty());
    // Tetrahedron is closed — no boundary faces either
}

TEST(MeshBoundary, SingleTriangleHasBoundary)
{
    auto mesh = MakeSingleTriangle();
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Single triangle: all 3 vertices are on the boundary
    EXPECT_EQ(result->BoundaryVertices.size(), 3u);
    // All 3 edges are boundary edges
    EXPECT_EQ(result->BoundaryEdges.size(), 3u);
    // The single face has boundary edges, so it's a boundary face
    EXPECT_EQ(result->BoundaryFaces.size(), 1u);
}

TEST(MeshBoundary, TwoTriangleSquareBoundary)
{
    auto mesh = MakeTwoTriangleSquare();
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());

    // All 4 vertices are on the boundary
    EXPECT_EQ(result->BoundaryVertices.size(), 4u);
    // The shared interior edge is not a boundary edge. 4 boundary edges remain.
    EXPECT_EQ(result->BoundaryEdges.size(), 4u);
    // Both faces touch boundary edges
    EXPECT_EQ(result->BoundaryFaces.size(), 2u);
}

TEST(MeshBoundary, IcosahedronFullyClosed)
{
    auto mesh = MakeIcosahedron();
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_TRUE(result->BoundaryVertices.empty());
    EXPECT_TRUE(result->BoundaryEdges.empty());
    EXPECT_TRUE(result->BoundaryFaces.empty());
}

TEST(MeshBoundary, MarkBoundaryVerticesAsFeature)
{
    auto mesh = MakeSingleTriangle();
    Geometry::MeshBoundary::BoundaryParams params;
    params.MarkBoundaryVerticesAsFeature = true;

    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->BoundaryVertices.size(), 3u);

    // Verify that the feature property was written
    auto featureProp = mesh.VertexProperties().Get<bool>("vertex_feature");
    ASSERT_TRUE(featureProp.IsValid());
    for (const auto& v : result->BoundaryVertices)
    {
        EXPECT_TRUE(featureProp[v.Index]);
    }
}

TEST(MeshBoundary, MarkBoundaryEdgesAsFeature)
{
    auto mesh = MakeSingleTriangle();
    Geometry::MeshBoundary::BoundaryParams params;
    params.MarkBoundaryEdgeAsFeature = true;

    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->BoundaryEdges.size(), 3u);

    auto featureProp = mesh.EdgeProperties().Get<bool>("edge_feature");
    ASSERT_TRUE(featureProp.IsValid());
    for (const auto& e : result->BoundaryEdges)
    {
        EXPECT_TRUE(featureProp[e.Index]);
    }
}

TEST(MeshBoundary, BoundaryPropertyTracksCorrectly)
{
    auto mesh = MakeSingleTriangle();
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());

    // Verify per-vertex boundary property
    for (const auto& v : result->BoundaryVertices)
    {
        EXPECT_TRUE(result->IsBoundaryVertex[v]);
    }
    // Verify per-edge boundary property
    for (const auto& e : result->BoundaryEdges)
    {
        EXPECT_TRUE(result->IsBoundaryEdge[e]);
    }
}

TEST(MeshBoundary, CubeIsFullyClosed)
{
    auto mesh = MakeCube();
    Geometry::MeshBoundary::BoundaryParams params;
    auto result = Geometry::MeshBoundary::Boundary(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->BoundaryVertices.empty());
    EXPECT_TRUE(result->BoundaryEdges.empty());
    EXPECT_TRUE(result->BoundaryFaces.empty());
}
