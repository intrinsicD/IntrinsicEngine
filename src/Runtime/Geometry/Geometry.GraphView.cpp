module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Geometry.GraphView;

namespace Geometry::Graph
{
    Graph::Graph()
    {
        EnsureProperties();
    }

    Graph::Graph(const Graph& rhs) = default;
    Graph::~Graph() = default;
    Graph& Graph::operator=(const Graph& rhs) = default;

    void Graph::EnsureProperties()
    {
        m_VPoint = Geometry::VertexProperty<glm::vec3>(m_Vertices.GetOrAdd<glm::vec3>("v:point", glm::vec3(0.0f)));
        m_VConn = Geometry::VertexProperty<VertexConnectivity>(m_Vertices.GetOrAdd<VertexConnectivity>("v:connectivity", {}));
        m_HConn = Geometry::HalfedgeProperty<HalfedgeConnectivity>(m_Halfedges.GetOrAdd<HalfedgeConnectivity>("h:connectivity", {}));

        m_VDeleted = Geometry::VertexProperty<bool>(m_Vertices.GetOrAdd<bool>("v:deleted", false));
        m_EDeleted = Geometry::EdgeProperty<bool>(m_Edges.GetOrAdd<bool>("e:deleted", false));
    }

    void Graph::Clear()
    {
        m_Vertices.Clear();
        m_Halfedges.Clear();
        m_Edges.Clear();

        EnsureProperties();

        m_DeletedVertices = 0;
        m_DeletedEdges = 0;
        m_HasGarbage = false;
    }

    void Graph::FreeMemory()
    {
        m_Vertices.Shrink_to_fit();
        m_Halfedges.Shrink_to_fit();
        m_Edges.Shrink_to_fit();
    }

    void Graph::Reserve(std::size_t nVertices, std::size_t nEdges)
    {
        m_Vertices.Registry().Reserve(nVertices);
        m_Halfedges.Registry().Reserve(2 * nEdges);
        m_Edges.Registry().Reserve(nEdges);
    }

    VertexHandle Graph::NewVertex()
    {
        if (VerticesSize() >= kInvalidIndex) return {};
        m_Vertices.Resize(VerticesSize() + 1);
        return VertexHandle{static_cast<PropertyIndex>(VerticesSize() - 1)};
    }

    HalfedgeHandle Graph::NewEdge()
    {
        if (HalfedgesSize() >= kInvalidIndex) return {};

        m_Edges.Resize(EdgesSize() + 1);
        m_Halfedges.Resize(HalfedgesSize() + 2);

        return HalfedgeHandle{static_cast<PropertyIndex>(HalfedgesSize() - 2)};
    }

    HalfedgeHandle Graph::NewEdge(VertexHandle start, VertexHandle end)
    {
        assert(start != end);
        if (HalfedgesSize() >= kInvalidIndex) return {};

        m_Edges.Resize(EdgesSize() + 1);
        m_Halfedges.Resize(HalfedgesSize() + 2);

        const HalfedgeHandle h0{static_cast<PropertyIndex>(HalfedgesSize() - 2)};
        const HalfedgeHandle h1{static_cast<PropertyIndex>(HalfedgesSize() - 1)};

        // Define a minimal boundary convention: for an undirected edge {start,end}
        // h0 is start->end and h1 is end->start, and each halfedge's boundary loop
        // is its own opposite (so iterators/circulators can treat it as boundary).
        SetVertex(h0, end);
        SetVertex(h1, start);

        SetNextHalfedge(h0, h1);
        SetNextHalfedge(h1, h0);

        return h0;
    }

    VertexHandle Graph::AddVertex()
    {
        return NewVertex();
    }

    VertexHandle Graph::AddVertex(glm::vec3 position)
    {
        const VertexHandle v = NewVertex();
        if (v.IsValid())
        {
            m_VPoint[v] = position;
        }
        return v;
    }

    HalfedgeHandle Graph::Halfedge(VertexHandle v) const
    {
        return m_VConn[v].Halfedge;
    }

    void Graph::SetHalfedge(VertexHandle v, HalfedgeHandle h)
    {
        m_VConn[v].Halfedge = h;
    }

    bool Graph::IsIsolated(VertexHandle v) const
    {
        return !Halfedge(v).IsValid();
    }

    VertexHandle Graph::ToVertex(HalfedgeHandle h) const
    {
        return m_HConn[h].Vertex;
    }

    void Graph::SetVertex(HalfedgeHandle h, VertexHandle v)
    {
        m_HConn[h].Vertex = v;
    }

    HalfedgeHandle Graph::NextHalfedge(HalfedgeHandle h) const
    {
        return m_HConn[h].Next;
    }

    HalfedgeHandle Graph::PrevHalfedge(HalfedgeHandle h) const
    {
        return m_HConn[h].Prev;
    }

    void Graph::SetNextHalfedge(HalfedgeHandle h, HalfedgeHandle next)
    {
        m_HConn[h].Next = next;
        m_HConn[next].Prev = h;
    }

    void Graph::SetPrevHalfedge(HalfedgeHandle h, HalfedgeHandle prev)
    {
        m_HConn[h].Prev = prev;
        m_HConn[prev].Next = h;
    }

    HalfedgeHandle Graph::OppositeHalfedge(HalfedgeHandle h) const
    {
        return HalfedgeHandle{static_cast<PropertyIndex>((h.Index & 1U) ? (h.Index - 1U) : (h.Index + 1U))};
    }

    EdgeHandle Graph::Edge(HalfedgeHandle h) const
    {
        return EdgeHandle{static_cast<PropertyIndex>(h.Index >> 1U)};
    }

    HalfedgeHandle Graph::Halfedge(EdgeHandle e, unsigned int i) const
    {
        assert(i <= 1);
        return HalfedgeHandle{static_cast<PropertyIndex>((e.Index << 1U) + i)};
    }

    bool Graph::IsBoundary(VertexHandle v) const
    {
        // Boundary convention for graphs: a vertex is boundary if its outgoing halfedge
        // lives on a boundary loop (next == opposite), enabling boundary-aware traversal.
        const HalfedgeHandle h = Halfedge(v);
        return h.IsValid() && NextHalfedge(h) == OppositeHalfedge(h);
    }

    std::optional<HalfedgeHandle> Graph::FindHalfedge(VertexHandle start, VertexHandle end) const
    {
        assert(IsValid(start) && IsValid(end));

        for (const HalfedgeHandle h : HalfedgesAroundVertex(start))
        {
            if (ToVertex(h) == end)
            {
                return h;
            }
        }

        return std::nullopt;
    }

    std::optional<EdgeHandle> Graph::FindEdge(VertexHandle a, VertexHandle b) const
    {
        if (auto h = FindHalfedge(a, b)) return Edge(*h);
        return std::nullopt;
    }

    glm::vec3 Graph::VertexPosition(VertexHandle v) const
    {
        assert(IsValid(v));
        return m_VPoint[v];
    }

    void Graph::SetVertexPosition(VertexHandle v, glm::vec3 position)
    {
        assert(IsValid(v));
        m_VPoint[v] = position;
    }

    std::pair<VertexHandle, VertexHandle> Graph::EdgeVertices(EdgeHandle e) const
    {
        assert(IsValid(e));
        const HalfedgeHandle h0 = Halfedge(e, 0);
        const HalfedgeHandle h1 = OppositeHalfedge(h0);
        return {ToVertex(h1), ToVertex(h0)};
    }

    std::optional<EdgeHandle> Graph::AddEdge(VertexHandle v0, VertexHandle v1)
    {
        if (!IsValid(v0) || !IsValid(v1) || v0 == v1) return std::nullopt;

        if (FindEdge(v0, v1).has_value() || FindEdge(v1, v0).has_value())
        {
            return std::nullopt;
        }

        const HalfedgeHandle h0 = NewEdge(v0, v1);
        if (!h0.IsValid()) return std::nullopt;

        const HalfedgeHandle h1 = OppositeHalfedge(h0);

        // Stitch into vertex stars.
        // We maintain a circular list of outgoing halfedges per vertex via (next/opposite).
        // For the simplest policy here, insert h0/h1 as isolated boundary loops if the vertex
        // has no halfedge; otherwise splice them after the current representative.
        auto splice_into_vertex = [&](VertexHandle v, HalfedgeHandle h)
        {
            if (IsIsolated(v))
            {
                SetHalfedge(v, h);
                // already boundary-looped in NewEdge
                return;
            }

            const HalfedgeHandle hv = Halfedge(v);
            // Insert h after hv in the outgoing ring.
            const HalfedgeHandle hvNext = NextHalfedge(OppositeHalfedge(hv));

            // Make (Opp(hv))->next point to h, and Opp(h) -> next point to hvNext.
            SetNextHalfedge(OppositeHalfedge(hv), h);
            SetNextHalfedge(OppositeHalfedge(h), hvNext);
        };

        splice_into_vertex(v0, h0);
        splice_into_vertex(v1, h1);

        return Edge(h0);
    }


}
