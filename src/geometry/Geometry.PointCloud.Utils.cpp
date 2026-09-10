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
#include <utility>

#include <glm/glm.hpp>

module Geometry.PointCloud.Utils;

import Geometry.AABB;
import Geometry.Octree;
import Geometry.Sampling;
import Geometry.Sphere;

namespace Geometry::PointCloud
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const glm::vec3& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        // Builds an owned cloud from the given (already ascending) original
        // indices, copying optional normals/colors/radii when present. Shared by
        // the outlier-removal operators so kept-point construction stays
        // deterministic and consistent with RandomSubsample / VoxelDownsample.
        [[nodiscard]] Cloud BuildSubsetCloud(
            const Cloud& cloud, const std::vector<std::size_t>& keptIndices)
        {
            Cloud out;
            out.Reserve(keptIndices.size());
            if (cloud.HasNormals()) out.EnableNormals();
            if (cloud.HasColors())  out.EnableColors();
            if (cloud.HasRadii())   out.EnableRadii();

            auto positions = cloud.Positions();
            auto normals   = cloud.HasNormals() ? cloud.Normals() : std::span<const glm::vec3>{};
            auto colors    = cloud.HasColors()  ? cloud.Colors()  : std::span<const glm::vec4>{};
            auto radii     = cloud.HasRadii()   ? cloud.Radii()   : std::span<const float>{};

            for (std::size_t idx : keptIndices)
            {
                const VertexHandle ph = out.AddPoint(positions[idx]);
                if (cloud.HasNormals()) out.Normal(ph) = normals[idx];
                if (cloud.HasColors())  out.Color(ph)  = colors[idx];
                if (cloud.HasRadii())   out.Radius(ph) = radii[idx];
            }
            return out;
        }

        // Partitions [0, n) by a keep predicate into ascending kept/rejected
        // lists and materializes the filtered cloud + counts onto the result.
        template <typename KeepFn>
        void FinalizeRemoval(
            const Cloud& cloud, std::size_t n, OutlierRemovalResult& result, KeepFn keep)
        {
            result.OriginalCount = n;
            result.KeptIndices.reserve(n);
            for (std::size_t i = 0; i < n; ++i)
            {
                if (keep(i))
                    result.KeptIndices.push_back(i);
                else
                    result.RejectedIndices.push_back(i);
            }
            result.KeptCount     = result.KeptIndices.size();
            result.RejectedCount = result.RejectedIndices.size();
            result.Filtered      = BuildSubsetCloud(cloud, result.KeptIndices);
            result.Status        = OutlierRemovalStatus::Success;
        }
    }

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

    namespace
    {
        std::optional<CloudStatistics> BaseStatistics(std::span<const glm::vec3> points)
        {
            if (points.empty()) return std::nullopt;
            CloudStatistics stats;
            stats.PointCount = points.size();
            glm::vec3 low = points[0], high = low, sum{0};
            for (const auto p : points)
            {
                if (!IsFinite(p)) return std::nullopt;
                low = glm::min(low, p); high = glm::max(high, p); sum += p;
            }
            stats.BoundingBox = AABB{low, high};
            stats.BoundingBoxDiagonal = glm::length(high-low);
            stats.Centroid = sum / float(points.size());
            if (!IsFinite(stats.Centroid) || !std::isfinite(stats.BoundingBoxDiagonal)) return std::nullopt;
            return stats;
        }
        std::size_t SpacingSamples(std::size_t count, const StatisticsParams& params)
        {
            return params.SpacingSampleCount ? std::min(count, params.SpacingSampleCount) : count;
        }
        // Monotonic distance/ID ordering also rejects duplicate IDs without a set per row.
        template<class Id>
        bool NeighborDistances(std::span<const glm::vec3> points, std::size_t source,
            std::span<const Id> row, float& sum, float& nearest, std::size_t& count, bool requireOther = true)
        {
            float previous = -1;
            std::size_t previousId = 0;
            sum = 0; nearest = std::numeric_limits<float>::max(); count = 0;
            for (const auto id : row)
            {
                if (id >= points.size()) return false;
                const auto delta = points[id]-points[source];
                const float squared = glm::dot(delta, delta);
                if (!std::isfinite(squared) || squared < previous ||
                    (squared == previous && id <= previousId)) return false;
                previous = squared; previousId = id;
                if (id == source) continue;
                const float distance = std::sqrt(squared);
                sum += distance; nearest = std::min(nearest, distance); ++count;
            }
            return (count || !requireOther) && std::isfinite(sum);
        }
        void AddSpacing(CloudStatistics& stats, float distance, std::size_t sample)
        {
            stats.AverageSpacing += distance;
            stats.MinSpacing = sample ? std::min(stats.MinSpacing, distance) : distance;
            stats.MaxSpacing = std::max(stats.MaxSpacing, distance);
        }
        template<class Query>
        std::optional<CloudStatistics> StatisticsWithQueries(std::span<const glm::vec3> points,
            const StatisticsParams& params, Query query)
        {
            auto stats = BaseStatistics(points);
            if (!stats || points.size() < 2) return stats;
            const auto samples = SpacingSamples(points.size(), params);
            const auto stride = points.size()/samples;
            for (std::size_t i=0; i<samples; ++i)
            {
                float sum, nearest; std::size_t count;
                const auto row = query(i, i*stride);
                if (row.size()!=2 || !NeighborDistances(points, i*stride, row, sum, nearest, count)) return std::nullopt;
                AddSpacing(*stats, nearest, i);
            }
            stats->AverageSpacing /= float(samples);
            if (!std::isfinite(stats->AverageSpacing)) return std::nullopt;
            return stats;
        }
    }
    std::optional<CloudStatistics> ComputeStatistics(const Cloud& cloud, const StatisticsParams& params)
    {
        return ComputeStatistics(cloud.Positions(), params);
    }
    std::optional<CloudStatistics> ComputeStatistics(std::span<const glm::vec3> positions,
        const StatisticsParams& params)
    {
        if (!BaseStatistics(positions)) return std::nullopt;
        Octree tree;
        Octree::SplitPolicy policy{}; policy.SplitPoint=Octree::SplitPoint::Center; policy.TightChildren=true;
        if (positions.size()>1 && !tree.BuildFromPoints(positions, policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return std::nullopt;
        std::vector<std::size_t> ids;
        return StatisticsWithQueries(positions, params, [&](std::size_t, std::size_t source) {
            ids.clear(); tree.QueryKNN(positions[source], 2, ids); return std::span<const std::size_t>(ids);
        });
    }
    std::optional<CloudStatistics> ComputeStatisticsFromNeighbors(std::span<const glm::vec3> positions,
        std::span<const std::uint32_t> candidates, const StatisticsParams& params)
    {
        const auto samples = positions.size()<2 ? 0 : SpacingSamples(positions.size(), params);
        if (samples > std::numeric_limits<std::size_t>::max()/2 || candidates.size()!=samples*2) return std::nullopt;
        return StatisticsWithQueries(positions, params, [&](std::size_t sample, std::size_t) {
            return candidates.subspan(sample*2, 2);
        });
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

        if (!std::isfinite(params.VoxelSize) || params.VoxelSize <= 0.0f)
            return std::nullopt;

        const float invVoxel = 1.0f / params.VoxelSize;

        // Keeps the original float floor expression bit-for-bit so cell
        // assignment for valid input is unchanged, then rejects anything the
        // int conversion could not represent. The range check widens to double
        // because (float)INT_MAX rounds up to 2^31 and would admit a value one
        // past the representable maximum.
        const auto tryCellCoordinate = [invVoxel](float value, int& out) noexcept
        {
            if (!std::isfinite(value))
                return false;
            const float scaled = std::floor(value * invVoxel);
            if (!std::isfinite(scaled))
                return false;
            const double widened = static_cast<double>(scaled);
            if (widened < static_cast<double>(std::numeric_limits<int>::min())
                || widened > static_cast<double>(std::numeric_limits<int>::max()))
            {
                return false;
            }
            out = static_cast<int>(scaled);
            return true;
        };

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
            glm::ivec3 cell{0};
            if (!tryCellCoordinate(p.x, cell.x)
                || !tryCellCoordinate(p.y, cell.y)
                || !tryCellCoordinate(p.z, cell.z))
            {
                return std::nullopt;
            }

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

        // Emission order is ascending lexicographic (x, then y, then z) over the
        // occupied cell keys. Accumulation above already ran in input order, so
        // only the order points are appended in depends on this sort, not the
        // sums themselves.
        std::vector<glm::ivec3> orderedCells;
        orderedCells.reserve(cells.size());
        for (const auto& [cell, acc] : cells)
        {
            orderedCells.push_back(cell);
        }
        std::sort(orderedCells.begin(), orderedCells.end(),
                  [](const glm::ivec3& a, const glm::ivec3& b) noexcept
                  {
                      if (a.x != b.x) return a.x < b.x;
                      if (a.y != b.y) return a.y < b.y;
                      return a.z < b.z;
                  });

        for (const glm::ivec3& cell : orderedCells)
        {
            const CellAccum& acc = cells.at(cell);
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

    namespace
    {
        std::size_t RadiusWidth(std::size_t n, std::size_t k)
        {
            return n<2 ? n : std::min(n-1, std::max(k, std::size_t{1}))+1;
        }
        template<class Query>
        std::optional<RadiusEstimationResult> RadiiWithQueries(std::span<const glm::vec3> points,
            const RadiusEstimationParams& params, Query query)
        {
            if (points.size()<2 || !std::isfinite(params.ScaleFactor) || params.ScaleFactor<0) return std::nullopt;
            auto stats = BaseStatistics(points);
            if (!stats) return std::nullopt;
            RadiusEstimationResult result;
            result.Radii.reserve(points.size());
            const auto width = RadiusWidth(points.size(), params.KNeighbors);
            float radiusSum = 0;
            for (std::size_t i=0; i<points.size(); ++i)
            {
                float sum, nearest; std::size_t count;
                const auto row = query(i);
                if (row.size()!=width || !NeighborDistances(points, i, row, sum, nearest, count)) return std::nullopt;
                const float radius = (sum/float(count))*params.ScaleFactor;
                if (!std::isfinite(radius)) return std::nullopt;
                result.Radii.push_back(radius); radiusSum += radius;
                result.MinRadius = i ? std::min(result.MinRadius, radius) : radius;
                result.MaxRadius = std::max(result.MaxRadius, radius);
                AddSpacing(*stats, nearest, i);
            }
            result.AverageRadius = radiusSum/float(points.size());
            stats->AverageSpacing /= float(points.size());
            result.Statistics = *stats;
            if (!std::isfinite(result.AverageRadius) || !std::isfinite(stats->AverageSpacing)) return std::nullopt;
            return result;
        }
    }
    std::optional<RadiusEstimationResult> EstimateRadii(const Cloud& cloud, const RadiusEstimationParams& params)
    {
        return EstimateRadii(cloud.Positions(), params);
    }
    std::optional<RadiusEstimationResult> EstimateRadii(std::span<const glm::vec3> positions,
        const RadiusEstimationParams& params)
    {
        if (positions.size()<2 || !BaseStatistics(positions) || !std::isfinite(params.ScaleFactor) || params.ScaleFactor<0)
            return std::nullopt;
        Octree tree;
        Octree::SplitPolicy policy{}; policy.SplitPoint=Octree::SplitPoint::Center; policy.TightChildren=true;
        if (!tree.BuildFromPoints(positions, policy, params.OctreeMaxPerNode, params.OctreeMaxDepth)) return std::nullopt;
        std::vector<std::size_t> ids;
        return RadiiWithQueries(positions, params, [&](std::size_t source) {
            ids.clear(); tree.QueryKNN(positions[source], RadiusWidth(positions.size(), params.KNeighbors), ids);
            return std::span<const std::size_t>(ids);
        });
    }
    std::optional<RadiusEstimationResult> EstimateRadiiFromNeighbors(std::span<const glm::vec3> positions,
        std::span<const std::uint32_t> candidates, const RadiusEstimationParams& params)
    {
        const auto width = RadiusWidth(positions.size(), params.KNeighbors);
        if (!width || positions.size()>std::numeric_limits<std::size_t>::max()/width ||
            candidates.size()!=positions.size()*width) return std::nullopt;
        return RadiiWithQueries(positions, params, [&](std::size_t source) {
            return candidates.subspan(source*width, width);
        });
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

    namespace
    {
        std::size_t BilateralWidth(std::size_t n, std::size_t k)
        {
            return n < 2 ? n : std::min(n - 1, k) + 1;
        }
        bool ValidBilateralInput(std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
            const BilateralFilterParams& params)
        {
            return positions.size() >= 2 && positions.size() == normals.size() &&
                std::isfinite(params.SpatialSigma) && std::isfinite(params.NormalSigma) &&
                std::ranges::all_of(positions, IsFinite) && std::ranges::all_of(normals, IsFinite);
        }
        template<class Query>
        std::optional<BilateralFilterOutput> BilateralStepWithQueries(
            std::span<const glm::vec3> points, std::span<const glm::vec3> normals,
            const BilateralFilterParams& params, Query query)
        {
            if (!ValidBilateralInput(points, normals, params) || params.SpatialSigma <= 0 || params.Iterations != 1)
                return std::nullopt;
            const float spatialSquared = params.SpatialSigma * params.SpatialSigma;
            const float normalSigma = std::max(params.NormalSigma, 1e-6f);
            const float normalSquared = normalSigma * normalSigma;
            const float invSpatial = -0.5f / spatialSquared, invNormal = -0.5f / normalSquared;
            if (!(spatialSquared > 0) || !std::isfinite(spatialSquared) ||
                !std::isfinite(normalSquared) || !std::isfinite(invSpatial) || !std::isfinite(invNormal)) return std::nullopt;
            BilateralFilterOutput output;
            output.Positions.assign(points.begin(), points.end());
            output.SpatialSigmaUsed = params.SpatialSigma;
            float displacementSum = 0;
            const auto width = BilateralWidth(points.size(), params.KNeighbors);
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                const auto row = query(i);
                float distanceSum, nearest; std::size_t count;
                if (row.size() != width || !NeighborDistances(points, i, row, distanceSum, nearest, count, false))
                    return std::nullopt;
                const float length = glm::length(normals[i]);
                if (!std::isfinite(length)) return std::nullopt;
                if (length < 1e-8f) { ++output.Diagnostics.DegenerateNormals; continue; }
                const glm::vec3 normal = normals[i] / length;
                float weights = 0, signedDistances = 0;
                for (const auto id : row)
                {
                    if (id == i) continue;
                    const glm::vec3 difference = points[id] - points[i];
                    const float distance = glm::length(difference);
                    const float neighborLength = glm::length(normals[id]);
                    if (!std::isfinite(neighborLength)) return std::nullopt;
                    float normalDot = glm::dot(normal, normals[id]);
                    if (neighborLength > 1e-8f) normalDot /= neighborLength;
                    const float normalDifference = 1.f - std::abs(normalDot);
                    const float weight = std::exp(distance * distance * invSpatial) *
                                         std::exp(normalDifference * normalDifference * invNormal);
                    weights += weight; signedDistances += weight * glm::dot(difference, normal);
                }
                if (!std::isfinite(weights) || !std::isfinite(signedDistances)) return std::nullopt;
                const float displacement = weights > 1e-12f ? signedDistances / weights : 0.f;
                output.Positions[i] = points[i] + normal * displacement;
                if (!IsFinite(output.Positions[i])) return std::nullopt;
                displacementSum += std::abs(displacement);
                output.Diagnostics.MaxDisplacement = std::max(output.Diagnostics.MaxDisplacement, std::abs(displacement));
            }
            output.Diagnostics.PointsFiltered = points.size() - output.Diagnostics.DegenerateNormals;
            output.Diagnostics.AverageDisplacement = displacementSum / float(points.size());
            if (!std::isfinite(output.Diagnostics.AverageDisplacement)) return std::nullopt;
            return output;
        }
    }
    std::optional<BilateralFilterOutput> BilateralFilter(std::span<const glm::vec3> positions,
        std::span<const glm::vec3> normals, const BilateralFilterParams& params)
    {
        if (!ValidBilateralInput(positions, normals, params)) return std::nullopt;
        BilateralFilterOutput output;
        output.Positions.assign(positions.begin(), positions.end());
        if (!params.Iterations) return output;
        auto step = params; step.Iterations = 1;
        if (step.SpatialSigma <= 0)
        {
            const auto stats = ComputeStatistics(positions, {.SpacingSampleCount = std::min(positions.size(), std::size_t{500})});
            if (!stats) return std::nullopt;
            step.SpatialSigma = stats->AverageSpacing > 0 ? 2.f * stats->AverageSpacing : 0.01f;
        }
        for (std::uint32_t iteration = 0; iteration < params.Iterations; ++iteration)
        {
            Octree tree;
            Octree::SplitPolicy policy{}; policy.SplitPoint = Octree::SplitPoint::Center; policy.TightChildren = true;
            if (!tree.BuildFromPoints(output.Positions, policy, 32, 10)) return std::nullopt;
            std::vector<std::size_t> ids;
            auto filtered = BilateralStepWithQueries(output.Positions, normals, step, [&](std::size_t i) {
                ids.clear(); tree.QueryKNN(output.Positions[i], BilateralWidth(positions.size(), params.KNeighbors), ids);
                return std::span<const std::size_t>(ids);
            });
            if (!filtered) return std::nullopt;
            output = std::move(*filtered);
        }
        return output;
    }
    std::optional<BilateralFilterOutput> BilateralFilterStepFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const glm::vec3> normals,
        std::span<const std::uint32_t> candidates, const BilateralFilterParams& params)
    {
        const auto width = BilateralWidth(positions.size(), params.KNeighbors);
        if (positions.size() < 2 || positions.size() > std::numeric_limits<std::size_t>::max() / width ||
            candidates.size() != positions.size() * width) return std::nullopt;
        return BilateralStepWithQueries(positions, normals, params,
            [&](std::size_t i) { return candidates.subspan(i * width, width); });
    }
    std::optional<BilateralFilterResult> BilateralFilter(Cloud& cloud, const BilateralFilterParams& params)
    {
        if (!cloud.HasNormals()) return std::nullopt;
        const auto filtered = BilateralFilter(cloud.Positions(), cloud.Normals(), params);
        if (!filtered) return std::nullopt;
        if (params.Iterations) std::ranges::copy(filtered->Positions, cloud.Positions().begin());
        return filtered->Diagnostics;
    }

    // =========================================================================
    // EstimateOutlierProbability
    // =========================================================================

    namespace
    {
        std::size_t OutlierCandidateWidth(std::size_t n, std::size_t k)
        {
            return n < 2 ? n : std::min(n - 1, std::max(k, std::size_t{2})) + 1;
        }
        template<class Query>
        std::optional<OutlierEstimationResult> DistanceRatioWithQueries(
            std::span<const glm::vec3> points, const OutlierEstimationParams& params, Query query)
        {
            if (points.size() < 2 || !std::isfinite(params.ScoreThreshold) || params.ScoreThreshold < 0 ||
                !std::ranges::all_of(points, IsFinite)) return std::nullopt;
            const auto width = OutlierCandidateWidth(points.size(), params.KNeighbors);
            std::vector<float> means(points.size());
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                float sum, nearest; std::size_t count;
                const auto row = query(i);
                if (row.size() != width || !NeighborDistances(points, i, row, sum, nearest, count)) return std::nullopt;
                means[i] = sum / float(count);
            }
            OutlierEstimationResult result;
            result.Scores.reserve(points.size());
            float scoreSum = 0;
            for (std::size_t i = 0; i < points.size(); ++i)
            {
                float sum = 0; std::size_t count = 0;
                for (const auto id : query(i))
                    if (id != i) { sum += means[id]; ++count; }
                if (!std::isfinite(sum)) return std::nullopt;
                const float neighborMean = count ? sum / float(count) : 1.f;
                const float score = neighborMean > 1e-12f ? means[i] / neighborMean : 0.f;
                if (!std::isfinite(score)) return std::nullopt;
                result.Scores.push_back(score);
                scoreSum += score;
                result.MaxScore = std::max(result.MaxScore, score);
                result.OutlierCount += score > params.ScoreThreshold;
            }
            result.MeanScore = scoreSum / float(points.size());
            if (!std::isfinite(result.MeanScore)) return std::nullopt;
            return result;
        }
    }
    std::optional<OutlierEstimationResult> EstimateOutlierProbability(
        std::span<const glm::vec3> positions, const OutlierEstimationParams& params)
    {
        if (positions.size() < 2 || !std::ranges::all_of(positions, IsFinite)) return std::nullopt;
        Octree tree;
        Octree::SplitPolicy policy{}; policy.SplitPoint = Octree::SplitPoint::Center; policy.TightChildren = true;
        if (!tree.BuildFromPoints(positions, policy, 32, 10)) return std::nullopt;
        std::vector<std::vector<std::size_t>> rows(positions.size());
        const auto width = OutlierCandidateWidth(positions.size(), params.KNeighbors);
        for (std::size_t i = 0; i < positions.size(); ++i) tree.QueryKNN(positions[i], width, rows[i]);
        return DistanceRatioWithQueries(positions, params, [&](std::size_t i) { return std::span<const std::size_t>(rows[i]); });
    }
    std::optional<OutlierEstimationResult> EstimateOutlierProbabilityFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const std::uint32_t> candidates,
        const OutlierEstimationParams& params)
    {
        const auto width = OutlierCandidateWidth(positions.size(), params.KNeighbors);
        if (positions.size() < 2 || positions.size() > std::numeric_limits<std::size_t>::max() / width ||
            candidates.size() != positions.size() * width) return std::nullopt;
        return DistanceRatioWithQueries(positions, params, [&](std::size_t i) { return candidates.subspan(i * width, width); });
    }
    std::optional<OutlierEstimationResult> EstimateOutlierProbability(Cloud& cloud, const OutlierEstimationParams& params)
    {
        auto result = EstimateOutlierProbability(cloud.Positions(), params);
        if (!result) return std::nullopt;
        auto property = cloud.GetOrAddVertexProperty<float>("p:outlier_score", 0.f);
        for (std::size_t i = 0; i < result->Scores.size(); ++i) property[Cloud::Handle(i)] = result->Scores[i];
        return result;
    }

    // =========================================================================
    // RemoveStatisticalOutliers
    // =========================================================================

    OutlierAnalysisResult AnalyzeStatisticalOutliers(
        std::span<const glm::vec3> positions,
        const StatisticalOutlierRemovalParams& params)
    {
        OutlierAnalysisResult result{};

        const std::size_t n = positions.size();
        if (n == 0)
        {
            result.Status = OutlierRemovalStatus::EmptyInput;
            return result;
        }
        if (params.KNeighbors == 0)
        {
            result.Status = OutlierRemovalStatus::InvalidParameters;
            return result;
        }
        const std::size_t k = params.KNeighbors;
        // Need at least k neighbors plus the point itself. Compare without
        // computing k + 1, which would wrap to 0 for a very large KNeighbors
        // (e.g. unchecked config/UI input) and silently bypass this guard.
        if (k >= n)
        {
            result.Status = OutlierRemovalStatus::InsufficientPoints;
            return result;
        }


        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;
        if (!octree.BuildFromPoints(positions, policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
        {
            result.Status = OutlierRemovalStatus::BuildFailed;
            return result;
        }

        // Per-point mean distance to its k nearest neighbors. Non-finite points
        // get a sentinel NaN so they always fall on the reject side and never
        // pollute the global mean/std-dev estimate.
        const std::size_t kQuery = k + 1; // +1 for self.
        std::vector<float> meanDist(n, 0.0f);
        std::vector<std::size_t> knn;


        for (std::size_t i = 0; i < n; ++i)
        {
            if (!IsFinite(positions[i]))
            {
                meanDist[i] = std::numeric_limits<float>::quiet_NaN();
                ++result.NonFiniteCount;
                continue;
            }

            knn.clear();
            octree.QueryKNN(positions[i], kQuery, knn);

            float distSum = 0.0f;
            std::size_t count = 0;
            for (std::size_t ni : knn)
            {
                if (ni == i || !IsFinite(positions[ni]))
                    continue;
                distSum += glm::length(positions[ni] - positions[i]);
                ++count;
            }

            const float m = (count > 0) ? distSum / static_cast<float>(count) : 0.0f;
            meanDist[i] = m;
        }

        return ClassifyStatisticalOutliers(meanDist, params.StdDevMultiplier);
    }

    OutlierAnalysisResult ClassifyStatisticalOutliers(std::span<const float> distances, float multiplier)
    {
        OutlierAnalysisResult result;
        if (distances.empty()) { result.Status = OutlierRemovalStatus::EmptyInput; return result; }
        result.Scores.assign(distances.begin(), distances.end());
        double sum=0, sumSq=0;
        std::size_t count=0;
        for (const float distance : distances)
        {
            if (std::isnan(distance)) { ++result.NonFiniteCount; continue; }
            sum += distance;
            sumSq += double(distance)*double(distance);
            ++count;
        }
        const double mean = count ? sum/double(count) : 0;
        const double variance = count ? std::max(0.0, sumSq/double(count)-mean*mean) : 0;
        const double stddev = std::sqrt(variance);
        const double threshold = mean + double(multiplier)*stddev;
        result.MeanDistance=float(mean);result.StdDevDistance=float(stddev);result.DistanceThreshold=float(threshold);
        for (float distance : distances)
        {
            const bool rejected = !std::isfinite(distance) || !(double(distance)<=threshold);
            result.Mask.push_back(rejected);result.RejectedCount += rejected;
        }
        return result;
    }

    OutlierAnalysisResult ClassifyRadiusOutliers(std::span<const std::uint32_t> counts, std::uint32_t minimum)
    {
        OutlierAnalysisResult result;
        if (counts.empty()) { result.Status=OutlierRemovalStatus::EmptyInput;return result; }
        for (auto count : counts)
        {
            result.Scores.push_back(float(count));result.Mask.push_back(count<minimum);
            result.RejectedCount += count<minimum;
        }
        return result;
    }

    OutlierAnalysisResult AnalyzeRadiusOutliers(std::span<const glm::vec3> positions,
                                               const RadiusOutlierRemovalParams& params)
    {
        OutlierAnalysisResult result;
        if (positions.empty()) { result.Status=OutlierRemovalStatus::EmptyInput;return result; }
        if (!(params.SearchRadius>0) || !std::isfinite(params.SearchRadius))
        { result.Status=OutlierRemovalStatus::InvalidParameters;return result; }
        Octree tree;
        Octree::SplitPolicy policy{};policy.SplitPoint=Octree::SplitPoint::Center;policy.TightChildren=true;
        if (!tree.BuildFromPoints(positions,policy,params.OctreeMaxPerNode,params.OctreeMaxDepth))
        { result.Status=OutlierRemovalStatus::BuildFailed;return result; }
        std::vector<std::size_t> hits;
        for (std::size_t i=0;i<positions.size();++i)
        {
            if (!IsFinite(positions[i]))
            {
                result.Mask.push_back(1);result.Scores.push_back(std::numeric_limits<float>::quiet_NaN());
                ++result.NonFiniteCount;++result.RejectedCount;continue;
            }
            hits.clear();tree.QuerySphere(Sphere{positions[i],params.SearchRadius},hits);
            std::size_t count=0;
            for (auto neighbor : hits)
                if (neighbor!=i && IsFinite(positions[neighbor]) &&
                    glm::length(positions[neighbor]-positions[i])<=params.SearchRadius) ++count;
            result.Scores.push_back(float(count));result.Mask.push_back(count<params.MinNeighbors);
            result.RejectedCount += count<params.MinNeighbors;
        }
        return result;
    }

    namespace
    {
        OutlierRemovalResult MaterializeOutlierRemoval(const Cloud& cloud, const OutlierAnalysisResult& analysis)
        {
            OutlierRemovalResult result;
            result.Status=analysis.Status;
            if (analysis.Status!=OutlierRemovalStatus::Success) return result;
            result.NonFiniteCount=analysis.NonFiniteCount;
            result.MeanDistance=analysis.MeanDistance;result.StdDevDistance=analysis.StdDevDistance;
            result.DistanceThreshold=analysis.DistanceThreshold;
            FinalizeRemoval(cloud,cloud.VerticesSize(),result,[&](std::size_t i){return analysis.Mask[i]==0;});
            return result;
        }
    }
    OutlierRemovalResult RemoveStatisticalOutliers(const Cloud& cloud,const StatisticalOutlierRemovalParams& params)
    {
        return MaterializeOutlierRemoval(cloud,AnalyzeStatisticalOutliers(cloud.Positions(),params));
    }
    OutlierRemovalResult RemoveRadiusOutliers(const Cloud& cloud,const RadiusOutlierRemovalParams& params)
    {
        return MaterializeOutlierRemoval(cloud,AnalyzeRadiusOutliers(cloud.Positions(),params));
    }

    // =========================================================================
    // EstimateKernelDensity
    // =========================================================================

    std::optional<KDEResult> EstimateKernelDensityFromNeighbors(
        std::span<const glm::vec3> positions, std::span<const std::uint32_t> candidates,
        const KDEParams& params)
    {
        const std::size_t n = positions.size();
        if (n < 2 || n > std::numeric_limits<std::uint32_t>::max() ||
            !std::isfinite(params.Bandwidth) || params.Bandwidth < 0 ||
            params.KNeighbors == std::numeric_limits<std::size_t>::max()) return std::nullopt;
        const auto width = std::min(n, std::max(params.KNeighbors, std::size_t{2}) + 1);
        if (n > std::numeric_limits<std::size_t>::max() / width || candidates.size() != n * width)
            return std::nullopt;
        for (auto p : positions)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return std::nullopt;
        std::vector<float> nnDists(n);
        std::vector<std::size_t> seen(n, n);
        for (std::size_t i = 0; i < n; ++i)
        {
            float nearest = std::numeric_limits<float>::infinity();
            for (auto id : candidates.subspan(i * width, width))
            {
                if (id >= n || seen[id] == i) return std::nullopt;
                seen[id] = i;
                if (id != i) nearest = std::min(nearest, glm::length(positions[id] - positions[i]));
            }
            if (!std::isfinite(nearest)) return std::nullopt;
            nnDists[i] = nearest;
        }
        // Preserve the spacing heuristic: a univariate rule applied to NN distances,
        // with mean-spacing and absolute floors. This is not a multivariate bandwidth fit.
        float bandwidth = params.Bandwidth;
        if (bandwidth <= 0.0f)
        {
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

        if (!std::isfinite(bandwidth) || !std::isfinite(invH2) || !std::isfinite(normFactor) || normFactor <= 0)
            return std::nullopt;

        // Phase 2: Compute density at each point via KNN Gaussian KDE.
        KDEResult result{};
        result.Densities.resize(n, 0.0f);
        result.UsedBandwidth = bandwidth;
        float densitySum = 0.0f;
        float minDensity = std::numeric_limits<float>::max();
        float maxDensity = 0.0f;

        for (std::size_t i = 0; i < n; ++i)
        {

            float kde = 0.0f;
            uint32_t neighborCount = 0;
            for (auto ni : candidates.subspan(i * width, width))
            {
                if (ni == i) continue;
                float dist = glm::length(positions[ni] - positions[i]);
                kde += normFactor * std::exp(dist * dist * invH2);
                ++neighborCount;
            }

            if (neighborCount > 0)
                kde /= static_cast<float>(neighborCount);

            if (!std::isfinite(kde)) return std::nullopt;
            result.Densities[i] = kde;
            densitySum += kde;
            minDensity = std::min(minDensity, kde);
            maxDensity = std::max(maxDensity, kde);
        }

        if (!std::isfinite(densitySum)) return std::nullopt;
        result.MeanDensity = (n > 0) ? densitySum / static_cast<float>(n) : 0.0f;
        result.MinDensity = minDensity;
        result.MaxDensity = maxDensity;

        return result;
    }

    std::optional<KDEResult> EstimateKernelDensity(
        std::span<const glm::vec3> positions,
        const KDEParams& params)
    {
        const std::size_t n = positions.size();
        if (n < 2)
            return std::nullopt;

        if (!std::isfinite(params.Bandwidth) || params.Bandwidth < 0 ||
            params.KNeighbors == std::numeric_limits<std::size_t>::max()) return std::nullopt;
        for (auto p : positions)
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return std::nullopt;

        Octree octree;
        Octree::SplitPolicy policy{};
        policy.SplitPoint = Octree::SplitPoint::Center;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(positions, policy, 32, 10))
            return std::nullopt;

        const std::size_t k = std::max(params.KNeighbors, std::size_t{2});
        const std::size_t kQuery = std::min(n, k + 1); // +1 for self

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
            if (!std::isfinite(nearestDist)) return std::nullopt;
            nnDists[i] = nearestDist;
        }

        // Preserve the inherited univariate NN-spacing heuristic and its floors.
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

        if (!std::isfinite(bandwidth) || !std::isfinite(invH2) || !std::isfinite(normFactor) || normFactor <= 0)
            return std::nullopt;

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

            if (!std::isfinite(kde)) return std::nullopt;
            result.Densities[i] = kde;
            densitySum += kde;
            minDensity = std::min(minDensity, kde);
            maxDensity = std::max(maxDensity, kde);
        }

        if (!std::isfinite(densitySum)) return std::nullopt;
        result.MeanDensity = (n > 0) ? densitySum / static_cast<float>(n) : 0.0f;
        result.MinDensity = minDensity;
        result.MaxDensity = maxDensity;

        return result;
    }

    std::optional<KDEResult> EstimateKernelDensity(Cloud& cloud, const KDEParams& params)
    {
        const auto positions = std::as_const(cloud).Positions();
        auto result = EstimateKernelDensity(positions, params);
        if (!result) return std::nullopt;
        auto prop = cloud.GetOrAddVertexProperty<float>("p:density", 0.0f);
        for (std::size_t i = 0; i < result->Densities.size(); ++i)
            prop[Cloud::Handle(i)] = result->Densities[i];
        return result;
    }

    GaussianNoiseResult ApplyGaussianNoise(
        Cloud& cloud, const PointCloudGaussianNoiseParams& params)
    {
        GaussianNoiseResult result{};
        result.ElementCount = cloud.VertexCount();

        if (cloud.VertexCount() == 0)
        {
            result.Status = GaussianNoiseStatus::EmptyInput;
            return result;
        }
        if (!std::isfinite(params.StdDevFraction) || params.StdDevFraction < 0.0F)
        {
            result.Status = GaussianNoiseStatus::InvalidParameters;
            return result;
        }

        for (const VertexHandle point : cloud.LivePoints())
        {
            if (!IsFinite(cloud.Position(point)))
            {
                result.Status = GaussianNoiseStatus::NonFinitePosition;
                return result;
            }
        }

        if (params.StdDevFraction == 0.0F)
        {
            result.Status = GaussianNoiseStatus::Success;
            return result;
        }

        const std::optional<CloudStatistics> stats = ComputeStatistics(cloud);
        if (!stats.has_value() || !std::isfinite(stats->AverageSpacing) || stats->AverageSpacing <= 0.0F)
        {
            result.Status = GaussianNoiseStatus::DegenerateScale;
            return result;
        }

        result.Scale = params.StdDevFraction * stats->AverageSpacing;
        glm::vec3 displacementSum{0.0F};

        for (const VertexHandle point : cloud.LivePoints())
        {
            const glm::vec3 displacement = Geometry::Sampling::GaussianDisplacement(
                params.Seed,
                point.Index,
                result.Scale);
            cloud.Position(point) += displacement;
            displacementSum += displacement;
            result.MaxDisplacement = std::max(result.MaxDisplacement, glm::length(displacement));
            ++result.DisplacedCount;
        }

        if (result.DisplacedCount > 0)
        {
            result.MeanDisplacement = displacementSum / static_cast<float>(result.DisplacedCount);
        }
        result.Status = GaussianNoiseStatus::Success;
        return result;
    }

} // namespace Geometry::PointCloud
