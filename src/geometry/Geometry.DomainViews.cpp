module;

module Geometry.DomainViews;

namespace Geometry::DomainViews
{
    Graph::Graph BorrowMeshAsGraphReadOnly(const HalfedgeMesh::Mesh& mesh)
    {
        // `Graph::Graph(PropertySet&, ...)` is the only borrow constructor and
        // it must accept mutable references because the returned graph supports
        // in-place writes (vertex positions, etc.). The factory takes a const
        // reference to encode the no-topology-mutation contract at the public
        // surface; see the .cppm doc for the full safety statement.
        auto& mut = const_cast<HalfedgeMesh::Mesh&>(mesh);
        return Graph::Graph(mut.VertexProperties(),
                            mut.HalfedgeProperties(),
                            mut.EdgeProperties(),
                            mut.DeletedVertexCount(),
                            mut.DeletedEdgeCount());
    }

    PointCloud::Cloud BorrowMeshAsCloud(HalfedgeMesh::Mesh& mesh)
    {
        // Share the mesh's vertex `PropertySet` only; the cloud owns its own
        // deletion counter so `Cloud::DeletePoint` cannot increment
        // `mesh.DeletedVertexCount()` and desynchronize it from the mesh's
        // `v:deleted` marker.
        return PointCloud::Cloud(mesh.VertexProperties());
    }

    PointCloud::Cloud BorrowGraphAsCloud(Graph::Graph& graph)
    {
        // Share the graph's vertex `PropertySet` only. The graph's
        // halfedge/edge property sets are not borrowed, so edge/halfedge data
        // is not reachable through the cloud surface. The cloud owns its own
        // deletion counter so `Cloud::DeletePoint` cannot desynchronize the
        // graph's `v:deleted` marker; see the .cppm doc for the full contract.
        return PointCloud::Cloud(graph.VertexProperties());
    }

    // GEOM-012 Slice D read-only view constructors. Each member is initialized
    // directly from the prvalue returned by the corresponding `Borrow*`
    // factory; guaranteed copy elision means no copy/move of the borrowed
    // container occurs, so the view shares the source's storage rather than
    // deep-copying it. The sources are taken by mutable reference because the
    // borrow factories call `EnsureProperties`/`GetOrAdd`, which lazily
    // materialize the shared `v:point`/`p:deleted`/connectivity columns on the
    // source property set; const_cast-ing a genuinely const source into that
    // mutating path would be undefined behavior. Passing a mutable source also
    // makes the internal const_cast inside `BorrowMeshAsGraphReadOnly`
    // well-defined. The post-construction view surface is read-only regardless.
    ConstMeshBackedGraphView::ConstMeshBackedGraphView(HalfedgeMesh::Mesh& mesh)
        : m_Graph(BorrowMeshAsGraphReadOnly(mesh))
    {
    }

    ConstMeshBackedCloudView::ConstMeshBackedCloudView(HalfedgeMesh::Mesh& mesh)
        : m_Cloud(BorrowMeshAsCloud(mesh))
    {
    }

    ConstGraphBackedCloudView::ConstGraphBackedCloudView(Graph::Graph& graph)
        : m_Cloud(BorrowGraphAsCloud(graph))
    {
    }
}
