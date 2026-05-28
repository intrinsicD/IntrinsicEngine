module;

export module Geometry.DomainViews;

export import Geometry.HalfedgeMesh;
export import Geometry.Graph;

export namespace Geometry::DomainViews
{
    // Borrow the property storage of an existing `HalfedgeMesh::Mesh` as a
    // `Graph::Graph`. The returned graph shares the source mesh's vertex,
    // halfedge, and edge `PropertySet`s plus the deletion counters and reuses
    // the canonical `v:point`, `v:connectivity`, `h:connectivity`, `v:deleted`,
    // and `e:deleted` slots — no compatibility-copy properties are allocated.
    //
    // The source mesh must outlive the returned view; lifetime is the caller's
    // responsibility and mirrors `HalfedgeMesh::Mesh::CreateView`. Mutations
    // through the view (`AddVertex`, `AddEdge`, `DeleteEdge`, position writes)
    // mutate the source mesh in place. Algorithms that want a read-only view
    // should consume the result through a `const Graph::Graph&` callsite until
    // GEOM-012 Slice D introduces a distinct read-only view type.
    [[nodiscard]] Graph::Graph BorrowMeshAsGraph(HalfedgeMesh::Mesh& mesh);
}
