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
    // where w_i = 1 / (||g - p_i||^2 + epsilon)
    static float SignedDistanceWeighted(
        const glm::vec3& queryPoint,
        const Octree& octree,
        const std::vector<glm::vec3>& points,
        const std::vector<glm::vec3>& normals,
        std::size_t k,
        std::vector<std::size_t>& neighborBuffer)
    {
        octree.QueryKnn(queryPoint, k + 1, neighborBuffer);

        if (neighborBuffer.empty())
            return std::numeric_limits<float>::max();

        constexpr float eps = 1e-8f;
        float sumWD = 0.0f;
        float sumW = 0.0f;

        for (std::size_t idx : neighborBuffer)
        {
            if (idx >= points.size()) continue;

            glm::vec3 diff = queryPoint - points[idx];
            float dist2 = glm::dot(diff, diff);
            float w = 1.0f / (dist2 + eps);

            float d = glm::dot(diff, normals[idx]);
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
        std::vector<glm::vec3> usedNormals;

        if (!normals.empty())
        {
            usedNormals = normals;
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

            usedNormals = std::move(neResult->Normals);
        }

        // -----------------------------------------------------------------
        // Step 2: Compute bounding box with padding
        // -----------------------------------------------------------------
        glm::vec3 bbMin(std::numeric_limits<float>::max());
        glm::vec3 bbMax(-std::numeric_limits<float>::max());

        for (const auto& p : points)
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
        std::vector<AABB> pointAABBs(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            pointAABBs[i] = {.Min = points[i], .Max = points[i]};
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
                            gp, octree, points, usedNormals,
                            params.KNeighbors, neighborBuffer);
                    }
                    else
                    {
                        sd = SignedDistanceNearest(
                            gp, octree, points, usedNormals);
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
