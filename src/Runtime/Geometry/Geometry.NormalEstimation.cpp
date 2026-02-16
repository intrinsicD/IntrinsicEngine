module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <queue>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:NormalEstimation.Impl;

import :NormalEstimation;
import :AABB;
import :Octree;
import :Primitives;

namespace Geometry::NormalEstimation
{
    // =========================================================================
    // 3x3 Symmetric Eigendecomposition (Analytical)
    // =========================================================================
    //
    // Computes eigenvalues and eigenvectors of a 3x3 symmetric matrix using
    // Cardano's method for the characteristic polynomial, followed by
    // eigenvector extraction.
    //
    // The covariance matrix is always real symmetric positive semi-definite,
    // so all eigenvalues are real and non-negative.

    struct Eigen3
    {
        glm::dvec3 Eigenvalues;     // sorted ascending: λ0 <= λ1 <= λ2
        glm::dvec3 Eigenvectors[3]; // corresponding unit eigenvectors
    };

    // Compute eigenvalues of 3x3 symmetric matrix using Cardano's formula
    // Matrix stored as: [a00 a01 a02; a01 a11 a12; a02 a12 a22]
    static Eigen3 SymmetricEigen3(double a00, double a01, double a02,
                                   double a11, double a12, double a22)
    {
        Eigen3 result{};

        // Shift matrix by trace/3 for numerical stability (Smith's method)
        const double c0 = a00 * a11 * a22 + 2.0 * a01 * a02 * a12
                          - a00 * a12 * a12 - a11 * a02 * a02 - a22 * a01 * a01;
        const double c1 = a00 * a11 - a01 * a01 + a00 * a22 - a02 * a02 + a11 * a22 - a12 * a12;
        const double c2 = a00 + a11 + a22;

        // Shift: solve t^3 - c2*t^2 + c1*t - c0 = 0
        // Substitution: t = s + c2/3
        const double c2over3 = c2 / 3.0;
        const double aVal = c1 - c2 * c2over3;        // = a' coefficient
        const double bVal = c0 - c1 * c2over3 + 2.0 * c2over3 * c2over3 * c2over3; // = b' coefficient

        const double halfB = bVal / 2.0;
        const double q = halfB * halfB + (aVal / 3.0) * (aVal / 3.0) * (aVal / 3.0);

        double lambda0, lambda1, lambda2;

        if (q <= 0.0)
        {
            // Three real roots (typical for covariance matrices)
            const double sqrtMinusA3 = std::sqrt(std::max(0.0, -aVal / 3.0));
            const double r = sqrtMinusA3 * sqrtMinusA3 * sqrtMinusA3;
            double theta = 0.0;
            if (r > 1e-30)
                theta = std::acos(std::clamp(-halfB / r, -1.0, 1.0)) / 3.0;

            const double twoSqrt = 2.0 * sqrtMinusA3;
            lambda0 = c2over3 + twoSqrt * std::cos(theta + 2.0 * std::numbers::pi / 3.0);
            lambda1 = c2over3 + twoSqrt * std::cos(theta + 4.0 * std::numbers::pi / 3.0);
            lambda2 = c2over3 + twoSqrt * std::cos(theta);
        }
        else
        {
            // Degenerate case: use direct formula
            const double sqrtQ = std::sqrt(q);
            const double u = std::cbrt(-halfB + sqrtQ);
            const double v = std::cbrt(-halfB - sqrtQ);
            lambda0 = lambda1 = lambda2 = c2over3 + u + v;
        }

        // Sort eigenvalues ascending
        if (lambda0 > lambda1) std::swap(lambda0, lambda1);
        if (lambda1 > lambda2) std::swap(lambda1, lambda2);
        if (lambda0 > lambda1) std::swap(lambda0, lambda1);

        result.Eigenvalues = {lambda0, lambda1, lambda2};

        // Compute eigenvectors by solving (A - λI)x = 0
        // For each eigenvalue, find the null space of (A - λI)
        auto computeEigenvector = [&](double lambda) -> glm::dvec3
        {
            // (A - λI) rows
            double r00 = a00 - lambda, r01 = a01, r02 = a02;
            double r10 = a01, r11 = a11 - lambda, r12 = a12;
            double r20 = a02, r21 = a12, r22 = a22 - lambda;

            // Cross products of row pairs to find null space
            glm::dvec3 row0(r00, r01, r02);
            glm::dvec3 row1(r10, r11, r12);
            glm::dvec3 row2(r20, r21, r22);

            glm::dvec3 c01 = glm::cross(row0, row1);
            glm::dvec3 c02 = glm::cross(row0, row2);
            glm::dvec3 c12 = glm::cross(row1, row2);

            double d01 = glm::dot(c01, c01);
            double d02 = glm::dot(c02, c02);
            double d12 = glm::dot(c12, c12);

            // Pick the cross product with the largest magnitude
            glm::dvec3 best;
            double bestLen;
            if (d01 >= d02 && d01 >= d12)
            {
                best = c01;
                bestLen = d01;
            }
            else if (d02 >= d12)
            {
                best = c02;
                bestLen = d02;
            }
            else
            {
                best = c12;
                bestLen = d12;
            }

            if (bestLen > 1e-30)
                return best / std::sqrt(bestLen);

            // Fallback: matrix is essentially zero
            return {1.0, 0.0, 0.0};
        };

        result.Eigenvectors[0] = computeEigenvector(lambda0);
        result.Eigenvectors[1] = computeEigenvector(lambda1);
        result.Eigenvectors[2] = computeEigenvector(lambda2);

        // Ensure orthogonality via Gram-Schmidt on the first two
        // (eigenvectors of distinct eigenvalues of a symmetric matrix are
        // orthogonal, but numerical error can break this for close eigenvalues)
        double dot01 = glm::dot(result.Eigenvectors[0], result.Eigenvectors[1]);
        result.Eigenvectors[1] -= dot01 * result.Eigenvectors[0];
        double len1 = glm::length(result.Eigenvectors[1]);
        if (len1 > 1e-15)
            result.Eigenvectors[1] /= len1;

        // Third eigenvector via cross product for perfect orthogonality
        result.Eigenvectors[2] = glm::cross(result.Eigenvectors[0], result.Eigenvectors[1]);
        double len2 = glm::length(result.Eigenvectors[2]);
        if (len2 > 1e-15)
            result.Eigenvectors[2] /= len2;

        return result;
    }

    // =========================================================================
    // Prim's MST for consistent normal orientation
    // =========================================================================

    struct KNNEdge
    {
        std::size_t From;
        std::size_t To;
        float Weight;
    };

    static void OrientNormalsMST(
        const std::vector<glm::vec3>& points,
        std::vector<glm::vec3>& normals,
        const std::vector<std::vector<std::size_t>>& neighborhoods,
        std::size_t& flippedCount)
    {
        const std::size_t n = points.size();
        if (n == 0) return;

        // Find seed: point with largest z-component (heuristic: likely an
        // outward-pointing normal when viewing from above)
        std::size_t seed = 0;
        float maxZ = -std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < n; ++i)
        {
            if (points[i].z > maxZ)
            {
                maxZ = points[i].z;
                seed = i;
            }
        }

        // Ensure seed normal points "upward" (+z)
        if (normals[seed].z < 0.0f)
        {
            normals[seed] = -normals[seed];
            ++flippedCount;
        }

        // Prim's algorithm on the Riemannian graph
        // Edge weight = 1 - |n_i . n_j| (prefer edges where normals are nearly parallel)
        std::vector<bool> visited(n, false);

        // Priority queue: (weight, vertex, parent)
        using PQEntry = std::tuple<float, std::size_t, std::size_t>;
        std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;

        pq.emplace(0.0f, seed, seed);

        while (!pq.empty())
        {
            auto [w, u, parent] = pq.top();
            pq.pop();

            if (visited[u]) continue;
            visited[u] = true;

            // Orient: flip normal to agree with parent's normal
            if (u != parent)
            {
                if (glm::dot(normals[u], normals[parent]) < 0.0f)
                {
                    normals[u] = -normals[u];
                    ++flippedCount;
                }
            }

            // Add neighbors to queue
            for (std::size_t v : neighborhoods[u])
            {
                if (!visited[v])
                {
                    float weight = 1.0f - std::abs(glm::dot(normals[u], normals[v]));
                    pq.emplace(weight, v, u);
                }
            }
        }
    }

    // =========================================================================
    // Main estimation function
    // =========================================================================

    std::optional<EstimationResult> EstimateNormals(
        const std::vector<glm::vec3>& points,
        const EstimationParams& params)
    {
        if (points.size() < 3)
            return std::nullopt;

        const std::size_t n = points.size();
        const std::size_t k = std::min(params.KNeighbors, n - 1);

        EstimationResult result;
        result.Normals.resize(n, glm::vec3(0.0f, 0.0f, 1.0f));

        // Build octree for spatial queries
        // Each point is represented as a zero-volume AABB
        std::vector<AABB> pointAABBs(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            pointAABBs[i] = {.Min = points[i], .Max = points[i]};
        }

        Octree octree;
        Octree::SplitPolicy policy;
        policy.SplitPoint = Octree::SplitPoint::Mean;
        policy.TightChildren = true;

        if (!octree.Build(std::move(pointAABBs), policy, params.OctreeMaxPerNode, params.OctreeMaxDepth))
            return std::nullopt;

        // Store neighborhoods for MST orientation later
        std::vector<std::vector<std::size_t>> neighborhoods(n);

        // For each point, compute PCA normal
        for (std::size_t i = 0; i < n; ++i)
        {
            // Find k nearest neighbors
            std::vector<std::size_t> neighbors;
            octree.QueryKnn(points[i], k + 1, neighbors); // +1 because the point itself is included

            // Remove self from neighbors
            auto selfIt = std::find(neighbors.begin(), neighbors.end(), i);
            if (selfIt != neighbors.end())
                neighbors.erase(selfIt);

            // Limit to k neighbors
            if (neighbors.size() > k)
                neighbors.resize(k);

            neighborhoods[i] = neighbors;

            if (neighbors.size() < 2)
            {
                // Not enough neighbors for PCA
                ++result.DegenerateCount;
                continue;
            }

            // Compute centroid of neighborhood (including the point itself)
            glm::dvec3 centroid(0.0);
            centroid += glm::dvec3(points[i]);
            for (std::size_t j : neighbors)
                centroid += glm::dvec3(points[j]);
            centroid /= static_cast<double>(neighbors.size() + 1);

            // Compute 3x3 covariance matrix
            double c00 = 0.0, c01 = 0.0, c02 = 0.0;
            double c11 = 0.0, c12 = 0.0, c22 = 0.0;

            auto addPoint = [&](const glm::vec3& p)
            {
                glm::dvec3 d = glm::dvec3(p) - centroid;
                c00 += d.x * d.x;
                c01 += d.x * d.y;
                c02 += d.x * d.z;
                c11 += d.y * d.y;
                c12 += d.y * d.z;
                c22 += d.z * d.z;
            };

            addPoint(points[i]);
            for (std::size_t j : neighbors)
                addPoint(points[j]);

            double invN = 1.0 / static_cast<double>(neighbors.size() + 1);
            c00 *= invN;
            c01 *= invN;
            c02 *= invN;
            c11 *= invN;
            c12 *= invN;
            c22 *= invN;

            // Eigendecomposition
            Eigen3 eigen = SymmetricEigen3(c00, c01, c02, c11, c12, c22);

            // Normal = eigenvector of smallest eigenvalue (index 0, sorted ascending)
            glm::dvec3 normal = eigen.Eigenvectors[0];
            double len = glm::length(normal);

            if (len < 1e-12)
            {
                ++result.DegenerateCount;
                continue;
            }

            result.Normals[i] = glm::vec3(normal / len);
        }

        // Consistent orientation via MST
        if (params.OrientNormals)
        {
            OrientNormalsMST(points, result.Normals, neighborhoods, result.FlippedCount);
        }

        return result;
    }

} // namespace Geometry::NormalEstimation
