// Rigid ICP with shared CPU solve stages and an optional batched correspondence provider.
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

    // Query coordinates are float3; the solve and residual accumulation use doubles.
    // Return one compact target index per query, or UINT32_MAX for no match.
    using NearestQuery = std::function<bool(std::span<const glm::vec3>,
                                           std::span<std::uint32_t>)>;
    enum class ICPStepStatus : std::uint8_t { Continue, Finished, InvalidInput };

    [[nodiscard]] std::vector<glm::vec3> MakeICPQueries(
        std::span<const glm::vec3> source, const glm::dmat4& transform);
    // Indices must correspond to MakeICPQueries(source, result.Transform).
    // A fresh RegistrationResult starts a run; Finished means no further steps are needed.
    [[nodiscard]] ICPStepStatus AdvanceICP(
        std::span<const glm::vec3> source, std::span<const glm::vec3> target,
        std::span<const glm::vec3> normals, const RegistrationParams& params,
        std::span<const std::uint32_t> indices, RegistrationResult& result,
        const IterationObserver& observer = {});
    [[nodiscard]] std::optional<RegistrationResult> AlignICPWithQueries(
        std::span<const glm::vec3> source, std::span<const glm::vec3> target,
        std::span<const glm::vec3> normals, const RegistrationParams& params,
        const NearestQuery& query, const IterationObserver& observer = {});

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
