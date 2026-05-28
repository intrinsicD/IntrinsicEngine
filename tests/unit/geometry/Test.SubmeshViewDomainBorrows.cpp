// tests/unit/geometry/Test.SubmeshViewDomainBorrows.cpp
// GEOM-012 Slice A: mesh-backed graph borrow adaptor contract.
// Covers shared property identity, deletion-counter sharing, absence of
// compatibility-copy properties, position-write propagation in both
// directions, and face-state isolation (the borrow's no-topology-mutation
// contract excludes face storage from the view, so the source mesh's face
// PropertySet and FacesSize/DeletedFaceCount remain untouched).

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{

using Geometry::DomainViews::BorrowMeshAsGraphReadOnly;

TEST(SubmeshViewDomainBorrows, MeshAsGraphSharesVertexAndConnectivityPropertyStorage)
{
    auto mesh = MakeSingleTriangle();

    const auto meshPoint = mesh.VertexProperties().Get<glm::vec3>("v:point");
    const auto meshVertexConn =
        mesh.VertexProperties().Get<Geometry::Graph::VertexConnectivity>("v:connectivity");
    const auto meshHalfedgeConn =
        mesh.HalfedgeProperties().Get<Geometry::Graph::HalfedgeConnectivity>("h:connectivity");
    ASSERT_TRUE(meshPoint.IsValid());
    ASSERT_TRUE(meshVertexConn.IsValid());
    ASSERT_TRUE(meshHalfedgeConn.IsValid());

    Geometry::Graph::Graph view = BorrowMeshAsGraphReadOnly(mesh);

    const auto graphPoint = view.VertexProperties().Get<glm::vec3>("v:point");
    const auto graphVertexConn =
        view.VertexProperties().Get<Geometry::Graph::VertexConnectivity>("v:connectivity");
    const auto graphHalfedgeConn =
        view.HalfedgeProperties().Get<Geometry::Graph::HalfedgeConnectivity>("h:connectivity");
    ASSERT_TRUE(graphPoint.IsValid());
    ASSERT_TRUE(graphVertexConn.IsValid());
    ASSERT_TRUE(graphHalfedgeConn.IsValid());

    EXPECT_EQ(meshPoint.Handle().Id(), graphPoint.Handle().Id());
    EXPECT_EQ(meshVertexConn.Handle().Id(), graphVertexConn.Handle().Id());
    EXPECT_EQ(meshHalfedgeConn.Handle().Id(), graphHalfedgeConn.Handle().Id());
}

TEST(SubmeshViewDomainBorrows, MeshAsGraphDoesNotAllocateCompatibilityCopyProperties)
{
    auto mesh = MakeSingleTriangle();
    Geometry::Graph::Graph view = BorrowMeshAsGraphReadOnly(mesh);

    // No `*_graph_*` shadow slots should appear on the mesh property sets.
    EXPECT_FALSE(mesh.VertexProperties().Exists("v:graph_connectivity"));
    EXPECT_FALSE(mesh.HalfedgeProperties().Exists("h:graph_connectivity"));
    EXPECT_FALSE(mesh.VertexProperties().Exists("v:graph_point"));

    // Symmetric assertion through the view's accessor.
    EXPECT_FALSE(view.VertexProperties().Exists("v:graph_connectivity"));
    EXPECT_FALSE(view.HalfedgeProperties().Exists("h:graph_connectivity"));
}

TEST(SubmeshViewDomainBorrows, MeshAsGraphSharesSizesAndIgnoresFaceState)
{
    auto mesh = MakeTwoTriangleSquare();
    const std::size_t facesSizeBefore = mesh.FacesSize();
    const std::size_t deletedFacesBefore = mesh.DeletedFaceCount();
    ASSERT_GT(facesSizeBefore, 0u);

    Geometry::Graph::Graph view = BorrowMeshAsGraphReadOnly(mesh);

    EXPECT_EQ(view.VerticesSize(), mesh.VerticesSize());
    EXPECT_EQ(view.HalfedgesSize(), mesh.HalfedgesSize());
    EXPECT_EQ(view.EdgesSize(), mesh.EdgesSize());
    EXPECT_EQ(view.VertexCount(), mesh.VerticesSize() - mesh.DeletedVertexCount());
    EXPECT_EQ(view.EdgeCount(), mesh.EdgesSize() - mesh.DeletedEdgeCount());

    // The borrow exposes vertex/halfedge/edge storage only. The mesh's face
    // property set and face deletion counter must remain untouched — that is
    // the safety boundary that makes the borrow valid on face-bearing meshes.
    EXPECT_EQ(mesh.FacesSize(), facesSizeBefore);
    EXPECT_EQ(mesh.DeletedFaceCount(), deletedFacesBefore);
    EXPECT_TRUE(mesh.FaceProperties().Exists("f:connectivity"));
    EXPECT_TRUE(mesh.HalfedgeProperties().Exists("h:face"));
}

TEST(SubmeshViewDomainBorrows, MeshSidePositionEditIsVisibleThroughGraphView)
{
    auto mesh = MakeSingleTriangle();
    Geometry::Graph::Graph view = BorrowMeshAsGraphReadOnly(mesh);

    const Geometry::VertexHandle v0{0};
    ASSERT_TRUE(mesh.IsValid(v0));
    ASSERT_TRUE(view.IsValid(v0));

    const glm::vec3 newPosition{5.0f, -1.0f, 3.0f};
    mesh.Position(v0) = newPosition;

    EXPECT_EQ(view.VertexPosition(v0), newPosition);
}

TEST(SubmeshViewDomainBorrows, GraphSidePositionEditIsVisibleOnSourceMesh)
{
    // Position writes through the borrowed graph do not change topology, so
    // they are explicitly allowed even when the source mesh has faces — face
    // incidence references vertex indices, not positions.
    auto mesh = MakeSingleTriangle();
    Geometry::Graph::Graph view = BorrowMeshAsGraphReadOnly(mesh);

    const Geometry::VertexHandle v0{0};
    ASSERT_TRUE(view.IsValid(v0));

    const glm::vec3 newPosition{-2.0f, 4.0f, 0.5f};
    view.SetVertexPosition(v0, newPosition);

    EXPECT_EQ(mesh.Position(v0), newPosition);
    // Face state untouched.
    EXPECT_EQ(mesh.FacesSize(), 1u);
    EXPECT_EQ(mesh.DeletedFaceCount(), 0u);
}

TEST(SubmeshViewDomainBorrows, EmptyMeshBorrowsAsEmptyGraph)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    Geometry::Graph::Graph view = BorrowMeshAsGraphReadOnly(mesh);

    EXPECT_EQ(view.VerticesSize(), 0u);
    EXPECT_EQ(view.HalfedgesSize(), 0u);
    EXPECT_EQ(view.EdgesSize(), 0u);
    EXPECT_EQ(view.VertexCount(), 0u);
    EXPECT_EQ(view.EdgeCount(), 0u);
    EXPECT_EQ(mesh.FacesSize(), 0u);
}

} // namespace
