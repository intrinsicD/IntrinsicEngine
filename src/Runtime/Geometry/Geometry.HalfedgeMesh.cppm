module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <memory>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMesh;

import Geometry.HalfedgeMeshFwd;
import Geometry.Properties;
import Geometry.Circulators;

export namespace Geometry::Halfedge
{
    class Mesh
    {
    public:
        using TraversalSentinel = Circulators::TraversalSentinel;
        using HalfedgesAroundFaceRange = Circulators::HalfedgesAroundFaceRange<Mesh>;
        using VerticesAroundFaceRange = Circulators::VerticesAroundFaceRange<Mesh>;
        using HalfedgesAroundVertexRange = Circulators::HalfedgesAroundVertexRange<Mesh>;
        using FacesAroundVertexRange = Circulators::FacesAroundVertexRange<Mesh>;
        using BoundaryHalfedgesRange = Circulators::BoundaryHalfedgesRange<Mesh>;
        using BoundaryVerticesRange = Circulators::BoundaryVerticesRange<Mesh>;

        Mesh();
        Mesh(PropertySet& vertices, PropertySet& halfedges, PropertySet& edges, PropertySet& faces, size_t &deletedVertices, size_t &deletedEdges, size_t &deletedFaces) noexcept;
        Mesh(const Mesh &other);
        ~Mesh();

        Mesh &operator=(const Mesh &other) noexcept;
        Mesh &operator=(Mesh &&other) noexcept;

        // Construction
        [[nodiscard]] VertexHandle AddVertex();
        [[nodiscard]] VertexHandle AddVertex(glm::vec3 position);

        [[nodiscard]] std::optional<FaceHandle> AddFace(std::span<const VertexHandle> vertices);
        [[nodiscard]] std::optional<FaceHandle> AddTriangle(VertexHandle v0, VertexHandle v1, VertexHandle v2);
        [[nodiscard]] std::optional<FaceHandle> AddQuad(VertexHandle v0, VertexHandle v1, VertexHandle v2, VertexHandle v3);

        void Clear();
        void FreeMemory();
        void Reserve(std::size_t nVertices, std::size_t nEdges, std::size_t nFaces);
        void GarbageCollection();

        // Sizes
        [[nodiscard]] std::size_t VerticesSize() const noexcept { return m_Vertices.Size(); }
        [[nodiscard]] std::size_t HalfedgesSize() const noexcept { return m_Halfedges.Size(); }
        [[nodiscard]] std::size_t EdgesSize() const noexcept { return m_Edges.Size(); }
        [[nodiscard]] std::size_t FacesSize() const noexcept { return m_Faces.Size(); }

        [[nodiscard]] std::size_t VertexCount() const noexcept { return VerticesSize() - m_DeletedVertices; }
        [[nodiscard]] std::size_t HalfedgeCount() const noexcept { return HalfedgesSize() - 2u * m_DeletedEdges; }
        [[nodiscard]] std::size_t EdgeCount() const noexcept { return EdgesSize() - m_DeletedEdges; }
        [[nodiscard]] std::size_t FaceCount() const noexcept { return FacesSize() - m_DeletedFaces; }

        [[nodiscard]] bool IsEmpty() const noexcept { return VertexCount() == 0; }

        // Validity / deletion
        [[nodiscard]] bool IsValid(VertexHandle v) const { return v.Index < VerticesSize(); }
        [[nodiscard]] bool IsValid(HalfedgeHandle h) const { return h.Index < HalfedgesSize(); }
        [[nodiscard]] bool IsValid(EdgeHandle e) const { return e.Index < EdgesSize(); }
        [[nodiscard]] bool IsValid(FaceHandle f) const { return f.Index < FacesSize(); }

        [[nodiscard]] bool IsDeleted(VertexHandle v) const { return m_VDeleted[v]; }
        [[nodiscard]] bool IsDeleted(HalfedgeHandle h) const { return m_EDeleted[Edge(h)]; }
        [[nodiscard]] bool IsDeleted(EdgeHandle e) const { return m_EDeleted[e]; }
        [[nodiscard]] bool IsDeleted(FaceHandle f) const { return m_FDeleted[f]; }

        // Connectivity access
        [[nodiscard]] HalfedgeHandle Halfedge(VertexHandle v) const { return m_VConn[v].Halfedge; }
        void SetHalfedge(VertexHandle v, HalfedgeHandle h) { m_VConn[v].Halfedge = h; }

        [[nodiscard]] VertexHandle ToVertex(HalfedgeHandle h) const { return m_HConn[h].Vertex; }
        [[nodiscard]] VertexHandle FromVertex(HalfedgeHandle h) const { return ToVertex(OppositeHalfedge(h)); }
        void SetVertex(HalfedgeHandle h, VertexHandle v) { m_HConn[h].Vertex = v; }

        [[nodiscard]] FaceHandle Face(HalfedgeHandle h) const { return m_HConn[h].Face; }
        void SetFace(HalfedgeHandle h, FaceHandle f) { m_HConn[h].Face = f; }

        [[nodiscard]] HalfedgeHandle NextHalfedge(HalfedgeHandle h) const { return m_HConn[h].Next; }
        [[nodiscard]] HalfedgeHandle PrevHalfedge(HalfedgeHandle h) const { return m_HConn[h].Prev; }

        void SetNextHalfedge(HalfedgeHandle h, HalfedgeHandle next);
        void SetPrevHalfedge(HalfedgeHandle h, HalfedgeHandle prev);

        [[nodiscard]] HalfedgeHandle OppositeHalfedge(HalfedgeHandle h) const
        {
            return HalfedgeHandle{static_cast<PropertyIndex>((h.Index & 1u) ? (h.Index - 1u) : (h.Index + 1u))};
        }

        [[nodiscard]] HalfedgeHandle CCWRotatedHalfedge(HalfedgeHandle h) const { return OppositeHalfedge(PrevHalfedge(h)); }
        [[nodiscard]] HalfedgeHandle CWRotatedHalfedge(HalfedgeHandle h) const { return NextHalfedge(OppositeHalfedge(h)); }

        [[nodiscard]] EdgeHandle Edge(HalfedgeHandle h) const { return EdgeHandle{static_cast<PropertyIndex>(h.Index >> 1u)}; }
        [[nodiscard]] HalfedgeHandle Halfedge(EdgeHandle e, unsigned int i) const;

        [[nodiscard]] HalfedgeHandle Halfedge(FaceHandle f) const { return m_FConn[f].Halfedge; }
        void SetHalfedge(FaceHandle f, HalfedgeHandle h) { m_FConn[f].Halfedge = h; }

        [[nodiscard]] bool IsBoundary(HalfedgeHandle h) const { return !Face(h).IsValid(); }
        [[nodiscard]] bool IsBoundary(VertexHandle v) const;
        [[nodiscard]] bool IsBoundary(EdgeHandle e) const;
        [[nodiscard]] bool IsBoundary(FaceHandle f) const;

        [[nodiscard]] HalfedgesAroundFaceRange HalfedgesAroundFace(FaceHandle f) const { return HalfedgesAroundFaceRange{this, f}; }
        [[nodiscard]] VerticesAroundFaceRange VerticesAroundFace(FaceHandle f) const { return VerticesAroundFaceRange{this, f}; }
        [[nodiscard]] HalfedgesAroundVertexRange HalfedgesAroundVertex(VertexHandle v) const { return HalfedgesAroundVertexRange{this, v}; }
        [[nodiscard]] FacesAroundVertexRange FacesAroundVertex(VertexHandle v) const { return FacesAroundVertexRange{this, v}; }
        [[nodiscard]] BoundaryHalfedgesRange BoundaryHalfedges(HalfedgeHandle h) const { return BoundaryHalfedgesRange{this, h}; }
        [[nodiscard]] BoundaryVerticesRange BoundaryVertices(HalfedgeHandle h) const { return BoundaryVerticesRange{this, h}; }

        [[nodiscard]] bool IsIsolated(VertexHandle v) const { return !Halfedge(v).IsValid(); }
        [[nodiscard]] bool IsManifold(VertexHandle v) const;

        // Geometry payload (PMP-style): store vertex positions as a built-in property.
        [[nodiscard]] const glm::vec3& Position(VertexHandle v) const { return m_VPoint[v]; }
        [[nodiscard]] glm::vec3& Position(VertexHandle v) { return m_VPoint[v]; }
        [[nodiscard]] std::span<const glm::vec3> Positions() const { return m_VPoint.Span(); }
        [[nodiscard]] std::span<glm::vec3> Positions() { return m_VPoint.Span(); }

        // Basic editing utilities (subset)
        [[nodiscard]] std::optional<HalfedgeHandle> FindHalfedge(VertexHandle start, VertexHandle end) const;
        [[nodiscard]] std::optional<EdgeHandle> FindEdge(VertexHandle a, VertexHandle b) const;

        [[nodiscard]] std::size_t Valence(VertexHandle v) const;
        [[nodiscard]] std::size_t Valence(FaceHandle f) const;

        void DeleteVertex(VertexHandle v);
        void DeleteEdge(EdgeHandle e);
        void DeleteFace(FaceHandle f);

        // -----------------------------------------------------------------
        // Topological editing operations (Euler operations)
        // -----------------------------------------------------------------

        // Edge collapse: contract edge e, merging its two endpoint vertices
        // into one. The surviving vertex is placed at the given position.
        // Returns the surviving vertex handle, or nullopt if the collapse
        // would create a non-manifold configuration.
        //
        // Preconditions checked:
        //   - Link condition: |link(v0) ∩ link(v1)| == 2 for interior edges,
        //     == 1 for boundary edges. Violation means the collapse would
        //     produce a non-manifold mesh.
        //   - Neither endpoint is isolated or deleted.
        [[nodiscard]] std::optional<VertexHandle> Collapse(EdgeHandle e, glm::vec3 newPosition);

        // Directed edge collapse: collapsing halfedge h removes FromVertex(h)
        // and keeps ToVertex(h) at the given position.
        [[nodiscard]] std::optional<VertexHandle> Collapse(HalfedgeHandle h, glm::vec3 newPosition);

        // Edge flip: rotate the edge shared by two adjacent triangles.
        // Given edge (a,b) with adjacent faces (a,b,c) and (b,a,d),
        // the flip produces edge (c,d) with faces (c,d,a) and (d,c,b).
        // Returns true on success.
        //
        // Preconditions checked:
        //   - Edge is interior (not boundary).
        //   - Both adjacent faces are triangles.
        //   - The flip would not create a duplicate edge.
        //   - Neither opposite vertex has valence <= 2 after flip.
        [[nodiscard]] bool Flip(EdgeHandle e);

        // Edge split: insert a new vertex at the given position on edge e,
        // splitting it and its adjacent triangles. For an interior edge with
        // two adjacent triangles, this produces 4 triangles and 1 new vertex.
        // Returns the new vertex handle.
        [[nodiscard]] VertexHandle Split(EdgeHandle e, glm::vec3 position);

        // Check whether collapsing edge e would violate the link condition.
        [[nodiscard]] bool IsCollapseOk(EdgeHandle e) const;

        // Check whether collapsing the directed halfedge h is topologically valid.
        [[nodiscard]] bool IsCollapseOk(HalfedgeHandle h) const;

        // Check whether flipping edge e is topologically valid.
        [[nodiscard]] bool IsFlipOk(EdgeHandle e) const;

        [[nodiscard]] bool HasGarbage() const noexcept { return m_DeletedVertices > 0u || m_DeletedEdges > 0u || m_DeletedFaces > 0u; }

        // -----------------------------------------------------------------
        // Property system access (PMP-style)
        // -----------------------------------------------------------------
        // Our topology operators (Split/Collapse) can optionally propagate
        // user-defined vertex properties. This matches the PMP design where
        // connectivity edits live in the mesh core, while attribute policies
        // are configured by the caller.

        [[nodiscard]] Vertices& VertexProperties() noexcept { return m_Vertices; }
        [[nodiscard]] ConstPropertySet VertexProperties() const noexcept { return ConstPropertySet(m_Vertices); } // NOLINT(readability-convert-member-functions-to-static)

        [[nodiscard]] Edges& EdgeProperties() noexcept { return m_Edges; }
        [[nodiscard]] ConstPropertySet EdgeProperties() const noexcept { return ConstPropertySet(m_Edges); } // NOLINT(readability-convert-member-functions-to-static)

        [[nodiscard]] Faces& FaceProperties() noexcept { return m_Faces; }
        [[nodiscard]] ConstPropertySet FaceProperties() const noexcept { return ConstPropertySet(m_Faces); } // NOLINT(readability-convert-member-functions-to-static)

        [[nodiscard]] Halfedges& HalfedgeProperties() noexcept { return m_Halfedges; }
        [[nodiscard]] ConstPropertySet HalfedgeProperties() const noexcept { return ConstPropertySet(m_Halfedges); } // NOLINT(readability-convert-member-functions-to-static)

        // -----------------------------------------------------------------
        // Bulk edge extraction for GPU upload
        // -----------------------------------------------------------------
        // Returns all non-deleted edges as (FromVertex, ToVertex) index pairs.
        // Uses direct halfedge connectivity array access — O(1) per edge, no
        // per-edge function call overhead. Output is contiguous and ready for
        // SSBO upload as edge index buffer.
        [[nodiscard]] std::vector<EdgeVertexPair> ExtractEdgeVertexPairs() const;

        // Writes non-deleted edge pairs into a pre-allocated output span.
        // Returns the number of pairs actually written (≤ out.size()).
        // Useful when the caller owns the buffer (e.g., staging belt region).
        [[nodiscard]] std::size_t ExtractEdgeVertexPairs(std::span<EdgeVertexPair> out) const;

        // -----------------------------------------------------------------
        // Attribute propagation policies for topology edits
        // -----------------------------------------------------------------
        struct VertexAttributeTransfer
        {
            // Name of the vertex property in the registry (e.g. "v:tex", "v:color").
            std::string Name;

            enum class Policy : std::uint8_t
            {
                // New vertex value = lerp(a, b, t), where t is inferred from the
                // requested split/collapse position projected onto the source edge.
                // Degenerate edges fall back to t = 0.5.
                Average,

                // New vertex value = a, Collapse survivor value = a.
                KeepA,

                // New vertex value = b, Collapse survivor value = b.
                KeepB,

                // Do not modify the property at all.
                None,
            } Rule{Policy::Average};
        };

        // Configure which vertex properties are propagated by subsequent
        // Split/Collapse calls.
        void SetVertexAttributeTransferRules(std::span<const VertexAttributeTransfer> rules);

        // Clears any configured transfer rules (Split/Collapse will only affect topology + v:point).
        void ClearVertexAttributeTransferRules();

    private:
        void EnsureProperties();
        void AdjustOutgoingHalfedge(VertexHandle v);

        [[nodiscard]] VertexHandle NewVertex();
        [[nodiscard]] HalfedgeHandle NewEdge();
        [[nodiscard]] HalfedgeHandle NewEdge(VertexHandle start, VertexHandle end);
        [[nodiscard]] FaceHandle NewFace();

        // Storage
        std::shared_ptr<MeshProperties> m_Properties;
        PropertySet& m_Vertices;
        PropertySet& m_Halfedges;
        PropertySet& m_Edges;
        PropertySet& m_Faces;

        // Core properties
        VertexProperty<glm::vec3> m_VPoint;
        VertexProperty<VertexConnectivity> m_VConn;
        HalfedgeProperty<HalfedgeConnectivity> m_HConn;
        FaceProperty<FaceConnectivity> m_FConn;

        VertexProperty<bool> m_VDeleted;
        EdgeProperty<bool> m_EDeleted;
        FaceProperty<bool> m_FDeleted;

        std::size_t &m_DeletedVertices;
        std::size_t &m_DeletedEdges;
        std::size_t &m_DeletedFaces;

        // Scratch buffers for AddFace (mirrors provided implementation)
        using NextCacheEntry = std::pair<HalfedgeHandle, HalfedgeHandle>;
        using NextCache = std::vector<NextCacheEntry>;
        std::vector<VertexHandle> m_AddFaceVertices;
        std::vector<HalfedgeHandle> m_AddFaceHalfedges;
        std::vector<bool> m_AddFaceIsNew;
        std::vector<bool> m_AddFaceNeedsAdjust;
        NextCache m_AddFaceNextCache;

        // Cached transfer rules.
        std::vector<VertexAttributeTransfer> m_VertexAttrTransfer;

        // Helpers used by Split/Collapse.
        void RemoveLoopHelper(HalfedgeHandle h);
        void TransferVertexAttributes_OnSplit(VertexHandle va, VertexHandle vb, VertexHandle vm, glm::vec3 newPosition);
        void TransferVertexAttributes_OnCollapse(VertexHandle va, VertexHandle vb, VertexHandle vSurvivor, glm::vec3 newPosition);

        friend class ConstMeshView;
    };

    using MeshView = Mesh;
}
