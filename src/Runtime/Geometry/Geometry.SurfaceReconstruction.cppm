module;

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:SurfaceReconstruction;

import :HalfedgeMesh;

export namespace Geometry::SurfaceReconstruction
{
    // =========================================================================
    // Surface Reconstruction — Point Cloud to Triangle Mesh
    // =========================================================================
    //
    // Reconstructs a triangle mesh from an unstructured point cloud using
    // an implicit surface approach:
    //
    //   1. Build a spatial index (Octree) over the input points.
    //   2. Estimate surface normals if not provided (PCA + MST orientation,
    //      via the Geometry.NormalEstimation module).
    //   3. Construct a regular 3D grid covering the point cloud bounding box
    //      (with configurable padding).
    //   4. For each grid vertex, compute a signed distance to the implicit
    //      surface defined by the oriented point cloud:
    //        d(g) = dot(g - p_nearest, n_nearest)
    //      where p_nearest is the nearest point and n_nearest its normal.
    //      Optionally, a weighted average over k neighbors is used for
    //      smoother results.
    //   5. Extract the zero-level-set isosurface via Marching Cubes.
    //   6. Convert the triangle soup to a halfedge mesh.
    //
    // References:
    //   - Hoppe, DeRose, Duchamp, McDonald, Stuetzle, "Surface Reconstruction
    //     from Unorganized Points", SIGGRAPH 1992.
    //   - Lorensen & Cline, "Marching Cubes", SIGGRAPH 1987.

    // -------------------------------------------------------------------------
    // Parameters
    // -------------------------------------------------------------------------

    struct ReconstructionParams
    {
        // Grid resolution: number of cells along the longest bounding box axis.
        // Higher values produce finer detail but consume more memory and time.
        // Memory: O(Resolution^3). Time: O(Resolution^3 * KNeighbors).
        std::size_t Resolution{64};

        // Number of nearest neighbors for signed distance computation.
        // k=1 uses only the nearest point (fast, may be noisy).
        // k>1 uses weighted averaging over neighbors (smoother).
        std::size_t KNeighbors{1};

        // Weight exponent for normal-consistency term when KNeighbors > 1.
        // Higher values suppress contributions from neighbors whose normals
        // disagree with the nearest-neighbor normal direction.
        float NormalAgreementPower{2.0f};

        // Gaussian kernel bandwidth scale for weighted signed distance when
        // KNeighbors > 1. For each query point, the bandwidth is estimated as
        // h = KernelSigmaScale * max_i ||g - p_i|| over the k-neighborhood.
        // Larger values smooth more aggressively.
        float KernelSigmaScale{2.0f};

        // Bounding box padding: fraction of the bounding box diagonal added
        // on each side. Ensures the isosurface doesn't clip at the grid boundary.
        float BoundingBoxPadding{0.1f};

        // If true, estimate normals from the point cloud using PCA + MST
        // orientation (Geometry.NormalEstimation). If false, the caller must
        // provide pre-computed normals.
        bool EstimateNormals{true};

        // Normal estimation neighborhood size (used when EstimateNormals = true).
        std::size_t NormalKNeighbors{15};

        // Octree parameters for spatial queries.
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    // -------------------------------------------------------------------------
    // Result
    // -------------------------------------------------------------------------

    struct ReconstructionResult
    {
        // The reconstructed triangle mesh.
        Halfedge::Mesh OutputMesh;

        // Mesh statistics.
        std::size_t OutputVertexCount{0};
        std::size_t OutputFaceCount{0};

        // Grid dimensions used for the scalar field.
        std::size_t GridNX{0};
        std::size_t GridNY{0};
        std::size_t GridNZ{0};
    };

    // -------------------------------------------------------------------------
    // Reconstruction
    // -------------------------------------------------------------------------

    // Reconstruct a triangle mesh from a point cloud.
    //
    // points: Input 3D point positions.
    // normals: Pre-computed normals (one per point). If empty and
    //          params.EstimateNormals is true, normals are estimated
    //          automatically.
    //
    // Returns nullopt if:
    //   - points is empty or has fewer than 3 points
    //   - normals is non-empty but size doesn't match points
    //   - normals is empty and EstimateNormals is false
    //   - Normal estimation fails
    //   - The isosurface is empty (no geometry extracted)
    [[nodiscard]] std::optional<ReconstructionResult> Reconstruct(
        const std::vector<glm::vec3>& points,
        const std::vector<glm::vec3>& normals = {},
        const ReconstructionParams& params = {});

} // namespace Geometry::SurfaceReconstruction
