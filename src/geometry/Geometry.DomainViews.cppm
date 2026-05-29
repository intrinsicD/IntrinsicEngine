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
    // mesh's vertex `PropertySet` and reuses the canonical `v:point` slot;
    // no `p:position` compatibility-copy property is allocated. Any existing
    // per-vertex attributes on the mesh (for example `v:normal`) are
    // reachable through the cloud's `GetVertexProperty<T>` accessor over the
    // same shared `PropertySet`.
    //
    // The returned cloud owns its own deletion counter; cloud-side deletes
    // mark the cloud's `p:deleted` marker but do **not** increment
    // `mesh.DeletedVertexCount()`, so the mesh's `VertexCount()` and
    // `HasGarbage()` continue to reflect only mesh-side `v:deleted`
    // semantics. The cloud's `p:deleted` marker is allocated lazily by
    // `Cloud::EnsureProperties` on the shared vertex `PropertySet`; it is
    // independent from the mesh's topology-aware `v:deleted` and the mesh
    // never reads `p:deleted`. Route topology-aware deletion through
    // `Mesh::DeleteVertex` / `Mesh::GarbageCollection`; calling
    // `Cloud::GarbageCollection` on a mesh-backed borrow is undefined
    // behavior on face-bearing source meshes because it physically reshuffles
    // and resizes vertex slots and would invalidate mesh halfedge/edge
    // connectivity that references vertex indices.
    //
    // `PointCloud::Cloud::AddPoint` appends a row to the shared vertex
    // `PropertySet`, so a point added through the returned view becomes a
    // new vertex slot on the source mesh; this is safe on face-bearing
    // meshes because face/halfedge/edge storage is not modified and the new
    // vertex is left isolated (no incident halfedges).
    //
    // `PointCloud::Cloud::CreateView` is well-defined on the returned cloud:
    // subrange clamping and the returned view's bound storage both follow
    // the mesh-backed `v:point` data rather than the cloud's empty owning
    // `Properties`.
    //
    // Lifetime: the source mesh must outlive the returned view, mirroring
    // `HalfedgeMesh::Mesh::CreateView` and `BorrowMeshAsGraphReadOnly`. A
    // distinct compile-time-checked read-only cloud-view type is owned by
    // GEOM-012 Slice D.
    [[nodiscard]] PointCloud::Cloud BorrowMeshAsCloud(HalfedgeMesh::Mesh& mesh);

    // Borrow a `Graph::Graph`'s vertex storage as a `PointCloud::Cloud` so
    // point-cloud algorithms can consume the graph's vertex domain without
    // copying positions or attributes. This is the symmetric companion to
    // `BorrowMeshAsCloud`: the returned cloud shares the source graph's vertex
    // `PropertySet` and reuses the canonical `v:point` slot; no `p:position`
    // compatibility-copy property is allocated. Any existing per-vertex
    // attributes on the graph (for example `v:normal`) are reachable through
    // the cloud's `GetVertexProperty<T>` accessor over the same shared
    // `PropertySet`.
    //
    // Only the graph's vertex `PropertySet` is borrowed. The graph's
    // halfedge/edge storage (`h:connectivity`, `e:deleted`, and the
    // `v:connectivity` graph-domain slot that remains on the shared vertex
    // `PropertySet`) is **not** exposed through the cloud surface: the cloud
    // has no edge/halfedge accessors and never reads `v:connectivity`. The
    // source graph's `EdgesSize()`/`HalfedgesSize()`/`EdgeCount()` and its
    // edge/halfedge property sets are untouched by cloud-side operations.
    //
    // The returned cloud owns its own deletion counter; cloud-side deletes
    // mark the cloud's `p:deleted` marker but do **not** increment the graph's
    // deleted-vertex counter, so the graph's `VertexCount()` and `HasGarbage()`
    // continue to reflect only graph-side `v:deleted` semantics. The cloud's
    // `p:deleted` marker is allocated lazily by `Cloud::EnsureProperties` on
    // the shared vertex `PropertySet`; it is independent from the graph's
    // topology-aware `v:deleted` and the graph never reads `p:deleted`. Route
    // topology-aware deletion through `Graph::DeleteVertex` /
    // `Graph::GarbageCollection`; calling `Cloud::GarbageCollection` on a
    // graph-backed borrow is undefined behavior on an edge-bearing source
    // graph because it physically reshuffles and resizes vertex slots and would
    // invalidate graph halfedge/edge connectivity that references vertex
    // indices.
    //
    // `PointCloud::Cloud::AddPoint` appends a row to the shared vertex
    // `PropertySet`, so a point added through the returned view becomes a new
    // vertex slot on the source graph; this is safe on edge-bearing graphs
    // because halfedge/edge storage is not modified and the new vertex is left
    // isolated (no incident halfedges).
    //
    // Lifetime: the source graph must outlive the returned view, mirroring
    // `BorrowMeshAsCloud`. A distinct compile-time-checked read-only cloud-view
    // type is owned by GEOM-012 Slice D.
    [[nodiscard]] PointCloud::Cloud BorrowGraphAsCloud(Graph::Graph& graph);
}
