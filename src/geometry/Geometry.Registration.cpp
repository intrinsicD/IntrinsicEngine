module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>

module Geometry.Registration;

import Geometry.KDTree;
import Geometry.Robust;
import Geometry.Rotation;

namespace Geometry::Registration
{
    // =========================================================================
    // Internal Helpers
    // =========================================================================

    namespace
    {
        struct CorrespondencePair
        {
            std::size_t SourceIndex;
            std::size_t TargetIndex;
            double DistanceSq;
            double Weight{1.0};
        };

        // Apply a 4x4 rigid transform to a vec3 point.
        [[nodiscard]] glm::dvec3 TransformPoint(const glm::dmat4& T, const glm::vec3& p)
        {
            const glm::dvec4 result = T * glm::dvec4(p.x, p.y, p.z, 1.0);
            return glm::dvec3(result.x, result.y, result.z);
        }

        // Reference interface #1 — correspondence estimator (see
        // docs/architecture/geometry-pipeline-modularity.md §2). Currently a
        // fixed KDTree k=1 nearest search; a later slice makes this swappable via
        // a CorrespondenceKind axis. Returns correspondence pairs with distances.
        void FindCorrespondences(
            std::span<const glm::vec3> sourcePoints,
            const glm::dmat4& currentTransform,
            const KDTree& targetTree,
            double maxDistSq,
            std::vector<CorrespondencePair>& outPairs,
            std::vector<glm::dvec3>& transformedSourceCache)
        {
            outPairs.clear();
            const std::size_t n = sourcePoints.size();
            transformedSourceCache.resize(n);

            // Transform all source points by current estimate
            for (std::size_t i = 0; i < n; ++i)
                transformedSourceCache[i] = TransformPoint(currentTransform, sourcePoints[i]);

            std::vector<KDTree::ElementIndex> neighbors;

            for (std::size_t i = 0; i < n; ++i)
            {
                const glm::vec3 query(
                    static_cast<float>(transformedSourceCache[i].x),
                    static_cast<float>(transformedSourceCache[i].y),
                    static_cast<float>(transformedSourceCache[i].z));

                auto knnResult = targetTree.QueryKNN(query, 1, neighbors);
                if (!knnResult || neighbors.empty())
                    continue;

                const std::size_t targetIdx = neighbors[0];
                const auto& targetAabbs = targetTree.ElementAabbs();
                const glm::dvec3 targetPt(
                    (targetAabbs[targetIdx].Min.x + targetAabbs[targetIdx].Max.x) * 0.5,
                    (targetAabbs[targetIdx].Min.y + targetAabbs[targetIdx].Max.y) * 0.5,
                    (targetAabbs[targetIdx].Min.z + targetAabbs[targetIdx].Max.z) * 0.5);

                const glm::dvec3 diff = transformedSourceCache[i] - targetPt;
                const double distSq = glm::dot(diff, diff);

                if (distSq <= maxDistSq)
                    outPairs.push_back({i, targetIdx, distSq, 1.0});
            }
        }

        // Reference interface #2 — correspondence rejector (hard cut). Keeps only
        // the closest inlierRatio fraction of pairs; a later slice generalizes
        // this to a composable RejectorChain.
        void RejectOutliers(std::vector<CorrespondencePair>& pairs, double inlierRatio)
        {
            if (pairs.empty() || inlierRatio >= 1.0)
                return;

            const std::size_t keepCount = std::max(
                std::size_t{3},
                static_cast<std::size_t>(std::ceil(pairs.size() * inlierRatio)));

            if (keepCount >= pairs.size())
                return;

            // Partial sort to find the threshold distance
            std::nth_element(pairs.begin(), pairs.begin() + static_cast<std::ptrdiff_t>(keepCount),
                             pairs.end(),
                             [](const auto& a, const auto& b) { return a.DistanceSq < b.DistanceSq; });

            pairs.resize(keepCount);
        }

        // Reference interface #4 — robust kernel (soft IRLS weighting, orthogonal
        // to the rejector). Applies per-residual weights to the surviving
        // correspondences.
        void ApplyRobustWeights(std::vector<CorrespondencePair>& pairs,
                                Geometry::Robust::RobustKernel kernel,
                                double scale)
        {
            for (auto& pair : pairs)
            {
                const double residual = std::sqrt(std::max(0.0, pair.DistanceSq));
                const std::optional<double> weight = Geometry::Robust::Weight(kernel, residual, scale);
                pair.Weight = (weight && std::isfinite(*weight) && *weight > 0.0) ? *weight : 0.0;
            }

            std::erase_if(pairs,
                          [](const CorrespondencePair& pair)
                          {
                              return !std::isfinite(pair.Weight) || !(pair.Weight > 0.0);
                          });
        }

        // Compute RMSE from correspondence pairs. Robust ICP reports the
        // weighted objective so convergence follows the IRLS energy; the default
        // path keeps the historical unweighted RMSE.
        [[nodiscard]] double ComputeRMSE(const std::vector<CorrespondencePair>& pairs, bool weighted)
        {
            if (pairs.empty())
                return 0.0;

            double sumSq = 0.0;
            double sumWeight = 0.0;
            for (const auto& p : pairs)
            {
                const double w = weighted ? p.Weight : 1.0;
                if (!std::isfinite(w) || !(w > 0.0))
                {
                    continue;
                }
                sumSq += w * p.DistanceSq;
                sumWeight += w;
            }

            if (!(sumWeight > 0.0))
            {
                return 0.0;
            }
            return std::sqrt(sumSq / sumWeight);
        }

        // =====================================================================
        // Point-to-Point: rigid alignment (Arun et al. 1987)
        // =====================================================================
        //
        // Given corresponding point pairs (s_i, t_i), find R, t minimizing:
        //   sum_i ||R * s_i + t - t_i||^2
        //
        // Solution:
        //   1. Compute centroids of both sets.
        //   2. Center the points: s'_i = s_i - centroid_s, t'_i = t_i - centroid_t
        //   3. Solve the Kabsch/Umeyama optimal rotation through Geometry.Rotation.
        //   4. t = centroid_t - R * centroid_s

        [[nodiscard]] glm::dmat4 SolvePointToPoint(
            const std::vector<CorrespondencePair>& pairs,
            const std::vector<glm::dvec3>& transformedSource,
            std::span<const glm::vec3> targetPoints)
        {
            if (pairs.size() < 3)
                return glm::dmat4(1.0);

            glm::dvec3 centroidS(0.0);
            glm::dvec3 centroidT(0.0);
            double totalWeight = 0.0;
            std::size_t validCount = 0;
            for (const auto& pair : pairs)
            {
                if (!std::isfinite(pair.Weight) || !(pair.Weight > 0.0))
                {
                    continue;
                }
                centroidS += pair.Weight * transformedSource[pair.SourceIndex];
                centroidT += pair.Weight * glm::dvec3(targetPoints[pair.TargetIndex]);
                totalWeight += pair.Weight;
                ++validCount;
            }
            if (validCount < 3 || !(totalWeight > 0.0))
            {
                return glm::dmat4(1.0);
            }
            centroidS /= totalWeight;
            centroidT /= totalWeight;

            std::vector<glm::dvec3> sourceCentered;
            std::vector<glm::dvec3> targetCentered;
            std::vector<double> weights;
            sourceCentered.reserve(pairs.size());
            targetCentered.reserve(pairs.size());
            weights.reserve(pairs.size());
            for (const auto& pair : pairs)
            {
                if (!std::isfinite(pair.Weight) || !(pair.Weight > 0.0))
                {
                    continue;
                }
                sourceCentered.push_back(transformedSource[pair.SourceIndex] - centroidS);
                targetCentered.push_back(glm::dvec3(targetPoints[pair.TargetIndex]) - centroidT);
                weights.push_back(pair.Weight);
            }

            const glm::dmat3 R = Geometry::Rotation::OptimalRotation(sourceCentered, targetCentered, weights);

            // Translation
            const glm::dvec3 t = centroidT - R * centroidS;

            // Build 4x4 homogeneous transform
            glm::dmat4 result(1.0);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    result[col][row] = R[col][row];
            result[3][0] = t.x;
            result[3][1] = t.y;
            result[3][2] = t.z;

            return result;
        }

        // =====================================================================
        // Point-to-Plane: Linearized 6-DOF solve (Chen & Medioni 1992)
        // =====================================================================
        //
        // Minimize sum_i ((R*s_i + t - c_i) . n_i)^2
        //
        // Linearize rotation (small angle): R ≈ I + [α]_x where [α]_x is the
        // skew-symmetric matrix from rotation vector α = (rx, ry, rz).
        //
        // This gives a 6x6 linear system A^T A x = A^T b where:
        //   x = (rx, ry, rz, tx, ty, tz)
        //   Each row of A: [(s_i × n_i), n_i]
        //   Each element of b: (c_i - s_i) . n_i

        [[nodiscard]] glm::dmat4 SolvePointToPlane(
            const std::vector<CorrespondencePair>& pairs,
            const std::vector<glm::dvec3>& transformedSource,
            std::span<const glm::vec3> targetPoints,
            std::span<const glm::vec3> targetNormals)
        {
            if (pairs.size() < 6) // Need at least 6 pairs for a 6-DOF system
                return glm::dmat4(1.0);

            // Build normal equations: A^T A x = A^T b  (6x6 symmetric system)
            double ATA[6][6] = {};
            double ATb[6] = {};
            std::size_t validRows = 0;

            for (const auto& pair : pairs)
            {
                if (!std::isfinite(pair.Weight) || !(pair.Weight > 0.0))
                {
                    continue;
                }
                const glm::dvec3 s = transformedSource[pair.SourceIndex];
                const glm::dvec3 c = glm::dvec3(targetPoints[pair.TargetIndex]);
                const glm::dvec3 n = glm::dvec3(targetNormals[pair.TargetIndex]);

                // Validate normal
                const double nLen = glm::length(n);
                if (nLen < 1e-8)
                    continue;

                // Row of A: [s × n, n]
                const glm::dvec3 sxn = glm::cross(s, n);
                const double a[6] = {sxn.x, sxn.y, sxn.z, n.x, n.y, n.z};

                // Element of b: (c - s) . n
                const double b = glm::dot(c - s, n);

                // Accumulate A^T A (symmetric, only upper triangle needed)
                for (int i = 0; i < 6; ++i)
                {
                    for (int j = i; j < 6; ++j)
                        ATA[i][j] += pair.Weight * a[i] * a[j];
                    ATb[i] += pair.Weight * a[i] * b;
                }
                ++validRows;
            }

            if (validRows < 6)
            {
                return glm::dmat4(1.0);
            }

            // Fill lower triangle
            for (int i = 0; i < 6; ++i)
                for (int j = 0; j < i; ++j)
                    ATA[i][j] = ATA[j][i];

            // Solve 6x6 system via Cholesky decomposition (A^T A is SPD if
            // we have sufficient non-degenerate correspondences).
            // L * L^T * x = ATb

            double L[6][6] = {};
            for (int i = 0; i < 6; ++i)
            {
                for (int j = 0; j <= i; ++j)
                {
                    double sum = ATA[i][j];
                    for (int k = 0; k < j; ++k)
                        sum -= L[i][k] * L[j][k];

                    if (i == j)
                    {
                        if (sum <= 0.0)
                            return glm::dmat4(1.0); // Not positive definite
                        L[i][j] = std::sqrt(sum);
                    }
                    else
                    {
                        L[i][j] = sum / L[j][j];
                    }
                }
            }

            // Forward substitution: L * y = ATb
            double y[6] = {};
            for (int i = 0; i < 6; ++i)
            {
                double sum = ATb[i];
                for (int j = 0; j < i; ++j)
                    sum -= L[i][j] * y[j];
                y[i] = sum / L[i][i];
            }

            // Back substitution: L^T * x = y
            double x[6] = {};
            for (int i = 5; i >= 0; --i)
            {
                double sum = y[i];
                for (int j = i + 1; j < 6; ++j)
                    sum -= L[j][i] * x[j];
                x[i] = sum / L[i][i];
            }

            // Extract rotation (small-angle) and translation
            const double rx = x[0], ry = x[1], rz = x[2];
            const double tx = x[3], ty = x[4], tz = x[5];

            // Build rotation matrix from small angles using Rodrigues' formula
            // for better accuracy than the raw skew-symmetric approximation.
            const double angle = std::sqrt(rx * rx + ry * ry + rz * rz);

            glm::dmat3 R(1.0);
            if (angle > 1e-15)
            {
                const glm::dvec3 axis(rx / angle, ry / angle, rz / angle);
                const double c = std::cos(angle);
                const double s = std::sin(angle);
                const double omc = 1.0 - c;

                R[0][0] = c + axis.x * axis.x * omc;
                R[1][0] = axis.x * axis.y * omc - axis.z * s;
                R[2][0] = axis.x * axis.z * omc + axis.y * s;
                R[0][1] = axis.y * axis.x * omc + axis.z * s;
                R[1][1] = c + axis.y * axis.y * omc;
                R[2][1] = axis.y * axis.z * omc - axis.x * s;
                R[0][2] = axis.z * axis.x * omc - axis.y * s;
                R[1][2] = axis.z * axis.y * omc + axis.x * s;
                R[2][2] = c + axis.z * axis.z * omc;
            }

            glm::dmat4 result(1.0);
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 3; ++col)
                    result[col][row] = R[col][row];
            result[3][0] = tx;
            result[3][1] = ty;
            result[3][2] = tz;

            return result;
        }

        // =====================================================================
        // Reference interface #3 — transformation estimator dispatch.
        // =====================================================================
        //
        // Selects the per-iteration incremental solve. A later slice replaces this
        // ICPVariant branch with a TransformKind axis (adding e.g. symmetric
        // point-to-plane) following the Algorithm-Variant-Dispatch idiom. Behavior
        // is identical to the historical inline if/else.
        [[nodiscard]] glm::dmat4 SolveIncrement(
            ICPVariant variant,
            const std::vector<CorrespondencePair>& pairs,
            const std::vector<glm::dvec3>& transformedSource,
            std::span<const glm::vec3> targetPoints,
            std::span<const glm::vec3> targetNormals)
        {
            if (variant == ICPVariant::PointToPlane)
            {
                return SolvePointToPlane(pairs, transformedSource, targetPoints, targetNormals);
            }
            return SolvePointToPoint(pairs, transformedSource, targetPoints);
        }

        // =====================================================================
        // Reference interface #5 — convergence criteria.
        // =====================================================================
        //
        // Relative-RMSE stopping test, identical to the historical inline check
        // (guarded by iter > 0). A later slice adds the oscillation /
        // similar-transform guard and promotes the criteria to a public
        // ConvergenceCriteria struct.
        [[nodiscard]] bool EvaluateConvergence(
            double prevRMSE, double rmse, double threshold, std::size_t iter)
        {
            const double relChange = (prevRMSE > 1e-15)
                ? std::abs(prevRMSE - rmse) / prevRMSE
                : std::abs(prevRMSE - rmse);
            return relChange < threshold && iter > 0;
        }

        // =====================================================================
        // ICP loop driver — runs the named stage sequence per iteration.
        // =====================================================================
        //
        // Pure CPU reference path (geometry -> core only). Each iteration runs, in
        // order:
        //   #1 correspondence  (FindCorrespondences)
        //   #2 rejection       (RejectOutliers)
        //   #4 robust weights  (ApplyRobustWeights, optional)
        //   #3 transform solve (SolveIncrement)
        //   #5 convergence     (EvaluateConvergence)
        // Behavior is bit-for-bit identical to the historical monolithic loop;
        // this driver only makes the stage boundaries explicit so later slices can
        // make each axis swappable. See
        // docs/architecture/geometry-pipeline-modularity.md.
        [[nodiscard]] RegistrationResult RunIcpLoop(
            std::span<const glm::vec3> sourcePoints,
            std::span<const glm::vec3> targetPoints,
            std::span<const glm::vec3> targetNormals,
            const KDTree& targetTree,
            ICPVariant effectiveVariant,
            double maxDistSq,
            const RegistrationParams& params)
        {
            const bool robustWeightingEnabled = params.RobustKernelKind.has_value();

            RegistrationResult result;
            result.Transform = glm::dmat4(1.0);
            result.RMSEHistory.reserve(params.MaxIterations);

            std::vector<CorrespondencePair> pairs;
            std::vector<glm::dvec3> transformedSourceCache;
            pairs.reserve(sourcePoints.size());
            transformedSourceCache.reserve(sourcePoints.size());

            double prevRMSE = std::numeric_limits<double>::max();

            for (std::size_t iter = 0; iter < params.MaxIterations; ++iter)
            {
                // #1 Correspondence estimation.
                FindCorrespondences(sourcePoints, result.Transform, targetTree,
                                    maxDistSq, pairs, transformedSourceCache);

                if (pairs.size() < 3)
                    break;

                // #2 Outlier rejection (+ #4 optional robust weighting).
                RejectOutliers(pairs, params.InlierRatio);

                if (robustWeightingEnabled)
                {
                    ApplyRobustWeights(pairs, *params.RobustKernelKind, params.RobustScale);
                }

                if (pairs.size() < 3)
                    break;

                // Objective before solving (weighted for robust ICP).
                const double rmse = ComputeRMSE(pairs, robustWeightingEnabled);
                result.RMSEHistory.push_back(rmse);

                // #3 Incremental transform solve.
                const glm::dmat4 increment = SolveIncrement(
                    effectiveVariant, pairs, transformedSourceCache,
                    targetPoints, targetNormals);

                // Update cumulative transform.
                result.Transform = increment * result.Transform;

                result.IterationsPerformed = iter + 1;
                result.FinalRMSE = rmse;
                result.FinalInlierCount = pairs.size();

                // #5 Convergence check.
                if (EvaluateConvergence(prevRMSE, rmse, params.ConvergenceThreshold, iter))
                {
                    result.Converged = true;
                    break;
                }

                prevRMSE = rmse;
            }

            return result;
        }

    } // anonymous namespace

    // =========================================================================
    // Public API
    // =========================================================================

    std::optional<RegistrationResult> AlignICP(
        std::span<const glm::vec3> sourcePoints,
        std::span<const glm::vec3> targetPoints,
        std::span<const glm::vec3> targetNormals,
        const RegistrationParams& params)
    {
        // --- Input validation ---
        if (sourcePoints.size() < 3 || targetPoints.size() < 3)
            return std::nullopt;

        if (params.MaxIterations == 0)
            return std::nullopt;

        if (params.InlierRatio <= 0.0 || params.InlierRatio > 1.0)
            return std::nullopt;

        const bool robustWeightingEnabled = params.RobustKernelKind.has_value();
        if (robustWeightingEnabled &&
            (!std::isfinite(params.RobustScale) || !(params.RobustScale > 0.0)))
        {
            return std::nullopt;
        }

        // Determine effective variant: fall back to PointToPoint if normals unavailable
        ICPVariant effectiveVariant = params.Variant;
        if (effectiveVariant == ICPVariant::PointToPlane &&
            (targetNormals.empty() || targetNormals.size() != targetPoints.size()))
        {
            effectiveVariant = ICPVariant::PointToPoint;
        }

        // --- Build KDTree for target points ---
        KDTree targetTree;
        KDTreeBuildParams kdParams;
        kdParams.LeafSize = static_cast<uint32_t>(params.KDTreeLeafSize);
        auto buildResult = targetTree.BuildFromPoints(targetPoints, kdParams);
        if (!buildResult)
            return std::nullopt;

        // --- ICP loop ---
        // The per-iteration stage sequence (correspondence, rejection, robust
        // weighting, transform solve, convergence) lives in RunIcpLoop so the
        // stage boundaries are explicit and independently swappable in later
        // slices. See docs/architecture/geometry-pipeline-modularity.md.
        const double maxDistSq = params.MaxCorrespondenceDistance * params.MaxCorrespondenceDistance;

        return RunIcpLoop(sourcePoints, targetPoints, targetNormals, targetTree,
                          effectiveVariant, maxDistSq, params);
    }

} // namespace Geometry::Registration
