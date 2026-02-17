module;

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <utility>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Geometry:Graph.Impl;

import :Graph;
import :AABB;
import :Octree;
import :Properties;

namespace Geometry::Graph
{
    namespace
    {
        [[nodiscard]] bool IsFiniteVec2(const glm::vec2& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] glm::vec2 UnitDirectionFromPair(std::uint32_t i, std::uint32_t j)
        {
            const float phase = static_cast<float>((((i + 1U) * 1664525U) ^ ((j + 1U) * 1013904223U)) & 0xFFFFU);
            const float angle = phase * 0.0000958738F;
            return glm::vec2(std::cos(angle), std::sin(angle));
        }

        void RemoveMean(std::vector<float>& values)
        {
            if (values.empty()) return;

            float mean = 0.0F;
            for (const float value : values) mean += value;
            mean /= static_cast<float>(values.size());
            for (float& value : values) value -= mean;
        }

        [[nodiscard]] float Dot(const std::vector<float>& a, const std::vector<float>& b)
        {
            assert(a.size() == b.size());
            float sum = 0.0F;
            for (std::size_t i = 0; i < a.size(); ++i) sum += a[i] * b[i];
            return sum;
        }

        [[nodiscard]] float Normalize(std::vector<float>& values, float minNorm)
        {
            const float norm2 = Dot(values, values);
            if (!std::isfinite(norm2) || norm2 <= minNorm * minNorm) return 0.0F;
            const float invNorm = 1.0F / std::sqrt(norm2);
            for (float& value : values) value *= invNorm;
            return std::sqrt(norm2);
        }

        [[nodiscard]] float OrthogonalizeAgainst(std::vector<float>& values, const std::vector<float>& basis)
        {
            const float proj = Dot(values, basis);
            for (std::size_t i = 0; i < values.size(); ++i) values[i] -= proj * basis[i];
            return proj;
        }

        void MultiplyCombinatorialLaplacian(std::span<const std::pair<std::uint32_t, std::uint32_t>> edges,
            std::span<const float> x, std::span<float> y)
        {
            std::fill(y.begin(), y.end(), 0.0F);
            for (const auto& [i, j] : edges)
            {
                const float d = x[static_cast<std::size_t>(i)] - x[static_cast<std::size_t>(j)];
                y[static_cast<std::size_t>(i)] += d;
                y[static_cast<std::size_t>(j)] -= d;
            }
        }

        void MultiplyNormalizedSymmetricLaplacian(std::span<const std::pair<std::uint32_t, std::uint32_t>> edges,
            std::span<const float> invSqrtDegree,
            std::span<const float> x,
            std::span<float> y)
        {
            std::fill(y.begin(), y.end(), 0.0F);
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const float degreeTerm = invSqrtDegree[i] > 0.0F ? (x[i] / invSqrtDegree[i]) : x[i];
                y[i] += degreeTerm;
            }

            for (const auto& [i, j] : edges)
            {
                const std::size_t is = static_cast<std::size_t>(i);
                const std::size_t js = static_cast<std::size_t>(j);
                const float weight = invSqrtDegree[is] * invSqrtDegree[js];
                if (weight <= 0.0F) continue;
                y[is] -= weight * x[js];
                y[js] -= weight * x[is];
            }
        }
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

        std::vector<AABB> pointAabbs(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            pointAabbs[i] = {.Min = points[i], .Max = points[i]};
        }

        Octree octree;
        Octree::SplitPolicy splitPolicy{};
        splitPolicy.SplitPoint = Octree::SplitPoint::Mean;
        splitPolicy.TightChildren = true;

        constexpr std::size_t kOctreeMaxPerNode = 32U;
        constexpr std::size_t kOctreeMaxDepth = 16U;
        if (!octree.Build(std::move(pointAabbs), splitPolicy, kOctreeMaxPerNode, kOctreeMaxDepth))
        {
            return std::nullopt;
        }

        std::vector<std::vector<std::uint32_t>> neighborhoods(n);
        std::vector<std::size_t> queryNeighbors;
        queryNeighbors.reserve(std::min(n, effectiveK + 1U));

        const float minDistance2 = std::max(0.0F, params.MinDistanceEpsilon * params.MinDistanceEpsilon);

        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(n); ++i)
        {
            queryNeighbors.clear();
            octree.QueryKnn(points[static_cast<std::size_t>(i)], effectiveK + 1U, queryNeighbors);

            auto& output = neighborhoods[static_cast<std::size_t>(i)];
            output.reserve(effectiveK);

            for (const std::size_t neighborIndex : queryNeighbors)
            {
                if (neighborIndex >= n)
                {
                    ++result.DegeneratePairCount;
                    continue;
                }

                const auto j = static_cast<std::uint32_t>(neighborIndex);
                if (i == j) continue;

                const glm::vec3 d = points[static_cast<std::size_t>(j)] - points[static_cast<std::size_t>(i)];
                const float distance2 = glm::dot(d, d);
                if (!std::isfinite(distance2))
                {
                    ++result.DegeneratePairCount;
                    continue;
                }
                if (distance2 <= minDistance2)
                {
                    ++result.DegeneratePairCount;
                    continue;
                }

                output.push_back(j);
                if (output.size() == effectiveK) break;
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

    std::optional<ForceDirectedLayoutResult> ComputeForceDirectedLayout(
        const Graph& graph, std::span<glm::vec2> ioPositions, const ForceDirectedLayoutParams& params)
    {
        if (params.MaxIterations == 0U || ioPositions.size() < graph.VerticesSize()) return std::nullopt;

        std::vector<std::uint32_t> activeVertices;
        activeVertices.reserve(graph.VerticesSize());
        for (std::uint32_t idx = 0; idx < static_cast<std::uint32_t>(graph.VerticesSize()); ++idx)
        {
            const VertexHandle v{idx};
            if (!graph.IsDeleted(v)) activeVertices.push_back(idx);
        }
        if (activeVertices.size() < 2U) return std::nullopt;

        std::vector<std::pair<std::uint32_t, std::uint32_t>> activeEdges;
        activeEdges.reserve(graph.EdgesSize());
        for (std::uint32_t idx = 0; idx < static_cast<std::uint32_t>(graph.EdgesSize()); ++idx)
        {
            const EdgeHandle e{idx};
            if (graph.IsDeleted(e)) continue;

            const auto [start, end] = graph.EdgeVertices(e);
            if (!start.IsValid() || !end.IsValid()) continue;
            if (graph.IsDeleted(start) || graph.IsDeleted(end)) continue;
            activeEdges.emplace_back(start.Index, end.Index);
        }

        const float areaExtent = std::max(params.AreaExtent, 1.0e-3F);
        const float area = areaExtent * areaExtent;
        const float minDistance = std::max(params.MinDistanceEpsilon, 1.0e-7F);
        const float k = std::sqrt(area / static_cast<float>(activeVertices.size()));
        float temperature = std::max(minDistance, areaExtent * params.InitialTemperatureFactor);
        const float cooling = std::clamp(params.CoolingFactor, 0.5F, 0.9999F);

        std::vector<glm::vec2> displacement(ioPositions.size(), glm::vec2(0.0F));

        ForceDirectedLayoutResult result{};
        result.ActiveVertexCount = activeVertices.size();
        result.ActiveEdgeCount = activeEdges.size();

        for (std::uint32_t iteration = 0; iteration < params.MaxIterations; ++iteration)
        {
            std::fill(displacement.begin(), displacement.end(), glm::vec2(0.0F));

            for (std::size_t localI = 0; localI < activeVertices.size(); ++localI)
            {
                const std::uint32_t vi = activeVertices[localI];
                for (std::size_t localJ = localI + 1U; localJ < activeVertices.size(); ++localJ)
                {
                    const std::uint32_t vj = activeVertices[localJ];
                    glm::vec2 delta = ioPositions[vi] - ioPositions[vj];
                    float distance = glm::length(delta);
                    if (!std::isfinite(distance) || distance < minDistance)
                    {
                        delta = UnitDirectionFromPair(vi, vj) * minDistance;
                        distance = minDistance;
                    }

                    const glm::vec2 dir = delta / distance;
                    const float force = (k * k) / distance;
                    const glm::vec2 forceVector = dir * force;
                    displacement[vi] += forceVector;
                    displacement[vj] -= forceVector;
                }
            }

            for (const auto& [vi, vj] : activeEdges)
            {
                glm::vec2 delta = ioPositions[vi] - ioPositions[vj];
                float distance = glm::length(delta);
                if (!std::isfinite(distance) || distance < minDistance)
                {
                    delta = UnitDirectionFromPair(vi, vj) * minDistance;
                    distance = minDistance;
                }

                const glm::vec2 dir = delta / distance;
                const float force = (distance * distance) / std::max(k, minDistance);
                const glm::vec2 forceVector = dir * force;
                displacement[vi] -= forceVector;
                displacement[vj] += forceVector;
            }

            float maxDisplacement = 0.0F;
            for (const std::uint32_t vi : activeVertices)
            {
                displacement[vi] -= ioPositions[vi] * params.Gravity;

                glm::vec2 move = displacement[vi];
                float moveLength = glm::length(move);
                if (!std::isfinite(moveLength) || moveLength <= 0.0F) continue;

                if (moveLength > temperature)
                {
                    move *= (temperature / moveLength);
                    moveLength = temperature;
                }

                ioPositions[vi] += move;
                if (!IsFiniteVec2(ioPositions[vi])) return std::nullopt;
                maxDisplacement = std::max(maxDisplacement, moveLength);
            }

            result.IterationsPerformed = iteration + 1U;
            result.MaxDisplacement = maxDisplacement;

            temperature *= cooling;
            result.FinalTemperature = temperature;

            if (maxDisplacement <= params.ConvergenceTolerance)
            {
                result.Converged = true;
                break;
            }
        }

        return result;
    }

    std::optional<SpectralLayoutResult> ComputeSpectralLayout(
        const Graph& graph, std::span<glm::vec2> ioPositions, const SpectralLayoutParams& params)
    {
        if (params.MaxIterations == 0U || ioPositions.size() < graph.VerticesSize()) return std::nullopt;

        std::vector<std::uint32_t> activeVertices;
        activeVertices.reserve(graph.VerticesSize());
        std::vector<std::uint32_t> globalToLocal(graph.VerticesSize(), std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t idx = 0; idx < static_cast<std::uint32_t>(graph.VerticesSize()); ++idx)
        {
            const VertexHandle v{idx};
            if (graph.IsDeleted(v)) continue;
            globalToLocal[idx] = static_cast<std::uint32_t>(activeVertices.size());
            activeVertices.push_back(idx);
        }
        if (activeVertices.size() < 2U) return std::nullopt;

        std::vector<std::pair<std::uint32_t, std::uint32_t>> localEdges;
        localEdges.reserve(graph.EdgesSize());
        std::vector<std::uint32_t> degree(activeVertices.size(), 0U);
        for (std::uint32_t idx = 0; idx < static_cast<std::uint32_t>(graph.EdgesSize()); ++idx)
        {
            const EdgeHandle e{idx};
            if (graph.IsDeleted(e)) continue;

            const auto [start, end] = graph.EdgeVertices(e);
            if (!start.IsValid() || !end.IsValid() || graph.IsDeleted(start) || graph.IsDeleted(end)) continue;
            const std::uint32_t ls = globalToLocal[start.Index];
            const std::uint32_t le = globalToLocal[end.Index];
            if (ls == std::numeric_limits<std::uint32_t>::max() || le == std::numeric_limits<std::uint32_t>::max() || ls == le) continue;
            localEdges.emplace_back(ls, le);
            ++degree[ls];
            ++degree[le];
        }

        const std::size_t n = activeVertices.size();
        const auto maxDegreeIt = std::max_element(degree.begin(), degree.end());
        const float maxDegree = maxDegreeIt != degree.end() ? static_cast<float>(*maxDegreeIt) : 0.0F;
        const float alpha = params.StepScale / std::max(1.0F, maxDegree);
        const float minNorm = std::max(params.MinNormEpsilon, 1.0e-12F);

        std::vector<float> invSqrtDegree(n, 0.0F);
        for (std::size_t i = 0; i < n; ++i)
        {
            const float d = static_cast<float>(degree[i]);
            invSqrtDegree[i] = d > 0.0F ? 1.0F / std::sqrt(d) : 1.0F;
        }

        std::array<std::vector<float>, 2> q{
            std::vector<float>(n, 0.0F),
            std::vector<float>(n, 0.0F)
        };
        for (std::size_t i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i + 1U);
            q[0][i] = std::sin(0.73F * t) + 0.17F * std::cos(1.11F * t);
            q[1][i] = std::cos(0.61F * t) - 0.21F * std::sin(1.37F * t);
        }

        RemoveMean(q[0]);
        if (Normalize(q[0], minNorm) == 0.0F) return std::nullopt;
        RemoveMean(q[1]);
        OrthogonalizeAgainst(q[1], q[0]);
        if (Normalize(q[1], minNorm) == 0.0F)
        {
            for (std::size_t i = 0; i < n; ++i) q[1][i] = static_cast<float>((i & 1U) ? 1.0F : -1.0F);
            RemoveMean(q[1]);
            OrthogonalizeAgainst(q[1], q[0]);
            if (Normalize(q[1], minNorm) == 0.0F) return std::nullopt;
        }

        std::array<std::vector<float>, 2> y{
            std::vector<float>(n, 0.0F),
            std::vector<float>(n, 0.0F)
        };
        std::vector<float> laplace(n, 0.0F);

        SpectralLayoutResult result{};
        result.ActiveVertexCount = n;
        result.ActiveEdgeCount = localEdges.size();

        for (std::uint32_t iteration = 0; iteration < params.MaxIterations; ++iteration)
        {
            float subspaceDelta = 0.0F;
            for (std::size_t col = 0; col < 2; ++col)
            {
                if (params.Variant == SpectralLayoutParams::LaplacianVariant::NormalizedSymmetric)
                {
                    MultiplyNormalizedSymmetricLaplacian(localEdges, invSqrtDegree, q[col], laplace);
                }
                else
                {
                    MultiplyCombinatorialLaplacian(localEdges, q[col], laplace);
                }
                for (std::size_t i = 0; i < n; ++i) y[col][i] = q[col][i] - alpha * laplace[i];
                RemoveMean(y[col]);
            }

            Normalize(y[0], minNorm);
            OrthogonalizeAgainst(y[1], y[0]);
            if (Normalize(y[1], minNorm) == 0.0F) return std::nullopt;

            for (std::size_t col = 0; col < 2; ++col)
            {
                for (std::size_t i = 0; i < n; ++i)
                {
                    subspaceDelta = std::max(subspaceDelta, std::abs(y[col][i] - q[col][i]));
                    q[col][i] = y[col][i];
                }
            }

            result.IterationsPerformed = iteration + 1U;
            result.SubspaceDelta = subspaceDelta;
            if (subspaceDelta <= params.ConvergenceTolerance)
            {
                result.Converged = true;
                break;
            }
        }

        float maxAbsCoord = 0.0F;
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::uint32_t global = activeVertices[i];
            ioPositions[global] = glm::vec2(q[0][i], q[1][i]);
            maxAbsCoord = std::max(maxAbsCoord, std::abs(q[0][i]));
            maxAbsCoord = std::max(maxAbsCoord, std::abs(q[1][i]));
        }

        if (maxAbsCoord > minNorm)
        {
            const float scale = std::max(params.AreaExtent, 1.0e-3F) / maxAbsCoord;
            for (const std::uint32_t global : activeVertices)
            {
                ioPositions[global] *= scale;
            }
        }

        return result;
    }

    std::optional<HierarchicalLayoutResult> ComputeHierarchicalLayout(
        const Graph& graph, std::span<glm::vec2> ioPositions, const HierarchicalLayoutParams& params)
    {
        if (ioPositions.size() < graph.VerticesSize()) return std::nullopt;
        if (params.LayerSpacing <= 0.0F || params.NodeSpacing <= 0.0F || params.ComponentSpacing < 0.0F)
        {
            return std::nullopt;
        }

        std::vector<std::uint32_t> activeVertices;
        activeVertices.reserve(graph.VerticesSize());
        std::vector<std::uint32_t> globalToLocal(graph.VerticesSize(), std::numeric_limits<std::uint32_t>::max());
        for (std::uint32_t idx = 0; idx < static_cast<std::uint32_t>(graph.VerticesSize()); ++idx)
        {
            const VertexHandle v{idx};
            if (graph.IsDeleted(v)) continue;
            globalToLocal[idx] = static_cast<std::uint32_t>(activeVertices.size());
            activeVertices.push_back(idx);
        }
        if (activeVertices.empty()) return std::nullopt;

        std::vector<std::vector<std::uint32_t>> adjacency(activeVertices.size());
        std::size_t activeEdgeCount = 0;
        for (std::uint32_t idx = 0; idx < static_cast<std::uint32_t>(graph.EdgesSize()); ++idx)
        {
            const EdgeHandle e{idx};
            if (graph.IsDeleted(e)) continue;

            const auto [start, end] = graph.EdgeVertices(e);
            if (!start.IsValid() || !end.IsValid() || graph.IsDeleted(start) || graph.IsDeleted(end)) continue;

            const std::uint32_t ls = globalToLocal[start.Index];
            const std::uint32_t le = globalToLocal[end.Index];
            if (ls == std::numeric_limits<std::uint32_t>::max() || le == std::numeric_limits<std::uint32_t>::max() || ls == le)
            {
                continue;
            }

            adjacency[ls].push_back(le);
            adjacency[le].push_back(ls);
            ++activeEdgeCount;
        }

        for (auto& neighbors : adjacency)
        {
            std::sort(neighbors.begin(), neighbors.end());
            neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
        }

        std::vector<bool> visited(activeVertices.size(), false);
        std::vector<std::int32_t> localLayer(activeVertices.size(), -1);
        std::vector<float> localX(activeVertices.size(), 0.0F);
        std::vector<float> previousOrder(activeVertices.size(), 0.0F);
        std::vector<float> nextOrder(activeVertices.size(), 0.0F);

        const auto first_unvisited_local = [&]() -> std::uint32_t
        {
            for (std::uint32_t i = 0; i < visited.size(); ++i)
            {
                if (!visited[i]) return i;
            }
            return std::numeric_limits<std::uint32_t>::max();
        };

        const auto resolve_root = [&]() -> std::uint32_t
        {
            if (params.RootVertexIndex != kInvalidIndex && params.RootVertexIndex < graph.VerticesSize())
            {
                const std::uint32_t local = globalToLocal[params.RootVertexIndex];
                if (local != std::numeric_limits<std::uint32_t>::max()) return local;
            }
            return first_unvisited_local();
        };

        float componentXOffset = 0.0F;
        std::size_t maxLayerWidth = 0;
        std::size_t globalLayerCount = 0;
        std::size_t componentCount = 0;

        for (std::uint32_t seed = resolve_root(); seed != std::numeric_limits<std::uint32_t>::max(); seed = first_unvisited_local())
        {
            ++componentCount;

            std::vector<std::uint32_t> componentVertices;
            componentVertices.reserve(16);
            {
                std::queue<std::uint32_t> queue;
                queue.push(seed);
                visited[seed] = true;

                while (!queue.empty())
                {
                    const std::uint32_t u = queue.front();
                    queue.pop();
                    componentVertices.push_back(u);

                    for (const std::uint32_t v : adjacency[u])
                    {
                        if (visited[v]) continue;
                        visited[v] = true;
                        queue.push(v);
                    }
                }
            }

            std::sort(componentVertices.begin(), componentVertices.end());

            std::vector<bool> inComponent(activeVertices.size(), false);
            for (const std::uint32_t u : componentVertices) inComponent[u] = true;

            auto bfs_farthest = [&](std::uint32_t start,
                                    std::vector<std::int32_t>& outDistance,
                                    std::vector<std::uint32_t>* outParent) -> std::uint32_t
            {
                outDistance.assign(activeVertices.size(), -1);
                if (outParent != nullptr)
                {
                    outParent->assign(activeVertices.size(), std::numeric_limits<std::uint32_t>::max());
                }

                std::queue<std::uint32_t> queue;
                queue.push(start);
                outDistance[start] = 0;
                std::uint32_t farthest = start;

                while (!queue.empty())
                {
                    const std::uint32_t u = queue.front();
                    queue.pop();
                    const std::int32_t du = outDistance[u];

                    if (du > outDistance[farthest] || (du == outDistance[farthest] && u < farthest)) farthest = u;

                    for (const std::uint32_t v : adjacency[u])
                    {
                        if (!inComponent[v] || outDistance[v] >= 0) continue;
                        outDistance[v] = du + 1;
                        if (outParent != nullptr) (*outParent)[v] = u;
                        queue.push(v);
                    }
                }

                return farthest;
            };

            std::uint32_t componentRoot = seed;
            const bool userRootInComponent =
                params.RootVertexIndex != kInvalidIndex &&
                params.RootVertexIndex < graph.VerticesSize() &&
                globalToLocal[params.RootVertexIndex] != std::numeric_limits<std::uint32_t>::max() &&
                inComponent[globalToLocal[params.RootVertexIndex]];

            if (userRootInComponent)
            {
                componentRoot = globalToLocal[params.RootVertexIndex];
            }
            else if (componentVertices.size() > 1U)
            {
                std::vector<std::int32_t> distances;
                std::vector<std::uint32_t> parent;
                const std::uint32_t start = componentVertices.front();
                const std::uint32_t endpointA = bfs_farthest(start, distances, nullptr);
                const std::uint32_t endpointB = bfs_farthest(endpointA, distances, &parent);

                std::vector<std::uint32_t> diameterPath;
                for (std::uint32_t current = endpointB; current != std::numeric_limits<std::uint32_t>::max();)
                {
                    diameterPath.push_back(current);
                    if (current == endpointA) break;
                    current = parent[current];
                }

                if (!diameterPath.empty())
                {
                    componentRoot = diameterPath[diameterPath.size() / 2U];
                }
            }

            std::vector<std::uint32_t> frontier{componentRoot};
            std::vector<std::vector<std::uint32_t>> layers;
            layers.reserve(8);
            for (const std::uint32_t localVertex : componentVertices) localLayer[localVertex] = -1;
            localLayer[componentRoot] = 0;

            while (!frontier.empty())
            {
                layers.push_back(frontier);
                std::vector<std::uint32_t> next;
                for (const std::uint32_t u : frontier)
                {
                    for (const std::uint32_t v : adjacency[u])
                    {
                        if (!inComponent[v] || localLayer[v] >= 0) continue;
                        localLayer[v] = static_cast<std::int32_t>(layers.size());
                        next.push_back(v);
                    }
                }

                std::sort(next.begin(), next.end());
                next.erase(std::unique(next.begin(), next.end()), next.end());
                frontier = std::move(next);
            }

            globalLayerCount = std::max(globalLayerCount, layers.size());
            for (const auto& layer : layers) maxLayerWidth = std::max(maxLayerWidth, layer.size());

            for (std::size_t li = 0; li < layers.size(); ++li)
            {
                auto& layer = layers[li];
                std::sort(layer.begin(), layer.end());
                for (std::size_t i = 0; i < layer.size(); ++i)
                {
                    previousOrder[layer[i]] = static_cast<float>(i);
                }
            }

            const auto layer_sweep = [&](std::size_t li, bool forward)
            {
                if (layers[li].size() <= 1U) return;

                auto barycenter = [&](std::uint32_t vertex)
                {
                    float sum = 0.0F;
                    std::uint32_t count = 0;
                    const std::int32_t targetLayer = static_cast<std::int32_t>(li) + (forward ? -1 : 1);
                    for (const std::uint32_t n : adjacency[vertex])
                    {
                        if (localLayer[n] != targetLayer) continue;
                        sum += previousOrder[n];
                        ++count;
                    }
                    if (count == 0U) return previousOrder[vertex];
                    return sum / static_cast<float>(count);
                };

                auto& layer = layers[li];
                std::stable_sort(layer.begin(), layer.end(), [&](std::uint32_t a, std::uint32_t b)
                {
                    const float ba = barycenter(a);
                    const float bb = barycenter(b);
                    if (std::abs(ba - bb) > 1.0e-6F) return ba < bb;
                    return a < b;
                });

                for (std::size_t i = 0; i < layer.size(); ++i)
                {
                    nextOrder[layer[i]] = static_cast<float>(i);
                }
            };

            for (std::uint32_t sweep = 0; sweep < params.CrossingMinimizationSweeps; ++sweep)
            {
                for (std::size_t li = 1; li < layers.size(); ++li) layer_sweep(li, true);
                std::swap(previousOrder, nextOrder);

                if (layers.size() > 1U)
                {
                    for (std::size_t li = layers.size() - 1; li-- > 0;) layer_sweep(li, false);
                    std::swap(previousOrder, nextOrder);
                }
            }

            float componentWidth = 0.0F;
            for (std::size_t li = 0; li < layers.size(); ++li)
            {
                auto& layer = layers[li];
                const float layerCenter = static_cast<float>(layer.size() - 1U) * 0.5F;

                for (std::size_t i = 0; i < layer.size(); ++i)
                {
                    const float x = (static_cast<float>(i) - layerCenter) * params.NodeSpacing;
                    localX[layer[i]] = x;
                    componentWidth = std::max(componentWidth, std::abs(x));
                }
            }

            for (std::size_t li = 0; li < layers.size(); ++li)
            {
                for (const std::uint32_t localVertex : layers[li])
                {
                    const std::uint32_t globalVertex = activeVertices[localVertex];
                    ioPositions[globalVertex] = glm::vec2(
                        componentXOffset + localX[localVertex],
                        -static_cast<float>(li) * params.LayerSpacing);
                    if (!IsFiniteVec2(ioPositions[globalVertex])) return std::nullopt;
                }
            }

            componentXOffset += 2.0F * componentWidth + params.ComponentSpacing + params.NodeSpacing;
        }

        HierarchicalLayoutResult result{};
        result.ActiveVertexCount = activeVertices.size();
        result.ActiveEdgeCount = activeEdgeCount;
        result.ComponentCount = componentCount;
        result.LayerCount = globalLayerCount;
        result.MaxLayerWidth = maxLayerWidth;
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
