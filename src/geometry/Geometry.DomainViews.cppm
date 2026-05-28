module;

export module Geometry.DomainViews;

export import Geometry.HalfedgeMesh;
export import Geometry.Graph;

export namespace Geometry::DomainViews
{
    // Borrow a `HalfedgeMesh::Mesh`'s vertex/halfedge/edge property storage as
    // a `Graph::Graph` for **graph-domain reads and vertex-position writes**.
    // Safe on any source mesh, including face-bearing meshes.
    //
    // The returned graph shares the source mesh's `PropertySet`s and deletion
    // counters and reuses the canonical `v:point`, `v:connectivity`,
    // `h:connectivity`, `v:deleted`, and `e:deleted` slots — no compatibility-
    // copy properties are allocated. Face storage (`h:face`, `f:connectivity`,
    // `f:deleted`, and `Mesh::FacesSize()`/`DeletedFaceCount()`) is not part
    // of the view; reading or writing positions and running graph-domain
    // algorithms (e.g. `Geometry::ShortestPath::Dijkstra`) is safe and does
    // not touch face state.
    //
    // **Topology mutation through the returned graph is undefined behavior
    // on face-bearing source meshes.** `Graph::AddVertex`, `AddEdge`,
    // `DeleteVertex`, `DeleteEdge`, `GarbageCollection`, `Clear`,
    // `SetNextHalfedge`/`SetPrevHalfedge`/`SetVertex`/`SetHalfedge`, and the
    // halfedge new-edge helpers update vertex/halfedge/edge data and the
    // deleted-vertex/edge counters but cannot observe or update the mesh's
    // face property set; calling them on a face-bearing source mesh leaves
    // `h:face`, `f:connectivity`, `f:deleted`, and `FacesSize()` stale and
    // corrupts the mesh. Route topology changes through the mesh's own
    // `Mesh::DeleteEdge`/`DeleteVertex`/`DeleteFace`/`GarbageCollection`
    // operations, which cascade through face incidence. Vertex-position
    // writes (`Graph::SetVertexPosition`) do not change topology and are
    // explicitly allowed.
    //
    // Lifetime: the source mesh must outlive the returned view, mirroring
    // `HalfedgeMesh::Mesh::CreateView`. The const reference is the safety
    // intent signal; internally the factory routes through the mutable
    // `Graph::Graph(PropertySet&, ...)` constructor because the returned
    // graph supports the in-place mutations listed above. A distinct
    // compile-time-checked read-only view type is owned by GEOM-012 Slice D.
    [[nodiscard]] Graph::Graph BorrowMeshAsGraphReadOnly(const HalfedgeMesh::Mesh& mesh);
}
