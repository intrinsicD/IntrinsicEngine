module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <span>
#include <vector>

#include <glm/glm.hpp>

module Geometry.Graph.Utils;

import Geometry.AABB;
import Geometry.BVH;
import Geometry.Octree;
import Geometry.Properties;
import Geometry.Queries;
import Geometry.Sphere;
import Geometry.Validation;

namespace Geometry::Graph
{
    using Validation::IsFinite;

    namespace
    {
        struct EdgeSegmentRecord
        {
            EdgeHandle Edge{};
            VertexHandle Start{};
            VertexHandle End{};
            glm::vec3 A{0.0F};
            glm::vec3 B{0.0F};
            AABB Bounds{};
        };

        struct EdgeSegmentIndex
        {
            EdgeQueryStatus Status{EdgeQueryStatus::Success};
            EdgeHandle FirstFailedEdge{};
            std::vector<EdgeSegmentRecord> Records{};
            BVH Tree{};
        };

        [[nodiscard]] AABB SegmentAabb(const glm::vec3& a, const glm::vec3& b)
        {
            return AABB{glm::min(a, b), glm::max(a, b)};
        }

        [[nodiscard]] bool IsBetterEdgeResult(
            const ClosestEdgeQueryResult& candidate,
            const ClosestEdgeQueryResult& current)
        {
            if (candidate.SquaredDistance < current.SquaredDistance) return true;
            if (candidate.SquaredDistance > current.SquaredDistance) return false;
            return candidate.Edge.Index < current.Edge.Index;
        }

        [[nodiscard]] ClosestEdgeQueryResult EvaluateEdgeQuery(
            const EdgeSegmentRecord& record, const glm::vec3& point)
        {
            const PointSegmentResult segmentResult = ClosestPointSegment(point, record.A, record.B);
            return ClosestEdgeQueryResult{
                EdgeQueryStatus::Success,
                record.Edge,
                segmentResult.ClosestPoint,
                segmentResult.DistanceSq,
                segmentResult.SegmentT};
        }

        [[nodiscard]] EdgeSegmentIndex BuildEdgeSegmentIndex(
            const Graph& graph, const float minLength = 1.0e-12F)
        {
            EdgeSegmentIndex out{};
            if (graph.EdgeCount() == 0)
            {
                out.Status = EdgeQueryStatus::EmptyGraph;
                return out;
            }

            std::vector<AABB> aabbs;
            aabbs.reserve(graph.EdgeCount());
            out.Records.reserve(graph.EdgeCount());

            const float minLengthSq = std::max(0.0F, minLength * minLength);

            for (const EdgeHandle edge : graph.LiveEdges())
            {
                if (!graph.IsValid(edge) || graph.IsDeleted(edge))
                {
                    out.Status = EdgeQueryStatus::InvalidParameters;
                    out.FirstFailedEdge = edge;
                    return out;
                }

                const auto [start, end] = graph.EdgeVertices(edge);
                if (!start.IsValid() || !end.IsValid() || graph.IsDeleted(start) || graph.IsDeleted(end))
                {
                    out.Status = EdgeQueryStatus::InvalidParameters;
                    out.FirstFailedEdge = edge;
                    return out;
                }

                const glm::vec3 a = graph.VertexPosition(start);
                const glm::vec3 b = graph.VertexPosition(end);
                if (!IsFinite(a) || !IsFinite(b))
                {
                    out.Status = EdgeQueryStatus::NonFinitePosition;
                    out.FirstFailedEdge = edge;
                    return out;
                }

                const glm::vec3 delta = b - a;
                const float lengthSq = glm::dot(delta, delta);
                if (!std::isfinite(lengthSq) || lengthSq <= minLengthSq)
                {
                    out.Status = EdgeQueryStatus::ZeroLengthEdge;
                    out.FirstFailedEdge = edge;
                    return out;
                }

                const AABB bounds = SegmentAabb(a, b);
                aabbs.push_back(bounds);
                out.Records.push_back(EdgeSegmentRecord{edge, start, end, a, b, bounds});
            }

            if (out.Records.empty())
            {
                out.Status = EdgeQueryStatus::EmptyGraph;
                return out;
            }

            if (!out.Tree.Build(std::move(aabbs)).has_value())
            {
                out.Status = EdgeQueryStatus::NoCandidates;
                return out;
            }

            return out;
        }

        [[nodiscard]] EdgeQuerySetResult EvaluateEdgeSet(
            const EdgeSegmentIndex& index,
            const glm::vec3& point,
            std::span<const BVH::ElementIndex> elementIndices,
            const std::optional<float> maxSquaredDistance)
        {
            EdgeQuerySetResult result{};
            result.Status = EdgeQueryStatus::Success;
            result.Edges.reserve(elementIndices.size());

            for (const BVH::ElementIndex elementIndex : elementIndices)
            {
                if (elementIndex >= index.Records.size()) continue;
                ClosestEdgeQueryResult candidate = EvaluateEdgeQuery(index.Records[elementIndex], point);
                if (maxSquaredDistance.has_value()
                    && candidate.SquaredDistance > *maxSquaredDistance + 1.0e-8F)
                {
                    continue;
                }
                result.Edges.push_back(candidate);
            }

            std::sort(result.Edges.begin(), result.Edges.end(),
                [](const ClosestEdgeQueryResult& a, const ClosestEdgeQueryResult& b)
                {
                    if (a.SquaredDistance != b.SquaredDistance) return a.SquaredDistance < b.SquaredDistance;
                    return a.Edge.Index < b.Edge.Index;
                });
            result.Edges.erase(std::unique(result.Edges.begin(), result.Edges.end(),
                [](const ClosestEdgeQueryResult& a, const ClosestEdgeQueryResult& b)
                {
                    return a.Edge.Index == b.Edge.Index;
                }), result.Edges.end());

            return result;
        }

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

    std::optional<KNNBuildResult> BuildKNNGraphFromNeighbors(Graph& graph,
                                                             std::span<const glm::vec3> points,
                                                             PointNeighborhoods rows,
                                                             const KNNBuildParams& params)
    {
        if (points.empty() || points.size() > std::numeric_limits<std::uint32_t>::max() ||
            params.K == 0 || !std::isfinite(params.MinDistanceEpsilon) ||
            params.MinDistanceEpsilon < 0 ||
            (params.Connectivity != KNNConnectivity::Union &&
             params.Connectivity != KNNConnectivity::Mutual) ||
            rows.Offsets.size() != points.size() + 1 || rows.Offsets.front() != 0 ||
            rows.Offsets.back() != rows.Indices.size())
            return {};
        for (auto point : points)
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z))
                return {};
        const auto effectiveK = std::min<std::size_t>(params.K, points.size() - 1),
                   width = effectiveK + 1;
        const auto minimum2 = params.MinDistanceEpsilon * params.MinDistanceEpsilon;
        if (!std::isfinite(minimum2))
            return {};
        std::vector<std::vector<std::uint32_t>> neighbors(points.size());
        std::size_t degenerate{};
        for (std::uint32_t i = 0; i < points.size(); ++i)
        {
            const auto begin = rows.Offsets[i], end = rows.Offsets[i + 1];
            if (begin > end || end > rows.Indices.size() || end - begin != width)
                return {};
            float previous = -1;
            std::uint32_t previousId{};
            // Validate the full row even after the accepted-neighbor limit is reached.
            for (auto id : rows.Indices.subspan(begin, width))
            {
                if (id >= points.size())
                    return {};
                const auto delta = points[id] - points[i];
                const auto distance = glm::dot(delta, delta);
                if (!std::isfinite(distance) || distance < previous ||
                    (distance == previous && id <= previousId))
                    return {};
                previous = distance;
                previousId = id;
                if (neighbors[i].size() == effectiveK || id == i)
                    continue;
                if (distance <= minimum2)
                {
                    ++degenerate;
                    continue;
                }
                neighbors[i].push_back(id);
            }
        }
        auto result = BuildKNNGraphFromIndices(
            graph, points, neighbors,
            {.MinDistanceEpsilon = params.MinDistanceEpsilon, .Connectivity = params.Connectivity});
        if (result)
        {
            result->RequestedK = params.K;
            result->EffectiveK = effectiveK;
            result->DegeneratePairCount = degenerate;
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

        Octree octree;
        Octree::SplitPolicy splitPolicy{};
        splitPolicy.SplitPoint = Octree::SplitPoint::Mean;
        splitPolicy.TightChildren = true;

        constexpr std::size_t kOctreeMaxPerNode = 32U;
        constexpr std::size_t kOctreeMaxDepth = 16U;
        if (!octree.BuildFromPoints(points, splitPolicy, kOctreeMaxPerNode, kOctreeMaxDepth))
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
            octree.QueryKNN(points[static_cast<std::size_t>(i)], effectiveK + 1U, queryNeighbors);

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

    EdgeLengthResult FillEdgeLengths(
        const Graph& graph, std::span<float> outLengths, const EdgeLengthParams& params)
    {
        EdgeLengthResult result{};
        result.EdgeCount = graph.EdgeCount();

        if (graph.EdgeCount() == 0)
        {
            result.Status = EdgeLengthStatus::EmptyGraph;
            return result;
        }

        if (outLengths.size() < graph.EdgesSize())
        {
            result.Status = EdgeLengthStatus::InsufficientOutput;
            return result;
        }

        const float minLength = std::isfinite(params.MinLength) ? std::max(0.0F, params.MinLength) : 0.0F;

        for (const EdgeHandle edge : graph.LiveEdges())
        {
            if (!graph.IsValid(edge) || graph.IsDeleted(edge))
            {
                result.Status = EdgeLengthStatus::InvalidEdge;
                result.FirstFailedEdge = edge;
                return result;
            }

            const auto [start, end] = graph.EdgeVertices(edge);
            if (!start.IsValid() || !end.IsValid() || graph.IsDeleted(start) || graph.IsDeleted(end))
            {
                result.Status = EdgeLengthStatus::InvalidEdge;
                result.FirstFailedEdge = edge;
                return result;
            }

            const glm::vec3 a = graph.VertexPosition(start);
            const glm::vec3 b = graph.VertexPosition(end);
            if (!IsFinite(a) || !IsFinite(b))
            {
                result.Status = EdgeLengthStatus::NonFinitePosition;
                result.FirstFailedEdge = edge;
                return result;
            }

            const float length = glm::length(b - a);
            if (!std::isfinite(length) || (params.RejectZeroLengthEdges && length <= minLength))
            {
                result.Status = EdgeLengthStatus::ZeroLengthEdge;
                result.FirstFailedEdge = edge;
                return result;
            }

            outLengths[edge.Index] = length;
            result.MinLength = std::min(result.MinLength, length);
            result.MaxLength = std::max(result.MaxLength, length);
            ++result.FilledCount;
        }

        if (result.FilledCount == 0)
        {
            result.Status = EdgeLengthStatus::EmptyGraph;
            return result;
        }

        result.Status = EdgeLengthStatus::Success;
        return result;
    }

    EdgeLengthResult EnsureEdgeLengths(Graph& graph, const EdgeLengthParams& params)
    {
        std::vector<float> lengths(graph.EdgesSize(), 0.0F);
        EdgeLengthResult result = FillEdgeLengths(graph, lengths, params);
        if (result.Status != EdgeLengthStatus::Success) return result;

        EdgeProperty<float> lengthProperty = graph.GetOrAddEdgeProperty<float>("e:length", 0.0F);
        for (const EdgeHandle edge : graph.LiveEdges())
        {
            lengthProperty[edge] = lengths[edge.Index];
        }

        return result;
    }

    ClosestEdgeQueryResult ClosestEdge(const Graph& graph, glm::vec3 point)
    {
        if (!IsFinite(point))
        {
            ClosestEdgeQueryResult result{};
            result.Status = EdgeQueryStatus::InvalidQueryPoint;
            return result;
        }

        const EdgeSegmentIndex index = BuildEdgeSegmentIndex(graph);
        if (index.Status != EdgeQueryStatus::Success)
        {
            ClosestEdgeQueryResult result{};
            result.Status = index.Status;
            result.Edge = index.FirstFailedEdge;
            return result;
        }

        const auto& elements = index.Tree.ElementIndices();
        const EdgeQuerySetResult set = EvaluateEdgeSet(
            index, point, std::span<const BVH::ElementIndex>(elements.data(), elements.size()), std::nullopt);
        if (set.Edges.empty())
        {
            ClosestEdgeQueryResult result{};
            result.Status = EdgeQueryStatus::NoCandidates;
            return result;
        }

        return set.Edges.front();
    }

    EdgeQuerySetResult KClosestEdges(const Graph& graph, glm::vec3 point, std::size_t k)
    {
        EdgeQuerySetResult result{};
        if (!IsFinite(point))
        {
            result.Status = EdgeQueryStatus::InvalidQueryPoint;
            return result;
        }
        if (k == 0)
        {
            result.Status = EdgeQueryStatus::InvalidParameters;
            return result;
        }

        const EdgeSegmentIndex index = BuildEdgeSegmentIndex(graph);
        if (index.Status != EdgeQueryStatus::Success)
        {
            result.Status = index.Status;
            return result;
        }

        const auto& elements = index.Tree.ElementIndices();
        result = EvaluateEdgeSet(
            index, point, std::span<const BVH::ElementIndex>(elements.data(), elements.size()), std::nullopt);
        if (result.Edges.size() > k) result.Edges.resize(k);
        return result;
    }

    EdgeQuerySetResult EdgesWithinRadius(const Graph& graph, glm::vec3 point, float radius)
    {
        EdgeQuerySetResult result{};
        if (!IsFinite(point))
        {
            result.Status = EdgeQueryStatus::InvalidQueryPoint;
            return result;
        }
        if (!std::isfinite(radius) || radius < 0.0F)
        {
            result.Status = EdgeQueryStatus::InvalidParameters;
            return result;
        }

        const EdgeSegmentIndex index = BuildEdgeSegmentIndex(graph);
        if (index.Status != EdgeQueryStatus::Success)
        {
            result.Status = index.Status;
            return result;
        }

        std::vector<BVH::ElementIndex> candidates;
        index.Tree.QuerySphere(Sphere{point, radius}, candidates);
        const float radiusSq = radius * radius;
        return EvaluateEdgeSet(index, point, std::span<const BVH::ElementIndex>(candidates.data(), candidates.size()), radiusSq);
    }

    ClosestEdgeQueryResult ClosestEdgeWithinOneRing(
        const Graph& graph, VertexHandle seedVertex, glm::vec3 point)
    {
        if (!IsFinite(point))
        {
            ClosestEdgeQueryResult result{};
            result.Status = EdgeQueryStatus::InvalidQueryPoint;
            return result;
        }

        if (!graph.IsValid(seedVertex) || graph.IsDeleted(seedVertex))
        {
            ClosestEdgeQueryResult result{};
            result.Status = EdgeQueryStatus::InvalidSeedVertex;
            return result;
        }

        ClosestEdgeQueryResult best{};
        best.Status = EdgeQueryStatus::NoCandidates;

        std::vector<EdgeHandle> incidentEdges;
        for (const HalfedgeHandle halfedge : graph.HalfedgesAroundVertex(seedVertex))
        {
            const EdgeHandle edge = graph.Edge(halfedge);
            if (graph.IsValid(edge) && !graph.IsDeleted(edge)) incidentEdges.push_back(edge);
        }
        std::sort(incidentEdges.begin(), incidentEdges.end(),
            [](EdgeHandle a, EdgeHandle b) { return a.Index < b.Index; });
        incidentEdges.erase(std::unique(incidentEdges.begin(), incidentEdges.end(),
            [](EdgeHandle a, EdgeHandle b) { return a.Index == b.Index; }), incidentEdges.end());

        for (const EdgeHandle edge : incidentEdges)
        {
            const auto [start, end] = graph.EdgeVertices(edge);
            if (!start.IsValid() || !end.IsValid() || graph.IsDeleted(start) || graph.IsDeleted(end))
            {
                best.Status = EdgeQueryStatus::InvalidParameters;
                best.Edge = edge;
                return best;
            }

            const glm::vec3 a = graph.VertexPosition(start);
            const glm::vec3 b = graph.VertexPosition(end);
            if (!IsFinite(a) || !IsFinite(b))
            {
                best.Status = EdgeQueryStatus::NonFinitePosition;
                best.Edge = edge;
                return best;
            }

            const float lengthSq = glm::dot(b - a, b - a);
            if (!std::isfinite(lengthSq) || lengthSq <= 1.0e-24F)
            {
                best.Status = EdgeQueryStatus::ZeroLengthEdge;
                best.Edge = edge;
                return best;
            }

            ClosestEdgeQueryResult candidate = EvaluateEdgeQuery(EdgeSegmentRecord{edge, start, end, a, b, SegmentAabb(a, b)}, point);
            if (best.Status != EdgeQueryStatus::Success || IsBetterEdgeResult(candidate, best))
            {
                best = candidate;
            }
        }

        return best;
    }
}
