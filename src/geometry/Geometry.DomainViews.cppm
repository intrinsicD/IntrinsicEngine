module;

#include <cstddef>
#include <span>
#include <string_view>

#include <glm/glm.hpp>

export module Geometry.DomainViews;

export import Geometry.HalfedgeMesh;
export import Geometry.Graph;
export import Geometry.PointCloud;

import Geometry.Properties;

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
    // halfedge/edge storage (`h:connectivity`, `e:deleted`) lives on separate
    // `PropertySet`s that the cloud does not hold, so it is not reachable
    // through the cloud surface at all, and the source graph's
    // `EdgesSize()`/`HalfedgesSize()`/`EdgeCount()` are untouched by cloud-side
    // operations.
    //
    // The graph-domain `v:connectivity` slot, by contrast, lives on the
    // **shared** vertex `PropertySet` and therefore remains physically
    // reachable through generic `Cloud::PointProperties()` /
    // `GetVertexProperty<T>` access. The `Cloud` type itself owns no
    // connectivity accessor and never reads or writes `v:connectivity`, but
    // generic point-property code that enumerates, copies, clears, or edits the
    // shared set can still reach it. **Mutating or clearing `v:connectivity`
    // through a graph-backed borrow — including via `Cloud::Clear()` or
    // `Cloud::GarbageCollection()` — is undefined behavior on an edge-bearing
    // source graph**, the same topology-mutation boundary documented for the
    // other borrows: it desynchronizes the graph's halfedge/edge connectivity
    // from its vertex records. Route topology changes through the graph's own
    // APIs. Type-level prevention of reaching graph-domain slots through the
    // borrow is owned by GEOM-012 Slice D's restricted const-view types.
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

    // -----------------------------------------------------------------------
    // GEOM-012 Slice D: type-enforced read-only borrow surfaces.
    //
    // Each `Const*View` wraps the same shared-storage borrow produced by its
    // mutable factory above, but exposes **only** `const`-returning accessors:
    // there is no `Add*`, `Delete*`, `SetVertexPosition`, `Clear`,
    // `GarbageCollection`, or `GetOrAdd*Property` member. Mutating the borrowed
    // source storage through these types is therefore ill-formed at compile
    // time, promoting the documented mutable-borrow rule ("read-only algorithms
    // must not mutate source storage") from convention to type.
    //
    // Each view shares storage with its source (reads observe live source
    // edits) and is non-copyable / non-movable: it is a borrow bound to its
    // source at construction, and the source must outlive it. The constructors
    // take a **mutable** source reference because construction routes through
    // the corresponding mutable `Borrow*` factory, which lazily materializes
    // the shared property columns (`v:point`, the cloud's `p:deleted`,
    // connectivity records) via `EnsureProperties`/`GetOrAdd` on the source
    // property set. A genuinely const-qualified source is therefore rejected at
    // compile time rather than const_cast into the mutating borrow (which would
    // be undefined behavior); the view surface is read-only once constructed.
    // -----------------------------------------------------------------------

    // Read-only graph-domain borrow of a `HalfedgeMesh::Mesh`. Wraps the
    // `BorrowMeshAsGraphReadOnly` borrow (see that factory for the
    // storage-sharing and face-state-isolation contract).
    //
    // `AsGraph()` returns the underlying `const Graph::Graph&` for interop with
    // algorithms that accept `const Graph::Graph&`; the graph's mutating
    // methods are not reachable through the const reference. Algorithms that
    // allocate scratch properties on the graph (e.g. `ShortestPath::Dijkstra`,
    // which takes a mutable `Graph::Graph&`) are not const consumers and must
    // use the mutable `BorrowMeshAsGraphReadOnly` borrow instead.
    class ConstMeshBackedGraphView
    {
    public:
        explicit ConstMeshBackedGraphView(HalfedgeMesh::Mesh& mesh);

        ConstMeshBackedGraphView(const ConstMeshBackedGraphView&) = delete;
        ConstMeshBackedGraphView& operator=(const ConstMeshBackedGraphView&) = delete;
        ConstMeshBackedGraphView(ConstMeshBackedGraphView&&) = delete;
        ConstMeshBackedGraphView& operator=(ConstMeshBackedGraphView&&) = delete;

        // Full read-only interop surface for const-taking graph algorithms.
        [[nodiscard]] const Graph::Graph& AsGraph() const noexcept { return m_Graph; }

        [[nodiscard]] std::size_t VerticesSize()  const noexcept { return m_Graph.VerticesSize(); }
        [[nodiscard]] std::size_t HalfedgesSize() const noexcept { return m_Graph.HalfedgesSize(); }
        [[nodiscard]] std::size_t EdgesSize()     const noexcept { return m_Graph.EdgesSize(); }
        [[nodiscard]] std::size_t VertexCount()   const noexcept { return m_Graph.VertexCount(); }
        [[nodiscard]] std::size_t EdgeCount()     const noexcept { return m_Graph.EdgeCount(); }

        [[nodiscard]] bool IsValid(VertexHandle v)   const { return m_Graph.IsValid(v); }
        [[nodiscard]] bool IsValid(EdgeHandle e)     const { return m_Graph.IsValid(e); }
        [[nodiscard]] bool IsValid(HalfedgeHandle h) const { return m_Graph.IsValid(h); }

        [[nodiscard]] bool IsDeleted(VertexHandle v)   const { return m_Graph.IsDeleted(v); }
        [[nodiscard]] bool IsDeleted(EdgeHandle e)     const { return m_Graph.IsDeleted(e); }
        [[nodiscard]] bool IsDeleted(HalfedgeHandle h) const { return m_Graph.IsDeleted(h); }

        [[nodiscard]] bool HasGarbage() const noexcept { return m_Graph.HasGarbage(); }

        [[nodiscard]] Graph::Graph::LiveVertexRange LiveVertices() const { return m_Graph.LiveVertices(); }
        [[nodiscard]] Graph::Graph::LiveHalfedgeRange LiveHalfedges() const { return m_Graph.LiveHalfedges(); }
        [[nodiscard]] Graph::Graph::LiveEdgeRange LiveEdges() const { return m_Graph.LiveEdges(); }

        [[nodiscard]] glm::vec3 VertexPosition(VertexHandle v) const { return m_Graph.VertexPosition(v); }

        [[nodiscard]] ConstPropertySet VertexProperties()   const noexcept { return m_Graph.VertexProperties(); }
        [[nodiscard]] ConstPropertySet EdgeProperties()     const noexcept { return m_Graph.EdgeProperties(); }
        [[nodiscard]] ConstPropertySet HalfedgeProperties() const noexcept { return m_Graph.HalfedgeProperties(); }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetVertexProperty(std::string_view name) const
        {
            return m_Graph.VertexProperties().Get<T>(name);
        }

    private:
        Graph::Graph m_Graph;
    };

    // Read-only point-cloud borrow of a `HalfedgeMesh::Mesh`. Wraps the
    // `BorrowMeshAsCloud` borrow (see that factory for the storage-sharing and
    // deletion-counter-isolation contract). `AsCloud()` returns the underlying
    // `const PointCloud::Cloud&` for interop with const-taking cloud
    // algorithms. Because the view exposes no mutable property access, the
    // shared `v:point`/attribute storage cannot be mutated through it.
    class ConstMeshBackedCloudView
    {
    public:
        explicit ConstMeshBackedCloudView(HalfedgeMesh::Mesh& mesh);

        ConstMeshBackedCloudView(const ConstMeshBackedCloudView&) = delete;
        ConstMeshBackedCloudView& operator=(const ConstMeshBackedCloudView&) = delete;
        ConstMeshBackedCloudView(ConstMeshBackedCloudView&&) = delete;
        ConstMeshBackedCloudView& operator=(ConstMeshBackedCloudView&&) = delete;

        [[nodiscard]] const PointCloud::Cloud& AsCloud() const noexcept { return m_Cloud; }

        [[nodiscard]] std::size_t VerticesSize() const noexcept { return m_Cloud.VerticesSize(); }
        [[nodiscard]] std::size_t VertexCount()  const noexcept { return m_Cloud.VertexCount(); }
        [[nodiscard]] bool        IsEmpty()      const noexcept { return m_Cloud.IsEmpty(); }

        [[nodiscard]] bool IsValid(VertexHandle v)   const          { return m_Cloud.IsValid(v); }
        [[nodiscard]] bool IsDeleted(VertexHandle v) const          { return m_Cloud.IsDeleted(v); }
        [[nodiscard]] bool HasGarbage()              const noexcept { return m_Cloud.HasGarbage(); }
        [[nodiscard]] PointCloud::Cloud::LivePointRange LivePoints() const { return m_Cloud.LivePoints(); }

        [[nodiscard]] const glm::vec3&           Position(VertexHandle v) const { return m_Cloud.Position(v); }
        [[nodiscard]] std::span<const glm::vec3> Positions()             const { return m_Cloud.Positions(); }

        [[nodiscard]] bool HasNormals() const noexcept { return m_Cloud.HasNormals(); }
        [[nodiscard]] bool HasColors()  const noexcept { return m_Cloud.HasColors(); }
        [[nodiscard]] bool HasRadii()   const noexcept { return m_Cloud.HasRadii(); }

        [[nodiscard]] ConstPropertySet PointProperties() const noexcept { return m_Cloud.PointProperties(); }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetVertexProperty(std::string_view name) const
        {
            return m_Cloud.GetVertexProperty<T>(name);
        }

    private:
        PointCloud::Cloud m_Cloud;
    };

    // Read-only point-cloud borrow of a `Graph::Graph`. Wraps the
    // `BorrowGraphAsCloud` borrow (see that factory for the storage-sharing,
    // edge/halfedge non-exposure, and deletion-counter-isolation contract).
    //
    // Unlike the mutable `BorrowGraphAsCloud` borrow, this read-only view
    // exposes no mutable property access and no `Clear`/`GarbageCollection`, so
    // the graph-domain `v:connectivity` slot that lives on the shared vertex
    // `PropertySet` cannot be mutated or cleared through this type: the
    // documented-UB boundary that `BorrowGraphAsCloud` could only describe is
    // closed by construction here.
    class ConstGraphBackedCloudView
    {
    public:
        explicit ConstGraphBackedCloudView(Graph::Graph& graph);

        ConstGraphBackedCloudView(const ConstGraphBackedCloudView&) = delete;
        ConstGraphBackedCloudView& operator=(const ConstGraphBackedCloudView&) = delete;
        ConstGraphBackedCloudView(ConstGraphBackedCloudView&&) = delete;
        ConstGraphBackedCloudView& operator=(ConstGraphBackedCloudView&&) = delete;

        [[nodiscard]] const PointCloud::Cloud& AsCloud() const noexcept { return m_Cloud; }

        [[nodiscard]] std::size_t VerticesSize() const noexcept { return m_Cloud.VerticesSize(); }
        [[nodiscard]] std::size_t VertexCount()  const noexcept { return m_Cloud.VertexCount(); }
        [[nodiscard]] bool        IsEmpty()      const noexcept { return m_Cloud.IsEmpty(); }

        [[nodiscard]] bool IsValid(VertexHandle v)   const          { return m_Cloud.IsValid(v); }
        [[nodiscard]] bool IsDeleted(VertexHandle v) const          { return m_Cloud.IsDeleted(v); }
        [[nodiscard]] bool HasGarbage()              const noexcept { return m_Cloud.HasGarbage(); }
        [[nodiscard]] PointCloud::Cloud::LivePointRange LivePoints() const { return m_Cloud.LivePoints(); }

        [[nodiscard]] const glm::vec3&           Position(VertexHandle v) const { return m_Cloud.Position(v); }
        [[nodiscard]] std::span<const glm::vec3> Positions()             const { return m_Cloud.Positions(); }

        [[nodiscard]] bool HasNormals() const noexcept { return m_Cloud.HasNormals(); }
        [[nodiscard]] bool HasColors()  const noexcept { return m_Cloud.HasColors(); }
        [[nodiscard]] bool HasRadii()   const noexcept { return m_Cloud.HasRadii(); }

        [[nodiscard]] ConstPropertySet PointProperties() const noexcept { return m_Cloud.PointProperties(); }

        template <class T>
        [[nodiscard]] ConstProperty<T> GetVertexProperty(std::string_view name) const
        {
            return m_Cloud.GetVertexProperty<T>(name);
        }

    private:
        PointCloud::Cloud m_Cloud;
    };
}
