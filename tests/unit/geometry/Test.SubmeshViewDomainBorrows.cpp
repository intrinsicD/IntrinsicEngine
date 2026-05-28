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

using Geometry::DomainViews::BorrowMeshAsCloud;
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

// ---------------------------------------------------------------------------
// GEOM-012 Slice B: mesh-backed point-cloud borrow adaptor contract.
// ---------------------------------------------------------------------------

TEST(SubmeshViewDomainBorrows, MeshAsCloudSharesVertexPositionPropertyStorage)
{
    auto mesh = MakeSingleTriangle();

    const auto meshPoint = mesh.VertexProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(meshPoint.IsValid());

    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    const auto cloudPoint = view.PointProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(cloudPoint.IsValid());

    EXPECT_EQ(meshPoint.Handle().Id(), cloudPoint.Handle().Id());
    EXPECT_EQ(view.VerticesSize(), mesh.VerticesSize());
}

TEST(SubmeshViewDomainBorrows, MeshAsCloudDoesNotAllocatePPositionCompatibilityCopy)
{
    auto mesh = MakeSingleTriangle();
    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    // The borrow must reuse `v:point`; no legacy `p:position` slot should be
    // allocated on the shared vertex `PropertySet`.
    EXPECT_FALSE(mesh.VertexProperties().Exists("p:position"));
    EXPECT_FALSE(view.PointProperties().Exists("p:position"));
}

TEST(SubmeshViewDomainBorrows, MeshAsCloudReusesExistingVertexNormalProperty)
{
    auto mesh = MakeSingleTriangle();

    // Seed a mesh-side normal attribute under the canonical `v:normal` name
    // before borrowing, then write a sentinel value through a typed
    // `VertexProperty<glm::vec3>` wrapper.
    Geometry::VertexProperty<glm::vec3> meshNormal(
        mesh.VertexProperties().GetOrAdd<glm::vec3>("v:normal", glm::vec3(0.0f, 1.0f, 0.0f)));
    ASSERT_TRUE(meshNormal.IsValid());
    const Geometry::VertexHandle v0{0};
    meshNormal[v0] = glm::vec3(0.0f, 0.0f, 1.0f);

    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    const auto cloudNormal = view.GetVertexProperty<glm::vec3>("v:normal");
    ASSERT_TRUE(cloudNormal.IsValid());
    EXPECT_EQ(meshNormal.Handle().Id(), cloudNormal.Handle().Id());
    EXPECT_EQ(cloudNormal[v0.Index], glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(SubmeshViewDomainBorrows, MeshSidePositionEditIsVisibleThroughCloudView)
{
    auto mesh = MakeSingleTriangle();
    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    const Geometry::VertexHandle v0{0};
    ASSERT_TRUE(mesh.IsValid(v0));
    ASSERT_TRUE(view.IsValid(v0));

    const glm::vec3 newPosition{7.0f, -3.0f, 2.0f};
    mesh.Position(v0) = newPosition;

    EXPECT_EQ(view.Position(v0), newPosition);
}

TEST(SubmeshViewDomainBorrows, CloudSidePositionEditIsVisibleOnSourceMesh)
{
    auto mesh = MakeSingleTriangle();
    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    const Geometry::VertexHandle v0{0};
    ASSERT_TRUE(view.IsValid(v0));

    const glm::vec3 newPosition{-4.0f, 6.0f, 1.5f};
    view.Position(v0) = newPosition;

    EXPECT_EQ(mesh.Position(v0), newPosition);
    // Face state untouched by a position write.
    EXPECT_EQ(mesh.FacesSize(), 1u);
    EXPECT_EQ(mesh.DeletedFaceCount(), 0u);
}

TEST(SubmeshViewDomainBorrows, PointAddedThroughCloudViewAppearsOnSourceMesh)
{
    auto mesh = MakeSingleTriangle();
    const std::size_t verticesBefore = mesh.VerticesSize();
    const std::size_t facesBefore = mesh.FacesSize();

    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    const glm::vec3 newPosition{2.5f, 2.5f, 2.5f};
    const Geometry::VertexHandle vNew = view.AddPoint(newPosition);

    // The new point is appended to the shared vertex `PropertySet`, so the
    // source mesh observes one additional vertex slot.
    EXPECT_EQ(mesh.VerticesSize(), verticesBefore + 1u);
    EXPECT_EQ(view.VerticesSize(), verticesBefore + 1u);
    EXPECT_EQ(mesh.Position(vNew), newPosition);
    EXPECT_EQ(view.Position(vNew), newPosition);

    // Topology-only state on the source mesh is unaffected by a vertex append.
    EXPECT_EQ(mesh.FacesSize(), facesBefore);
    EXPECT_EQ(mesh.DeletedFaceCount(), 0u);
    EXPECT_EQ(mesh.DeletedVertexCount(), 0u);
}

TEST(SubmeshViewDomainBorrows, EmptyMeshBorrowsAsEmptyCloud)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    Geometry::PointCloud::Cloud view = BorrowMeshAsCloud(mesh);

    EXPECT_EQ(view.VerticesSize(), 0u);
    EXPECT_EQ(view.VertexCount(), 0u);
    EXPECT_TRUE(view.IsEmpty());

    // The borrow must wire `v:point` on the shared `PropertySet` without
    // forcing any legacy `p:position` slot or face-domain allocations.
    EXPECT_TRUE(mesh.VertexProperties().Exists("v:point"));
    EXPECT_FALSE(mesh.VertexProperties().Exists("p:position"));
    EXPECT_EQ(mesh.FacesSize(), 0u);
}

TEST(SubmeshViewDomainBorrows, CloudSubviewOfMeshBorrowSeesMeshBackedRows)
{
    // `Cloud::CreateView` is documented to return a subrange view over the
    // source cloud's vertex storage. For a mesh-backed borrow, the bound
    // storage is the mesh's vertex `PropertySet`, not the cloud's own
    // (empty) owning `Properties`; the subview must see the mesh's rows.
    auto mesh = MakeTwoTriangleSquare();
    ASSERT_GE(mesh.VerticesSize(), 4u);

    Geometry::PointCloud::Cloud borrowed = BorrowMeshAsCloud(mesh);
    ASSERT_EQ(borrowed.VerticesSize(), mesh.VerticesSize());

    Geometry::PointCloud::Cloud sub =
        Geometry::PointCloud::Cloud::CreateView(borrowed,
                                                Geometry::ElementRange{1u, 2u});

    EXPECT_TRUE(sub.IsSubmeshView());
    EXPECT_EQ(sub.VertexRange().Offset, 1u);
    EXPECT_EQ(sub.VertexRange().Size, 2u);
    EXPECT_EQ(sub.Positions().size(), 2u);

    const Geometry::VertexHandle v1{1};
    const Geometry::VertexHandle v2{2};
    EXPECT_EQ(sub.Position(v1), mesh.Position(v1));
    EXPECT_EQ(sub.Position(v2), mesh.Position(v2));
}

TEST(SubmeshViewDomainBorrows, CloudSubviewOfEmptyMeshBorrowClampsToZero)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    Geometry::PointCloud::Cloud borrowed = BorrowMeshAsCloud(mesh);

    Geometry::PointCloud::Cloud sub =
        Geometry::PointCloud::Cloud::CreateView(borrowed,
                                                Geometry::ElementRange{0u, 8u});

    EXPECT_TRUE(sub.IsSubmeshView());
    EXPECT_EQ(sub.VertexRange().Offset, 0u);
    EXPECT_EQ(sub.VertexRange().Size, 0u);
    EXPECT_TRUE(sub.IsEmpty());
}

TEST(SubmeshViewDomainBorrows, CloudDeleteThroughMeshBorrowDoesNotTouchMeshDeletionCounter)
{
    // Cloud and mesh use independent deletion markers (`p:deleted` vs
    // `v:deleted`); cloud-side deletes must not corrupt the mesh's deletion
    // counter or the mesh's view of which vertices are alive.
    auto mesh = MakeTwoTriangleSquare();
    const std::size_t verticesBefore = mesh.VerticesSize();
    const std::size_t meshDeletedBefore = mesh.DeletedVertexCount();
    ASSERT_GE(verticesBefore, 4u);

    Geometry::PointCloud::Cloud borrowed = BorrowMeshAsCloud(mesh);
    EXPECT_EQ(borrowed.VertexCount(), verticesBefore - meshDeletedBefore);

    const Geometry::VertexHandle v0{0};
    borrowed.DeletePoint(v0);

    // Cloud sees its own deleted point.
    EXPECT_TRUE(borrowed.IsDeleted(v0));
    EXPECT_EQ(borrowed.VertexCount(), verticesBefore - meshDeletedBefore - 1u);

    // Mesh state is unchanged: deletion counter untouched, IsDeleted is
    // false, no mesh-side garbage reported.
    EXPECT_EQ(mesh.DeletedVertexCount(), meshDeletedBefore);
    EXPECT_EQ(mesh.VerticesSize(), verticesBefore);
    EXPECT_FALSE(mesh.IsDeleted(v0));
    EXPECT_FALSE(mesh.HasGarbage());
}

TEST(SubmeshViewDomainBorrows, MeshGarbageCollectionAfterCloudDeleteIsConsistent)
{
    // After a cloud-side delete the mesh must still be safe to mutate
    // topology and garbage-collect on its own terms — i.e. cloud-side
    // `p:deleted` markers must not interfere with the mesh's `v:deleted`
    // / topology-aware GC pass.
    auto mesh = MakeTwoTriangleSquare();
    const std::size_t verticesBefore = mesh.VerticesSize();

    Geometry::PointCloud::Cloud borrowed = BorrowMeshAsCloud(mesh);
    const Geometry::VertexHandle v0{0};
    borrowed.DeletePoint(v0);

    // Mesh-side state is unaffected.
    EXPECT_EQ(mesh.VerticesSize(), verticesBefore);
    EXPECT_EQ(mesh.DeletedVertexCount(), 0u);
    EXPECT_FALSE(mesh.HasGarbage());

    // A mesh GC pass is a no-op (nothing is deleted from the mesh's PoV).
    mesh.GarbageCollection();
    EXPECT_EQ(mesh.VerticesSize(), verticesBefore);
    EXPECT_EQ(mesh.DeletedVertexCount(), 0u);
}

} // namespace
