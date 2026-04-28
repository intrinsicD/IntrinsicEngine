module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
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
        };

        // 3x3 symmetric eigendecomposition (Cardano's method).
        // Eigenvalues sorted descending, eigenvectors orthonormalized.
        // Same algorithm used throughout the geometry kernel (PCA, NormalEstimation).
        struct SymEigen3Result
        {
            double Values[3]{};        // sorted descending
            glm::dvec3 Vectors[3]{};   // corresponding eigenvectors
        };

        [[nodiscard]] SymEigen3Result SymEigen3(
            double a00, double a01, double a02,
            double a11, double a12, double a22)
        {
            SymEigen3Result result{};

            const double mean = (a00 + a11 + a22) / 3.0;
            const double b00 = a00 - mean;
            const double b11 = a11 - mean;
            const double b22 = a22 - mean;
            const double p2 = (b00 * b00 + b11 * b11 + b22 * b22
                              + 2.0 * (a01 * a01 + a02 * a02 + a12 * a12)) / 6.0;
            const double scale = std::max({std::abs(a00), std::abs(a11), std::abs(a22),
                                            std::abs(a01), std::abs(a02), std::abs(a12), 1.0});
            if (!(p2 > std::numeric_limits<double>::epsilon() * scale * scale))
            {
                result.Values[0] = result.Values[1] = result.Values[2] = mean;
                result.Vectors[0] = {1.0, 0.0, 0.0};
                result.Vectors[1] = {0.0, 1.0, 0.0};
                result.Vectors[2] = {0.0, 0.0, 1.0};
                return result;
            }

            const double p = std::sqrt(p2);
            const double invP = 1.0 / p;
            const double c00 = b00 * invP, c01 = a01 * invP, c02 = a02 * invP;
            const double c11 = b11 * invP, c12 = a12 * invP, c22 = b22 * invP;
            const double detC = c00 * c11 * c22 + 2.0 * c01 * c02 * c12
                              - c00 * c12 * c12 - c11 * c02 * c02 - c22 * c01 * c01;
            const double phi = std::acos(std::clamp(detC * 0.5, -1.0, 1.0)) / 3.0;

            double lambda0 = mean + 2.0 * p * std::cos(phi);
            double lambda2 = mean + 2.0 * p * std::cos(phi + 2.0 * std::acos(-1.0) / 3.0);
            double lambda1 = 3.0 * mean - lambda0 - lambda2;

            // Sort descending
            if (lambda0 < lambda1) std::swap(lambda0, lambda1);
            if (lambda0 < lambda2) std::swap(lambda0, lambda2);
            if (lambda1 < lambda2) std::swap(lambda1, lambda2);

            result.Values[0] = lambda0;
            result.Values[1] = lambda1;
            result.Values[2] = lambda2;

            auto computeEigenvector = [&](double lambda) -> glm::dvec3
            {
                const glm::dvec3 row0{a00 - lambda, a01, a02};
                const glm::dvec3 row1{a01, a11 - lambda, a12};
                const glm::dvec3 row2{a02, a12, a22 - lambda};

                const glm::dvec3 cross01 = glm::cross(row0, row1);
                const glm::dvec3 cross02 = glm::cross(row0, row2);
                const glm::dvec3 cross12 = glm::cross(row1, row2);

                const double lenSq01 = glm::dot(cross01, cross01);
                const double lenSq02 = glm::dot(cross02, cross02);
                const double lenSq12 = glm::dot(cross12, cross12);

                glm::dvec3 best{1.0, 0.0, 0.0};
                double bestLenSq = 0.0;
                if (lenSq01 >= lenSq02 && lenSq01 >= lenSq12)
                { best = cross01; bestLenSq = lenSq01; }
                else if (lenSq02 >= lenSq12)
                { best = cross02; bestLenSq = lenSq02; }
                else
                { best = cross12; bestLenSq = lenSq12; }

                if (bestLenSq > 1e-30)
                    return best / std::sqrt(bestLenSq);
                return {1.0, 0.0, 0.0};
            };

            result.Vectors[0] = computeEigenvector(lambda0);
            result.Vectors[1] = computeEigenvector(lambda1);
            result.Vectors[2] = computeEigenvector(lambda2);

            // Gram-Schmidt orthogonalization
            const double dot01 = glm::dot(result.Vectors[0], result.Vectors[1]);
            result.Vectors[1] -= dot01 * result.Vectors[0];
            const double len1 = glm::length(result.Vectors[1]);
            if (len1 > 1e-15) result.Vectors[1] /= len1;

            result.Vectors[2] = glm::cross(result.Vectors[0], result.Vectors[1]);
            const double len2 = glm::length(result.Vectors[2]);
            if (len2 > 1e-15) result.Vectors[2] /= len2;

            return result;
        }

        // Apply a 4x4 rigid transform to a vec3 point.
        [[nodiscard]] glm::dvec3 TransformPoint(const glm::dmat4& T, const glm::vec3& p)
        {
            const glm::dvec4 result = T * glm::dvec4(p.x, p.y, p.z, 1.0);
            return glm::dvec3(result.x, result.y, result.z);
        }

        // Find closest-point correspondences using KDTree.
        // Returns sorted correspondence pairs with distances.
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
                    outPairs.push_back({i, targetIdx, distSq});
            }
        }

        // Reject outliers: keep only the closest inlierRatio fraction of pairs.
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

        // Compute RMSE from correspondence pairs.
        [[nodiscard]] double ComputeRMSE(const std::vector<CorrespondencePair>& pairs)
        {
            if (pairs.empty())
                return 0.0;

            double sumSq = 0.0;
            for (const auto& p : pairs)
                sumSq += p.DistanceSq;

            return std::sqrt(sumSq / static_cast<double>(pairs.size()));
        }

        // =====================================================================
        // Point-to-Point: SVD-based rigid alignment (Arun et al. 1987)
        // =====================================================================
        //
        // Given corresponding point pairs (s_i, t_i), find R, t minimizing:
        //   sum_i ||R * s_i + t - t_i||^2
        //
        // Solution:
        //   1. Compute centroids of both sets.
        //   2. Center the points: s'_i = s_i - centroid_s, t'_i = t_i - centroid_t
        //   3. Compute cross-covariance H = sum_i s'_i * t'_i^T
        //   4. SVD of H = U * S * V^T
        //   5. R = V * U^T (with det correction for reflections)
        //   6. t = centroid_t - R * centroid_s

        [[nodiscard]] glm::dmat4 SolvePointToPoint(
            const std::vector<CorrespondencePair>& pairs,
            const std::vector<glm::dvec3>& transformedSource,
            std::span<const glm::vec3> targetPoints)
        {
            if (pairs.size() < 3)
                return glm::dmat4(1.0);

            const double invN = 1.0 / static_cast<double>(pairs.size());

            // Compute centroids
            glm::dvec3 centroidS(0.0);
            glm::dvec3 centroidT(0.0);
            for (const auto& pair : pairs)
            {
                centroidS += transformedSource[pair.SourceIndex];
                centroidT += glm::dvec3(targetPoints[pair.TargetIndex]);
            }
            centroidS *= invN;
            centroidT *= invN;

            // Build 3x3 cross-covariance matrix H = sum (s'_i)(t'_i)^T
            // H[row][col] = sum s'[row] * t'[col]
            double h00 = 0, h01 = 0, h02 = 0;
            double h10 = 0, h11 = 0, h12 = 0;
            double h20 = 0, h21 = 0, h22 = 0;

            for (const auto& pair : pairs)
            {
                const glm::dvec3 s = transformedSource[pair.SourceIndex] - centroidS;
                const glm::dvec3 t = glm::dvec3(targetPoints[pair.TargetIndex]) - centroidT;

                h00 += s.x * t.x; h01 += s.x * t.y; h02 += s.x * t.z;
                h10 += s.y * t.x; h11 += s.y * t.y; h12 += s.y * t.z;
                h20 += s.z * t.x; h21 += s.z * t.y; h22 += s.z * t.z;
            }

            // SVD of H via eigendecomposition of H^T * H (3x3 symmetric).
            // H = U * S * V^T  =>  H^T * H = V * S^2 * V^T

            // Compute H^T * H
            const double g00 = h00*h00 + h10*h10 + h20*h20;
            const double g01 = h00*h01 + h10*h11 + h20*h21;
            const double g02 = h00*h02 + h10*h12 + h20*h22;
            const double g11 = h01*h01 + h11*h11 + h21*h21;
            const double g12 = h01*h02 + h11*h12 + h21*h22;
            const double g22 = h02*h02 + h12*h12 + h22*h22;

            // Inline 3x3 symmetric eigendecomposition (Cardano's method)
            auto eigenResult = SymEigen3(g00, g01, g02, g11, g12, g22);

            // Compute singular values (sqrt of eigenvalues of H^T * H)
            double sigma[3];
            for (int i = 0; i < 3; ++i)
                sigma[i] = std::sqrt(std::max(0.0, eigenResult.Values[i]));

            // V = eigenvectors of H^T * H (sorted descending)
            glm::dvec3 V[3], U[3];
            for (int i = 0; i < 3; ++i)
                V[i] = eigenResult.Vectors[i];

            // Compute U columns: U_i = H * V_i / sigma_i
            // For degenerate singular values, complete orthogonally.
            for (int i = 0; i < 3; ++i)
            {
                if (sigma[i] > 1e-15)
                {
                    const glm::dvec3& v = V[i];
                    U[i] = glm::dvec3(
                        h00 * v.x + h01 * v.y + h02 * v.z,
                        h10 * v.x + h11 * v.y + h12 * v.z,
                        h20 * v.x + h21 * v.y + h22 * v.z) / sigma[i];
                }
                else
                {
                    // Degenerate: orthogonal completion via cross products
                    if (i == 0)
                    {
                        U[i] = glm::dvec3(1.0, 0.0, 0.0);
                    }
                    else if (i == 1)
                    {
                        // Find a vector orthogonal to U[0]
                        const glm::dvec3 candidate = (std::abs(U[0].x) < 0.9)
                            ? glm::dvec3(1, 0, 0) : glm::dvec3(0, 1, 0);
                        U[i] = glm::normalize(glm::cross(U[0], candidate));
                    }
                    else
                    {
                        U[i] = glm::cross(U[0], U[1]);
                    }
                }
            }

            // Rotation: R = V * D * U^T where D = diag(1, 1, det(V)*det(U))
            // corrects for reflections when det(H) < 0 (Umeyama 1991).

            // Reflection correction
            auto det3 = [](const glm::dvec3& c0, const glm::dvec3& c1, const glm::dvec3& c2) -> double
            {
                return glm::dot(c0, glm::cross(c1, c2));
            };

            const double detU = det3(U[0], U[1], U[2]);
            const double detV = det3(V[0], V[1], V[2]);
            const double d = (detU * detV < 0.0) ? -1.0 : 1.0;

            // R = V * D * U^T = sum_i d_i * outer(V[i], U[i])
            glm::dmat3 R(0.0);
            for (int i = 0; i < 3; ++i)
            {
                const double sign = (i == 2) ? d : 1.0;
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 3; ++col)
                        R[col][row] += sign * V[i][row] * U[i][col];
            }

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

            for (const auto& pair : pairs)
            {
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
                        ATA[i][j] += a[i] * a[j];
                    ATb[i] += a[i] * b;
                }
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
        const double maxDistSq = params.MaxCorrespondenceDistance * params.MaxCorrespondenceDistance;

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
            // 1. Find correspondences
            FindCorrespondences(sourcePoints, result.Transform, targetTree,
                                maxDistSq, pairs, transformedSourceCache);

            if (pairs.size() < 3)
                break;

            // 2. Reject outliers
            RejectOutliers(pairs, params.InlierRatio);

            if (pairs.size() < 3)
                break;

            // 3. Compute RMSE before solving
            const double rmse = ComputeRMSE(pairs);
            result.RMSEHistory.push_back(rmse);

            // 4. Solve for incremental transform
            glm::dmat4 increment;
            if (effectiveVariant == ICPVariant::PointToPlane)
            {
                increment = SolvePointToPlane(pairs, transformedSourceCache,
                                               targetPoints, targetNormals);
            }
            else
            {
                increment = SolvePointToPoint(pairs, transformedSourceCache,
                                               targetPoints);
            }

            // 5. Update cumulative transform
            result.Transform = increment * result.Transform;

            // 6. Check convergence
            const double relChange = (prevRMSE > 1e-15)
                ? std::abs(prevRMSE - rmse) / prevRMSE
                : std::abs(prevRMSE - rmse);

            result.IterationsPerformed = iter + 1;
            result.FinalRMSE = rmse;
            result.FinalInlierCount = pairs.size();

            if (relChange < params.ConvergenceThreshold && iter > 0)
            {
                result.Converged = true;
                break;
            }

            prevRMSE = rmse;
        }

        return result;
    }

} // namespace Geometry::Registration
