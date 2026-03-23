module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.KMeans;

import Geometry.KDTree;

namespace Geometry::KMeans
{
    namespace
    {
        [[nodiscard]] bool IsFiniteVec3(const glm::vec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] float SquaredDistance(const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 d = a - b;
            return glm::dot(d, d);
        }

        [[nodiscard]] std::vector<glm::vec3> InitializeRandom(
            std::span<const glm::vec3> points,
            uint32_t k,
            uint32_t seed)
        {
            std::vector<uint32_t> indices(points.size());
            std::iota(indices.begin(), indices.end(), 0u);

            std::mt19937 rng(seed);
            std::shuffle(indices.begin(), indices.end(), rng);

            std::vector<glm::vec3> centroids;
            centroids.reserve(k);
            for (uint32_t i = 0; i < k; ++i)
                centroids.push_back(points[indices[i]]);
            return centroids;
        }

        [[nodiscard]] std::vector<glm::vec3> InitializeHierarchical(
            std::span<const glm::vec3> points,
            uint32_t k)
        {
            glm::vec3 mean(0.0f);
            for (const glm::vec3& p : points)
                mean += p;
            mean /= static_cast<float>(points.size());

            std::vector<glm::vec3> centroids;
            centroids.reserve(k);
            centroids.push_back(mean);

            std::vector<float> minDistances(points.size(), std::numeric_limits<float>::max());
            for (uint32_t c = 1; c < k; ++c)
            {
                uint32_t farthestIndex = 0;
                float farthestDistance = -1.0f;

                for (std::size_t i = 0; i < points.size(); ++i)
                {
                    const float d = SquaredDistance(points[i], centroids.back());
                    minDistances[i] = std::min(minDistances[i], d);
                    if (minDistances[i] > farthestDistance)
                    {
                        farthestDistance = minDistances[i];
                        farthestIndex = static_cast<uint32_t>(i);
                    }
                }

                centroids.push_back(points[farthestIndex]);
            }

            return centroids;
        }

        [[nodiscard]] std::vector<glm::vec3> SanitizeInitialCentroids(
            std::span<const glm::vec3> initialCentroids,
            uint32_t k)
        {
            std::vector<glm::vec3> seeds;
            seeds.reserve(k);
            for (const glm::vec3& centroid : initialCentroids)
            {
                if (!IsFiniteVec3(centroid))
                    continue;
                seeds.push_back(centroid);
                if (seeds.size() == k)
                    break;
            }
            return seeds;
        }

        [[nodiscard]] bool RebuildCentroidTree(
            std::span<const glm::vec3> centroids,
            CpuScratch* cpuScratch)
        {
            if (cpuScratch == nullptr)
                return false;

            return cpuScratch->CentroidTree.BuildFromPoints(
                centroids, cpuScratch->CentroidTreeBuildParams).has_value();
        }

        [[nodiscard]] std::optional<std::pair<uint32_t, float>> FindNearestCentroid(
            const glm::vec3& point,
            std::span<const glm::vec3> centroids,
            CpuScratch* cpuScratch)
        {
            if (centroids.empty() || !IsFiniteVec3(point))
                return std::nullopt;

            if (cpuScratch != nullptr && !cpuScratch->CentroidTree.Nodes().empty())
            {
                auto& nearest = cpuScratch->QueryBuffer;
                if (cpuScratch->CentroidTree.QueryKnn(point, 1u, nearest) && !nearest.empty())
                {
                    const uint32_t bestCluster = nearest.front();
                    if (bestCluster < centroids.size())
                        return std::pair<uint32_t, float>{bestCluster, SquaredDistance(point, centroids[bestCluster])};
                }
            }

            uint32_t bestCluster = 0;
            float bestDistance = SquaredDistance(point, centroids[0]);
            for (uint32_t c = 1; c < static_cast<uint32_t>(centroids.size()); ++c)
            {
                const float d = SquaredDistance(point, centroids[c]);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    bestCluster = c;
                }
            }

            return std::pair<uint32_t, float>{bestCluster, bestDistance};
        }
    }

    std::optional<Result> Cluster(std::span<const glm::vec3> points, const Params& params)
    {
        return Cluster(points, {}, params, nullptr);
    }

    std::optional<Result> Cluster(std::span<const glm::vec3> points,
                                  std::span<const glm::vec3> initialCentroids,
                                  const Params& params,
                                  CpuScratch* cpuScratch)
    {
        if (points.empty() || params.ClusterCount == 0 || params.MaxIterations == 0)
            return std::nullopt;

        const uint32_t k = std::min<uint32_t>(params.ClusterCount, static_cast<uint32_t>(points.size()));
        if (k == 0)
            return std::nullopt;

        Result result{};
        result.ActualBackend = Backend::CPU;
        result.Labels.assign(points.size(), 0u);
        result.SquaredDistances.assign(points.size(), 0.0f);
        result.Centroids = BuildInitialCentroids(points, initialCentroids, params, k);
        if (result.Centroids.size() != k)
            return std::nullopt;

        std::vector<glm::vec3> sums(k, glm::vec3(0.0f));
        std::vector<uint32_t> counts(k, 0u);
        std::vector<uint32_t> nextLabels(points.size(), 0u);

        const float tol2 = params.ConvergenceTolerance * params.ConvergenceTolerance;
        bool anyLabelChanged = true;

        for (uint32_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            std::fill(sums.begin(), sums.end(), glm::vec3(0.0f));
            std::fill(counts.begin(), counts.end(), 0u);
            static_cast<void>(RebuildCentroidTree(result.Centroids, cpuScratch));

            float inertia = 0.0f;
            float maxDistance = -1.0f;
            uint32_t maxDistanceIndex = 0;
            anyLabelChanged = false;

            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto assignment = FindNearestCentroid(points[i], result.Centroids, cpuScratch);
                if (!assignment)
                    return std::nullopt;

                const auto [bestCluster, bestDistance] = *assignment;
                nextLabels[i] = bestCluster;
                result.SquaredDistances[i] = bestDistance;
                sums[bestCluster] += points[i];
                ++counts[bestCluster];
                inertia += bestDistance;

                if (bestDistance > maxDistance)
                {
                    maxDistance = bestDistance;
                    maxDistanceIndex = static_cast<uint32_t>(i);
                }

                if (nextLabels[i] != result.Labels[i])
                    anyLabelChanged = true;
            }

            float maxShift = 0.0f;
            for (uint32_t c = 0; c < k; ++c)
            {
                glm::vec3 nextCentroid = result.Centroids[c];
                if (counts[c] > 0)
                {
                    nextCentroid = sums[c] / static_cast<float>(counts[c]);
                }
                else
                {
                    nextCentroid = points[maxDistanceIndex];
                }

                maxShift = std::max(maxShift, SquaredDistance(result.Centroids[c], nextCentroid));
                result.Centroids[c] = nextCentroid;
            }

            result.Iterations = iter + 1;
            result.Inertia = inertia;
            result.MaxDistanceIndex = maxDistanceIndex;
            result.Labels = nextLabels;

            if (!anyLabelChanged || maxShift <= tol2)
            {
                result.Converged = true;
                break;
            }
        }

        return result;
    }

    std::vector<glm::vec3> BuildInitialCentroids(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const Params& params,
        uint32_t clusterCount)
    {
        if (points.empty() || clusterCount == 0)
            return {};

        if (std::vector<glm::vec3> seeds = SanitizeInitialCentroids(initialCentroids, clusterCount);
            seeds.size() == clusterCount)
        {
            return seeds;
        }

        return (params.Init == Initialization::Random)
            ? InitializeRandom(points, clusterCount, params.Seed)
            : InitializeHierarchical(points, clusterCount);
    }

    std::vector<glm::vec3> RecomputeCentroids(
        std::span<const glm::vec3> points,
        std::span<const uint32_t> labels,
        uint32_t clusterCount)
    {
        if (clusterCount == 0 || points.empty() || labels.size() != points.size())
            return {};

        std::vector<glm::vec3> centroids(clusterCount, glm::vec3(0.0f));
        std::vector<uint32_t> counts(clusterCount, 0u);

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            const uint32_t label = labels[i];
            if (label >= clusterCount || !IsFiniteVec3(points[i]))
                continue;

            centroids[label] += points[i];
            ++counts[label];
        }

        for (uint32_t c = 0; c < clusterCount; ++c)
        {
            if (counts[c] > 0)
                centroids[c] /= static_cast<float>(counts[c]);
        }

        return centroids;
    }

    std::optional<uint32_t> ClassifyPointToCentroid(
        const glm::vec3& point,
        std::span<const glm::vec3> centroids) noexcept
    {
        if (centroids.empty() || !IsFiniteVec3(point))
            return std::nullopt;

        std::optional<uint32_t> bestCluster;
        float bestDistance = 0.0f;

        for (uint32_t c = 0; c < static_cast<uint32_t>(centroids.size()); ++c)
        {
            if (!IsFiniteVec3(centroids[c]))
                continue;

            const float d = SquaredDistance(point, centroids[c]);
            if (!bestCluster || d < bestDistance)
            {
                bestCluster = c;
                bestDistance = d;
            }
        }

        return bestCluster;
    }
}
