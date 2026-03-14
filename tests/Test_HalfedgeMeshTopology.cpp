#include <gtest/gtest.h>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "TestMeshBuilders.h"

// =============================================================================
// Mesh Construction — AddVertex / AddTriangle / AddFace
// =============================================================================

TEST(HalfedgeMesh_Topology, EmptyMesh_HasZeroCounts)
{
    Geometry::Halfedge::Mesh mesh;
    EXPECT_EQ(mesh.VertexCount(), 0u);
    EXPECT_EQ(mesh.EdgeCount(), 0u);
    EXPECT_EQ(mesh.FaceCount(), 0u);
    EXPECT_EQ(mesh.HalfedgeCount(), 0u);
    EXPECT_TRUE(mesh.IsEmpty());
}

TEST(HalfedgeMesh_Topology, AddVertex_IncreasesCount)
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0, 0, 0});
    EXPECT_EQ(mesh.VertexCount(), 1u);
    EXPECT_EQ(mesh.Position(v0), glm::vec3(0, 0, 0));

    auto v1 = mesh.AddVertex({1, 2, 3});
    EXPECT_EQ(mesh.VertexCount(), 2u);
    EXPECT_EQ(mesh.Position(v1), glm::vec3(1, 2, 3));
}

TEST(HalfedgeMesh_Topology, SingleTriangle_Counts)
{
    auto mesh = MakeSingleTriangle();
    EXPECT_EQ(mesh.VertexCount(), 3u);
    EXPECT_EQ(mesh.FaceCount(), 1u);
    EXPECT_EQ(mesh.EdgeCount(), 3u);
    EXPECT_EQ(mesh.HalfedgeCount(), 6u); // 3 edges × 2
}

TEST(HalfedgeMesh_Topology, TwoTriangles_SharedEdge)
{
    auto mesh = MakeTwoTriangleSquare();
    EXPECT_EQ(mesh.VertexCount(), 4u);
    EXPECT_EQ(mesh.FaceCount(), 2u);
    EXPECT_EQ(mesh.EdgeCount(), 5u); // 4 boundary + 1 shared
}

TEST(HalfedgeMesh_Topology, Tetrahedron_ClosedManifold)
{
    auto mesh = MakeTetrahedron();
    EXPECT_EQ(mesh.VertexCount(), 4u);
    EXPECT_EQ(mesh.FaceCount(), 4u);
    EXPECT_EQ(mesh.EdgeCount(), 6u);

    // Euler formula: V - E + F = 2 for closed manifold
    int euler = static_cast<int>(mesh.VertexCount())
              - static_cast<int>(mesh.EdgeCount())
              + static_cast<int>(mesh.FaceCount());
    EXPECT_EQ(euler, 2);
}

TEST(HalfedgeMesh_Topology, Icosahedron_ClosedManifold)
{
    auto mesh = MakeIcosahedron();
    EXPECT_EQ(mesh.VertexCount(), 12u);
    EXPECT_EQ(mesh.FaceCount(), 20u);
    EXPECT_EQ(mesh.EdgeCount(), 30u);

    int euler = static_cast<int>(mesh.VertexCount())
              - static_cast<int>(mesh.EdgeCount())
              + static_cast<int>(mesh.FaceCount());
    EXPECT_EQ(euler, 2);
}

TEST(HalfedgeMesh_Topology, Cube_ClosedManifold)
{
    auto mesh = MakeCube();
    EXPECT_EQ(mesh.VertexCount(), 8u);
    EXPECT_EQ(mesh.FaceCount(), 12u);
    EXPECT_EQ(mesh.EdgeCount(), 18u);

    int euler = static_cast<int>(mesh.VertexCount())
              - static_cast<int>(mesh.EdgeCount())
              + static_cast<int>(mesh.FaceCount());
    EXPECT_EQ(euler, 2);
}

TEST(HalfedgeMesh_Topology, QuadPair_Counts)
{
    auto mesh = MakeQuadPair();
    EXPECT_EQ(mesh.VertexCount(), 6u);
    EXPECT_EQ(mesh.FaceCount(), 2u);
}

// =============================================================================
// Boundary Detection
// =============================================================================

TEST(HalfedgeMesh_Topology, SingleTriangle_AllVerticesBoundary)
{
    auto mesh = MakeSingleTriangle();
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_TRUE(mesh.IsBoundary(v)) << "Vertex " << i << " should be boundary";
    }
}

TEST(HalfedgeMesh_Topology, Tetrahedron_NoBoundaryVertices)
{
    auto mesh = MakeTetrahedron();
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_FALSE(mesh.IsBoundary(v)) << "Vertex " << i << " should not be boundary";
    }
}

TEST(HalfedgeMesh_Topology, Tetrahedron_NoBoundaryEdges)
{
    auto mesh = MakeTetrahedron();
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(e)) continue;
        EXPECT_FALSE(mesh.IsBoundary(e)) << "Edge " << i << " should not be boundary";
    }
}

// =============================================================================
// Valence
// =============================================================================

TEST(HalfedgeMesh_Topology, SingleTriangle_AllValence2)
{
    auto mesh = MakeSingleTriangle();
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_EQ(mesh.Valence(v), 2u) << "Boundary triangle vertex should have valence 2";
    }
}

TEST(HalfedgeMesh_Topology, Tetrahedron_AllValence3)
{
    auto mesh = MakeTetrahedron();
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_EQ(mesh.Valence(v), 3u) << "Tetrahedron vertex should have valence 3";
    }
}

TEST(HalfedgeMesh_Topology, SubdividedTriangle_InteriorVertexValence)
{
    auto mesh = MakeSubdividedTriangle();
    // v3 (index 3) is the interior midpoint vertex — should have higher valence
    // than boundary vertices in a subdivided triangle
    Geometry::VertexHandle v3{3};
    EXPECT_GT(mesh.Valence(v3), 2u);
}

// =============================================================================
// Face Deletion and Garbage Collection
// =============================================================================

TEST(HalfedgeMesh_Topology, DeleteFace_ReducesFaceCount)
{
    auto mesh = MakeTwoTriangleSquare();
    ASSERT_EQ(mesh.FaceCount(), 2u);

    mesh.DeleteFace(Geometry::FaceHandle{0});
    EXPECT_EQ(mesh.FaceCount(), 1u);
    EXPECT_TRUE(mesh.HasGarbage());
}

TEST(HalfedgeMesh_Topology, DeleteAllFaces_EmptyAfterGC)
{
    auto mesh = MakeTwoTriangleSquare();
    mesh.DeleteFace(Geometry::FaceHandle{0});
    mesh.DeleteFace(Geometry::FaceHandle{1});
    EXPECT_EQ(mesh.FaceCount(), 0u);

    mesh.GarbageCollection();
    EXPECT_FALSE(mesh.HasGarbage());
    EXPECT_EQ(mesh.FaceCount(), 0u);
}

TEST(HalfedgeMesh_Topology, GarbageCollection_CompactsStorage)
{
    auto mesh = MakeTwoTriangleSquare();
    std::size_t faceSizeBefore = mesh.FacesSize();

    mesh.DeleteFace(Geometry::FaceHandle{0});
    EXPECT_EQ(mesh.FacesSize(), faceSizeBefore); // storage not compacted yet

    mesh.GarbageCollection();
    EXPECT_LT(mesh.FacesSize(), faceSizeBefore); // now compacted
    EXPECT_EQ(mesh.FacesSize(), mesh.FaceCount());
}

// =============================================================================
// Position Access
// =============================================================================

TEST(HalfedgeMesh_Topology, SetPosition_UpdatesCorrectly)
{
    Geometry::Halfedge::Mesh mesh;
    auto v = mesh.AddVertex({0, 0, 0});
    EXPECT_EQ(mesh.Position(v), glm::vec3(0, 0, 0));

    mesh.Position(v) = glm::vec3(5, 10, 15);
    EXPECT_EQ(mesh.Position(v), glm::vec3(5, 10, 15));
}

TEST(HalfedgeMesh_Topology, ConstMesh_PositionIsReadOnly)
{
    const auto mesh = MakeSingleTriangle();
    // This should compile — const position access
    glm::vec3 pos = mesh.Position(Geometry::VertexHandle{0});
    EXPECT_EQ(pos, glm::vec3(0, 0, 0));
}

// =============================================================================
// Halfedge Connectivity
// =============================================================================

TEST(HalfedgeMesh_Topology, OppositeHalfedge_IsSymmetric)
{
    auto mesh = MakeSingleTriangle();
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(h)) continue;
        auto opp = mesh.OppositeHalfedge(h);
        EXPECT_EQ(mesh.OppositeHalfedge(opp), h) << "Opposite should be symmetric for halfedge " << i;
    }
}

TEST(HalfedgeMesh_Topology, NextHalfedge_CyclesBackToStart)
{
    auto mesh = MakeSingleTriangle();
    Geometry::FaceHandle f{0};
    auto start = mesh.Halfedge(f);
    auto current = start;
    int count = 0;
    do
    {
        current = mesh.NextHalfedge(current);
        ++count;
        ASSERT_LE(count, 100) << "Infinite loop in NextHalfedge cycle";
    } while (current != start);

    EXPECT_EQ(count, 3); // triangle has 3 halfedges per face
}

TEST(HalfedgeMesh_Topology, FromToVertex_Consistency)
{
    auto mesh = MakeSingleTriangle();
    for (std::size_t i = 0; i < mesh.HalfedgesSize(); ++i)
    {
        Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(h)) continue;

        auto from = mesh.FromVertex(h);
        auto to = mesh.ToVertex(h);
        EXPECT_NE(from, to) << "FromVertex and ToVertex should differ for halfedge " << i;

        // Opposite halfedge should swap from/to
        auto opp = mesh.OppositeHalfedge(h);
        EXPECT_EQ(mesh.FromVertex(opp), to);
        EXPECT_EQ(mesh.ToVertex(opp), from);
    }
}

// =============================================================================
// Edge ↔ Halfedge Relationship
// =============================================================================

TEST(HalfedgeMesh_Topology, EdgeHalfedge_RoundTrip)
{
    auto mesh = MakeTwoTriangleSquare();
    for (std::size_t i = 0; i < mesh.EdgesSize(); ++i)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(i)};
        if (mesh.IsDeleted(e)) continue;

        auto h0 = mesh.Halfedge(e, 0);
        auto h1 = mesh.Halfedge(e, 1);

        // The two halfedges of an edge should be opposites
        EXPECT_EQ(mesh.OppositeHalfedge(h0), h1);
        EXPECT_EQ(mesh.OppositeHalfedge(h1), h0);

        // Both halfedges should map back to the same edge
        EXPECT_EQ(mesh.Edge(h0), e);
        EXPECT_EQ(mesh.Edge(h1), e);
    }
}

// =============================================================================
// Isolated Vertex
// =============================================================================

TEST(HalfedgeMesh_Topology, IsolatedVertex_HasNoConnectivity)
{
    Geometry::Halfedge::Mesh mesh;
    auto v = mesh.AddVertex({1, 2, 3});
    EXPECT_TRUE(mesh.IsIsolated(v));
    EXPECT_EQ(mesh.Valence(v), 0u);
}

TEST(HalfedgeMesh_Topology, ConnectedVertex_IsNotIsolated)
{
    auto mesh = MakeSingleTriangle();
    for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
        EXPECT_FALSE(mesh.IsIsolated(v));
    }
}

// =============================================================================
// Alias Builders
// =============================================================================

TEST(HalfedgeMesh_Topology, MakeEquilateralTriangle_SameAsSingleTriangle)
{
    auto a = MakeSingleTriangle();
    auto b = MakeEquilateralTriangle();
    EXPECT_EQ(a.VertexCount(), b.VertexCount());
    EXPECT_EQ(a.FaceCount(), b.FaceCount());
    EXPECT_EQ(a.EdgeCount(), b.EdgeCount());
}

TEST(HalfedgeMesh_Topology, MakeTwoTriangles_SameAsTwoTriangleSquare)
{
    auto a = MakeTwoTriangleSquare();
    auto b = MakeTwoTriangles();
    EXPECT_EQ(a.VertexCount(), b.VertexCount());
    EXPECT_EQ(a.FaceCount(), b.FaceCount());
    EXPECT_EQ(a.EdgeCount(), b.EdgeCount());
}
