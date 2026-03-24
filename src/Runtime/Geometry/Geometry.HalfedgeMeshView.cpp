module;

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <span>
#include <vector>

module Geometry.HalfedgeMeshView;

import Geometry.HalfedgeMesh;

namespace Geometry::Halfedge
{
    ConstMeshView::ConstMeshView(const Mesh& mesh) noexcept
        : ConstMeshView(mesh.m_Vertices, mesh.m_Halfedges, mesh.m_Edges, mesh.m_Faces)
    {
        m_HasGarbage = mesh.m_HasGarbage;
    }

    bool ConstMeshView::IsManifold(VertexHandle v) const
    {
        int gaps = 0;
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            std::size_t safety = 0;
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                if (IsBoundary(h)) ++gaps;
                h = CWRotatedHalfedge(h);
                if (++safety > maxIter) break;
            } while (h != start);
        }
        return gaps < 2;
    }

    std::optional<HalfedgeHandle> ConstMeshView::FindHalfedge(VertexHandle start, VertexHandle end) const
    {
        assert(IsValid(start) && IsValid(end));

        HalfedgeHandle h = Halfedge(start);
        const HalfedgeHandle hh = h;

        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            std::size_t iter = 0;
            do
            {
                if (ToVertex(h) == end) return h;
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) break;
            } while (h != hh);
        }

        return std::nullopt;
    }

    std::optional<EdgeHandle> ConstMeshView::FindEdge(VertexHandle a, VertexHandle b) const
    {
        if (auto h = FindHalfedge(a, b)) return Edge(*h);
        return std::nullopt;
    }

    std::size_t ConstMeshView::Valence(VertexHandle v) const
    {
        std::size_t count = 0;
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            std::size_t iter = 0;
            do
            {
                ++count;
                h = CWRotatedHalfedge(h);
                if (++iter > maxIter) return count;
            } while (h != start);
        }
        return count;
    }

    std::size_t ConstMeshView::Valence(FaceHandle f) const
    {
        std::size_t count = 0;
        HalfedgeHandle h = Halfedge(f);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                ++count;
                h = NextHalfedge(h);
                if (count > maxIter) break;
            } while (h != start);
        }
        return count;
    }

    std::vector<EdgeVertexPair> ConstMeshView::ExtractEdgeVertexPairs() const
    {
        const std::size_t nEdges = EdgesSize();
        if (nEdges == 0)
            return {};

        const auto hConn = m_HConn.Span();

        std::vector<EdgeVertexPair> result;
        result.reserve(EdgeCount());

        for (std::size_t i = 0; i < nEdges; ++i)
        {
            if (m_EDeleted.Handle()[i])
                continue;

            const auto v0 = static_cast<uint32_t>(hConn[2 * i + 1].Vertex.Index);
            const auto v1 = static_cast<uint32_t>(hConn[2 * i].Vertex.Index);
            result.push_back({v0, v1});
        }

        return result;
    }

    std::size_t ConstMeshView::ExtractEdgeVertexPairs(std::span<EdgeVertexPair> out) const
    {
        const std::size_t nEdges = EdgesSize();
        if (nEdges == 0 || out.empty())
            return 0;

        const auto hConn = m_HConn.Span();

        std::size_t written = 0;

        for (std::size_t i = 0; i < nEdges && written < out.size(); ++i)
        {
            if (m_EDeleted.Handle()[i])
                continue;

            out[written++] = EdgeVertexPair{
                static_cast<uint32_t>(hConn[2 * i + 1].Vertex.Index),
                static_cast<uint32_t>(hConn[2 * i].Vertex.Index)};
        }

        return written;
    }

    void Mesh::EnsureProperties()
    {
        // Built-in properties (match PMP naming to ease porting/debugging)
        m_VPoint = VertexProperty<glm::vec3>(m_Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3(0.0f)));
        m_VConn = VertexProperty<VertexConnectivity>(m_Vertices.GetOrAdd<VertexConnectivity>("v:connectivity", {}));
        m_HConn = HalfedgeProperty<HalfedgeConnectivity>(m_Halfedges.GetOrAdd<HalfedgeConnectivity>("h:connectivity", {}));
        m_FConn = FaceProperty<FaceConnectivity>(m_Faces.GetOrAdd<FaceConnectivity>("f:connectivity", {}));

        m_VDeleted = VertexProperty<bool>(m_Vertices.GetOrAdd<bool>("v:deleted", false));
        m_EDeleted = EdgeProperty<bool>(m_Edges.GetOrAdd<bool>("e:deleted", false));
        m_FDeleted = FaceProperty<bool>(m_Faces.GetOrAdd<bool>("f:deleted", false));
    }

    void Mesh::Clear()
    {
        m_Vertices.Clear();
        m_Halfedges.Clear();
        m_Edges.Clear();
        m_Faces.Clear();

        EnsureProperties();

        m_DeletedVertices = 0;
        m_DeletedEdges = 0;
        m_DeletedFaces = 0;
        m_HasGarbage = false;
    }

    void Mesh::FreeMemory()
    {
        m_Vertices.Shrink_to_fit();
        m_Halfedges.Shrink_to_fit();
        m_Edges.Shrink_to_fit();
        m_Faces.Shrink_to_fit();
    }

    void Mesh::Reserve(std::size_t nVertices, std::size_t nEdges, std::size_t nFaces)
    {
        m_Vertices.Registry().Reserve(nVertices);
        m_Halfedges.Registry().Reserve(2 * nEdges);
        m_Edges.Registry().Reserve(nEdges);
        m_Faces.Registry().Reserve(nFaces);
    }

    VertexHandle Mesh::NewVertex()
    {
        if (VerticesSize() >= kInvalidIndex) return {};
        // PropertySet::PushBack doesn't currently bump registry size; grow explicitly.
        m_Vertices.Resize(VerticesSize() + 1);
        return VertexHandle{static_cast<PropertyIndex>(VerticesSize() - 1)};
    }

    HalfedgeHandle Mesh::NewEdge()
    {
        if (HalfedgesSize() >= kInvalidIndex) return {};

        // One edge => 2 halfedges.
        m_Edges.Resize(EdgesSize() + 1);
        m_Halfedges.Resize(HalfedgesSize() + 2);

        return HalfedgeHandle{static_cast<PropertyIndex>(HalfedgesSize() - 2)};
    }

    HalfedgeHandle Mesh::NewEdge(VertexHandle start, VertexHandle end)
    {
        assert(start != end);
        if (HalfedgesSize() >= kInvalidIndex) return {};

        m_Edges.Resize(EdgesSize() + 1);
        m_Halfedges.Resize(HalfedgesSize() + 2);

        const HalfedgeHandle h0{static_cast<PropertyIndex>(HalfedgesSize() - 2)};
        const HalfedgeHandle h1{static_cast<PropertyIndex>(HalfedgesSize() - 1)};

        SetVertex(h0, end);
        SetVertex(h1, start);

        return h0;
    }

    FaceHandle Mesh::NewFace()
    {
        if (FacesSize() >= kInvalidIndex) return {};
        m_Faces.Resize(FacesSize() + 1);
        return FaceHandle{static_cast<PropertyIndex>(FacesSize() - 1)};
    }

    VertexHandle Mesh::AddVertex()
    {
        return NewVertex();
    }

    VertexHandle Mesh::AddVertex(glm::vec3 position)
    {
        const VertexHandle v = NewVertex();
        if (v.IsValid())
        {
            m_VPoint[v] = position;
        }
        return v;
    }

    bool Mesh::IsBoundary(VertexHandle v) const
    {
        const HalfedgeHandle h = Halfedge(v);
        return !(h.IsValid() && Face(h).IsValid());
    }

    bool Mesh::IsManifold(VertexHandle v) const
    {
        int gaps = 0;
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle start = h;
        if (h.IsValid())
        {
            std::size_t safety = 0;
            const std::size_t maxIter = HalfedgesSize();
            do
            {
                if (IsBoundary(h)) ++gaps;
                h = CWRotatedHalfedge(h);
                if (++safety > maxIter) break;
            } while (h != start);
        }
        return gaps < 2;
    }

    HalfedgeHandle Mesh::Halfedge(EdgeHandle e, unsigned int i) const
    {
        assert(i <= 1);
        return HalfedgeHandle{static_cast<PropertyIndex>((e.Index << 1u) + i)};
    }

    bool Mesh::IsBoundary(EdgeHandle e) const
    {
        return IsBoundary(Halfedge(e, 0)) || IsBoundary(Halfedge(e, 1));
    }

    bool Mesh::IsBoundary(FaceHandle f) const
    {
        HalfedgeHandle h = Halfedge(f);
        const HalfedgeHandle start = h;
        std::size_t safety = 0;
        const std::size_t maxIter = HalfedgesSize();
        do
        {
            if (IsBoundary(OppositeHalfedge(h))) return true;
            h = NextHalfedge(h);
            if (++safety > maxIter) break;
        } while (h != start);
        return false;
    }

}
