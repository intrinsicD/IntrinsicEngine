module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:SurfaceReconstruction.Impl;

import :SurfaceReconstruction;
import :MarchingCubes;
import :NormalEstimation;
import :HalfedgeMesh;
import :AABB;
import :Octree;
import :Primitives;

namespace Geometry::SurfaceReconstruction
{
    static constexpr float kDistanceEpsilon = 1e-8f;
    static constexpr float kNormalLengthEpsilon = 1e-8f;

    [[nodiscard]] static bool IsFiniteVec3(const glm::vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

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
        const std::vector<glm::vec3>& points,
        const std::vector<glm::vec3>& normals)
    {
        std::size_t nearestIdx = 0;
        octree.QueryNearest(queryPoint, nearestIdx);

        if (nearestIdx >= points.size())
            return std::numeric_limits<float>::max();

        glm::vec3 diff = queryPoint - points[nearestIdx];
        return glm::dot(diff, normals[nearestIdx]);
    }

    // Compute signed distance at a query point using weighted average over
    // k nearest neighbors.
    // d(g) = sum(w_i * dot(g - p_i, n_i)) / sum(w_i)
    // where w_i = exp(-||g - p_i||^2 / (2 h^2)) * max(0, dot(n_i, n_ref))^p
    static float SignedDistanceWeighted(
        const glm::vec3& queryPoint,
        const Octree& octree,
        const std::vector<glm::vec3>& points,
        const std::vector<glm::vec3>& normals,
        std::size_t k,
        const ReconstructionParams& params,
        std::vector<std::size_t>& neighborBuffer)
    {
        octree.QueryKnn(queryPoint, k + 1, neighborBuffer);

        if (neighborBuffer.empty())
            return std::numeric_limits<float>::max();

        std::size_t nearestIdx = 0;
        octree.QueryNearest(queryPoint, nearestIdx);
        if (nearestIdx >= points.size())
            return std::numeric_limits<float>::max();

        const glm::vec3 refNormal = normals[nearestIdx];

        float maxDist2 = 0.0f;
        for (std::size_t idx : neighborBuffer)
        {
            if (idx >= points.size()) continue;
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

    // =========================================================================
    // Main reconstruction
    // =========================================================================

    std::optional<ReconstructionResult> Reconstruct(
        const std::vector<glm::vec3>& points,
        const std::vector<glm::vec3>& normals,
        const ReconstructionParams& params)
    {
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
                if (!IsFiniteVec3(points[i]))
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
            NormalEstimation::EstimationParams neParams;
            neParams.KNeighbors = params.NormalKNeighbors;
            neParams.OrientNormals = true;
            neParams.OctreeMaxPerNode = params.OctreeMaxPerNode;
            neParams.OctreeMaxDepth = params.OctreeMaxDepth;

            auto neResult = NormalEstimation::EstimateNormals(points, neParams);
            if (!neResult.has_value())
                return std::nullopt;

            for (std::size_t i = 0; i < n; ++i)
            {
                if (!IsFiniteVec3(points[i]))
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
        if (diagonal < 1e-10f)
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
        if (cellSize < 1e-10f)
            return std::nullopt;

        std::size_t gridNX = std::max(std::size_t{1},
            static_cast<std::size_t>(std::ceil(bbSize.x / cellSize)));
        std::size_t gridNY = std::max(std::size_t{1},
            static_cast<std::size_t>(std::ceil(bbSize.y / cellSize)));
        std::size_t gridNZ = std::max(std::size_t{1},
            static_cast<std::size_t>(std::ceil(bbSize.z / cellSize)));

        glm::vec3 spacing(cellSize);

        // -----------------------------------------------------------------
        // Step 4: Build octree for spatial queries
        // -----------------------------------------------------------------
        const std::size_t filteredCount = usedPoints.size();
        std::vector<AABB> pointAABBs(filteredCount);
        for (std::size_t i = 0; i < filteredCount; ++i)
        {
            pointAABBs[i] = {.Min = usedPoints[i], .Max = usedPoints[i]};
        }

        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Mean;
        policy.TightChildren = true;

        if (!octree.Build(std::move(pointAABBs), policy,
                          params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return std::nullopt;

        // -----------------------------------------------------------------
        // Step 5: Compute signed distance field on the grid
        // -----------------------------------------------------------------
        MarchingCubes::ScalarGrid grid;
        grid.NX = gridNX;
        grid.NY = gridNY;
        grid.NZ = gridNZ;
        grid.Origin = bbMin;
        grid.Spacing = spacing;
        grid.Values.resize((gridNX + 1) * (gridNY + 1) * (gridNZ + 1));

        const bool useWeighted = (params.KNeighbors > 1);
        const std::size_t effectiveK = std::min(params.KNeighbors, filteredCount);
        std::vector<std::size_t> neighborBuffer;

        for (std::size_t z = 0; z <= gridNZ; ++z)
        {
            for (std::size_t y = 0; y <= gridNY; ++y)
            {
                for (std::size_t x = 0; x <= gridNX; ++x)
                {
                    glm::vec3 gp = grid.VertexPosition(x, y, z);

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

                    grid.Set(x, y, z, sd);
                }
            }
        }

        // -----------------------------------------------------------------
        // Step 6: Extract isosurface via Marching Cubes
        // -----------------------------------------------------------------
        MarchingCubes::MarchingCubesParams mcParams;
        mcParams.Isovalue = 0.0f;
        mcParams.ComputeNormals = true;

        auto mcResult = MarchingCubes::Extract(grid, mcParams);
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
