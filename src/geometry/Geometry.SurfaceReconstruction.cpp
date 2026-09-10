module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.SurfaceReconstruction;

import Geometry.Grid;
import Geometry.MarchingCubes;
import Geometry.PointCloud.Normals;
import Geometry.HalfedgeMesh;
import Geometry.AABB;
import Geometry.Octree;
import Geometry.Properties;
import Geometry.Primitives;
import Geometry.Validation;

namespace Geometry::SurfaceReconstruction
{
    static constexpr float kDistanceEpsilon = 1e-8f;
    static constexpr float kNormalLengthEpsilon = 1e-8f;

    using Validation::IsFinite;

    [[nodiscard]] static bool NormalizeSafe(glm::vec3& n)
    {
        const float len2 = glm::dot(n, n);
        if (!std::isfinite(len2) || len2 <= (kNormalLengthEpsilon * kNormalLengthEpsilon))
            return false;
        n *= 1.0f / std::sqrt(len2);
        return true;
    }

    // =========================================================================
    // Signed distance computation
    // =========================================================================

    // Compute signed distance at a query point using the nearest point.
    // d(g) = dot(g - p_nearest, n_nearest)
    static float SignedDistanceNearest(
        const glm::vec3& queryPoint,
        const Octree& octree,
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> normals)
    {
        std::size_t nearestIdx = 0;
        octree.QueryNearest(queryPoint, nearestIdx);

        if (nearestIdx >= points.size())
            return std::numeric_limits<float>::max();

        glm::vec3 diff = queryPoint - points[nearestIdx];
        return glm::dot(diff, normals[nearestIdx]);
    }

    template <class Indices>
    static float
    SignedDistanceWeightedRow(const glm::vec3& queryPoint, std::span<const glm::vec3> points,
                              std::span<const glm::vec3> normals, std::size_t nearestIdx,
                              const Indices& neighborBuffer, const ReconstructionParams& params)
    {
        const glm::vec3 refNormal = normals[nearestIdx];

        float maxDist2 = 0.0f;
        for (std::size_t idx : neighborBuffer)
        {
            if (idx >= points.size())
                continue;
            const glm::vec3 diff = queryPoint - points[idx];
            const float dist2 = glm::dot(diff, diff);
            maxDist2 = std::max(maxDist2, dist2);
        }

        const float sigmaScale = std::max(1e-3f, params.KernelSigmaScale);
        const float sigma2 = std::max(kDistanceEpsilon, maxDist2 * sigmaScale * sigmaScale);
        const float inv2Sigma2 = 0.5f / sigma2;
        const float normalPower = std::max(0.0f, params.NormalAgreementPower);

        float sumWD = 0.0f;
        float sumW = 0.0f;

        for (std::size_t idx : neighborBuffer)
        {
            if (idx >= points.size()) continue;

            const glm::vec3 diff = queryPoint - points[idx];
            const float dist2 = glm::dot(diff, diff);
            const float spatialW = std::exp(-dist2 * inv2Sigma2);

            const float alignRaw = std::max(0.0f, glm::dot(normals[idx], refNormal));
            const float normalW = (normalPower > 0.0f) ? std::pow(alignRaw, normalPower) : 1.0f;
            const float w = std::max(kDistanceEpsilon, spatialW * normalW);

            const float d = glm::dot(diff, normals[idx]);
            sumWD += w * d;
            sumW += w;
        }

        if (sumW < 1e-12f)
            return std::numeric_limits<float>::max();

        return sumWD / sumW;
    }

    static float SignedDistanceWeighted(const glm::vec3& queryPoint, const Octree& octree,
                                        std::span<const glm::vec3> points,
                                        std::span<const glm::vec3> normals, std::size_t k,
                                        const ReconstructionParams& params,
                                        std::vector<std::size_t>& neighborBuffer)
    {
        octree.QueryKNN(queryPoint, k + 1, neighborBuffer);
        std::size_t nearestIdx{};
        octree.QueryNearest(queryPoint, nearestIdx);
        if (neighborBuffer.empty() || nearestIdx >= points.size())
            return std::numeric_limits<float>::max();
        return SignedDistanceWeightedRow(queryPoint, points, normals, nearestIdx, neighborBuffer,
                                         params);
    }

    // =========================================================================
    // Main reconstruction
    // =========================================================================

    std::optional<PreparedReconstruction> Prepare(
        std::span<const glm::vec3> points,
        std::span<const glm::vec3> normals,
        const ReconstructionParams& params)
    {
        if (params.Resolution == 0 || params.Resolution > 4096 || params.MaxGridVertices == 0 ||
            !std::isfinite(params.BoundingBoxPadding) || params.BoundingBoxPadding < 0 ||
            !std::isfinite(params.KernelSigmaScale) || !std::isfinite(params.NormalAgreementPower))
            return std::nullopt;
        // Validate input
        if (points.size() < 3)
            return std::nullopt;

        if (!normals.empty() && normals.size() != points.size())
            return std::nullopt;

        if (normals.empty() && !params.EstimateNormals)
            return std::nullopt;

        const std::size_t n = points.size();

        // -----------------------------------------------------------------
        // Step 1: Obtain normals
        // -----------------------------------------------------------------
        std::vector<glm::vec3> usedPoints;
        std::vector<glm::vec3> usedNormals;
        usedPoints.reserve(n);
        usedNormals.reserve(n);

        if (!normals.empty())
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                if (!IsFinite(points[i]))
                    continue;

                glm::vec3 nrm = normals[i];
                if (!NormalizeSafe(nrm))
                    continue;

                usedPoints.push_back(points[i]);
                usedNormals.push_back(nrm);
            }

            if (usedPoints.size() < 3)
                return std::nullopt;
        }
        else
        {
            PointCloud::Normals::Params neParams;
            neParams.KNeighbors = params.NormalKNeighbors;
            neParams.Orientation = PointCloud::Normals::OrientationMode::MinimumSpanningTree;

            auto neResult = PointCloud::Normals::Estimate(points, neParams);
            if (!neResult.has_value())
                return std::nullopt;

            for (std::size_t i = 0; i < n; ++i)
            {
                if (!IsFinite(points[i]))
                    continue;

                glm::vec3 nrm = neResult->Normals[i];
                if (!NormalizeSafe(nrm))
                    continue;

                usedPoints.push_back(points[i]);
                usedNormals.push_back(nrm);
            }

            if (usedPoints.size() < 3)
                return std::nullopt;
        }

        // -----------------------------------------------------------------
        // Step 2: Compute bounding box with padding
        // -----------------------------------------------------------------
        glm::vec3 bbMin(std::numeric_limits<float>::max());
        glm::vec3 bbMax(-std::numeric_limits<float>::max());

        for (const auto& p : usedPoints)
        {
            bbMin = glm::min(bbMin, p);
            bbMax = glm::max(bbMax, p);
        }

        glm::vec3 bbSize = bbMax - bbMin;
        float diagonal = glm::length(bbSize);

        // Ensure non-degenerate bounding box
        if (!std::isfinite(diagonal) || diagonal < 1e-10f)
            return std::nullopt;

        float padding = diagonal * params.BoundingBoxPadding;
        bbMin -= glm::vec3(padding);
        bbMax += glm::vec3(padding);
        bbSize = bbMax - bbMin;

        // -----------------------------------------------------------------
        // Step 3: Determine grid dimensions
        // -----------------------------------------------------------------
        float maxExtent = std::max({bbSize.x, bbSize.y, bbSize.z});
        if (params.Resolution == 0)
            return std::nullopt;

        float cellSize = maxExtent / static_cast<float>(params.Resolution);

        // Avoid degenerate cell size
        if (!IsFinite(bbSize) || !std::isfinite(cellSize) || cellSize < 1e-10f)
            return std::nullopt;

        std::size_t gridNX = std::max(std::size_t{1},
            static_cast<std::size_t>(std::ceil(bbSize.x / cellSize)));
        std::size_t gridNY = std::max(std::size_t{1},
            static_cast<std::size_t>(std::ceil(bbSize.y / cellSize)));
        std::size_t gridNZ = std::max(std::size_t{1},
            static_cast<std::size_t>(std::ceil(bbSize.z / cellSize)));

        const auto nx = gridNX + 1, ny = gridNY + 1, nz = gridNZ + 1;
        if (nx > params.MaxGridVertices / ny || nx * ny > params.MaxGridVertices / nz)
            return std::nullopt;
        return PreparedReconstruction{std::move(usedPoints),
                                      std::move(usedNormals),
                                      {.NX = gridNX,
                                       .NY = gridNY,
                                       .NZ = gridNZ,
                                       .Origin = bbMin,
                                       .Spacing = glm::vec3(cellSize)}};
    }

    std::optional<ReconstructionResult> Reconstruct(std::span<const glm::vec3> points,
                                                    std::span<const glm::vec3> normals,
                                                    const ReconstructionParams& params)
    {
        auto prepared = Prepare(points, normals, params);
        if (!prepared)
            return std::nullopt;
        const auto& usedPoints = prepared->Points;
        const auto& usedNormals = prepared->Normals;
        const auto& dims = prepared->Dimensions;
        const auto gridNX = dims.NX, gridNY = dims.NY, gridNZ = dims.NZ;
        // -----------------------------------------------------------------
        // Step 4: Build octree for spatial queries
        // -----------------------------------------------------------------
        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Mean;
        policy.TightChildren = true;

        if (!octree.BuildFromPoints(usedPoints, policy,
                          params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return std::nullopt;

        // -----------------------------------------------------------------
        // Step 5: Compute signed distance field on the grid
        // -----------------------------------------------------------------
        Grid::DenseGrid grid(dims);
        auto scalar = grid.AddProperty<float>("scalar", 0.0f);

        const bool useWeighted = (params.KNeighbors > 1);
        const std::size_t effectiveK = std::min(params.KNeighbors, usedPoints.size());
        std::vector<std::size_t> neighborBuffer;

        for (std::size_t z = 0; z <= gridNZ; ++z)
        {
            for (std::size_t y = 0; y <= gridNY; ++y)
            {
                for (std::size_t x = 0; x <= gridNX; ++x)
                {
                    glm::vec3 gp = grid.WorldPosition(x, y, z);

                    float sd;
                    if (useWeighted)
                    {
                        sd = SignedDistanceWeighted(
                            gp, octree, usedPoints, usedNormals,
                            effectiveK, params, neighborBuffer);
                    }
                    else
                    {
                        sd = SignedDistanceNearest(
                            gp, octree, usedPoints, usedNormals);
                    }

                    grid.Set(scalar, x, y, z, sd);
                }
            }
        }

        return Extract(*prepared, scalar.Vector());
    }

    static bool ValidDimensions(const Grid::GridDimensions& dims)
    {
        if (!dims.IsValid() || dims.NX > 4096 || dims.NY > 4096 || dims.NZ > 4096 ||
            !IsFinite(dims.Origin) || !IsFinite(dims.Spacing) || dims.Spacing.x <= 0 ||
            dims.Spacing.y <= 0 || dims.Spacing.z <= 0)
            return false;
        return IsFinite(dims.WorldPosition(dims.NX, dims.NY, dims.NZ));
    }

    std::vector<glm::vec3> GridQueries(const PreparedReconstruction& prepared, std::size_t first,
                                       std::size_t count)
    {
        const auto& dims = prepared.Dimensions;
        if (!ValidDimensions(dims) || first > dims.VertexCount() ||
            count > dims.VertexCount() - first)
            return {};
        std::vector<glm::vec3> result;
        result.reserve(count);
        for (std::size_t i = first; i < first + count; ++i)
        {
            const auto c = dims.GridCoord(i);
            result.push_back(dims.WorldPosition(c.x, c.y, c.z));
        }
        return result;
    }

    std::optional<std::vector<float>> EvaluateSignedDistances(std::span<const glm::vec3> points,
                                                              std::span<const glm::vec3> normals,
                                                              std::span<const glm::vec3> queries,
                                                              PointNeighborhoods rows,
                                                              const ReconstructionParams& params)
    {
        if (points.empty() || points.size() != normals.size() ||
            rows.Offsets.size() != queries.size() + 1 || rows.Offsets.empty() ||
            rows.Offsets.front() != 0 || rows.Offsets.back() != rows.Indices.size() ||
            !std::isfinite(params.KernelSigmaScale) || !std::isfinite(params.NormalAgreementPower))
            return {};
        const auto count =
            params.KNeighbors > 1
                ? std::min(points.size(), std::min(params.KNeighbors, points.size()) + 1)
                : 1;
        std::vector<float> field;
        field.reserve(queries.size());
        for (std::size_t i = 0; i < queries.size(); ++i)
        {
            const auto begin = rows.Offsets[i], end = rows.Offsets[i + 1];
            if (!IsFinite(queries[i]) || begin > end || end > rows.Indices.size() ||
                end - begin != count)
                return {};
            const auto row = rows.Indices.subspan(begin, count);
            float previous = -1;
            std::uint32_t previousId = 0;
            for (auto id : row)
            {
                if (id >= points.size() || !IsFinite(points[id]) || !IsFinite(normals[id]) ||
                    glm::dot(normals[id], normals[id]) <= 1e-16f)
                    return {};
                const auto delta = queries[i] - points[id];
                const auto distance = glm::dot(delta, delta);
                if (!std::isfinite(distance) || distance < previous ||
                    (distance == previous && id <= previousId))
                    return {};
                previous = distance;
                previousId = id;
            }
            const auto value =
                params.KNeighbors > 1
                    ? SignedDistanceWeightedRow(queries[i], points, normals, row.front(), row,
                                                params)
                    : glm::dot(queries[i] - points[row.front()], normals[row.front()]);
            if (!std::isfinite(value))
                return {};
            field.push_back(value);
        }
        return field;
    }

    std::optional<ReconstructionResult> Extract(const PreparedReconstruction& prepared,
                                                std::span<const float> field)
    {
        const auto& dims = prepared.Dimensions;
        if (!ValidDimensions(dims) || field.size() != dims.VertexCount() ||
            !std::all_of(field.begin(), field.end(), [](float v) { return std::isfinite(v); }))
            return {};
        const auto gridNX = dims.NX, gridNY = dims.NY, gridNZ = dims.NZ;
        Grid::DenseGrid grid(dims);
        auto scalar = grid.AddProperty<float>("scalar", 0);
        scalar.Vector().assign(field.begin(), field.end());
        // -----------------------------------------------------------------
        // Step 6: Extract isosurface via Marching Cubes
        // -----------------------------------------------------------------
        MarchingCubes::MarchingCubesParams mcParams;
        mcParams.Isovalue = 0.0f;
        mcParams.ComputeNormals = true;

        auto mcResult = MarchingCubes::Extract(grid, mcParams, "scalar");
        if (!mcResult.has_value())
            return std::nullopt;

        // -----------------------------------------------------------------
        // Step 7: Convert to HalfedgeMesh
        // -----------------------------------------------------------------
        auto meshOpt = MarchingCubes::ToMesh(*mcResult);
        if (!meshOpt.has_value())
            return std::nullopt;

        ReconstructionResult result;
        result.OutputMesh = std::move(*meshOpt);
        result.OutputVertexCount = result.OutputMesh.VertexCount();
        result.OutputFaceCount = result.OutputMesh.FaceCount();
        result.GridNX = gridNX;
        result.GridNY = gridNY;
        result.GridNZ = gridNZ;

        return result;
    }

} // namespace Geometry::SurfaceReconstruction
