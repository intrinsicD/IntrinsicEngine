module;


#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:HalfedgeMesh;

import :Properties;

export namespace Geometry::Halfedge
{
    // PMP-style connectivity
    struct VertexConnectivity
    {
        HalfedgeHandle Halfedge{};
    };

    struct HalfedgeConnectivity
    {
        VertexHandle Vertex{};     // to-vertex
        FaceHandle Face{};         // incident face (invalid => boundary)
        HalfedgeHandle Next{};
        HalfedgeHandle Prev{};
    };

    struct FaceConnectivity
    {
        HalfedgeHandle Halfedge{};
    };

    class Mesh
    {
    public:
        // Handle iterators/ranges are intentionally deferred; the engine doesn't yet have the iterator helpers.

        Mesh();
        Mesh(const Mesh& rhs);
        Mesh(Mesh&&) noexcept = default;
        ~Mesh();

        Mesh& operator=(const Mesh& rhs);
        Mesh& operator=(Mesh&&) noexcept = default;

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

        [[nodiscard]] bool HasGarbage() const noexcept { return m_HasGarbage; }

    private:
        void EnsureProperties();
        void AdjustOutgoingHalfedge(VertexHandle v);

        [[nodiscard]] VertexHandle NewVertex();
        [[nodiscard]] HalfedgeHandle NewEdge();
        [[nodiscard]] HalfedgeHandle NewEdge(VertexHandle start, VertexHandle end);
        [[nodiscard]] FaceHandle NewFace();

        // Storage
        PropertySet m_Vertices;
        PropertySet m_Halfedges;
        PropertySet m_Edges;
        PropertySet m_Faces;

        // Core properties
        VertexProperty<glm::vec3> m_VPoint;
        VertexProperty<VertexConnectivity> m_VConn;
        HalfedgeProperty<HalfedgeConnectivity> m_HConn;
        FaceProperty<FaceConnectivity> m_FConn;

        VertexProperty<bool> m_VDeleted;
        EdgeProperty<bool> m_EDeleted;
        FaceProperty<bool> m_FDeleted;

        PropertyIndex m_DeletedVertices{0};
        PropertyIndex m_DeletedEdges{0};
        PropertyIndex m_DeletedFaces{0};

        bool m_HasGarbage{false};

        // Scratch buffers for AddFace (mirrors provided implementation)
        using NextCacheEntry = std::pair<HalfedgeHandle, HalfedgeHandle>;
        using NextCache = std::vector<NextCacheEntry>;
        std::vector<VertexHandle> m_AddFaceVertices;
        std::vector<HalfedgeHandle> m_AddFaceHalfedges;
        std::vector<bool> m_AddFaceIsNew;
        std::vector<bool> m_AddFaceNeedsAdjust;
        NextCache m_AddFaceNextCache;
    };
}
