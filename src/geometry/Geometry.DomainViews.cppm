module;

export module Geometry.DomainViews;

export import Geometry.HalfedgeMesh;
export import Geometry.Graph;
export import Geometry.PointCloud;

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

    // Borrow a `HalfedgeMesh::Mesh`'s vertex storage as a `PointCloud::Cloud`
    // so point-cloud algorithms can consume the mesh's vertex domain without
    // copying positions or attributes. The returned cloud shares the source
    // mesh's vertex `PropertySet` and `DeletedVertexCount()` counter and
    // reuses the canonical `v:point` slot; no `p:position` compatibility-copy
    // property is allocated. Any existing per-vertex attributes on the mesh
    // (for example `v:normal`) are reachable through the cloud's
    // `GetVertexProperty<T>` accessor over the same shared `PropertySet`.
    //
    // `PointCloud::Cloud::AddPoint` appends a row to the shared vertex
    // `PropertySet`, so a point added through the returned view becomes a
    // new vertex slot on the source mesh; this is safe on face-bearing meshes
    // because face/halfedge/edge storage is not modified and the new vertex
    // is left isolated (no incident halfedges). `PointCloud::Cloud` owns its
    // own deletion marker (`p:deleted`) which `EnsureProperties` allocates on
    // the shared vertex `PropertySet` on first borrow; the mesh's own
    // `v:deleted` marker is independent. Removing a point through the cloud
    // therefore marks `p:deleted` and increments the shared
    // `DeletedVertexCount()` but does **not** update mesh-side topology;
    // route topology-aware deletion through `Mesh::DeleteVertex` /
    // `Mesh::GarbageCollection`.
    //
    // Lifetime: the source mesh must outlive the returned view, mirroring
    // `HalfedgeMesh::Mesh::CreateView` and `BorrowMeshAsGraphReadOnly`. A
    // distinct compile-time-checked read-only cloud-view type is owned by
    // GEOM-012 Slice D.
    [[nodiscard]] PointCloud::Cloud BorrowMeshAsCloud(HalfedgeMesh::Mesh& mesh);
}
