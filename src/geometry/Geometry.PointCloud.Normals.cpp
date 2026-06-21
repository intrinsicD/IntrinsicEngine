module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>

module Geometry.PointCloud.Normals;

import Geometry.KDTree;
import Geometry.Octree;
import Geometry.PCA;
import Geometry.PointCloud;
import Geometry.Properties;
import Geometry.Primitives;

namespace Geometry::PointCloud::Normals
{
    namespace
    {
        constexpr glm::vec3 kDefaultFallbackNormal{0.0f, 0.0f, 1.0f};
        constexpr std::size_t kInvalidPointIndex = std::numeric_limits<std::size_t>::max();

        struct QueryContext
        {
            NeighborhoodBackend Backend{NeighborhoodBackend::KDTree};
            const KDTree* KdTree{nullptr};
            const Octree* OctreeIndex{nullptr};
            KDTree OwnedKdTree{};
            std::vector<glm::vec3> CompactPoints{};
            std::vector<std::size_t> CompactToOriginal{};
            std::vector<std::size_t> OriginalToCompact{};
        };

        [[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept
        {
            return std::isfinite(value.x)
                && std::isfinite(value.y)
                && std::isfinite(value.z);
        }

        [[nodiscard]] double Epsilon(const Params& params) noexcept
        {
            return params.DegenerateNormalLengthEpsilon > 0.0
                ? params.DegenerateNormalLengthEpsilon
                : Params{}.DegenerateNormalLengthEpsilon;
        }

        [[nodiscard]] bool IsDeletedPoint(const ConstProperty<bool>& deleted,
                                          const std::size_t index,
                                          const Params& params) noexcept
        {
            return params.SkipDeleted
                && deleted.IsValid()
                && index < deleted.Vector().size()
                && deleted[index];
        }

        void NormalizeFallback(const Params& params,
                               Diagnostics& diagnostics,
                               glm::vec3& out) noexcept
        {
            if (IsFinite(params.FallbackNormal))
            {
                const double lenSq = static_cast<double>(glm::dot(params.FallbackNormal, params.FallbackNormal));
                const double epsilon = Epsilon(params);
                if (std::isfinite(lenSq) && lenSq > epsilon * epsilon)
                {
                    out = params.FallbackNormal * static_cast<float>(1.0 / std::sqrt(lenSq));
                    return;
                }
            }

            out = kDefaultFallbackNormal;
            diagnostics.FallbackNormalWasRepaired = true;
        }

        [[nodiscard]] bool NormalizeNormal(glm::vec3& normal,
                                           const Params& params) noexcept
        {
            const double lenSq = static_cast<double>(glm::dot(normal, normal));
            const double epsilon = Epsilon(params);
            if (!std::isfinite(lenSq) || lenSq <= epsilon * epsilon)
            {
                return false;
            }
            normal *= static_cast<float>(1.0 / std::sqrt(lenSq));
            return IsFinite(normal);
        }

        [[nodiscard]] bool IsCollinearNeighborhood(const PCAResult& pca,
                                                   const Params& params) noexcept
        {
            const float largest = std::max(pca.Eigenvalues.x, 0.0f);
            if (largest <= static_cast<float>(Epsilon(params)))
            {
                return true;
            }
            return pca.Eigenvalues.y <= largest * static_cast<float>(params.CollinearEigenvalueRatioEpsilon);
        }

        [[nodiscard]] std::size_t EffectiveNeighborTarget(const Params& params) noexcept
        {
            return std::max(params.KNeighbors, params.MinimumNeighbors);
        }

        void MarkAllNonDeletedFallbacks(std::span<const glm::vec3> points,
                                        const ConstProperty<bool>& deleted,
                                        const Params& params,
                                        Diagnostics& diagnostics)
        {
            diagnostics.WrittenCount = 0;
            diagnostics.FallbackPointCount = 0;
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                if (IsDeletedPoint(deleted, index, params))
                {
                    continue;
                }
                ++diagnostics.WrittenCount;
                ++diagnostics.FallbackPointCount;
            }
        }

        void SortAndFilterNeighbors(std::span<const glm::vec3> points,
                                    const ConstProperty<bool>& deleted,
                                    const std::size_t queryIndex,
                                    const Params& params,
                                    std::vector<std::size_t>& neighbors)
        {
            neighbors.erase(std::remove_if(neighbors.begin(),
                                           neighbors.end(),
                                           [&](const std::size_t index)
                                           {
                                               return index >= points.size()
                                                   || index == queryIndex
                                                   || IsDeletedPoint(deleted, index, params)
                                                   || !IsFinite(points[index]);
                                           }),
                            neighbors.end());

            std::sort(neighbors.begin(),
                      neighbors.end(),
                      [&](const std::size_t lhs, const std::size_t rhs)
                      {
                          const glm::vec3 lhsDelta = points[lhs] - points[queryIndex];
                          const glm::vec3 rhsDelta = points[rhs] - points[queryIndex];
                          const float lhsDist = glm::dot(lhsDelta, lhsDelta);
                          const float rhsDist = glm::dot(rhsDelta, rhsDelta);
                          if (lhsDist != rhsDist)
                          {
                              return lhsDist < rhsDist;
                          }
                          return lhs < rhs;
                      });

            neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
        }

        [[nodiscard]] bool PrepareInternalKDTree(std::span<const glm::vec3> points,
                                                 const ConstProperty<bool>& deleted,
                                                 const Params& params,
                                                 EstimateResult& result,
                                                 QueryContext& context)
        {
            context.Backend = NeighborhoodBackend::KDTree;
            context.OriginalToCompact.assign(points.size(), kInvalidPointIndex);
            context.CompactPoints.clear();
            context.CompactToOriginal.clear();

            for (std::size_t index = 0; index < points.size(); ++index)
            {
                if (IsDeletedPoint(deleted, index, params))
                {
                    ++result.Diagnostics.SkippedDeletedPointCount;
                    continue;
                }
                if (!IsFinite(points[index]))
                {
                    ++result.Diagnostics.NonFinitePointCount;
                    continue;
                }

                context.OriginalToCompact[index] = context.CompactPoints.size();
                context.CompactToOriginal.push_back(index);
                context.CompactPoints.push_back(points[index]);
            }

            result.Diagnostics.FinitePointCount = context.CompactPoints.size();
            if (context.CompactPoints.size() < 3u)
            {
                result.Status = RecomputeStatus::TooFewFinitePoints;
                MarkAllNonDeletedFallbacks(points, deleted, params, result.Diagnostics);
                return false;
            }

            const auto build = context.OwnedKdTree.BuildFromPoints(context.CompactPoints, params.KDTreeBuild);
            if (!build.has_value())
            {
                result.Status = RecomputeStatus::SpatialIndexBuildFailed;
                return false;
            }

            context.KdTree = &context.OwnedKdTree;
            return true;
        }

        [[nodiscard]] bool PrepareSuppliedIndex(std::span<const glm::vec3> points,
                                                const ConstProperty<bool>& deleted,
                                                const Params& params,
                                                EstimateResult& result)
        {
            for (std::size_t index = 0; index < points.size(); ++index)
            {
                if (IsDeletedPoint(deleted, index, params))
                {
                    ++result.Diagnostics.SkippedDeletedPointCount;
                    continue;
                }
                if (!IsFinite(points[index]))
                {
                    ++result.Diagnostics.NonFinitePointCount;
                    continue;
                }
                ++result.Diagnostics.FinitePointCount;
            }

            if (result.Diagnostics.FinitePointCount < 3u)
            {
                result.Status = RecomputeStatus::TooFewFinitePoints;
                MarkAllNonDeletedFallbacks(points, deleted, params, result.Diagnostics);
                return false;
            }

            return true;
        }

        [[nodiscard]] bool QueryKDTreeNeighbors(const KDTree& tree,
                                                const QueryContext& context,
                                                const bool compactIndex,
                                                const glm::vec3 query,
                                                const std::size_t queryIndex,
                                                const Params& params,
                                                EstimateResult& result,
                                                std::vector<std::size_t>& out)
        {
            out.clear();
            if (params.UseRadiusSearch)
            {
                if (!std::isfinite(params.Radius) || params.Radius <= 0.0f)
                {
                    ++result.Diagnostics.SpatialQueryFailureCount;
                    return false;
                }

                std::vector<KDTree::ElementIndex> indices;
                const auto queryResult = tree.QueryRadius(query, params.Radius, indices);
                if (!queryResult.has_value())
                {
                    ++result.Diagnostics.SpatialQueryFailureCount;
                    return false;
                }

                result.Diagnostics.KNNVisitedNodeCount += queryResult->VisitedNodes;
                result.Diagnostics.KNNDistanceEvaluationCount += queryResult->DistanceEvaluations;
                out.reserve(indices.size());
                for (const auto index : indices)
                {
                    const std::size_t mapped = compactIndex
                        ? context.CompactToOriginal[static_cast<std::size_t>(index)]
                        : static_cast<std::size_t>(index);
                    out.push_back(mapped);
                }
                return true;
            }

            const std::size_t target = EffectiveNeighborTarget(params) + 1u;
            std::vector<KDTree::ElementIndex> indices;
            const auto queryResult = tree.QueryKNN(query, static_cast<std::uint32_t>(target), indices);
            if (!queryResult.has_value())
            {
                ++result.Diagnostics.SpatialQueryFailureCount;
                return false;
            }

            result.Diagnostics.KNNVisitedNodeCount += queryResult->VisitedNodes;
            result.Diagnostics.KNNDistanceEvaluationCount += queryResult->DistanceEvaluations;
            out.reserve(indices.size());
            for (const auto index : indices)
            {
                const std::size_t mapped = compactIndex
                    ? context.CompactToOriginal[static_cast<std::size_t>(index)]
                    : static_cast<std::size_t>(index);
                out.push_back(mapped);
            }
            static_cast<void>(queryIndex);
            return true;
        }

        [[nodiscard]] bool QueryOctreeNeighbors(const Octree& octree,
                                                const glm::vec3 query,
                                                const Params& params,
                                                EstimateResult& result,
                                                std::vector<std::size_t>& out)
        {
            out.clear();
            if (params.UseRadiusSearch)
            {
                if (!std::isfinite(params.Radius) || params.Radius <= 0.0f)
                {
                    ++result.Diagnostics.SpatialQueryFailureCount;
                    return false;
                }

                octree.QuerySphere(Sphere{.Center = query, .Radius = params.Radius}, out);
                return true;
            }

            octree.QueryKNN(query, EffectiveNeighborTarget(params) + 1u, out);
            return true;
        }

        void CountDuplicateSamples(std::span<const glm::vec3> samples,
                                   const Params& params,
                                   Diagnostics& diagnostics)
        {
            const double epsilonSq = Epsilon(params) * Epsilon(params);
            for (std::size_t i = 1; i < samples.size(); ++i)
            {
                for (std::size_t j = 0; j < i; ++j)
                {
                    const glm::vec3 delta = samples[i] - samples[j];
                    const double distSq = static_cast<double>(glm::dot(delta, delta));
                    if (!std::isfinite(distSq) || distSq <= epsilonSq)
                    {
                        ++diagnostics.DuplicatePositionCount;
                        break;
                    }
                }
            }
        }

        void OrientNormalsMST(std::span<const glm::vec3> points,
                              const glm::vec3 fallbackNormal,
                              const std::vector<std::vector<std::size_t>>& neighborhoods,
                              const std::vector<bool>& validNormals,
                              std::vector<glm::vec3>& normals,
                              Diagnostics& diagnostics)
        {
            const std::size_t n = points.size();
            std::vector<bool> visited(n, false);

            using QueueEntry = std::tuple<float, std::size_t, std::size_t>;
            std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<>> queue;

            while (true)
            {
                std::size_t seed = kInvalidPointIndex;
                float bestZ = -std::numeric_limits<float>::max();
                for (std::size_t i = 0; i < n; ++i)
                {
                    if (!validNormals[i] || visited[i])
                    {
                        continue;
                    }
                    if (points[i].z > bestZ || (points[i].z == bestZ && i < seed))
                    {
                        bestZ = points[i].z;
                        seed = i;
                    }
                }

                if (seed == kInvalidPointIndex)
                {
                    break;
                }

                if (glm::dot(normals[seed], fallbackNormal) < 0.0f)
                {
                    normals[seed] = -normals[seed];
                    ++diagnostics.FlippedOrientationCount;
                }

                queue.emplace(0.0f, seed, seed);
                while (!queue.empty())
                {
                    const auto [weight, current, parent] = queue.top();
                    static_cast<void>(weight);
                    queue.pop();

                    if (visited[current])
                    {
                        continue;
                    }
                    visited[current] = true;

                    if (current != parent && glm::dot(normals[current], normals[parent]) < 0.0f)
                    {
                        normals[current] = -normals[current];
                        ++diagnostics.FlippedOrientationCount;
                    }

                    for (const std::size_t neighbor : neighborhoods[current])
                    {
                        if (neighbor >= n || visited[neighbor] || !validNormals[neighbor])
                        {
                            continue;
                        }
                        const float edgeWeight = 1.0f - std::abs(glm::dot(normals[current], normals[neighbor]));
                        queue.emplace(edgeWeight, neighbor, current);
                    }
                }
            }
        }

        [[nodiscard]] EstimateResult Compute(std::span<const glm::vec3> points,
                                             const ConstProperty<bool>& deleted,
                                             QueryContext& context,
                                             const Params& params)
        {
            EstimateResult result{};
            result.Backend = context.Backend;
            result.Diagnostics.PointSlotCount = points.size();

            glm::vec3 fallbackNormal{0.0f};
            NormalizeFallback(params, result.Diagnostics, fallbackNormal);
            result.Normals.assign(points.size(), fallbackNormal);

            if (points.empty())
            {
                result.Status = RecomputeStatus::EmptyInput;
                return result;
            }

            if (context.Backend == NeighborhoodBackend::KDTree)
            {
                if (!PrepareInternalKDTree(points, deleted, params, result, context))
                {
                    return result;
                }
            }
            else if (!PrepareSuppliedIndex(points, deleted, params, result))
            {
                return result;
            }

            std::vector<std::vector<std::size_t>> neighborhoods(points.size());
            std::vector<bool> validNormals(points.size(), false);
            std::vector<std::size_t> neighbors;
            std::vector<glm::vec3> samples;

            for (std::size_t index = 0; index < points.size(); ++index)
            {
                if (IsDeletedPoint(deleted, index, params))
                {
                    continue;
                }

                if (!IsFinite(points[index]))
                {
                    ++result.Diagnostics.FallbackPointCount;
                    ++result.Diagnostics.WrittenCount;
                    continue;
                }

                bool queryOk = false;
                if (context.Backend == NeighborhoodBackend::KDTree)
                {
                    queryOk = QueryKDTreeNeighbors(*context.KdTree,
                                                   context,
                                                   true,
                                                   points[index],
                                                   index,
                                                   params,
                                                   result,
                                                   neighbors);
                }
                else if (context.Backend == NeighborhoodBackend::SuppliedKDTree)
                {
                    queryOk = context.KdTree != nullptr
                        && QueryKDTreeNeighbors(*context.KdTree,
                                                context,
                                                false,
                                                points[index],
                                                index,
                                                params,
                                                result,
                                                neighbors);
                }
                else
                {
                    queryOk = context.OctreeIndex != nullptr
                        && QueryOctreeNeighbors(*context.OctreeIndex,
                                                points[index],
                                                params,
                                                result,
                                                neighbors);
                }

                if (!queryOk)
                {
                    ++result.Diagnostics.FallbackPointCount;
                    ++result.Diagnostics.WrittenCount;
                    continue;
                }

                SortAndFilterNeighbors(points, deleted, index, params, neighbors);
                neighborhoods[index] = neighbors;

                if (neighbors.size() < params.MinimumNeighbors)
                {
                    ++result.Diagnostics.TooFewNeighborCount;
                    ++result.Diagnostics.FallbackPointCount;
                    ++result.Diagnostics.WrittenCount;
                    continue;
                }

                samples.clear();
                samples.push_back(points[index]);
                for (const std::size_t neighbor : neighbors)
                {
                    samples.push_back(points[neighbor]);
                }

                if (samples.size() < 3u)
                {
                    ++result.Diagnostics.DegenerateNeighborhoodCount;
                    ++result.Diagnostics.FallbackPointCount;
                    ++result.Diagnostics.WrittenCount;
                    continue;
                }

                CountDuplicateSamples(samples, params, result.Diagnostics);

                const PCAResult pca = ToPCA(samples);
                if (!pca.Valid || IsCollinearNeighborhood(pca, params))
                {
                    ++result.Diagnostics.CollinearNeighborhoodCount;
                    ++result.Diagnostics.FallbackPointCount;
                    ++result.Diagnostics.WrittenCount;
                    continue;
                }

                glm::vec3 normal = pca.Eigenvectors[2];
                if (!NormalizeNormal(normal, params))
                {
                    ++result.Diagnostics.DegenerateNeighborhoodCount;
                    ++result.Diagnostics.FallbackPointCount;
                    ++result.Diagnostics.WrittenCount;
                    continue;
                }

                result.Normals[index] = normal;
                validNormals[index] = true;
                ++result.Diagnostics.ValidNormalPointCount;
                ++result.Diagnostics.WrittenCount;
            }

            if (params.Orientation == OrientationMode::MinimumSpanningTree)
            {
                OrientNormalsMST(points,
                                 fallbackNormal,
                                 neighborhoods,
                                 validNormals,
                                 result.Normals,
                                 result.Diagnostics);
            }

            if (result.Diagnostics.SpatialQueryFailureCount > 0u
                && result.Diagnostics.ValidNormalPointCount == 0u)
            {
                result.Status = RecomputeStatus::SpatialIndexQueryFailed;
            }

            return result;
        }

        [[nodiscard]] EstimateResult ComputeWithInternalKDTree(std::span<const glm::vec3> points,
                                                               const ConstProperty<bool>& deleted,
                                                               const Params& params)
        {
            QueryContext context{};
            context.Backend = NeighborhoodBackend::KDTree;
            return Compute(points, deleted, context, params);
        }

        [[nodiscard]] EstimateResult ComputeWithKDTree(std::span<const glm::vec3> points,
                                                       const ConstProperty<bool>& deleted,
                                                       const KDTree& index,
                                                       const Params& params)
        {
            QueryContext context{};
            context.Backend = NeighborhoodBackend::SuppliedKDTree;
            context.KdTree = &index;
            return Compute(points, deleted, context, params);
        }

        [[nodiscard]] EstimateResult ComputeWithOctree(std::span<const glm::vec3> points,
                                                       const ConstProperty<bool>& deleted,
                                                       const Octree& index,
                                                       const Params& params)
        {
            QueryContext context{};
            context.Backend = NeighborhoodBackend::SuppliedOctree;
            context.OctreeIndex = &index;
            return Compute(points, deleted, context, params);
        }

        [[nodiscard]] std::optional<EstimateResult> ToOptional(EstimateResult result)
        {
            if (result.Status != RecomputeStatus::Success)
            {
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] PropertySetResult RecomputeFromEstimate(Vertices& vertices,
                                                              const ConstProperty<glm::vec3> positions,
                                                              const ConstProperty<bool>& deleted,
                                                              const Params& params,
                                                              const auto& estimateFn)
        {
            PropertySetResult result{};

            if (params.OutputProperty.empty())
            {
                result.Status = RecomputeStatus::InvalidOutputProperty;
                return result;
            }

            Diagnostics fallbackDiagnostics{};
            glm::vec3 fallbackNormal{0.0f};
            NormalizeFallback(params, fallbackDiagnostics, fallbackNormal);

            auto normals = vertices.GetOrAdd<glm::vec3>(std::string(params.OutputProperty), fallbackNormal);
            if (!normals.IsValid())
            {
                result.Status = RecomputeStatus::PropertyTypeConflict;
                return result;
            }
            result.Normals = normals;

            if (vertices.Size() == 0u)
            {
                result.Status = RecomputeStatus::EmptyInput;
                result.Diagnostics.FallbackNormalWasRepaired = fallbackDiagnostics.FallbackNormalWasRepaired;
                return result;
            }

            if (!positions.IsValid())
            {
                result.Status = RecomputeStatus::InvalidPositionProperty;
                result.Diagnostics.FallbackNormalWasRepaired = fallbackDiagnostics.FallbackNormalWasRepaired;
                return result;
            }

            if (positions.Vector().size() != vertices.Size()
                || (deleted.IsValid() && deleted.Vector().size() != vertices.Size()))
            {
                result.Status = RecomputeStatus::CountMismatch;
                result.Diagnostics.FallbackNormalWasRepaired = fallbackDiagnostics.FallbackNormalWasRepaired;
                return result;
            }

            const EstimateResult estimate = estimateFn(positions.Span(), deleted, params);
            result.Status = estimate.Status;
            result.Backend = estimate.Backend;
            result.Diagnostics = estimate.Diagnostics;
            if (estimate.Normals.size() == vertices.Size())
            {
                normals.Vector() = estimate.Normals;
            }
            return result;
        }

        [[nodiscard]] Result ToCloudResult(const PropertySetResult& raw)
        {
            Result result{};
            result.Status = raw.Status;
            result.Backend = raw.Backend;
            result.Diagnostics = raw.Diagnostics;
            if (raw.Normals.IsValid())
            {
                result.Normals = VertexProperty<glm::vec3>(raw.Normals);
            }
            return result;
        }
    } // namespace

    std::string_view DebugName(const NeighborhoodBackend backend) noexcept
    {
        switch (backend)
        {
        case NeighborhoodBackend::KDTree:
            return "KDTree";
        case NeighborhoodBackend::SuppliedKDTree:
            return "SuppliedKDTree";
        case NeighborhoodBackend::SuppliedOctree:
            return "SuppliedOctree";
        }

        return "Unknown";
    }

    std::string_view DebugName(const OrientationMode mode) noexcept
    {
        switch (mode)
        {
        case OrientationMode::None:
            return "None";
        case OrientationMode::MinimumSpanningTree:
            return "MinimumSpanningTree";
        }

        return "Unknown";
    }

    std::string_view DebugName(const RecomputeStatus status) noexcept
    {
        switch (status)
        {
        case RecomputeStatus::Success:
            return "Success";
        case RecomputeStatus::EmptyInput:
            return "EmptyInput";
        case RecomputeStatus::TooFewFinitePoints:
            return "TooFewFinitePoints";
        case RecomputeStatus::InvalidPositionProperty:
            return "InvalidPositionProperty";
        case RecomputeStatus::InvalidOutputProperty:
            return "InvalidOutputProperty";
        case RecomputeStatus::PropertyTypeConflict:
            return "PropertyTypeConflict";
        case RecomputeStatus::CountMismatch:
            return "CountMismatch";
        case RecomputeStatus::SpatialIndexBuildFailed:
            return "SpatialIndexBuildFailed";
        case RecomputeStatus::SpatialIndexQueryFailed:
            return "SpatialIndexQueryFailed";
        }

        return "Unknown";
    }

    std::optional<EstimateResult> Estimate(std::span<const glm::vec3> points,
                                           const Params& params)
    {
        return ToOptional(ComputeWithInternalKDTree(points, ConstProperty<bool>{}, params));
    }

    std::optional<EstimateResult> Estimate(std::span<const glm::vec3> points,
                                           const KDTree& index,
                                           const Params& params)
    {
        return ToOptional(ComputeWithKDTree(points, ConstProperty<bool>{}, index, params));
    }

    std::optional<EstimateResult> Estimate(std::span<const glm::vec3> points,
                                           const Octree& index,
                                           const Params& params)
    {
        return ToOptional(ComputeWithOctree(points, ConstProperty<bool>{}, index, params));
    }

    PropertySetResult Recompute(Vertices& vertices, const Params& params)
    {
        const auto positions = ConstPropertySet(vertices).Get<glm::vec3>(params.PositionProperty);
        return Recompute(vertices, positions, params);
    }

    PropertySetResult Recompute(Vertices& vertices,
                                const ConstProperty<glm::vec3> positions,
                                const Params& params)
    {
        auto estimateFn = [](std::span<const glm::vec3> pointSpan,
                             const ConstProperty<bool>& deleted,
                             const Params& computeParams)
        {
            return ComputeWithInternalKDTree(pointSpan, deleted, computeParams);
        };
        return RecomputeFromEstimate(vertices, positions, ConstProperty<bool>{}, params, estimateFn);
    }

    PropertySetResult Recompute(Vertices& vertices,
                                const ConstProperty<glm::vec3> positions,
                                const KDTree& index,
                                const Params& params)
    {
        auto estimateFn = [&index](std::span<const glm::vec3> pointSpan,
                                   const ConstProperty<bool>& deleted,
                                   const Params& computeParams)
        {
            return ComputeWithKDTree(pointSpan, deleted, index, computeParams);
        };
        return RecomputeFromEstimate(vertices, positions, ConstProperty<bool>{}, params, estimateFn);
    }

    PropertySetResult Recompute(Vertices& vertices,
                                const ConstProperty<glm::vec3> positions,
                                const Octree& index,
                                const Params& params)
    {
        auto estimateFn = [&index](std::span<const glm::vec3> pointSpan,
                                   const ConstProperty<bool>& deleted,
                                   const Params& computeParams)
        {
            return ComputeWithOctree(pointSpan, deleted, index, computeParams);
        };
        return RecomputeFromEstimate(vertices, positions, ConstProperty<bool>{}, params, estimateFn);
    }

    Result Recompute(Cloud& cloud, const Params& params)
    {
        const auto positions = ConstPropertySet(cloud.PointProperties()).Get<glm::vec3>(params.PositionProperty);
        const auto deleted = ConstPropertySet(cloud.PointProperties()).Get<bool>("p:deleted");
        auto estimateFn = [](std::span<const glm::vec3> pointSpan,
                             const ConstProperty<bool>& deletedProperty,
                             const Params& computeParams)
        {
            return ComputeWithInternalKDTree(pointSpan, deletedProperty, computeParams);
        };
        return ToCloudResult(RecomputeFromEstimate(cloud.PointProperties(), positions, deleted, params, estimateFn));
    }

    Result Recompute(Cloud& cloud, const KDTree& index, const Params& params)
    {
        const auto positions = ConstPropertySet(cloud.PointProperties()).Get<glm::vec3>(params.PositionProperty);
        const auto deleted = ConstPropertySet(cloud.PointProperties()).Get<bool>("p:deleted");
        auto estimateFn = [&index](std::span<const glm::vec3> pointSpan,
                                   const ConstProperty<bool>& deletedProperty,
                                   const Params& computeParams)
        {
            return ComputeWithKDTree(pointSpan, deletedProperty, index, computeParams);
        };
        return ToCloudResult(RecomputeFromEstimate(cloud.PointProperties(), positions, deleted, params, estimateFn));
    }

    Result Recompute(Cloud& cloud, const Octree& index, const Params& params)
    {
        const auto positions = ConstPropertySet(cloud.PointProperties()).Get<glm::vec3>(params.PositionProperty);
        const auto deleted = ConstPropertySet(cloud.PointProperties()).Get<bool>("p:deleted");
        auto estimateFn = [&index](std::span<const glm::vec3> pointSpan,
                                   const ConstProperty<bool>& deletedProperty,
                                   const Params& computeParams)
        {
            return ComputeWithOctree(pointSpan, deletedProperty, index, computeParams);
        };
        return ToCloudResult(RecomputeFromEstimate(cloud.PointProperties(), positions, deleted, params, estimateFn));
    }
} // namespace Geometry::PointCloud::Normals
