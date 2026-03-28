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

module Geometry.GraphUtils;

import Geometry.AABB;
import Geometry.Octree;
import Geometry.Properties;
import Geometry.Validation;

namespace Geometry::Graph
{
    using Validation::IsFinite;

    namespace
    {

        // Deterministic pseudo-random unit direction for an edge (i,j).
        // Uses LCG-style hash constants to map the pair to a phase angle in [0, 2π),
        // producing consistent initial displacement for force-directed layout.
        // The mask 0xFFFF and scale 0.0000958738 ≈ 2π/65536 map to a uniform angle.
        [[nodiscard]] glm::vec2 UnitDirectionFromPair(std::uint32_t i, std::uint32_t j)
        {
            const auto phase = static_cast<float>((((i + 1U) * 1664525U) ^ ((j + 1U) * 1013904223U)) & 0xFFFFU);
            const auto angle = phase * 0.0000958738F;
            return {std::cos(angle), std::sin(angle)};
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

        float Normalize(std::vector<float>& values, float minNorm)
        {
            const float norm2 = Dot(values, values);
            if (!std::isfinite(norm2) || norm2 <= minNorm * minNorm) return 0.0F;
            const float invNorm = 1.0F / std::sqrt(norm2);
            for (float& value : values) value *= invNorm;
            return std::sqrt(norm2);
        }

        float OrthogonalizeAgainst(std::vector<float>& values, const std::vector<float>& basis)
        {
            const float proj = Dot(values, basis);
            for (std::size_t i = 0; i < values.size(); ++i) values[i] -= proj * basis[i];
            return proj;
        }

        // Combinatorial graph Laplacian: L = D - A, where D is the degree
        // matrix and A is the adjacency matrix. Computes y = L * x in O(|E|).
        // For each edge (i,j): y[i] += x[i] - x[j], y[j] += x[j] - x[i].
        // Used by spectral embedding (Fiedler vector / graph partitioning).
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

        // Symmetric normalized Laplacian: L_sym = D^{-1/2} L D^{-1/2} = I - D^{-1/2} A D^{-1/2}.
        // Eigenvalues in [0, 2]; avoids hub-dominated embeddings on irregular-degree
        // topologies. invSqrtDegree[i] = 1 / sqrt(deg(i)) is precomputed per vertex.
        void MultiplyNormalizedSymmetricLaplacian(std::span<const std::pair<std::uint32_t, std::uint32_t>> edges,
            std::span<const float> invSqrtDegree,
            std::span<const float> x,
            std::span<float> y)
        {
            // Identity term: y = x (equivalent to D^{-1/2} D D^{-1/2} x = x).
            std::fill(y.begin(), y.end(), 0.0F);
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const float degreeTerm = invSqrtDegree[i] > 0.0F ? (x[i] / invSqrtDegree[i]) : x[i];
                y[i] += degreeTerm;
            }

            // Adjacency term: y -= D^{-1/2} A D^{-1/2} x.
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

        // 2D orientation test (cross product of AB and AC).
        // Returns > 0 for CCW, < 0 for CW, 0 for collinear.
        // Used by edge-crossing detection for graph layout quality diagnostics.
        [[nodiscard]] float Orientation2D(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c)
        {
            const glm::vec2 ab = b - a;
            const glm::vec2 ac = c - a;
            return ab.x * ac.y - ab.y * ac.x;
        }

        // Check whether two 1D intervals [a0,a1] and [b0,b1] overlap within epsilon.
        [[nodiscard]] bool RangesOverlap(float a0, float a1, float b0, float b1, float epsilon)
        {
            if (a0 > a1) std::swap(a0, a1);
            if (b0 > b1) std::swap(b0, b1);
            return (a0 <= b1 + epsilon) && (b0 <= a1 + epsilon);
        }

        // Check whether point p lies on segment [a, b] within epsilon tolerance.
        [[nodiscard]] bool OnSegment(const glm::vec2& a, const glm::vec2& b, const glm::vec2& p, float epsilon)
        {
            return RangesOverlap(a.x, b.x, p.x, p.x, epsilon)
                && RangesOverlap(a.y, b.y, p.y, p.y, epsilon);
        }

        [[nodiscard]] bool SegmentsIntersect(
            const glm::vec2& a0,
            const glm::vec2& a1,
            const glm::vec2& b0,
            const glm::vec2& b1,
            const EdgeCrossingParams& params)
        {
            const float epsilon = std::max(params.IntersectionEpsilon, 0.0F);

            if (!RangesOverlap(a0.x, a1.x, b0.x, b1.x, epsilon)
                || !RangesOverlap(a0.y, a1.y, b0.y, b1.y, epsilon))
            {
                return false;
            }

            const float o0 = Orientation2D(a0, a1, b0);
            const float o1 = Orientation2D(a0, a1, b1);
            const float o2 = Orientation2D(b0, b1, a0);
            const float o3 = Orientation2D(b0, b1, a1);

            const auto sign = [epsilon](float value)
            {
                if (value > epsilon) return 1;
                if (value < -epsilon) return -1;
                return 0;
            };

            const int s0 = sign(o0);
            const int s1 = sign(o1);
            const int s2 = sign(o2);
            const int s3 = sign(o3);

            const bool collinear = s0 == 0 && s1 == 0 && s2 == 0 && s3 == 0;
            if (collinear)
            {
                if (!params.CountCollinearOverlap) return false;
                return RangesOverlap(a0.x, a1.x, b0.x, b1.x, epsilon)
                    && RangesOverlap(a0.y, a1.y, b0.y, b1.y, epsilon);
            }

            if (s0 == 0 && OnSegment(a0, a1, b0, epsilon)) return true;
            if (s1 == 0 && OnSegment(a0, a1, b1, epsilon)) return true;
            if (s2 == 0 && OnSegment(b0, b1, a0, epsilon)) return true;
            if (s3 == 0 && OnSegment(b0, b1, a1, epsilon)) return true;

            return (s0 * s1 < 0) && (s2 * s3 < 0);
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
                if (!IsFinite(ioPositions[vi])) return std::nullopt;
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
                const auto d = static_cast<float>(degree[i]);
            invSqrtDegree[i] = d > 0.0F ? 1.0F / std::sqrt(d) : 1.0F;
        }

        std::array<std::vector<float>, 2> q{
            std::vector<float>(n, 0.0F),
            std::vector<float>(n, 0.0F)
        };
        for (std::size_t i = 0; i < n; ++i)
        {
                const auto t = static_cast<float>(i + 1U);
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
                                    std::optional<std::reference_wrapper<std::vector<std::uint32_t>>> outParent) -> std::uint32_t
            {
                outDistance.assign(activeVertices.size(), -1);
                if (outParent.has_value())
                {
                    outParent->get().assign(activeVertices.size(), std::numeric_limits<std::uint32_t>::max());
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
                        if (outParent.has_value()) outParent->get()[v] = u;
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
                const std::uint32_t endpointA = bfs_farthest(start, distances, std::nullopt);
                const std::uint32_t endpointB = bfs_farthest(endpointA, distances, std::ref(parent));

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
                    if (!IsFinite(ioPositions[globalVertex])) return std::nullopt;
                }
            }

            componentXOffset += 2.0F * componentWidth + params.ComponentSpacing + params.NodeSpacing;
        }

        std::size_t crossingCount = 0;
        {
            std::vector<std::vector<std::uint32_t>> layerVertices(globalLayerCount);
            for (std::uint32_t local = 0; local < static_cast<std::uint32_t>(activeVertices.size()); ++local)
            {
                const std::int32_t layer = localLayer[local];
                if (layer < 0 || layer >= static_cast<std::int32_t>(globalLayerCount)) continue;
                layerVertices[static_cast<std::size_t>(layer)].push_back(local);
            }

            for (auto& vertices : layerVertices)
            {
                std::stable_sort(vertices.begin(), vertices.end(), [&](const std::uint32_t a, const std::uint32_t b)
                {
                    const glm::vec2 pa = ioPositions[activeVertices[a]];
                    const glm::vec2 pb = ioPositions[activeVertices[b]];
                    if (std::abs(pa.x - pb.x) > 1.0e-6F) return pa.x < pb.x;
                    return a < b;
                });
            }

            std::vector<std::uint32_t> localOrder(activeVertices.size(), std::numeric_limits<std::uint32_t>::max());
            std::vector<std::uint32_t> upperEdgeOrder;
            std::vector<std::uint32_t> lowerEdgeOrder;
            std::vector<std::uint32_t> bit;

            for (std::size_t li = 0; li + 1 < globalLayerCount; ++li)
            {
                const auto& upper = layerVertices[li];
                const auto& lower = layerVertices[li + 1U];
                if (upper.empty() || lower.empty()) continue;

                for (std::size_t i = 0; i < upper.size(); ++i) localOrder[upper[i]] = static_cast<std::uint32_t>(i);
                for (std::size_t i = 0; i < lower.size(); ++i) localOrder[lower[i]] = static_cast<std::uint32_t>(i);

                upperEdgeOrder.clear();
                lowerEdgeOrder.clear();

                for (const std::uint32_t u : upper)
                {
                    for (const std::uint32_t v : adjacency[u])
                    {
                        if (localLayer[v] != static_cast<std::int32_t>(li + 1U)) continue;
                        upperEdgeOrder.push_back(localOrder[u]);
                        lowerEdgeOrder.push_back(localOrder[v]);
                    }
                }

                if (upperEdgeOrder.size() < 2U) continue;

                std::vector<std::size_t> permutation(upperEdgeOrder.size());
                std::iota(permutation.begin(), permutation.end(), 0U);
                std::stable_sort(permutation.begin(), permutation.end(), [&](const std::size_t a, const std::size_t b)
                {
                    if (upperEdgeOrder[a] != upperEdgeOrder[b]) return upperEdgeOrder[a] < upperEdgeOrder[b];
                    return lowerEdgeOrder[a] < lowerEdgeOrder[b];
                });

                bit.assign(lower.size() + 1U, 0U);
                const auto bit_add = [&](std::size_t idx)
                {
                    for (++idx; idx < bit.size(); idx += idx & (~idx + 1U)) ++bit[idx];
                };
                const auto bit_sum = [&](std::size_t idx)
                {
                    std::uint32_t sum = 0;
                    for (++idx; idx > 0; idx &= idx - 1U) sum += bit[idx];
                    return sum;
                };

                std::size_t inserted = 0;
                for (const std::size_t edgeIdx : permutation)
                {
                    const std::size_t vOrder = lowerEdgeOrder[edgeIdx];
                    const std::size_t nonCrossing = bit_sum(vOrder);
                    crossingCount += inserted - nonCrossing;
                    bit_add(vOrder);
                    ++inserted;
                }
            }
        }

        HierarchicalLayoutResult result{};
        result.ActiveVertexCount = activeVertices.size();
        result.ActiveEdgeCount = activeEdgeCount;
        result.ComponentCount = componentCount;
        result.LayerCount = globalLayerCount;
        result.MaxLayerWidth = maxLayerWidth;
        result.CrossingCount = crossingCount;
        return result;
    }

    std::optional<std::size_t> CountEdgeCrossings(
        const Graph& graph, std::span<const glm::vec2> positions, const EdgeCrossingParams& params)
    {
        if (graph.VertexCount() == 0 || graph.EdgeCount() == 0) return std::nullopt;
        if (positions.size() < graph.VerticesSize()) return std::nullopt;
        if (params.IntersectionEpsilon < 0.0F) return std::nullopt;

        std::vector<std::pair<std::uint32_t, std::uint32_t>> edges;
        edges.reserve(graph.EdgeCount());

        for (std::uint32_t edgeIdx = 0; edgeIdx < static_cast<std::uint32_t>(graph.EdgesSize()); ++edgeIdx)
        {
            const EdgeHandle edge{edgeIdx};
            if (!graph.IsValid(edge) || graph.IsDeleted(edge)) continue;

            const auto [v0h, v1h] = graph.EdgeVertices(edge);
            if (!v0h.IsValid() || !v1h.IsValid() || graph.IsDeleted(v0h) || graph.IsDeleted(v1h)) continue;

            const std::uint32_t v0 = v0h.Index;
            const std::uint32_t v1 = v1h.Index;
            if (v0 == v1) continue;

            const glm::vec2 p0 = positions[v0];
            const glm::vec2 p1 = positions[v1];
            if (!IsFinite(p0) || !IsFinite(p1)) return std::nullopt;
            edges.emplace_back(v0, v1);
        }

        if (edges.size() < 2U) return std::size_t{0};

        std::size_t crossings = 0;
        for (std::size_t i = 0; i + 1 < edges.size(); ++i)
        {
            const auto [a0, a1] = edges[i];
            const glm::vec2 p0 = positions[a0];
            const glm::vec2 p1 = positions[a1];
            for (std::size_t j = i + 1; j < edges.size(); ++j)
            {
                const auto [b0, b1] = edges[j];

                if (params.IgnoreIncidentEdges)
                {
                    if (a0 == b0 || a0 == b1 || a1 == b0 || a1 == b1) continue;
                }

                const glm::vec2 q0 = positions[b0];
                const glm::vec2 q1 = positions[b1];
                if (SegmentsIntersect(p0, p1, q0, q1, params)) ++crossings;
            }
        }

        return crossings;
    }
}
