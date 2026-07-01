module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>

export module Geometry.Registration;

export import Geometry.Robust;

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
    //   - Optional robust IRLS-style per-residual weights (default off)
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

        // Optional robust weighting for correspondences after percentile
        // trimming. Disengaged by default to preserve the legacy ICP path.
        std::optional<Geometry::Robust::RobustKernel> RobustKernelKind{};

        // Robust residual scale in world-space units. Used only when
        // RobustKernelKind is engaged; must be finite and > 0.
        double RobustScale{1.0};

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
    // Optional iteration observability
    // -------------------------------------------------------------------------
    //
    // See docs/architecture/geometry-pipeline-modularity.md §3.4.
    //
    // Read-only snapshot emitted at the end of each ICP iteration. Observing
    // never mutates solver state, so an observed run and an unobserved run
    // produce identical results. For rigid registration the cumulative Transform
    // fully describes "the shape under the current solution" — apply it to the
    // source (e.g. on the GPU) to visualize convergence with no CPU point work.

    struct IterationTrace
    {
        // 0-based ICP iteration index.
        std::size_t Iteration{0};

        // Cumulative source->target estimate AFTER this iteration's update.
        glm::dmat4 Transform{1.0};

        // Inlier RMSE evaluated this iteration (equals RMSEHistory[Iteration]).
        double RMSE{0.0};

        // Number of inlier correspondences used this iteration.
        std::size_t InlierCount{0};
    };

    // Optional per-iteration callback. Null (the default) means zero overhead:
    // it is checked once per iteration, never per point. It is passed separately
    // from RegistrationParams so the serializable/reproducible config stays a
    // pure value (a std::function is not serializable). The callback must be
    // read-only with respect to solver state.
    using IterationObserver = std::function<void(const IterationTrace&)>;

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
    // If a non-null observer is supplied, it is invoked once at the end of each
    // completed iteration with an IterationTrace snapshot. The observer does not
    // affect the result; a null observer (the default) adds no per-point cost.
    //
    // Returns nullopt if:
    //   - Either point set has fewer than 3 points
    //   - InlierRatio is not in (0, 1]
    //   - RobustKernelKind is set and RobustScale is not finite or <= 0
    //   - MaxIterations is 0
    [[nodiscard]] std::optional<RegistrationResult> AlignICP(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        std::span<const glm::vec3> targetNormals = {},
        const RegistrationParams& params = {},
        const IterationObserver& observer = {});

} // namespace Geometry::Registration
