module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

module Geometry:PointCloud.Impl;

import :PointCloud;
import :AABB;
import :Octree;

namespace Geometry::PointCloud
{
    // =========================================================================
    // ComputeBoundingBox
    // =========================================================================

    AABB ComputeBoundingBox(const Cloud& cloud)
    {
        if (cloud.Empty())
            return AABB{glm::vec3(0.0f), glm::vec3(0.0f)};

        glm::vec3 mn = cloud.Positions[0];
        glm::vec3 mx = cloud.Positions[0];

        for (std::size_t i = 1; i < cloud.Positions.size(); ++i)
        {
            mn = glm::min(mn, cloud.Positions[i]);
            mx = glm::max(mx, cloud.Positions[i]);
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
        if (cloud.Empty())
            return std::nullopt;

        CloudStatistics stats{};
        stats.PointCount = cloud.Size();
        stats.BoundingBox = ComputeBoundingBox(cloud);
        stats.BoundingBoxDiagonal = glm::length(stats.BoundingBox.Max - stats.BoundingBox.Min);

        // Centroid
        glm::vec3 sum(0.0f);
        for (const auto& p : cloud.Positions)
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

        // Build octree for KNN queries.
        std::vector<AABB> pointAABBs;
        pointAABBs.reserve(cloud.Positions.size());
        for (const auto& p : cloud.Positions)
            pointAABBs.emplace_back(AABB{p, p});

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.Build(std::move(pointAABBs), policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return stats; // Octree build failed — return stats without spacing.

        // Sample points for spacing computation.
        std::size_t sampleCount = params.SpacingSampleCount;
        if (sampleCount == 0 || sampleCount > stats.PointCount)
            sampleCount = stats.PointCount;

        // Stride-based sampling for deterministic coverage.
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
            octree.QueryKnn(cloud.Positions[idx], 2, knnIndices); // 2 = self + nearest

            // Filter out self.
            float nearestDist = std::numeric_limits<float>::max();
            for (std::size_t ni : knnIndices)
            {
                if (ni == idx) continue;
                float d = glm::length(cloud.Positions[ni] - cloud.Positions[idx]);
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
        if (cloud.Empty())
            return std::nullopt;

        if (params.VoxelSize <= 0.0f)
            return std::nullopt;

        const float invVoxel = 1.0f / params.VoxelSize;

        // Hash function for voxel cell indices.
        struct CellHash
        {
            std::size_t operator()(const glm::ivec3& v) const noexcept
            {
                // FNV-1a inspired mixing.
                std::size_t h = 2166136261u;
                h ^= static_cast<std::size_t>(v.x);
                h *= 16777619u;
                h ^= static_cast<std::size_t>(v.y);
                h *= 16777619u;
                h ^= static_cast<std::size_t>(v.z);
                h *= 16777619u;
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
        cells.reserve(cloud.Size() / 4); // Heuristic reservation.

        const bool hasNormals = cloud.HasNormals() && params.PreserveNormals;
        const bool hasColors  = cloud.HasColors() && params.PreserveColors;
        const bool hasRadii   = cloud.HasRadii() && params.PreserveRadii;

        for (std::size_t i = 0; i < cloud.Size(); ++i)
        {
            const glm::vec3& p = cloud.Positions[i];
            glm::ivec3 cell(
                static_cast<int>(std::floor(p.x * invVoxel)),
                static_cast<int>(std::floor(p.y * invVoxel)),
                static_cast<int>(std::floor(p.z * invVoxel)));

            auto& acc = cells[cell];
            acc.PositionSum += p;
            if (hasNormals) acc.NormalSum += cloud.Normals[i];
            if (hasColors)  acc.ColorSum += cloud.Colors[i];
            if (hasRadii)   acc.RadiusSum += cloud.Radii[i];
            acc.Count++;
        }

        DownsampleResult result;
        result.OriginalCount = cloud.Size();
        result.ReducedCount = cells.size();
        result.ReductionRatio = static_cast<float>(result.ReducedCount) /
                                static_cast<float>(result.OriginalCount);

        auto& out = result.Downsampled;
        out.Positions.reserve(cells.size());
        if (hasNormals) out.Normals.reserve(cells.size());
        if (hasColors)  out.Colors.reserve(cells.size());
        if (hasRadii)   out.Radii.reserve(cells.size());

        for (const auto& [cell, acc] : cells)
        {
            const float invCount = 1.0f / static_cast<float>(acc.Count);
            out.Positions.push_back(acc.PositionSum * invCount);

            if (hasNormals)
            {
                glm::vec3 n = acc.NormalSum * invCount;
                float len = glm::length(n);
                out.Normals.push_back(len > 1e-8f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f));
            }
            if (hasColors)
                out.Colors.push_back(acc.ColorSum * invCount);
            if (hasRadii)
                out.Radii.push_back(acc.RadiusSum * invCount);
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
        if (cloud.Size() < 2)
            return std::nullopt;

        // Build octree.
        std::vector<AABB> pointAABBs;
        pointAABBs.reserve(cloud.Positions.size());
        for (const auto& p : cloud.Positions)
            pointAABBs.emplace_back(AABB{p, p});

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.Build(std::move(pointAABBs), policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return std::nullopt;

        const std::size_t k = std::max(params.KNeighbors, std::size_t{1});
        const std::size_t kQuery = k + 1; // Include self in query.

        RadiusEstimationResult result;
        result.Radii.resize(cloud.Size());
        float radiusSum = 0.0f;
        float minRadius = std::numeric_limits<float>::max();
        float maxRadius = 0.0f;

        std::vector<std::size_t> knnIndices;
        for (std::size_t i = 0; i < cloud.Size(); ++i)
        {
            knnIndices.clear();
            octree.QueryKnn(cloud.Positions[i], kQuery, knnIndices);

            float distSum = 0.0f;
            uint32_t neighborCount = 0;
            for (std::size_t ni : knnIndices)
            {
                if (ni == i) continue;
                distSum += glm::length(cloud.Positions[ni] - cloud.Positions[i]);
                ++neighborCount;
            }

            float avgDist = (neighborCount > 0)
                ? distSum / static_cast<float>(neighborCount)
                : 0.0f;

            float r = avgDist * params.ScaleFactor;
            result.Radii[i] = r;
            radiusSum += r;
            minRadius = std::min(minRadius, r);
            maxRadius = std::max(maxRadius, r);
        }

        result.AverageRadius = radiusSum / static_cast<float>(cloud.Size());
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
        if (cloud.Empty())
            return std::nullopt;

        const std::size_t n = cloud.Size();
        const std::size_t target = std::min(params.TargetCount, n);

        // Generate shuffled indices.
        std::vector<std::size_t> indices(n);
        std::iota(indices.begin(), indices.end(), 0);

        std::mt19937 rng(params.Seed);
        // Fisher-Yates partial shuffle (only need first `target` elements).
        for (std::size_t i = 0; i < target; ++i)
        {
            std::uniform_int_distribution<std::size_t> dist(i, n - 1);
            std::swap(indices[i], indices[dist(rng)]);
        }

        // Sort selected indices for cache-friendly access.
        std::vector<std::size_t> selected(indices.begin(), indices.begin() + static_cast<std::ptrdiff_t>(target));
        std::sort(selected.begin(), selected.end());

        SubsampleResult result;
        result.SelectedIndices = selected;

        auto& out = result.Subsampled;
        out.Positions.reserve(target);
        if (cloud.HasNormals()) out.Normals.reserve(target);
        if (cloud.HasColors())  out.Colors.reserve(target);
        if (cloud.HasRadii())   out.Radii.reserve(target);

        for (std::size_t idx : selected)
        {
            out.Positions.push_back(cloud.Positions[idx]);
            if (cloud.HasNormals()) out.Normals.push_back(cloud.Normals[idx]);
            if (cloud.HasColors())  out.Colors.push_back(cloud.Colors[idx]);
            if (cloud.HasRadii())   out.Radii.push_back(cloud.Radii[idx]);
        }

        return result;
    }

} // namespace Geometry::PointCloud
