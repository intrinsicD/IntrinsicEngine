module;


#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.HalfedgeMeshView;

import Geometry.Properties;
import Geometry.Circulators;

export namespace Geometry::Halfedge
{
    class Mesh;

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

    // GPU-friendly edge representation: pair of vertex indices.
    // Layout-compatible with the SSBO EdgePair struct consumed by line shaders.
    struct EdgeVertexPair
    {
        uint32_t i0;
        uint32_t i1;
    };
    static_assert(sizeof(EdgeVertexPair) == 8);

    struct MeshProperties
    {
        PropertySet Vertices{};
        PropertySet Halfedges{};
        PropertySet Edges{};
        PropertySet Faces{};
    };

    class ConstMeshView
    {
    public:
        using TraversalSentinel = Circulators::TraversalSentinel;
        using HalfedgesAroundFaceRange = Circulators::HalfedgesAroundFaceRange<ConstMeshView>;
        using VerticesAroundFaceRange = Circulators::VerticesAroundFaceRange<ConstMeshView>;
        using HalfedgesAroundVertexRange = Circulators::HalfedgesAroundVertexRange<ConstMeshView>;
        using FacesAroundVertexRange = Circulators::FacesAroundVertexRange<ConstMeshView>;
        using BoundaryHalfedgesRange = Circulators::BoundaryHalfedgesRange<ConstMeshView>;
        using BoundaryVerticesRange = Circulators::BoundaryVerticesRange<ConstMeshView>;

        explicit ConstMeshView(const Mesh& mesh) noexcept;
        ConstMeshView(const PropertySet& vertices,
                      const PropertySet& halfedges,
                      const PropertySet& edges,
                      const PropertySet& faces) noexcept;

        [[nodiscard]] std::size_t VerticesSize() const noexcept;
        [[nodiscard]] std::size_t HalfedgesSize() const noexcept;
        [[nodiscard]] std::size_t EdgesSize() const noexcept;
        [[nodiscard]] std::size_t FacesSize() const noexcept;

        [[nodiscard]] std::size_t VertexCount() const noexcept;
        [[nodiscard]] std::size_t HalfedgeCount() const noexcept;
        [[nodiscard]] std::size_t EdgeCount() const noexcept;
        [[nodiscard]] std::size_t FaceCount() const noexcept;

        [[nodiscard]] bool IsEmpty() const noexcept;
        [[nodiscard]] bool HasGarbage() const noexcept;

        [[nodiscard]] bool IsValid(VertexHandle v) const;
        [[nodiscard]] bool IsValid(HalfedgeHandle h) const;
        [[nodiscard]] bool IsValid(EdgeHandle e) const;
        [[nodiscard]] bool IsValid(FaceHandle f) const;

        [[nodiscard]] bool IsDeleted(VertexHandle v) const;
        [[nodiscard]] bool IsDeleted(HalfedgeHandle h) const;
        [[nodiscard]] bool IsDeleted(EdgeHandle e) const;
        [[nodiscard]] bool IsDeleted(FaceHandle f) const;

        [[nodiscard]] HalfedgeHandle Halfedge(VertexHandle v) const;
        [[nodiscard]] VertexHandle ToVertex(HalfedgeHandle h) const;
        [[nodiscard]] VertexHandle FromVertex(HalfedgeHandle h) const;
        [[nodiscard]] FaceHandle Face(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle NextHalfedge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle PrevHalfedge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle OppositeHalfedge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle CCWRotatedHalfedge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle CWRotatedHalfedge(HalfedgeHandle h) const;
        [[nodiscard]] EdgeHandle Edge(HalfedgeHandle h) const;
        [[nodiscard]] HalfedgeHandle Halfedge(EdgeHandle e, unsigned int i) const;
        [[nodiscard]] HalfedgeHandle Halfedge(FaceHandle f) const;

        [[nodiscard]] bool IsBoundary(HalfedgeHandle h) const;
        [[nodiscard]] bool IsBoundary(VertexHandle v) const;
        [[nodiscard]] bool IsBoundary(EdgeHandle e) const;
        [[nodiscard]] bool IsBoundary(FaceHandle f) const;

        [[nodiscard]] HalfedgesAroundFaceRange HalfedgesAroundFace(FaceHandle f) const;
        [[nodiscard]] VerticesAroundFaceRange VerticesAroundFace(FaceHandle f) const;
        [[nodiscard]] HalfedgesAroundVertexRange HalfedgesAroundVertex(VertexHandle v) const;
        [[nodiscard]] FacesAroundVertexRange FacesAroundVertex(VertexHandle v) const;
        [[nodiscard]] BoundaryHalfedgesRange BoundaryHalfedges(HalfedgeHandle h) const;
        [[nodiscard]] BoundaryVerticesRange BoundaryVertices(HalfedgeHandle h) const;

        [[nodiscard]] bool IsIsolated(VertexHandle v) const;
        [[nodiscard]] bool IsManifold(VertexHandle v) const;

        [[nodiscard]] const glm::vec3& Position(VertexHandle v) const;
        [[nodiscard]] std::span<const glm::vec3> Positions() const;

        [[nodiscard]] std::optional<HalfedgeHandle> FindHalfedge(VertexHandle start, VertexHandle end) const;
        [[nodiscard]] std::optional<EdgeHandle> FindEdge(VertexHandle a, VertexHandle b) const;

        [[nodiscard]] std::size_t Valence(VertexHandle v) const;
        [[nodiscard]] std::size_t Valence(FaceHandle f) const;

        [[nodiscard]] std::vector<EdgeVertexPair> ExtractEdgeVertexPairs() const;
        [[nodiscard]] std::size_t ExtractEdgeVertexPairs(std::span<EdgeVertexPair> out) const;

        [[nodiscard]] ConstPropertySet VertexProperties() const noexcept;
        [[nodiscard]] ConstPropertySet HalfedgeProperties() const noexcept;
        [[nodiscard]] ConstPropertySet EdgeProperties() const noexcept;
        [[nodiscard]] ConstPropertySet FaceProperties() const noexcept;

    protected:
        const PropertySet& m_Vertices;
        const PropertySet& m_Halfedges;
        const PropertySet& m_Edges;
        const PropertySet& m_Faces;

        ConstProperty<glm::vec3> m_VPoint;
        ConstProperty<VertexConnectivity> m_VConn;
        ConstProperty<HalfedgeConnectivity> m_HConn;
        ConstProperty<FaceConnectivity> m_FConn;

        ConstProperty<bool> m_VDeleted;
        ConstProperty<bool> m_EDeleted;
        ConstProperty<bool> m_FDeleted;

        PropertyIndex m_DeletedVertices{0};
        PropertyIndex m_DeletedEdges{0};
        PropertyIndex m_DeletedFaces{0};

        bool m_HasGarbage{false};
    };


}

namespace Geometry::Halfedge
{
    inline ConstMeshView::ConstMeshView(const PropertySet& vertices,
                                         const PropertySet& halfedges,
                                         const PropertySet& edges,
                                         const PropertySet& faces) noexcept
        : m_Vertices(vertices)
        , m_Halfedges(halfedges)
        , m_Edges(edges)
        , m_Faces(faces)
        , m_VPoint(ConstPropertySet(m_Vertices).Get<glm::vec3>("v:point"))
        , m_VConn(ConstPropertySet(m_Vertices).Get<VertexConnectivity>("v:connectivity"))
        , m_HConn(ConstPropertySet(m_Halfedges).Get<HalfedgeConnectivity>("h:connectivity"))
        , m_FConn(ConstPropertySet(m_Faces).Get<FaceConnectivity>("f:connectivity"))
        , m_VDeleted(ConstPropertySet(m_Vertices).Get<bool>("v:deleted"))
        , m_EDeleted(ConstPropertySet(m_Edges).Get<bool>("e:deleted"))
        , m_FDeleted(ConstPropertySet(m_Faces).Get<bool>("f:deleted"))
    {
        for (bool deleted : m_VDeleted.Vector()) if (deleted) ++m_DeletedVertices;
        for (bool deleted : m_EDeleted.Vector()) if (deleted) ++m_DeletedEdges;
        for (bool deleted : m_FDeleted.Vector()) if (deleted) ++m_DeletedFaces;
        m_HasGarbage = (m_DeletedVertices != 0) || (m_DeletedEdges != 0) || (m_DeletedFaces != 0);
    }

    inline std::size_t ConstMeshView::VerticesSize() const noexcept { return m_Vertices.Size(); }
    inline std::size_t ConstMeshView::HalfedgesSize() const noexcept { return m_Halfedges.Size(); }
    inline std::size_t ConstMeshView::EdgesSize() const noexcept { return m_Edges.Size(); }
    inline std::size_t ConstMeshView::FacesSize() const noexcept { return m_Faces.Size(); }

    inline std::size_t ConstMeshView::VertexCount() const noexcept { return VerticesSize() - m_DeletedVertices; }
    inline std::size_t ConstMeshView::HalfedgeCount() const noexcept { return HalfedgesSize() - 2u * m_DeletedEdges; }
    inline std::size_t ConstMeshView::EdgeCount() const noexcept { return EdgesSize() - m_DeletedEdges; }
    inline std::size_t ConstMeshView::FaceCount() const noexcept { return FacesSize() - m_DeletedFaces; }

    inline bool ConstMeshView::IsEmpty() const noexcept { return VertexCount() == 0; }
    inline bool ConstMeshView::HasGarbage() const noexcept { return m_HasGarbage; }

    inline bool ConstMeshView::IsValid(VertexHandle v) const { return v.Index < VerticesSize(); }
    inline bool ConstMeshView::IsValid(HalfedgeHandle h) const { return h.Index < HalfedgesSize(); }
    inline bool ConstMeshView::IsValid(EdgeHandle e) const { return e.Index < EdgesSize(); }
    inline bool ConstMeshView::IsValid(FaceHandle f) const { return f.Index < FacesSize(); }

    inline bool ConstMeshView::IsDeleted(VertexHandle v) const { return m_VDeleted.Vector()[v.Index]; }
    inline bool ConstMeshView::IsDeleted(HalfedgeHandle h) const { return m_EDeleted.Vector()[Edge(h).Index]; }
    inline bool ConstMeshView::IsDeleted(EdgeHandle e) const { return m_EDeleted.Vector()[e.Index]; }
    inline bool ConstMeshView::IsDeleted(FaceHandle f) const { return m_FDeleted.Vector()[f.Index]; }

    inline HalfedgeHandle ConstMeshView::Halfedge(VertexHandle v) const { return m_VConn.Vector()[v.Index].Halfedge; }
    inline VertexHandle ConstMeshView::ToVertex(HalfedgeHandle h) const { return m_HConn.Vector()[h.Index].Vertex; }
    inline VertexHandle ConstMeshView::FromVertex(HalfedgeHandle h) const { return ToVertex(OppositeHalfedge(h)); }
    inline FaceHandle ConstMeshView::Face(HalfedgeHandle h) const { return m_HConn.Vector()[h.Index].Face; }
    inline HalfedgeHandle ConstMeshView::NextHalfedge(HalfedgeHandle h) const { return m_HConn.Vector()[h.Index].Next; }
    inline HalfedgeHandle ConstMeshView::PrevHalfedge(HalfedgeHandle h) const { return m_HConn.Vector()[h.Index].Prev; }
    inline HalfedgeHandle ConstMeshView::OppositeHalfedge(HalfedgeHandle h) const { return HalfedgeHandle{static_cast<PropertyIndex>((h.Index & 1u) ? (h.Index - 1u) : (h.Index + 1u))}; }
    inline HalfedgeHandle ConstMeshView::CCWRotatedHalfedge(HalfedgeHandle h) const { return OppositeHalfedge(PrevHalfedge(h)); }
    inline HalfedgeHandle ConstMeshView::CWRotatedHalfedge(HalfedgeHandle h) const { return NextHalfedge(OppositeHalfedge(h)); }
    inline EdgeHandle ConstMeshView::Edge(HalfedgeHandle h) const { return EdgeHandle{static_cast<PropertyIndex>(h.Index >> 1u)}; }
    inline HalfedgeHandle ConstMeshView::Halfedge(EdgeHandle e, unsigned int i) const { return i == 0 ? HalfedgeHandle{static_cast<PropertyIndex>(2u * e.Index)} : HalfedgeHandle{static_cast<PropertyIndex>(2u * e.Index + 1u)}; }
    inline HalfedgeHandle ConstMeshView::Halfedge(FaceHandle f) const { return m_FConn.Vector()[f.Index].Halfedge; }

    inline bool ConstMeshView::IsBoundary(HalfedgeHandle h) const { return !Face(h).IsValid(); }
    inline bool ConstMeshView::IsBoundary(VertexHandle v) const { return IsIsolated(v) || IsBoundary(Halfedge(v)); }
    inline bool ConstMeshView::IsBoundary(EdgeHandle e) const { return IsBoundary(Halfedge(e, 0)) || IsBoundary(Halfedge(e, 1)); }
    inline bool ConstMeshView::IsBoundary(FaceHandle f) const { return !Halfedge(f).IsValid() || IsBoundary(Halfedge(f)); }

    inline ConstMeshView::HalfedgesAroundFaceRange ConstMeshView::HalfedgesAroundFace(FaceHandle f) const { return HalfedgesAroundFaceRange{this, f}; }
    inline ConstMeshView::VerticesAroundFaceRange ConstMeshView::VerticesAroundFace(FaceHandle f) const { return VerticesAroundFaceRange{this, f}; }
    inline ConstMeshView::HalfedgesAroundVertexRange ConstMeshView::HalfedgesAroundVertex(VertexHandle v) const { return HalfedgesAroundVertexRange{this, v}; }
    inline ConstMeshView::FacesAroundVertexRange ConstMeshView::FacesAroundVertex(VertexHandle v) const { return FacesAroundVertexRange{this, v}; }
    inline ConstMeshView::BoundaryHalfedgesRange ConstMeshView::BoundaryHalfedges(HalfedgeHandle h) const { return BoundaryHalfedgesRange{this, h}; }
    inline ConstMeshView::BoundaryVerticesRange ConstMeshView::BoundaryVertices(HalfedgeHandle h) const { return BoundaryVerticesRange{this, h}; }

    inline bool ConstMeshView::IsIsolated(VertexHandle v) const { return !Halfedge(v).IsValid(); }

    inline const glm::vec3& ConstMeshView::Position(VertexHandle v) const { return m_VPoint.Vector()[v.Index]; }
    inline std::span<const glm::vec3> ConstMeshView::Positions() const { return m_VPoint.Span(); }

    [[nodiscard]] ConstPropertySet ConstMeshView::VertexProperties() const noexcept { return ConstPropertySet(m_Vertices); } // NOLINT(readability-convert-member-functions-to-static)
    [[nodiscard]] ConstPropertySet ConstMeshView::HalfedgeProperties() const noexcept { return ConstPropertySet(m_Halfedges); } // NOLINT(readability-convert-member-functions-to-static)
    [[nodiscard]] ConstPropertySet ConstMeshView::EdgeProperties() const noexcept { return ConstPropertySet(m_Edges); } // NOLINT(readability-convert-member-functions-to-static)
    [[nodiscard]] ConstPropertySet ConstMeshView::FaceProperties() const noexcept { return ConstPropertySet(m_Faces); } // NOLINT(readability-convert-member-functions-to-static)
}
