module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>
#include <span>

#include <glm/glm.hpp>

module Geometry.PointCloudUtils;

import Geometry.AABB;
import Geometry.Octree;

namespace Geometry::PointCloud
{
    // =========================================================================
    // Cloud — construction
    // =========================================================================

    AABB ComputeBoundingBox(const Cloud& cloud)
    {
        if (cloud.IsEmpty())
            return AABB{glm::vec3(0.0f), glm::vec3(0.0f)};

        auto positions = cloud.Positions();
        glm::vec3 mn = positions[0];
        glm::vec3 mx = positions[0];

        for (std::size_t i = 1; i < positions.size(); ++i)
        {
            mn = glm::min(mn, positions[i]);
            mx = glm::max(mx, positions[i]);
        }

        return AABB{mn, mx};
    }

    // =========================================================================
    // ComputeStatistics
    // =========================================================================

    std::optional<CloudStatistics> ComputeStatistics(
        const Cloud& cloud,
        const StatisticsParams& params)
    {
        if (cloud.IsEmpty())
            return std::nullopt;

        CloudStatistics stats{};
        stats.PointCount = cloud.VerticesSize();
        stats.BoundingBox = ComputeBoundingBox(cloud);
        stats.BoundingBoxDiagonal = glm::length(stats.BoundingBox.Max - stats.BoundingBox.Min);

        auto positions = cloud.Positions();

        // Centroid
        glm::vec3 sum(0.0f);
        for (const auto& p : positions)
            sum += p;
        stats.Centroid = sum / static_cast<float>(stats.PointCount);

        // Spacing statistics via nearest-neighbor queries.
        if (stats.PointCount < 2)
        {
            stats.AverageSpacing = 0.0f;
            stats.MinSpacing = 0.0f;
            stats.MaxSpacing = 0.0f;
            return stats;
        }

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(positions, policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return stats;

        std::size_t sampleCount = params.SpacingSampleCount;
        if (sampleCount == 0 || sampleCount > stats.PointCount)
            sampleCount = stats.PointCount;

        const std::size_t stride = stats.PointCount / sampleCount;

        float spacingSum = 0.0f;
        float minSpacing = std::numeric_limits<float>::max();
        float maxSpacing = 0.0f;
        std::size_t validSamples = 0;

        std::vector<std::size_t> knnIndices;
        for (std::size_t si = 0; si < sampleCount; ++si)
        {
            const std::size_t idx = si * stride;
            if (idx >= stats.PointCount) break;

            knnIndices.clear();
            octree.QueryKNN(positions[idx], 2, knnIndices);

            float nearestDist = std::numeric_limits<float>::max();
            for (std::size_t ni : knnIndices)
            {
                if (ni == idx) continue;
                float d = glm::length(positions[ni] - positions[idx]);
                nearestDist = std::min(nearestDist, d);
            }

            if (nearestDist < std::numeric_limits<float>::max())
            {
                spacingSum += nearestDist;
                minSpacing = std::min(minSpacing, nearestDist);
                maxSpacing = std::max(maxSpacing, nearestDist);
                ++validSamples;
            }
        }

        if (validSamples > 0)
        {
            stats.AverageSpacing = spacingSum / static_cast<float>(validSamples);
            stats.MinSpacing = minSpacing;
            stats.MaxSpacing = maxSpacing;
        }

        return stats;
    }

    // =========================================================================
    // VoxelDownsample
    // =========================================================================

    std::optional<DownsampleResult> VoxelDownsample(
        const Cloud& cloud,
        const DownsampleParams& params)
    {
        if (cloud.IsEmpty())
            return std::nullopt;

        if (params.VoxelSize <= 0.0f)
            return std::nullopt;

        const float invVoxel = 1.0f / params.VoxelSize;

        struct CellHash
        {
            std::size_t operator()(const glm::ivec3& v) const noexcept
            {
                std::size_t h = 2166136261u;
                h ^= static_cast<std::size_t>(v.x); h *= 16777619u;
                h ^= static_cast<std::size_t>(v.y); h *= 16777619u;
                h ^= static_cast<std::size_t>(v.z); h *= 16777619u;
                return h;
            }
        };
        struct CellEqual
        {
            bool operator()(const glm::ivec3& a, const glm::ivec3& b) const noexcept
            {
                return a.x == b.x && a.y == b.y && a.z == b.z;
            }
        };

        struct CellAccum
        {
            glm::vec3 PositionSum{0.0f};
            glm::vec3 NormalSum{0.0f};
            glm::vec4 ColorSum{0.0f};
            float     RadiusSum{0.0f};
            uint32_t  Count{0};
        };

        std::unordered_map<glm::ivec3, CellAccum, CellHash, CellEqual> cells;
        cells.reserve(cloud.VerticesSize() / 4);

        const bool doNormals = cloud.HasNormals() && params.PreserveNormals;
        const bool doColors  = cloud.HasColors()  && params.PreserveColors;
        const bool doRadii   = cloud.HasRadii()   && params.PreserveRadii;

        auto positions = cloud.Positions();
        auto normals   = doNormals ? cloud.Normals() : std::span<const glm::vec3>{};
        auto colors    = doColors  ? cloud.Colors()  : std::span<const glm::vec4>{};
        auto radii     = doRadii   ? cloud.Radii()   : std::span<const float>{};

        for (std::size_t i = 0; i < cloud.VerticesSize(); ++i)
        {
            const glm::vec3& p = positions[i];
            const glm::ivec3 cell(
                static_cast<int>(std::floor(p.x * invVoxel)),
                static_cast<int>(std::floor(p.y * invVoxel)),
                static_cast<int>(std::floor(p.z * invVoxel)));

            auto& acc = cells[cell];
            acc.PositionSum += p;
            if (doNormals) acc.NormalSum += normals[i];
            if (doColors)  acc.ColorSum  += colors[i];
            if (doRadii)   acc.RadiusSum += radii[i];
            acc.Count++;
        }

        DownsampleResult result;
        result.OriginalCount = cloud.VerticesSize();
        result.ReducedCount  = cells.size();
        result.ReductionRatio = static_cast<float>(result.ReducedCount) /
                                static_cast<float>(result.OriginalCount);

        auto& out = result.Downsampled;
        out.Reserve(cells.size());
        if (doNormals) out.EnableNormals();
        if (doColors)  out.EnableColors();
        if (doRadii)   out.EnableRadii();

        for (const auto& [cell, acc] : cells)
        {
            const float invCount = 1.0f / static_cast<float>(acc.Count);
            const VertexHandle ph = out.AddPoint(acc.PositionSum * invCount);

            if (doNormals)
            {
                glm::vec3 n = acc.NormalSum * invCount;
                const float len = glm::length(n);
                out.Normal(ph) = (len > 1e-8f) ? n / len : glm::vec3(0.f, 1.f, 0.f);
            }
            if (doColors)  out.Color(ph)  = acc.ColorSum * invCount;
            if (doRadii)   out.Radius(ph) = acc.RadiusSum * invCount;
        }

        return result;
    }

    // =========================================================================
    // EstimateRadii
    // =========================================================================

    std::optional<RadiusEstimationResult> EstimateRadii(
        const Cloud& cloud,
        const RadiusEstimationParams& params)
    {
        if (cloud.VerticesSize() < 2)
            return std::nullopt;

        auto positions = cloud.Positions();

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(positions, policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return std::nullopt;

        const std::size_t k      = std::max(params.KNeighbors, std::size_t{1});
        const std::size_t kQuery = k + 1;

        RadiusEstimationResult result;
        result.Radii.resize(cloud.VerticesSize());
        float radiusSum = 0.0f;
        float minRadius = std::numeric_limits<float>::max();
        float maxRadius = 0.0f;

        std::vector<std::size_t> knnIndices;
        for (std::size_t i = 0; i < cloud.VerticesSize(); ++i)
        {
            knnIndices.clear();
            octree.QueryKNN(positions[i], kQuery, knnIndices);

            float distSum = 0.0f;
            uint32_t neighborCount = 0;
            for (std::size_t ni : knnIndices)
            {
                if (ni == i) continue;
                distSum += glm::length(positions[ni] - positions[i]);
                ++neighborCount;
            }

            const float avgDist = (neighborCount > 0)
                ? distSum / static_cast<float>(neighborCount)
                : 0.0f;

            const float r = avgDist * params.ScaleFactor;
            result.Radii[i] = r;
            radiusSum += r;
            minRadius = std::min(minRadius, r);
            maxRadius = std::max(maxRadius, r);
        }

        result.AverageRadius = radiusSum / static_cast<float>(cloud.VerticesSize());
        result.MinRadius = minRadius;
        result.MaxRadius = maxRadius;

        return result;
    }

    // =========================================================================
    // RandomSubsample
    // =========================================================================

    std::optional<SubsampleResult> RandomSubsample(
        const Cloud& cloud,
        const SubsampleParams& params)
    {
        if (cloud.IsEmpty())
            return std::nullopt;

        const std::size_t n      = cloud.VerticesSize();
        const std::size_t target = std::min(params.TargetCount, n);

        std::vector<std::size_t> indices(n);
        std::iota(indices.begin(), indices.end(), 0);

        std::mt19937 rng(params.Seed);
        for (std::size_t i = 0; i < target; ++i)
        {
            std::uniform_int_distribution<std::size_t> dist(i, n - 1);
            std::swap(indices[i], indices[dist(rng)]);
        }

        std::vector<std::size_t> selected(indices.begin(),
                                           indices.begin() + static_cast<std::ptrdiff_t>(target));
        std::sort(selected.begin(), selected.end());

        SubsampleResult result;
        result.SelectedIndices = selected;

        auto& out = result.Subsampled;
        out.Reserve(target);
        if (cloud.HasNormals()) out.EnableNormals();
        if (cloud.HasColors())  out.EnableColors();
        if (cloud.HasRadii())   out.EnableRadii();

        auto positions = cloud.Positions();
        auto normals   = cloud.HasNormals() ? cloud.Normals() : std::span<const glm::vec3>{};
        auto colors    = cloud.HasColors()  ? cloud.Colors()  : std::span<const glm::vec4>{};
        auto radii     = cloud.HasRadii()   ? cloud.Radii()   : std::span<const float>{};

        for (std::size_t idx : selected)
        {
            const VertexHandle ph = out.AddPoint(positions[idx]);
            if (cloud.HasNormals()) out.Normal(ph) = normals[idx];
            if (cloud.HasColors())  out.Color(ph)  = colors[idx];
            if (cloud.HasRadii())   out.Radius(ph) = radii[idx];
        }

        return result;
    }

    // =========================================================================
    // BilateralFilter
    // =========================================================================

    std::optional<BilateralFilterResult> BilateralFilter(
        Cloud& cloud,
        const BilateralFilterParams& params)
    {
        if (cloud.VerticesSize() < 2)
            return std::nullopt;
        if (!cloud.HasNormals())
            return std::nullopt;
        if (params.Iterations == 0)
            return BilateralFilterResult{};

        auto positions = cloud.Positions();
        auto normals   = cloud.Normals();
        const std::size_t n = cloud.VerticesSize();

        // Build KDTree for nearest-neighbor queries.
        // We use Octree (consistent with EstimateRadii / ComputeStatistics).
        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(positions, policy, 32, 10))
            return std::nullopt;

        // Auto spatial sigma: 2× average nearest-neighbor spacing.
        float spatialSigma = params.SpatialSigma;
        if (spatialSigma <= 0.0f)
        {
            float spacingSum = 0.0f;
            std::size_t validCount = 0;
            std::vector<std::size_t> knnTemp;
            const std::size_t sampleCount = std::min(n, std::size_t{500});
            const std::size_t stride = n / sampleCount;
            for (std::size_t si = 0; si < sampleCount; ++si)
            {
                const std::size_t idx = si * stride;
                if (idx >= n) break;
                knnTemp.clear();
                octree.QueryKNN(positions[idx], 2, knnTemp);
                for (std::size_t ni : knnTemp)
                {
                    if (ni == idx) continue;
                    spacingSum += glm::length(positions[ni] - positions[idx]);
                    ++validCount;
                    break;
                }
            }
            spatialSigma = (validCount > 0) ? 2.0f * (spacingSum / static_cast<float>(validCount)) : 0.01f;
        }

        const float normalSigma = std::max(params.NormalSigma, 1e-6f);
        const float invSpatial2 = -0.5f / (spatialSigma * spatialSigma);
        const float invNormal2  = -0.5f / (normalSigma * normalSigma);
        const std::size_t kQuery = params.KNeighbors + 1; // +1 for self

        BilateralFilterResult result{};
        std::vector<glm::vec3> newPositions(n);
        std::vector<std::size_t> knnIndices;

        for (uint32_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Rebuild octree each iteration (positions change).
            if (iter > 0)
            {
                octree = Octree{};
                if (!octree.BuildFromPoints(positions, policy, 32, 10))
                    break;
            }

            float dispSum = 0.0f;
            float dispMax = 0.0f;
            std::size_t degenerateCount = 0;

            for (std::size_t i = 0; i < n; ++i)
            {
                const glm::vec3& pi = positions[i];
                const glm::vec3& ni = normals[i];
                const float nLen = glm::length(ni);

                if (nLen < 1e-8f)
                {
                    newPositions[i] = pi;
                    ++degenerateCount;
                    continue;
                }

                const glm::vec3 nHat = ni / nLen;

                knnIndices.clear();
                octree.QueryKNN(pi, kQuery, knnIndices);

                float weightSum = 0.0f;
                float signedDistSum = 0.0f;

                for (std::size_t ji : knnIndices)
                {
                    if (ji == i) continue;
                    const glm::vec3 diff = positions[ji] - pi;
                    const float dist = glm::length(diff);

                    // Spatial weight
                    const float ws = std::exp(dist * dist * invSpatial2);

                    // Normal similarity weight
                    float normalDot = glm::dot(nHat, normals[ji]);
                    float nLen2 = glm::length(normals[ji]);
                    if (nLen2 > 1e-8f) normalDot /= nLen2;
                    const float normalDiff = 1.0f - std::abs(normalDot);
                    const float wn = std::exp(normalDiff * normalDiff * invNormal2);

                    const float w = ws * wn;
                    weightSum += w;
                    signedDistSum += w * glm::dot(diff, nHat);
                }

                float displacement = 0.0f;
                if (weightSum > 1e-12f)
                    displacement = signedDistSum / weightSum;

                newPositions[i] = pi + nHat * displacement;
                const float absDist = std::abs(displacement);
                dispSum += absDist;
                dispMax = std::max(dispMax, absDist);
            }

            // Apply filtered positions.
            for (std::size_t i = 0; i < n; ++i)
                positions[i] = newPositions[i];

            result.PointsFiltered = n - degenerateCount;
            result.DegenerateNormals = degenerateCount;
            result.AverageDisplacement = (n > 0) ? dispSum / static_cast<float>(n) : 0.0f;
            result.MaxDisplacement = dispMax;
        }

        return result;
    }

    // =========================================================================
    // EstimateOutlierProbability
    // =========================================================================

    std::optional<OutlierEstimationResult> EstimateOutlierProbability(
        Cloud& cloud,
        const OutlierEstimationParams& params)
    {
        const std::size_t n = cloud.VerticesSize();
        if (n < 2)
            return std::nullopt;

        auto positions = cloud.Positions();

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(positions, policy, 32, 10))
            return std::nullopt;

        const std::size_t k = std::max(params.KNeighbors, std::size_t{2});
        const std::size_t kQuery = k + 1; // +1 for self

        // Phase 1: Compute mean kNN distance for each point and cache neighbor lists.
        std::vector<float> meanKnnDist(n, 0.0f);
        std::vector<std::vector<std::size_t>> neighborCache(n);
        std::vector<std::size_t> knnIndices;

        for (std::size_t i = 0; i < n; ++i)
        {
            knnIndices.clear();
            octree.QueryKNN(positions[i], kQuery, knnIndices);

            float distSum = 0.0f;
            uint32_t neighborCount = 0;
            for (std::size_t ni : knnIndices)
            {
                if (ni == i) continue;
                distSum += glm::length(positions[ni] - positions[i]);
                ++neighborCount;
            }
            meanKnnDist[i] = (neighborCount > 0) ? distSum / static_cast<float>(neighborCount) : 0.0f;
            neighborCache[i] = std::move(knnIndices);
        }

        // Phase 2: Compute Local Outlier Factor (simplified LOF) using cached neighbors.
        // Score_i = meanKnnDist(i) / avg_j_in_kNN(meanKnnDist(j))
        OutlierEstimationResult result{};
        result.Scores.resize(n, 0.0f);
        float scoreSum = 0.0f;
        float maxScore = 0.0f;
        std::size_t outlierCount = 0;

        for (std::size_t i = 0; i < n; ++i)
        {
            float neighborDistSum = 0.0f;
            uint32_t neighborCount = 0;
            for (std::size_t ni : neighborCache[i])
            {
                if (ni == i) continue;
                neighborDistSum += meanKnnDist[ni];
                ++neighborCount;
            }

            float neighborMean = (neighborCount > 0) ? neighborDistSum / static_cast<float>(neighborCount) : 1.0f;
            float score = (neighborMean > 1e-12f) ? meanKnnDist[i] / neighborMean : 0.0f;

            result.Scores[i] = score;
            scoreSum += score;
            maxScore = std::max(maxScore, score);
            if (score > params.ScoreThreshold)
                ++outlierCount;
        }

        result.OutlierCount = outlierCount;
        result.MeanScore = (n > 0) ? scoreSum / static_cast<float>(n) : 0.0f;
        result.MaxScore = maxScore;

        // Publish as property.
        auto prop = cloud.GetOrAddVertexProperty<float>("p:outlier_score", 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            prop[Cloud::Handle(i)] = result.Scores[i];

        return result;
    }

    // =========================================================================
    // EstimateKernelDensity
    // =========================================================================

    std::optional<KDEResult> EstimateKernelDensity(
        Cloud& cloud,
        const KDEParams& params)
    {
        const std::size_t n = cloud.VerticesSize();
        if (n < 2)
            return std::nullopt;

        auto positions = cloud.Positions();

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(positions, policy, 32, 10))
            return std::nullopt;

        const std::size_t k = std::max(params.KNeighbors, std::size_t{2});
        const std::size_t kQuery = k + 1; // +1 for self

        // Phase 1: Compute nearest-neighbor distances for bandwidth estimation.
        std::vector<float> nnDists(n, 0.0f);
        std::vector<std::size_t> knnIndices;

        for (std::size_t i = 0; i < n; ++i)
        {
            knnIndices.clear();
            octree.QueryKNN(positions[i], 2, knnIndices);

            float nearestDist = 0.0f;
            for (std::size_t ni : knnIndices)
            {
                if (ni == i) continue;
                nearestDist = glm::length(positions[ni] - positions[i]);
                break;
            }
            nnDists[i] = nearestDist;
        }

        // Bandwidth: user-specified or Silverman's rule.
        float bandwidth = params.Bandwidth;
        if (bandwidth <= 0.0f)
        {
            // Silverman's rule: h = 1.06 * σ * n^(-1/5)
            // Use standard deviation of NN distances, floored by mean NN distance
            // to avoid degenerate bandwidth on uniform point clouds.
            float mean = 0.0f;
            for (float d : nnDists) mean += d;
            mean /= static_cast<float>(n);

            float variance = 0.0f;
            for (float d : nnDists)
            {
                float diff = d - mean;
                variance += diff * diff;
            }
            variance /= static_cast<float>(n);
            float sigma = std::sqrt(variance);

            // For uniform point clouds, σ ≈ 0 but mean spacing is meaningful.
            // Use max(σ, mean) so the bandwidth scales with point density.
            sigma = std::max(sigma, mean);

            bandwidth = 1.06f * sigma * std::pow(static_cast<float>(n), -0.2f);
            bandwidth = std::max(bandwidth, 1e-8f);
        }

        const float invH2 = -0.5f / (bandwidth * bandwidth);
        // 3D isotropic Gaussian normalization: 1 / ((2π)^(3/2) * h³)
        const float pi = static_cast<float>(std::numbers::pi);
        const float normFactor = 1.0f / (std::pow(2.0f * pi, 1.5f) * bandwidth * bandwidth * bandwidth);

        // Phase 2: Compute density at each point via KNN Gaussian KDE.
        KDEResult result{};
        result.Densities.resize(n, 0.0f);
        result.UsedBandwidth = bandwidth;
        float densitySum = 0.0f;
        float minDensity = std::numeric_limits<float>::max();
        float maxDensity = 0.0f;

        for (std::size_t i = 0; i < n; ++i)
        {
            knnIndices.clear();
            octree.QueryKNN(positions[i], kQuery, knnIndices);

            float kde = 0.0f;
            uint32_t neighborCount = 0;
            for (std::size_t ni : knnIndices)
            {
                if (ni == i) continue;
                float dist = glm::length(positions[ni] - positions[i]);
                kde += normFactor * std::exp(dist * dist * invH2);
                ++neighborCount;
            }

            if (neighborCount > 0)
                kde /= static_cast<float>(neighborCount);

            result.Densities[i] = kde;
            densitySum += kde;
            minDensity = std::min(minDensity, kde);
            maxDensity = std::max(maxDensity, kde);
        }

        result.MeanDensity = (n > 0) ? densitySum / static_cast<float>(n) : 0.0f;
        result.MinDensity = minDensity;
        result.MaxDensity = maxDensity;

        // Publish as property.
        auto prop = cloud.GetOrAddVertexProperty<float>("p:density", 0.0f);
        for (std::size_t i = 0; i < n; ++i)
            prop[Cloud::Handle(i)] = result.Densities[i];

        return result;
    }

} // namespace Geometry::PointCloud
