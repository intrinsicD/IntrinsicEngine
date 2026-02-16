module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Geometry:Graph.Impl;

import :Graph;
import :Properties;

namespace Geometry::Graph
{
    namespace
    {
        struct Neighbor
        {
            std::uint32_t Index{0};
            float Distance2{0.0F};
        };
    }

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

        HalfedgeHandle h = Halfedge(start);
        const HalfedgeHandle startH = h;

        if (h.IsValid())
        {
            do
            {
                if (ToVertex(h) == end) return h;
                h = NextHalfedge(OppositeHalfedge(h));
            } while (h != startH);
        }

        return std::nullopt;
    }

    std::optional<EdgeHandle> Graph::FindEdge(VertexHandle a, VertexHandle b) const
    {
        if (auto h = FindHalfedge(a, b)) return Edge(*h);
        return std::nullopt;
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


    std::optional<KNNBuildResult> BuildKNNGraphFromIndices(Graph& graph, std::span<const glm::vec3> points,
        std::span<const std::vector<std::uint32_t>> knnIndices, const KNNFromIndicesParams& params)
    {
        if (points.empty() || knnIndices.empty() || points.size() != knnIndices.size()) return std::nullopt;

        graph.Clear();

        std::size_t reservedEdges = 0;
        for (const auto& neighbors : knnIndices) reservedEdges += neighbors.size();

        graph.Reserve(points.size(), reservedEdges);
        for (const glm::vec3& point : points)
        {
            graph.AddVertex(point);
        }

        const std::size_t n = points.size();
        const float minDistance2 = std::max(0.0F, params.MinDistanceEpsilon * params.MinDistanceEpsilon);

        KNNBuildResult result{};
        result.VertexCount = n;

        auto has_reverse_edge = [&](std::uint32_t i, std::uint32_t j)
        {
            const auto& reverse = knnIndices[static_cast<std::size_t>(j)];
            return std::find(reverse.begin(), reverse.end(), i) != reverse.end();
        };

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i)
        {
            const auto& neighbors = knnIndices[static_cast<std::size_t>(i)];
            for (const std::uint32_t j : neighbors)
            {
                if (j >= n || i == j)
                {
                    ++result.DegeneratePairCount;
                    continue;
                }

                const glm::vec3 d = points[static_cast<std::size_t>(j)] - points[static_cast<std::size_t>(i)];
                const float distance2 = glm::dot(d, d);
                if (!std::isfinite(distance2) || distance2 <= minDistance2)
                {
                    ++result.DegeneratePairCount;
                    continue;
                }

                ++result.CandidateEdgeCount;

                if (params.Connectivity == KNNConnectivity::Mutual && !has_reverse_edge(i, j)) continue;

                const auto edge = graph.AddEdge(VertexHandle{i}, VertexHandle{j});
                if (edge.has_value()) ++result.InsertedEdgeCount;
            }
        }

        return result;
    }

    std::optional<KNNBuildResult> BuildKNNGraph(Graph& graph, std::span<const glm::vec3> points,
        const KNNBuildParams& params)
    {
        if (points.empty() || params.K == 0U) return std::nullopt;

        graph.Clear();
        graph.Reserve(points.size(), points.size() * static_cast<std::size_t>(params.K));

        for (const glm::vec3& point : points)
        {
            graph.AddVertex(point);
        }

        const std::size_t n = points.size();
        const std::size_t effectiveK = std::min<std::size_t>(params.K, n - 1U);

        KNNBuildResult result{};
        result.VertexCount = n;
        result.RequestedK = params.K;
        result.EffectiveK = effectiveK;

        if (effectiveK == 0U) return result;

        std::vector<std::vector<std::uint32_t>> neighborhoods(n);
        std::vector<Neighbor> candidates;
        candidates.reserve(n > 0 ? n - 1U : 0U);

        const float minDistance2 = std::max(0.0F, params.MinDistanceEpsilon * params.MinDistanceEpsilon);

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i)
        {
            candidates.clear();
            candidates.reserve(n - 1U);

            for (std::uint32_t j = 0; j < static_cast<std::uint32_t>(n); ++j)
            {
                if (i == j) continue;

                const glm::vec3 d = points[static_cast<std::size_t>(j)] - points[static_cast<std::size_t>(i)];
                const float distance2 = glm::dot(d, d);
                if (!std::isfinite(distance2)) continue;
                if (distance2 <= minDistance2)
                {
                    ++result.DegeneratePairCount;
                    continue;
                }

                candidates.push_back(Neighbor{j, distance2});
            }

            if (candidates.empty()) continue;

            const std::size_t selected = std::min(effectiveK, candidates.size());
            if (selected < candidates.size())
            {
                std::nth_element(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(selected),
                    candidates.end(), [](const Neighbor& a, const Neighbor& b)
                    {
                        return a.Distance2 < b.Distance2;
                    });
            }
            std::sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(selected),
                [](const Neighbor& a, const Neighbor& b)
                {
                    if (a.Distance2 != b.Distance2) return a.Distance2 < b.Distance2;
                    return a.Index < b.Index;
                });

            auto& output = neighborhoods[static_cast<std::size_t>(i)];
            output.reserve(selected);
            for (std::size_t idx = 0; idx < selected; ++idx)
            {
                output.push_back(candidates[idx].Index);
            }
        }

        auto has_reverse_edge = [&](std::uint32_t i, std::uint32_t j)
        {
            const auto& reverseNeighborhood = neighborhoods[static_cast<std::size_t>(j)];
            return std::find(reverseNeighborhood.begin(), reverseNeighborhood.end(), i) != reverseNeighborhood.end();
        };

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i)
        {
            for (const std::uint32_t j : neighborhoods[static_cast<std::size_t>(i)])
            {
                ++result.CandidateEdgeCount;
                if (params.Connectivity == KNNConnectivity::Mutual && !has_reverse_edge(i, j)) continue;

                const auto edge = graph.AddEdge(VertexHandle{i}, VertexHandle{j});
                if (edge.has_value()) ++result.InsertedEdgeCount;
            }
        }

        return result;
    }

    void Graph::DeleteEdge(EdgeHandle e)
    {
        if (!IsValid(e) || IsDeleted(e)) return;

        m_EDeleted[e] = true;
        ++m_DeletedEdges;
        m_HasGarbage = true;
    }

    void Graph::DeleteVertex(VertexHandle v)
    {
        if (!IsValid(v) || IsDeleted(v)) return;

        // Mark incident edges deleted.
        HalfedgeHandle h = Halfedge(v);
        const HalfedgeHandle startH = h;
        if (h.IsValid())
        {
            do
            {
                DeleteEdge(Edge(h));
                h = NextHalfedge(OppositeHalfedge(h));
            } while (h != startH);
        }

        m_VDeleted[v] = true;
        ++m_DeletedVertices;
        m_HasGarbage = true;
    }

    void Graph::GarbageCollection()
    {
        if (!m_HasGarbage) return;

        auto nv = VerticesSize();
        auto ne = EdgesSize();
        auto nh = HalfedgesSize();

        assert(nv <= std::numeric_limits<PropertyIndex>::max());
        assert(ne <= std::numeric_limits<PropertyIndex>::max());
        assert(nh <= std::numeric_limits<PropertyIndex>::max());

        auto vmap = Geometry::VertexProperty<VertexHandle>(m_Vertices.Add<VertexHandle>("v:garbage-collection", {}));
        auto hmap = Geometry::HalfedgeProperty<HalfedgeHandle>(m_Halfedges.Add<HalfedgeHandle>("h:garbage-collection", {}));

        for (std::size_t i = 0; i < nv; ++i) vmap[VertexHandle{static_cast<PropertyIndex>(i)}] = VertexHandle{static_cast<PropertyIndex>(i)};
        for (std::size_t i = 0; i < nh; ++i) hmap[HalfedgeHandle{static_cast<PropertyIndex>(i)}] = HalfedgeHandle{static_cast<PropertyIndex>(i)};

        auto swap_vertex_slots = [&](std::size_t a, std::size_t b)
        {
            m_Vertices.Swap(a, b);
            using std::swap;
            swap(vmap[VertexHandle{static_cast<PropertyIndex>(a)}], vmap[VertexHandle{static_cast<PropertyIndex>(b)}]);
        };
        auto swap_edge_slots = [&](std::size_t a, std::size_t b)
        {
            m_Edges.Swap(a, b);

            const std::size_t ha0 = 2 * a;
            const std::size_t ha1 = 2 * a + 1;
            const std::size_t hb0 = 2 * b;
            const std::size_t hb1 = 2 * b + 1;

            m_Halfedges.Swap(ha0, hb0);
            m_Halfedges.Swap(ha1, hb1);

            using std::swap;
            swap(hmap[HalfedgeHandle{static_cast<PropertyIndex>(ha0)}], hmap[HalfedgeHandle{static_cast<PropertyIndex>(hb0)}]);
            swap(hmap[HalfedgeHandle{static_cast<PropertyIndex>(ha1)}], hmap[HalfedgeHandle{static_cast<PropertyIndex>(hb1)}]);
        };

        if (nv > 0)
        {
            std::size_t i0 = 0;
            std::size_t i1 = nv - 1;
            while (true)
            {
                while (!m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i0)}] && i0 < i1) ++i0;
                while (m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i1)}] && i0 < i1) --i1;
                if (i0 >= i1) break;
                swap_vertex_slots(i0, i1);
            }
            nv = m_VDeleted[VertexHandle{static_cast<PropertyIndex>(i0)}] ? i0 : i0 + 1;
        }

        if (ne > 0)
        {
            std::size_t i0 = 0;
            std::size_t i1 = ne - 1;
            while (true)
            {
                while (!m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i0)}] && i0 < i1) ++i0;
                while (m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i1)}] && i0 < i1) --i1;
                if (i0 >= i1) break;
                swap_edge_slots(i0, i1);
            }
            ne = m_EDeleted[EdgeHandle{static_cast<PropertyIndex>(i0)}] ? i0 : i0 + 1;
            nh = 2 * ne;
        }

        // Remap connectivity to new compacted indices.
        for (std::size_t i = 0; i < nv; ++i)
        {
            const auto v = VertexHandle{static_cast<PropertyIndex>(i)};
            if (!IsIsolated(v))
            {
                SetHalfedge(v, hmap[Halfedge(v)]);
            }
        }

        for (std::size_t i = 0; i < nh; ++i)
        {
            const auto h = HalfedgeHandle{static_cast<PropertyIndex>(i)};
            SetVertex(h, vmap[ToVertex(h)]);
            SetNextHalfedge(h, hmap[NextHalfedge(h)]);
        }

        m_Vertices.Remove(vmap);
        m_Halfedges.Remove(hmap);

        m_Vertices.Resize(nv);
        m_Vertices.Shrink_to_fit();
        m_Halfedges.Resize(nh);
        m_Halfedges.Shrink_to_fit();
        m_Edges.Resize(ne);
        m_Edges.Shrink_to_fit();

        m_DeletedVertices = 0;
        m_DeletedEdges = 0;
        m_HasGarbage = false;
    }
}
