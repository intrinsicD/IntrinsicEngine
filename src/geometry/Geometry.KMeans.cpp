module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.KMeans;

import Geometry.Validation;
import Geometry.KDTree;

namespace Geometry::KMeans
{
    std::vector<uint32_t> SampleDistinctIndices(const uint32_t n, const uint32_t count, const uint32_t seed)
    {
        // Position i of the virtual array 0..n-1 holds swapped[i] if present, else i; slots below i are never read.
        std::unordered_map<uint32_t, uint32_t> swapped;
        swapped.reserve(count);
        const auto at = [&](const uint32_t i) {
            const auto found = swapped.find(i);
            return found == swapped.end() ? i : found->second;
        };
        std::mt19937 rng(seed);
        std::vector<uint32_t> picked;
        picked.reserve(count);
        for (uint32_t i = 0; i < count && i < n; ++i)
        {
            const uint32_t j = std::uniform_int_distribution<uint32_t>(i, n - 1u)(rng);
            picked.push_back(at(j));
            swapped[j] = at(i);
        }
        return picked;
    }

    namespace
    {
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
            std::vector<glm::vec3> centroids;
            centroids.reserve(k);
            for (const uint32_t index : SampleDistinctIndices(static_cast<uint32_t>(points.size()), k, seed))
                centroids.push_back(points[index]);
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

        [[nodiscard]] std::vector<glm::vec3> InitializeKMeansPlusPlus(
            std::span<const glm::vec3> points,
            const uint32_t k,
            const uint32_t seed)
        {
            std::mt19937 rng(seed);
            std::uniform_int_distribution<std::size_t> firstDistribution(
                0u, points.size() - 1u);
            const std::size_t firstIndex = firstDistribution(rng);

            std::vector<glm::vec3> centroids{};
            centroids.reserve(k);
            centroids.push_back(points[firstIndex]);

            std::vector<float> minDistances(
                points.size(),
                std::numeric_limits<float>::max());
            std::vector<bool> chosen(points.size(), false);
            chosen[firstIndex] = true;

            while (centroids.size() < k)
            {
                double totalWeight = 0.0;
                for (std::size_t i = 0u; i < points.size(); ++i)
                {
                    minDistances[i] = std::min(
                        minDistances[i],
                        SquaredDistance(points[i], centroids.back()));
                    if (!chosen[i])
                        totalWeight +=
                            static_cast<double>(minDistances[i]);
                }

                std::size_t nextIndex = points.size();
                if (totalWeight > 0.0 && std::isfinite(totalWeight))
                {
                    std::uniform_real_distribution<double> distribution(
                        0.0, totalWeight);
                    const double threshold = distribution(rng);
                    double cumulative = 0.0;
                    for (std::size_t i = 0u; i < points.size(); ++i)
                    {
                        if (chosen[i])
                            continue;
                        cumulative +=
                            static_cast<double>(minDistances[i]);
                        if (cumulative > threshold)
                        {
                            nextIndex = i;
                            break;
                        }
                    }
                }
                if (nextIndex == points.size())
                {
                    const auto unchosen = std::find(
                        chosen.begin(), chosen.end(), false);
                    if (unchosen == chosen.end())
                        break;
                    nextIndex = static_cast<std::size_t>(
                        std::distance(chosen.begin(), unchosen));
                }
                chosen[nextIndex] = true;
                centroids.push_back(points[nextIndex]);
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
                if (!Geometry::Validation::IsFinite(centroid))
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
            if (centroids.empty() || !Geometry::Validation::IsFinite(point))
                return std::nullopt;

            if (cpuScratch != nullptr && !cpuScratch->CentroidTree.Nodes().empty())
            {
                auto& nearest = cpuScratch->QueryBuffer;
                if (cpuScratch->CentroidTree.QueryKNN(point, 1u, nearest) && !nearest.empty())
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

    std::optional<KMeansResult> Cluster(std::span<const glm::vec3> points, const KMeansParams& params)
    {
        return Cluster(points, {}, params, nullptr);
    }

    namespace
    {
        // One assignment pass over the current centroids: labels, distances, inertia and the farthest sample, and
        // with `sums`/`counts` the update's accumulators. Whether a label changed; nullopt for a non-finite point.
        [[nodiscard]] std::optional<bool> AssignLloyd(
            LloydState& state, std::vector<glm::dvec3>* sums, std::vector<uint32_t>* counts)
        {
            KMeansResult& result = state.Result;
            static_cast<void>(RebuildCentroidTree(result.Centroids, state.Scratch));
            result.Inertia = 0.0f;
            float maxDistance = -1.0f;
            bool changed = false;
            for (std::size_t i = 0; i < state.Points.size(); ++i)
            {
                const auto assignment = FindNearestCentroid(state.Points[i], result.Centroids, state.Scratch);
                if (!assignment)
                    return std::nullopt;
                const auto [cluster, distance] = *assignment;
                changed |= result.Labels[i] != cluster;
                result.Labels[i] = cluster;
                result.SquaredDistances[i] = distance;
                result.Inertia += distance;
                if (distance > maxDistance)
                {
                    maxDistance = distance;
                    result.MaxDistanceIndex = static_cast<uint32_t>(i);
                }
                if (sums != nullptr)
                {
                    (*sums)[cluster] += glm::dvec3(state.Points[i]);
                    ++(*counts)[cluster];
                }
            }
            return changed;
        }
    }

    std::optional<LloydState> BeginLloyd(std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const KMeansParams& params,
        CpuScratch* cpuScratch)
    {
        if (points.empty() || params.ClusterCount == 0 || params.MaxIterations == 0)
            return std::nullopt;

        const uint32_t k = std::min<uint32_t>(params.ClusterCount, static_cast<uint32_t>(points.size()));
        if (k == 0)
            return std::nullopt;

        LloydState state{.Points = points,
            .Scratch = cpuScratch,
            .ToleranceSquared = params.ConvergenceTolerance * params.ConvergenceTolerance};
        KMeansResult& result = state.Result;
        result.RequestedBackend = params.Compute;
        result.ActualBackend = Backend::CPU;
        result.FellBackToCPU = params.Compute != Backend::CPU;
        result.Labels.assign(points.size(), 0u);
        result.SquaredDistances.assign(points.size(), 0.0f);
        result.Centroids = BuildInitialCentroids(points, initialCentroids, params, k);
        if (result.Centroids.size() != k)
            return std::nullopt;
        return state;
    }

    LloydStatus StepLloyd(LloydState& state)
    {
        KMeansResult& result = state.Result;
        const auto k = static_cast<uint32_t>(result.Centroids.size());
        std::vector<glm::dvec3> sums(k, glm::dvec3(0.0));
        std::vector<uint32_t> counts(k, 0u);
        const auto changed = AssignLloyd(state, &sums, &counts);
        if (!changed)
            return LloydStatus::Failed;
        const bool anyLabelChanged = result.Iterations == 0 || *changed;

        float maxShift = 0.0f;
        state.CentroidsMoved = false;
        for (uint32_t c = 0; c < k; ++c)
        {
            glm::vec3 nextCentroid = result.Centroids[c];
            if (counts[c] > 0)
            {
                nextCentroid = glm::vec3(sums[c] / static_cast<double>(counts[c]));
            }
            else
            {
                nextCentroid = state.Points[result.MaxDistanceIndex];
            }

            state.CentroidsMoved |= result.Centroids[c] != nextCentroid;
            maxShift = std::max(maxShift, SquaredDistance(result.Centroids[c], nextCentroid));
            result.Centroids[c] = nextCentroid;
        }

        ++result.Iterations;
        state.MaxShiftSquared = maxShift;
        if (!anyLabelChanged || maxShift <= state.ToleranceSquared)
        {
            result.Converged = true;
            return LloydStatus::Converged;
        }
        return LloydStatus::Continue;
    }

    std::optional<KMeansResult> FinishLloyd(LloydState state)
    {
        if (state.CentroidsMoved && !AssignLloyd(state, nullptr, nullptr))
            return std::nullopt;
        return std::move(state.Result);
    }

    std::optional<KMeansResult> Cluster(std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const KMeansParams& params,
        CpuScratch* cpuScratch,
        const IterationObserver& onIteration)
    {
        std::optional<LloydState> state = BeginLloyd(points, initialCentroids, params, cpuScratch);
        if (!state)
            return std::nullopt;
        for (uint32_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            const LloydStatus status = StepLloyd(*state);
            if (status == LloydStatus::Failed)
                return std::nullopt;
            if (onIteration)
                onIteration(iter + 1, params.MaxIterations);
            if (status == LloydStatus::Converged)
                break;
        }
        return FinishLloyd(std::move(*state));
    }

    std::vector<glm::vec3> BuildInitialCentroids(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> initialCentroids,
        const KMeansParams& params,
        uint32_t clusterCount)
    {
        if (points.empty() || clusterCount == 0)
            return {};

        if (std::vector<glm::vec3> seeds = SanitizeInitialCentroids(initialCentroids, clusterCount);
            seeds.size() == clusterCount)
        {
            return seeds;
        }

        switch (params.Init)
        {
        case Initialization::Random:
            return InitializeRandom(
                points, clusterCount, params.Seed);
        case Initialization::Hierarchical:
            return InitializeHierarchical(points, clusterCount);
        case Initialization::KMeansPlusPlus:
            return InitializeKMeansPlusPlus(
                points, clusterCount, params.Seed);
        }
        return {};
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
            if (label >= clusterCount || !Geometry::Validation::IsFinite(points[i]))
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
        if (centroids.empty() || !Geometry::Validation::IsFinite(point))
            return std::nullopt;

        std::optional<uint32_t> bestCluster;
        float bestDistance = 0.0f;

        for (uint32_t c = 0; c < static_cast<uint32_t>(centroids.size()); ++c)
        {
            if (!Geometry::Validation::IsFinite(centroids[c]))
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
