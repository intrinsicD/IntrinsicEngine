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
#include <span>

#include <glm/glm.hpp>

module Geometry.PointCloud;

import Geometry.AABB;
import Geometry.Octree;

namespace Geometry::PointCloud
{
    // =========================================================================
    // Cloud — construction
    // =========================================================================

    Cloud::Cloud()
    {
        // Allocate the mandatory position property up-front.
        m_PPoint = VertexProperty<glm::vec3>(m_Points.Add<glm::vec3>("p:position", glm::vec3(0.f)));
    }

    void Cloud::Clear()
    {
        // Wipe all point data but keep the property slots alive.
        m_Points.Clear();
        // Re-initialise so the registry size resets to 0.
        *this = Cloud();
    }

    VertexHandle Cloud::AddPoint(glm::vec3 position)
    {
        m_Points.PushBack();
        const VertexHandle p = Handle(m_Points.Size() - 1u);
        m_PPoint[p] = position;
        return p;
    }

    void Cloud::EnableNormals(glm::vec3 defaultNormal)
    {
        if (!m_PNormal.IsValid())
        {
            m_PNormal = VertexProperty<glm::vec3>(
                m_Points.GetOrAdd<glm::vec3>("p:normal", defaultNormal));
        }
    }

    void Cloud::EnableColors(glm::vec4 defaultColor)
    {
        if (!m_PColor.IsValid())
        {
            m_PColor = VertexProperty<glm::vec4>(
                m_Points.GetOrAdd<glm::vec4>("p:color", defaultColor));
        }
    }

    void Cloud::EnableRadii(float defaultRadius)
    {
        if (!m_PRadius.IsValid())
        {
            m_PRadius = VertexProperty<float>(
                m_Points.GetOrAdd<float>("p:radius", defaultRadius));
        }
    }

    bool Cloud::IsValid() const noexcept
    {
        // Empty cloud is valid.
        if (IsEmpty()) return true;
        // Optional properties must exactly cover all points when present.
        if (HasNormals() && m_PNormal.Span().size() != PointCount()) return false;
        if (HasColors()  && m_PColor.Span().size()  != PointCount()) return false;
        if (HasRadii()   && m_PRadius.Span().size() != PointCount()) return false;
        return true;
    }

    // =========================================================================
    // ComputeBoundingBox
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
        stats.PointCount = cloud.PointCount();
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
            octree.QueryKnn(positions[idx], 2, knnIndices);

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
        cells.reserve(cloud.PointCount() / 4);

        const bool doNormals = cloud.HasNormals() && params.PreserveNormals;
        const bool doColors  = cloud.HasColors()  && params.PreserveColors;
        const bool doRadii   = cloud.HasRadii()   && params.PreserveRadii;

        auto positions = cloud.Positions();
        auto normals   = doNormals ? cloud.Normals() : std::span<const glm::vec3>{};
        auto colors    = doColors  ? cloud.Colors()  : std::span<const glm::vec4>{};
        auto radii     = doRadii   ? cloud.Radii()   : std::span<const float>{};

        for (std::size_t i = 0; i < cloud.PointCount(); ++i)
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
        result.OriginalCount = cloud.PointCount();
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
        if (cloud.PointCount() < 2)
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
        result.Radii.resize(cloud.PointCount());
        float radiusSum = 0.0f;
        float minRadius = std::numeric_limits<float>::max();
        float maxRadius = 0.0f;

        std::vector<std::size_t> knnIndices;
        for (std::size_t i = 0; i < cloud.PointCount(); ++i)
        {
            knnIndices.clear();
            octree.QueryKnn(positions[i], kQuery, knnIndices);

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

        result.AverageRadius = radiusSum / static_cast<float>(cloud.PointCount());
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

        const std::size_t n      = cloud.PointCount();
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

} // namespace Geometry::PointCloud
