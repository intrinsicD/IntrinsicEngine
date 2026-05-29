// tests/unit/geometry/Test.SubmeshViewDomainBorrows.cpp
// GEOM-012 domain-view borrow adaptor contracts:
//  - Slice A: mesh-backed graph borrow (shared property identity,
//    deletion-counter sharing, absence of compatibility-copy properties,
//    bidirectional position-write propagation, and face-state isolation).
//  - Slice B: mesh-backed point-cloud borrow (shared v:point, no p:position
//    copy, v:normal reuse, deletion-counter isolation, subview correctness).
//  - Slice C: graph-backed point-cloud borrow (shared v:point, no p:position
//    copy, halfedge/edge-PropertySet non-exposure, the shared v:connectivity
//    slot pinned as a documented UB boundary, v:normal reuse, deletion
//    isolation).
//  - Slice D: type-enforced read-only view types (ConstMeshBackedGraphView,
//    ConstMeshBackedCloudView, ConstGraphBackedCloudView) — compile-time proof
//    that mutation through the view is ill-formed, plus runtime proof that the
//    views share source storage and observe live edits.

#include <gtest/gtest.h>

#include <concepts>
#include <cstddef>
#include <type_traits>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{

using Geometry::DomainViews::BorrowGraphAsCloud;
using Geometry::DomainViews::BorrowMeshAsCloud;
using Geometry::DomainViews::BorrowMeshAsGraphReadOnly;
using Geometry::DomainViews::ConstGraphBackedCloudView;
using Geometry::DomainViews::ConstMeshBackedCloudView;
using Geometry::DomainViews::ConstMeshBackedGraphView;

// Build a small standalone graph (a single edge between two positioned
// vertices) for the graph-backed point-cloud borrow tests.
Geometry::Graph::Graph MakeTwoVertexEdgeGraph()
{
    Geometry::Graph::Graph graph;
    const Geometry::VertexHandle v0 = graph.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
    const Geometry::VertexHandle v1 = graph.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
    (void)graph.AddEdge(v0, v1);
    return graph;
}

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

// ---------------------------------------------------------------------------
// GEOM-012 Slice C: graph-backed point-cloud borrow adaptor contract.
// Covers shared `v:point` storage, absence of compatibility-copy properties,
// non-exposure of edge/halfedge data through the cloud surface, attribute
// reuse, bidirectional position-edit visibility, point-addition propagation,
// the empty-graph case, and deletion-counter isolation.
// ---------------------------------------------------------------------------

TEST(SubmeshViewDomainBorrows, GraphAsCloudSharesVertexPositionPropertyStorage)
{
    auto graph = MakeTwoVertexEdgeGraph();

    const auto graphPoint = graph.VertexProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(graphPoint.IsValid());

    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    const auto cloudPoint = view.PointProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(cloudPoint.IsValid());

    EXPECT_EQ(graphPoint.Handle().Id(), cloudPoint.Handle().Id());
    EXPECT_EQ(view.VerticesSize(), graph.VerticesSize());
}

TEST(SubmeshViewDomainBorrows, GraphAsCloudDoesNotAllocatePPositionCompatibilityCopy)
{
    auto graph = MakeTwoVertexEdgeGraph();
    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    // The borrow must reuse `v:point`; no legacy `p:position` slot should be
    // allocated on the shared vertex `PropertySet`.
    EXPECT_FALSE(graph.VertexProperties().Exists("p:position"));
    EXPECT_FALSE(view.PointProperties().Exists("p:position"));
}

TEST(SubmeshViewDomainBorrows, GraphAsCloudDoesNotExposeHalfedgeOrEdgePropertySets)
{
    auto graph = MakeTwoVertexEdgeGraph();
    const std::size_t edgesBefore = graph.EdgesSize();
    const std::size_t halfedgesBefore = graph.HalfedgesSize();
    ASSERT_GT(edgesBefore, 0u);

    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    // Only the vertex `PropertySet` is borrowed: the halfedge/edge connectivity
    // slots live on separate `PropertySet`s the cloud never holds, so they are
    // unreachable through the cloud's vertex-domain accessor.
    EXPECT_FALSE(view.PointProperties().Exists("h:connectivity"));
    EXPECT_FALSE(view.PointProperties().Exists("e:deleted"));

    // The graph's edge/halfedge storage is untouched by the borrow.
    EXPECT_EQ(graph.EdgesSize(), edgesBefore);
    EXPECT_EQ(graph.HalfedgesSize(), halfedgesBefore);
}

TEST(SubmeshViewDomainBorrows, GraphAsCloudSharesVertexConnectivitySlotAsDocumentedUbBoundary)
{
    // Honest contract: because the cloud borrows the *entire* graph vertex
    // `PropertySet`, the graph-domain `v:connectivity` slot remains physically
    // reachable through generic point-property access. The `Cloud` type owns no
    // connectivity accessor and never reads/writes it, but generic code that
    // enumerates or clears point properties can still reach the shared slot.
    // Mutating or clearing it through the cloud is undefined behavior on an
    // edge-bearing graph (it desynchronizes graph topology); type-level
    // prevention is Slice D's restricted const-view work. This test pins the
    // reachability so the boundary is explicit rather than silently assumed
    // hidden.
    auto graph = MakeTwoVertexEdgeGraph();
    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    EXPECT_TRUE(view.PointProperties().Exists("v:connectivity"));

    const auto cloudConn =
        view.PointProperties().Get<Geometry::Graph::VertexConnectivity>("v:connectivity");
    const auto graphConn =
        graph.VertexProperties().Get<Geometry::Graph::VertexConnectivity>("v:connectivity");
    ASSERT_TRUE(cloudConn.IsValid());
    ASSERT_TRUE(graphConn.IsValid());

    // The reachable slot is the graph's shared storage, not a cloud-side copy.
    EXPECT_EQ(cloudConn.Handle().Id(), graphConn.Handle().Id());
}

TEST(SubmeshViewDomainBorrows, GraphAsCloudReusesExistingVertexNormalProperty)
{
    auto graph = MakeTwoVertexEdgeGraph();

    Geometry::VertexProperty<glm::vec3> graphNormal(
        graph.VertexProperties().GetOrAdd<glm::vec3>("v:normal", glm::vec3(0.0f, 1.0f, 0.0f)));
    ASSERT_TRUE(graphNormal.IsValid());
    const Geometry::VertexHandle v0{0};
    graphNormal[v0] = glm::vec3(0.0f, 0.0f, 1.0f);

    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    const auto cloudNormal = view.GetVertexProperty<glm::vec3>("v:normal");
    ASSERT_TRUE(cloudNormal.IsValid());
    EXPECT_EQ(graphNormal.Handle().Id(), cloudNormal.Handle().Id());
    EXPECT_EQ(cloudNormal[v0.Index], glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(SubmeshViewDomainBorrows, GraphSidePositionEditIsVisibleThroughCloudView)
{
    auto graph = MakeTwoVertexEdgeGraph();
    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    const Geometry::VertexHandle v0{0};
    ASSERT_TRUE(graph.IsValid(v0));
    ASSERT_TRUE(view.IsValid(v0));

    const glm::vec3 newPosition{7.0f, -3.0f, 2.0f};
    graph.SetVertexPosition(v0, newPosition);

    EXPECT_EQ(view.Position(v0), newPosition);
}

TEST(SubmeshViewDomainBorrows, CloudSidePositionEditIsVisibleOnSourceGraph)
{
    auto graph = MakeTwoVertexEdgeGraph();
    const std::size_t edgesBefore = graph.EdgesSize();
    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    const Geometry::VertexHandle v0{0};
    ASSERT_TRUE(view.IsValid(v0));

    const glm::vec3 newPosition{-4.0f, 6.0f, 1.5f};
    view.Position(v0) = newPosition;

    EXPECT_EQ(graph.VertexPosition(v0), newPosition);
    // Edge/topology state untouched by a position write.
    EXPECT_EQ(graph.EdgesSize(), edgesBefore);
}

TEST(SubmeshViewDomainBorrows, PointAddedThroughCloudViewAppearsOnSourceGraph)
{
    auto graph = MakeTwoVertexEdgeGraph();
    const std::size_t verticesBefore = graph.VerticesSize();
    const std::size_t edgesBefore = graph.EdgesSize();

    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    const glm::vec3 newPosition{2.5f, 2.5f, 2.5f};
    const Geometry::VertexHandle vNew = view.AddPoint(newPosition);

    // The new point is appended to the shared vertex `PropertySet`, so the
    // source graph observes one additional vertex slot.
    EXPECT_EQ(graph.VerticesSize(), verticesBefore + 1u);
    EXPECT_EQ(view.VerticesSize(), verticesBefore + 1u);
    EXPECT_EQ(graph.VertexPosition(vNew), newPosition);
    EXPECT_EQ(view.Position(vNew), newPosition);

    // The new vertex is isolated; edge/halfedge storage is unchanged.
    EXPECT_EQ(graph.EdgesSize(), edgesBefore);
    EXPECT_TRUE(graph.IsIsolated(vNew));
}

TEST(SubmeshViewDomainBorrows, EmptyGraphBorrowsAsEmptyCloud)
{
    Geometry::Graph::Graph graph;
    Geometry::PointCloud::Cloud view = BorrowGraphAsCloud(graph);

    EXPECT_EQ(view.VerticesSize(), 0u);
    EXPECT_EQ(view.VertexCount(), 0u);
    EXPECT_TRUE(view.IsEmpty());

    // The borrow must wire `v:point` on the shared `PropertySet` without
    // forcing any legacy `p:position` slot.
    EXPECT_TRUE(graph.VertexProperties().Exists("v:point"));
    EXPECT_FALSE(graph.VertexProperties().Exists("p:position"));
}

TEST(SubmeshViewDomainBorrows, CloudDeleteThroughGraphBorrowDoesNotTouchGraphDeletionCounter)
{
    // Cloud and graph use independent deletion markers (`p:deleted` vs
    // `v:deleted`); cloud-side deletes must not corrupt the graph's deletion
    // counter or the graph's view of which vertices are alive.
    auto graph = MakeTwoVertexEdgeGraph();
    const std::size_t verticesBefore = graph.VerticesSize();
    const std::size_t graphCountBefore = graph.VertexCount();

    Geometry::PointCloud::Cloud borrowed = BorrowGraphAsCloud(graph);
    EXPECT_EQ(borrowed.VertexCount(), verticesBefore);

    const Geometry::VertexHandle v0{0};
    borrowed.DeletePoint(v0);

    // Cloud sees its own deleted point.
    EXPECT_TRUE(borrowed.IsDeleted(v0));
    EXPECT_EQ(borrowed.VertexCount(), verticesBefore - 1u);

    // Graph state is unchanged: vertex count and deletion view untouched, no
    // graph-side garbage reported.
    EXPECT_EQ(graph.VertexCount(), graphCountBefore);
    EXPECT_FALSE(graph.IsDeleted(v0));
    EXPECT_FALSE(graph.HasGarbage());
}

// ---------------------------------------------------------------------------
// GEOM-012 Slice D: type-enforced read-only view types.
//
// Compile-time contract: each const-view type must expose ONLY const-returning
// accessors. The mutating operations of the borrowed container (`Add*`,
// `Delete*`, `SetVertexPosition`, `Clear`, `GarbageCollection`,
// `GetOrAdd*Property`, mutable element access) must be ill-formed both on the
// view itself and through its `As*()` const-reference accessor. The detection
// concepts below are required to be FALSE for the mutating probes and TRUE for
// the read probes; the `static_assert`s fail the build if the surface drifts.
// ---------------------------------------------------------------------------

// --- Mutating probes: must be FALSE for every const-view type. ---
template <class V>
concept HasAddVertex = requires(V& v) { v.AddVertex(); };
template <class V>
concept HasAddEdge =
    requires(V& v) { v.AddEdge(Geometry::VertexHandle{}, Geometry::VertexHandle{}); };
template <class V>
concept HasAddPoint = requires(V& v) { v.AddPoint(glm::vec3{}); };
template <class V>
concept HasDeleteVertex = requires(V& v) { v.DeleteVertex(Geometry::VertexHandle{}); };
template <class V>
concept HasDeleteEdge = requires(V& v) { v.DeleteEdge(Geometry::EdgeHandle{}); };
template <class V>
concept HasDeletePoint = requires(V& v) { v.DeletePoint(Geometry::VertexHandle{}); };
template <class V>
concept HasClear = requires(V& v) { v.Clear(); };
template <class V>
concept HasGarbageCollection = requires(V& v) { v.GarbageCollection(); };
template <class V>
concept HasSetVertexPosition =
    requires(V& v) { v.SetVertexPosition(Geometry::VertexHandle{}, glm::vec3{}); };
template <class V>
concept HasGetOrAddVertexProperty =
    requires(V& v) { v.template GetOrAddVertexProperty<int>("x"); };
// Mutable element access: assigning through `Position(h)` must be ill-formed
// (the view returns a const reference / by value).
template <class V>
concept HasMutablePositionAssign =
    requires(V& v) { v.Position(Geometry::VertexHandle{}) = glm::vec3{}; };
// Mutation reachable through the `As*()` interop escape hatch must also fail
// (the underlying container is handed out as a const reference).
template <class V>
concept HasAddVertexViaAsGraph = requires(V& v) { v.AsGraph().AddVertex(); };
template <class V>
concept HasAddPointViaAsCloud = requires(V& v) { v.AsCloud().AddPoint(glm::vec3{}); };

// --- Read probes: must be TRUE for every const-view type. ---
template <class V>
concept HasVerticesSize =
    requires(const V& v) { { v.VerticesSize() } -> std::convertible_to<std::size_t>; };
template <class V>
concept HasGetVertexProperty =
    requires(const V& v) { v.template GetVertexProperty<glm::vec3>("v:point"); };
template <class V>
concept HasReadViaAsGraph = requires(const V& v) { v.AsGraph().VerticesSize(); };
template <class V>
concept HasReadViaAsCloud = requires(const V& v) { v.AsCloud().VerticesSize(); };

// Positive controls: the mutating probes MUST detect the real mutating members
// of the underlying mutable containers. Without these, a malformed probe could
// silently make every `!Has*<ConstView>` assertion below pass vacuously.
static_assert(HasAddVertex<Geometry::Graph::Graph>);
static_assert(HasAddEdge<Geometry::Graph::Graph>);
static_assert(HasDeleteVertex<Geometry::Graph::Graph>);
static_assert(HasDeleteEdge<Geometry::Graph::Graph>);
static_assert(HasClear<Geometry::Graph::Graph>);
static_assert(HasGarbageCollection<Geometry::Graph::Graph>);
static_assert(HasSetVertexPosition<Geometry::Graph::Graph>);
static_assert(HasGetOrAddVertexProperty<Geometry::Graph::Graph>);
static_assert(HasAddPoint<Geometry::PointCloud::Cloud>);
static_assert(HasDeletePoint<Geometry::PointCloud::Cloud>);
static_assert(HasClear<Geometry::PointCloud::Cloud>);
static_assert(HasGarbageCollection<Geometry::PointCloud::Cloud>);
static_assert(HasGetOrAddVertexProperty<Geometry::PointCloud::Cloud>);
static_assert(HasMutablePositionAssign<Geometry::PointCloud::Cloud>);

// ConstMeshBackedGraphView: no graph-topology mutation, no scratch-property
// allocation, no position writes; reads are available directly and via AsGraph.
static_assert(!HasAddVertex<ConstMeshBackedGraphView>);
static_assert(!HasAddEdge<ConstMeshBackedGraphView>);
static_assert(!HasDeleteVertex<ConstMeshBackedGraphView>);
static_assert(!HasDeleteEdge<ConstMeshBackedGraphView>);
static_assert(!HasClear<ConstMeshBackedGraphView>);
static_assert(!HasGarbageCollection<ConstMeshBackedGraphView>);
static_assert(!HasSetVertexPosition<ConstMeshBackedGraphView>);
static_assert(!HasGetOrAddVertexProperty<ConstMeshBackedGraphView>);
static_assert(!HasAddVertexViaAsGraph<ConstMeshBackedGraphView>);
static_assert(HasVerticesSize<ConstMeshBackedGraphView>);
static_assert(HasGetVertexProperty<ConstMeshBackedGraphView>);
static_assert(HasReadViaAsGraph<ConstMeshBackedGraphView>);
static_assert(!std::is_copy_constructible_v<ConstMeshBackedGraphView>);
static_assert(!std::is_move_constructible_v<ConstMeshBackedGraphView>);

// ConstMeshBackedCloudView: no point add/delete, no GC/clear, no scratch-
// property allocation, no mutable position; reads available directly and via
// AsCloud.
static_assert(!HasAddPoint<ConstMeshBackedCloudView>);
static_assert(!HasDeletePoint<ConstMeshBackedCloudView>);
static_assert(!HasClear<ConstMeshBackedCloudView>);
static_assert(!HasGarbageCollection<ConstMeshBackedCloudView>);
static_assert(!HasGetOrAddVertexProperty<ConstMeshBackedCloudView>);
static_assert(!HasMutablePositionAssign<ConstMeshBackedCloudView>);
static_assert(!HasAddPointViaAsCloud<ConstMeshBackedCloudView>);
static_assert(HasVerticesSize<ConstMeshBackedCloudView>);
static_assert(HasGetVertexProperty<ConstMeshBackedCloudView>);
static_assert(HasReadViaAsCloud<ConstMeshBackedCloudView>);
static_assert(!std::is_copy_constructible_v<ConstMeshBackedCloudView>);
static_assert(!std::is_move_constructible_v<ConstMeshBackedCloudView>);

// ConstGraphBackedCloudView: same read-only cloud surface; in particular it
// exposes no mutable property access, so the shared graph-domain
// `v:connectivity` slot cannot be mutated/cleared through it (closing the
// Slice C documented-UB boundary by construction).
static_assert(!HasAddPoint<ConstGraphBackedCloudView>);
static_assert(!HasDeletePoint<ConstGraphBackedCloudView>);
static_assert(!HasClear<ConstGraphBackedCloudView>);
static_assert(!HasGarbageCollection<ConstGraphBackedCloudView>);
static_assert(!HasGetOrAddVertexProperty<ConstGraphBackedCloudView>);
static_assert(!HasMutablePositionAssign<ConstGraphBackedCloudView>);
static_assert(!HasAddPointViaAsCloud<ConstGraphBackedCloudView>);
static_assert(HasVerticesSize<ConstGraphBackedCloudView>);
static_assert(HasGetVertexProperty<ConstGraphBackedCloudView>);
static_assert(HasReadViaAsCloud<ConstGraphBackedCloudView>);
static_assert(!std::is_copy_constructible_v<ConstGraphBackedCloudView>);
static_assert(!std::is_move_constructible_v<ConstGraphBackedCloudView>);

TEST(SubmeshViewDomainBorrows, ConstViewsAreReadOnlyAtCompileTime)
{
    // The read-only contract is enforced by the file-scope `static_assert`s
    // above (this build would fail if any mutating member became reachable, or
    // if a read accessor or the non-copyable/non-movable binding regressed).
    // This case documents that the compile-time gate is part of the suite.
    SUCCEED();
}

TEST(SubmeshViewDomainBorrows, ConstMeshGraphViewSharesStorageAndSeesLiveEdits)
{
    auto mesh = MakeSingleTriangle();
    const auto meshPoint = mesh.VertexProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(meshPoint.IsValid());

    ConstMeshBackedGraphView view(mesh);

    // Shares the canonical v:point storage and the source sizes.
    const auto viewPoint = view.VertexProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(viewPoint.IsValid());
    EXPECT_EQ(meshPoint.Handle().Id(), viewPoint.Handle().Id());
    EXPECT_EQ(view.VerticesSize(), mesh.VerticesSize());
    EXPECT_EQ(view.EdgesSize(), mesh.EdgesSize());

    // A mesh-side position edit is visible through the read-only view, both via
    // the direct accessor and via the const AsGraph() reference.
    const Geometry::VertexHandle v0{0};
    const glm::vec3 newPosition{5.0f, -1.0f, 3.0f};
    mesh.Position(v0) = newPosition;
    EXPECT_EQ(view.VertexPosition(v0), newPosition);
    EXPECT_EQ(view.AsGraph().VertexPosition(v0), newPosition);
}

TEST(SubmeshViewDomainBorrows, ConstMeshCloudViewSharesStorageAndSeesLiveEdits)
{
    auto mesh = MakeSingleTriangle();
    const auto meshPoint = mesh.VertexProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(meshPoint.IsValid());

    ConstMeshBackedCloudView view(mesh);

    const auto viewPoint = view.PointProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(viewPoint.IsValid());
    EXPECT_EQ(meshPoint.Handle().Id(), viewPoint.Handle().Id());
    EXPECT_EQ(view.VerticesSize(), mesh.VerticesSize());
    EXPECT_FALSE(view.PointProperties().Exists("p:position"));

    const Geometry::VertexHandle v0{0};
    const glm::vec3 newPosition{7.0f, -3.0f, 2.0f};
    mesh.Position(v0) = newPosition;
    EXPECT_EQ(view.Position(v0), newPosition);
    EXPECT_EQ(view.AsCloud().Position(v0), newPosition);
}

TEST(SubmeshViewDomainBorrows, ConstGraphCloudViewSharesStorageAndSeesLiveEdits)
{
    auto graph = MakeTwoVertexEdgeGraph();
    const auto graphPoint = graph.VertexProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(graphPoint.IsValid());
    const std::size_t edgesBefore = graph.EdgesSize();

    ConstGraphBackedCloudView view(graph);

    const auto viewPoint = view.PointProperties().Get<glm::vec3>("v:point");
    ASSERT_TRUE(viewPoint.IsValid());
    EXPECT_EQ(graphPoint.Handle().Id(), viewPoint.Handle().Id());
    EXPECT_EQ(view.VerticesSize(), graph.VerticesSize());

    const Geometry::VertexHandle v0{0};
    const glm::vec3 newPosition{-4.0f, 6.0f, 1.5f};
    graph.SetVertexPosition(v0, newPosition);
    EXPECT_EQ(view.Position(v0), newPosition);
    EXPECT_EQ(view.AsCloud().Position(v0), newPosition);

    // The read-only cloud view never exposes the graph's edge/halfedge storage
    // and leaves edge state untouched.
    EXPECT_FALSE(view.PointProperties().Exists("h:connectivity"));
    EXPECT_EQ(graph.EdgesSize(), edgesBefore);
}

} // namespace
