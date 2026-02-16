module;

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:NormalEstimation;

export namespace Geometry::NormalEstimation
{
    // =========================================================================
    // Point Cloud Normal Estimation
    // =========================================================================
    //
    // Estimates surface normals for unstructured point clouds using PCA-based
    // local plane fitting, with consistent orientation via minimum spanning tree
    // (MST) propagation.
    //
    // Algorithm (Hoppe, DeRose, Duchamp, McDonald, Stuetzle, "Surface
    // Reconstruction from Unorganized Points", 1992):
    //
    //   1. For each point p_i, find its k nearest neighbors using a spatial
    //      index (Octree with KNN queries).
    //
    //   2. Compute the 3x3 covariance matrix of the local neighborhood:
    //        C = (1/k) * sum_j (p_j - centroid)(p_j - centroid)^T
    //
    //   3. The normal at p_i is the eigenvector of C corresponding to the
    //      smallest eigenvalue. For a 3x3 symmetric matrix, eigendecomposition
    //      is computed analytically (closed-form cubic solver, Cardano's method).
    //
    //   4. PCA normals have arbitrary sign (+/-). To establish a consistent
    //      orientation, build a Riemannian graph (k-nearest-neighbor graph)
    //      weighted by (1 - |n_i . n_j|), then compute a minimum spanning
    //      tree (MST) via Prim's algorithm and propagate orientation from a
    //      seed vertex (the one with the largest z-component) by flipping
    //      normals to agree with the MST parent's normal.
    //
    // The method assumes the point cloud samples a smooth surface with locally
    // uniform sampling density. It degrades gracefully on noisy data (larger k
    // improves robustness) and at sharp features (normals become averaged).

    struct EstimationParams
    {
        // Number of nearest neighbors for local plane fitting.
        // Larger k = smoother normals but slower and less sensitive to features.
        // Typical values: 10-30.
        std::size_t KNeighbors{15};

        // Whether to perform consistent orientation via MST propagation.
        // If false, normals may have arbitrary sign.
        bool OrientNormals{true};

        // Octree parameters for spatial indexing.
        std::size_t OctreeMaxPerNode{32};
        std::size_t OctreeMaxDepth{10};
    };

    struct EstimationResult
    {
        // Estimated normals, one per input point. Unit length.
        std::vector<glm::vec3> Normals;

        // Number of points that had degenerate neighborhoods
        // (collinear or too few neighbors for reliable PCA).
        std::size_t DegenerateCount{0};

        // Number of normals flipped during orientation propagation.
        std::size_t FlippedCount{0};
    };

    // -------------------------------------------------------------------------
    // Estimate normals for a point cloud
    // -------------------------------------------------------------------------
    //
    // Input: vector of 3D point positions.
    // Returns normals aligned with the estimated local surface orientation.
    //
    // Returns nullopt if:
    //   - Points is empty
    //   - Fewer than 3 points (cannot define a plane)
    [[nodiscard]] std::optional<EstimationResult> EstimateNormals(
        const std::vector<glm::vec3>& points,
        const EstimationParams& params = {});

} // namespace Geometry::NormalEstimation
