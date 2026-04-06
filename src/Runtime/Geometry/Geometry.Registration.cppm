module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

export module Geometry.Registration;

export namespace Geometry::Registration
{
    // =========================================================================
    // Point Cloud Registration — Iterative Closest Point (ICP)
    // =========================================================================
    //
    // Aligns a source point cloud to a target point cloud by iteratively
    // minimizing the distance between corresponding point pairs.
    //
    // Two variants are provided:
    //
    //   1. **Point-to-Point ICP** (Besl & McKay 1992):
    //      Minimizes sum_i ||R * s_i + t - c_i||^2 where c_i is the nearest
    //      target point. Uses SVD-based closed-form rigid alignment per
    //      iteration. Simple, robust, but converges slowly on smooth surfaces.
    //
    //   2. **Point-to-Plane ICP** (Chen & Medioni 1992):
    //      Minimizes sum_i ((R * s_i + t - c_i) . n_i)^2 where n_i is the
    //      target surface normal at c_i. Converges much faster on smooth
    //      surfaces (typically 5-10x fewer iterations). Requires target normals.
    //      Uses linearized rotation (small-angle approximation) solved via
    //      normal equations on a 6x6 system per iteration.
    //
    // Both variants support:
    //   - Maximum correspondence distance (reject pairs farther than threshold)
    //   - Outlier rejection by percentile (keep only the closest N% of pairs)
    //   - Convergence detection (RMSE change below threshold)
    //
    // Algorithm (per iteration):
    //   1. Transform source points by current estimate.
    //   2. Find nearest target point for each transformed source point (KDTree).
    //   3. Reject outlier pairs (distance threshold + percentile).
    //   4. Solve for incremental rigid transform (SVD or linear system).
    //   5. Update cumulative transform. Check convergence.
    //
    // References:
    //   - Besl & McKay, "A Method for Registration of 3-D Shapes" (PAMI 1992)
    //   - Chen & Medioni, "Object Modelling by Registration of Multiple Range
    //     Images" (Image & Vision Computing 1992)
    //   - Rusinkiewicz & Levoy, "Efficient Variants of the ICP Algorithm"
    //     (3DIM 2001)

    // -------------------------------------------------------------------------
    // ICP Variant
    // -------------------------------------------------------------------------

    enum class ICPVariant : uint8_t
    {
        PointToPoint,  // SVD-based (Besl & McKay 1992)
        PointToPlane,  // Linearized normal equations (Chen & Medioni 1992)
    };

    // -------------------------------------------------------------------------
    // Parameters
    // -------------------------------------------------------------------------

    struct RegistrationParams
    {
        // ICP variant selection.
        ICPVariant Variant{ICPVariant::PointToPlane};

        // Maximum number of ICP iterations.
        std::size_t MaxIterations{50};

        // Convergence threshold: stop when relative RMSE change < threshold.
        double ConvergenceThreshold{1e-6};

        // Maximum correspondence distance. Pairs farther than this are rejected.
        // Set to a large value to disable. In world-space units.
        double MaxCorrespondenceDistance{1e6};

        // Outlier rejection ratio: keep only this fraction of closest pairs.
        // 1.0 = keep all, 0.9 = reject worst 10%. Range: (0, 1].
        double InlierRatio{0.9};

        // KDTree build parameters for the target cloud.
        std::size_t KDTreeLeafSize{16};
    };

    // -------------------------------------------------------------------------
    // Result
    // -------------------------------------------------------------------------

    struct RegistrationResult
    {
        // Rigid transform (4x4 homogeneous) that aligns source to target.
        // Apply as: aligned_point = Transform * vec4(source_point, 1.0)
        glm::dmat4 Transform{1.0};

        // Final root mean square error of inlier correspondences.
        double FinalRMSE{0.0};

        // RMSE at each iteration (for convergence analysis).
        std::vector<double> RMSEHistory{};

        // Number of ICP iterations performed.
        std::size_t IterationsPerformed{0};

        // Whether the algorithm converged before reaching MaxIterations.
        bool Converged{false};

        // Number of inlier correspondences in the final iteration.
        std::size_t FinalInlierCount{0};
    };

    // -------------------------------------------------------------------------
    // ICP Alignment
    // -------------------------------------------------------------------------
    //
    // Align source points to target points via ICP.
    //
    // For PointToPlane variant, targetNormals must be provided and must have
    // the same size as targetPoints. If targetNormals is empty with PointToPlane,
    // the algorithm falls back to PointToPoint.
    //
    // Returns nullopt if:
    //   - Either point set has fewer than 3 points
    //   - InlierRatio is not in (0, 1]
    //   - MaxIterations is 0
    [[nodiscard]] std::optional<RegistrationResult> AlignICP(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        std::span<const glm::vec3> targetNormals = {},
        const RegistrationParams& params = {});

} // namespace Geometry::Registration
