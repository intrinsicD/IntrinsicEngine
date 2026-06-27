module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry.Curvature;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.PCA;

namespace Geometry::Curvature
{
    using MeshUtils::ComputeMixedVoronoiAreas;
    using MeshUtils::ComputeCotanLaplacian;
    using MeshUtils::ComputeVertexAngleDefect;
    using MeshUtils::ComputeVertexAngleSums;
    using MeshUtils::FaceArea;
    using MeshUtils::VertexNormal;

    namespace
    {
        // Per-vertex output of the Taubin curvature-tensor estimator. Default
        // state is the fail-closed sentinel (zero directions, not Valid).
        struct TensorVertex
        {
            glm::dvec3 Dir1{0.0}; // κ₁ (max) direction, unit tangent
            glm::dvec3 Dir2{0.0}; // κ₂ (min) direction, unit tangent
            double MaxPrincipal{0.0};
            double MinPrincipal{0.0};
            bool Valid{false};
        };

        // Estimate the per-vertex 3×3 curvature tensor (Taubin 1995) and
        // eigen-decompose it into principal directions. Interior, non-degenerate
        // vertices get Valid=true; boundary/flat/zero-area 1-rings stay at the
        // sentinel. Deterministic and allocation-bounded; never emits NaN/Inf.
        [[nodiscard]] std::vector<TensorVertex> ComputeTaubinTensor(HalfedgeMesh::Mesh& mesh)
        {
            constexpr double kTiny = 1e-18;
            const std::size_t nV = mesh.VerticesSize();
            std::vector<TensorVertex> out(nV);

            for (std::size_t i = 0; i < nV; ++i)
            {
                VertexHandle vh{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
                if (mesh.IsBoundary(vh)) continue; // open 1-ring -> sentinel

                const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));
                const double nLen = glm::length(normal);
                if (nLen <= kTiny) continue;
                const glm::dvec3 n = normal / nLen;
                const glm::dvec3 xi = glm::dvec3(mesh.Position(vh));

                // Accumulate M = Σ_j w_ij κ_ij T_ij T_ijᵀ (upper triangle).
                double m00 = 0.0, m01 = 0.0, m02 = 0.0, m11 = 0.0, m12 = 0.0, m22 = 0.0;
                double totalW = 0.0;
                for (const HalfedgeHandle h : mesh.HalfedgesAroundVertex(vh))
                {
                    const VertexHandle vj = mesh.ToVertex(h);
                    const glm::dvec3 d = glm::dvec3(mesh.Position(vj)) - xi;
                    const double dd = glm::dot(d, d);
                    if (dd <= kTiny) continue;

                    // Area-derived weight: sum of the areas of the two faces
                    // incident to this edge (one for a boundary edge — but the
                    // 1-ring is closed here, so generally two).
                    double w = 0.0;
                    const FaceHandle f0 = mesh.Face(h);
                    if (f0.IsValid()) w += FaceArea(mesh, f0);
                    const FaceHandle f1 = mesh.Face(mesh.OppositeHalfedge(h));
                    if (f1.IsValid()) w += FaceArea(mesh, f1);
                    if (w <= kTiny) continue;

                    const double kappa = 2.0 * glm::dot(n, d) / dd;
                    glm::dvec3 t = d - glm::dot(d, n) * n; // project onto tangent plane
                    const double tLen = glm::length(t);
                    if (tLen <= kTiny) continue; // edge parallel to normal
                    t /= tLen;

                    const double wk = w * kappa;
                    m00 += wk * t.x * t.x;
                    m01 += wk * t.x * t.y;
                    m02 += wk * t.x * t.z;
                    m11 += wk * t.y * t.y;
                    m12 += wk * t.y * t.z;
                    m22 += wk * t.z * t.z;
                    totalW += w;
                }

                if (totalW <= kTiny) continue; // zero-area / empty 1-ring
                const double inv = 1.0 / totalW;
                m00 *= inv; m01 *= inv; m02 *= inv; m11 *= inv; m12 *= inv; m22 *= inv;

                // Flat 1-ring: tensor numerically zero -> sentinel.
                const double frob = std::abs(m00) + std::abs(m01) + std::abs(m02)
                                  + std::abs(m11) + std::abs(m12) + std::abs(m22);
                if (frob <= 1e-12) continue;

                const PCA::Eigen3 eig = PCA::SymmetricEigen3(m00, m01, m02, m11, m12, m22);

                // Discard the eigenvector most aligned with the surface normal;
                // the other two are the tangent principal directions.
                int normalIdx = 0;
                double bestAlign = -1.0;
                for (int k = 0; k < 3; ++k)
                {
                    const double align = std::abs(glm::dot(eig.Eigenvectors[k], n));
                    if (align > bestAlign) { bestAlign = align; normalIdx = k; }
                }
                const int a = (normalIdx == 0) ? 1 : 0;
                const int b = (normalIdx == 2) ? 1 : 2;

                // Recover the *signed* tangent eigenvalues via the Rayleigh
                // quotient mᵏ = vᵏ·(M vᵏ). The shared PCA::SymmetricEigen3 clamps
                // its returned eigenvalues to non-negative (a PSD-covariance
                // assumption), which would erase the negative curvatures of the
                // curvature tensor; its eigenVECTORS are untouched, so we reuse
                // those and read the eigenvalues straight off M.
                auto rayleigh = [&](const glm::dvec3& v) -> double
                {
                    const glm::dvec3 mv{
                        m00 * v.x + m01 * v.y + m02 * v.z,
                        m01 * v.x + m11 * v.y + m12 * v.z,
                        m02 * v.x + m12 * v.y + m22 * v.z};
                    return glm::dot(v, mv);
                };

                // Taubin curvature recovery from the two tangent eigenvalues.
                const double la = rayleigh(eig.Eigenvectors[a]);
                const double lb = rayleigh(eig.Eigenvectors[b]);
                const double kappaA = 3.0 * la - lb;
                const double kappaB = 3.0 * lb - la;

                glm::dvec3 v1 = eig.Eigenvectors[a];
                glm::dvec3 v2 = eig.Eigenvectors[b];
                double k1 = kappaA;
                double k2 = kappaB;
                if (kappaB > kappaA)
                {
                    std::swap(v1, v2);
                    std::swap(k1, k2);
                }

                // Project onto the tangent plane and orthonormalize so the two
                // output directions are unit-length, mutually orthogonal, tangent.
                glm::dvec3 d1 = v1 - glm::dot(v1, n) * n;
                const double d1Len = glm::length(d1);
                if (d1Len <= kTiny) continue;
                d1 /= d1Len;

                glm::dvec3 d2 = v2 - glm::dot(v2, n) * n;
                d2 -= glm::dot(d2, d1) * d1;
                const double d2Len = glm::length(d2);
                if (d2Len <= kTiny) continue;
                d2 /= d2Len;

                out[i].Dir1 = d1;
                out[i].Dir2 = d2;
                out[i].MaxPrincipal = k1;
                out[i].MinPrincipal = k2;
                out[i].Valid = true;
            }

            return out;
        }

        [[nodiscard]] double ComputeSignedMeanCurvatureFromLaplaceBeltrami(
            const glm::dvec3& laplaceB,
            const glm::dvec3& normal) noexcept
        {
            const double laplaceLen = glm::length(laplaceB);
            if (laplaceLen <= 1e-18)
                return 0.0;

            const double normalLen = glm::length(normal);
            if (normalLen <= 1e-18)
                return laplaceLen / 2.0;

            // This implementation uses the cotan Laplace-Beltrami operator
            //   Δx = (1/A) Σ_j w_ij (x_j - x_i)
            // which points approximately toward -n on outward-oriented convex
            // surfaces. Therefore the signed scalar mean curvature satisfies
            //   Δx = -2 H n.
            const double orientation = glm::dot(laplaceB, normal) / (laplaceLen * normalLen);
            const double magnitude = laplaceLen / 2.0;
            return (orientation > 0.0) ? -magnitude : magnitude;
        }
    }

    // =========================================================================
    // ComputeMeanCurvature
    // =========================================================================
    //
    // Mean curvature normal at vertex i:
    //   Hn_i = (1 / 2A_i) * Σ_j (cot α_ij + cot β_ij) * (x_j - x_i)
    //
    // Mean curvature magnitude: H_i = ||Hn_i|| / 2
    // (The factor of 2 comes from the Laplace-Beltrami: ΔS x = -2H n)
    //
    // Actually, the discrete Laplace-Beltrami is:
    //   ΔS f(v_i) = (1/A_i) Σ_j w_ij (f(v_j) - f(v_i))
    // Applied to position: ΔS x = (1/A_i) Σ_j w_ij (x_j - x_i) = -2H n
    // So H = ||ΔS x|| / 2, but the sign needs the normal.

    std::optional<MeanCurvatureResult> ComputeMeanCurvature(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        MeanCurvatureResult result;
        result.Property = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:mean_curvature", 0.0));

        auto areas = ComputeMixedVoronoiAreas(mesh);
        auto laplacian = ComputeCotanLaplacian(mesh);

        // Normalize by area and compute magnitude
        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] > 1e-12)
            {
                // ΔS x = laplacian / area = -2H n
                glm::dvec3 laplaceB = laplacian[i] / areas[i];
                const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));
                result.Property[vh] = ComputeSignedMeanCurvatureFromLaplaceBeltrami(laplaceB, normal);
            }
        }

        return result;
    }

    // =========================================================================
    // ComputeGaussianCurvature
    // =========================================================================
    //
    // Discrete Gaussian curvature via angle defect (Descartes' theorem):
    //   K(v_i) = (2π - Σ_j θ_j) / A_i     for interior vertices
    //   K(v_i) = (π  - Σ_j θ_j) / A_i     for boundary vertices
    //
    // where θ_j is the angle at v_i in each incident triangle.

    std::optional<GaussianCurvatureResult> ComputeGaussianCurvature(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        GaussianCurvatureResult result;
        result.Property = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:gaussian_curvature", 0.0));

        auto areas = ComputeMixedVoronoiAreas(mesh);

        // Accumulate angle sum per vertex.
        const std::vector<double> vertexAngleSums = ComputeVertexAngleSums(mesh);

        // Compute Gaussian curvature
        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] > 1e-12)
            {
                const double defect = ComputeVertexAngleDefect(mesh, vh, vertexAngleSums[i]);
                result.Property[vh] = defect / areas[i];
            }
        }

        return result;
    }

    // =========================================================================
    // ComputeCurvature — Full curvature field
    // =========================================================================

    CurvatureField ComputeCurvature(HalfedgeMesh::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();

        CurvatureField result;
        result.MeanCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:mean_curvature", 0.0));
        result.GaussianCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:gaussian_curvature", 0.0));
        result.MinPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:min_principal_curvature", 0.0));
        result.MaxPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:max_principal_curvature", 0.0));
        result.MeanCurvatureNormalProperty = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:mean_curvature_normal", glm::vec3(0.0f)));
        result.PrincipalDir1Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir1", glm::vec3(0.0f)));
        result.PrincipalDir2Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir2", glm::vec3(0.0f)));

        // Shared computation: mixed Voronoi areas
        auto areas = ComputeMixedVoronoiAreas(mesh);

        // 1. Cotan-weighted Laplacian for mean curvature
        auto laplacian = ComputeCotanLaplacian(mesh);

        // 2. Accumulate angle sums for Gaussian curvature.
        const std::vector<double> vertexAngleSums = ComputeVertexAngleSums(mesh);

        // 3. Assemble per-vertex curvature
        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] < 1e-12) continue;

            glm::dvec3 laplaceB = laplacian[i] / areas[i];
            const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));
            double H = ComputeSignedMeanCurvatureFromLaplaceBeltrami(laplaceB, normal);

            // Gaussian curvature
            const double defect = ComputeVertexAngleDefect(mesh, vh, vertexAngleSums[i]);
            const double K = defect / areas[i];

            // Principal curvatures
            double discriminant = std::max(0.0, H * H - K);
            double sqrtDisc = std::sqrt(discriminant);
            double kappa1 = H + sqrtDisc; // max principal curvature
            double kappa2 = H - sqrtDisc; // min principal curvature

            result.MeanCurvatureProperty[vh] = H;
            result.GaussianCurvatureProperty[vh] = K;
            result.MaxPrincipalCurvatureProperty[vh] = kappa1;
            result.MinPrincipalCurvatureProperty[vh] = kappa2;

            // Mean curvature normal (half the Laplace-Beltrami of position)
            result.MeanCurvatureNormalProperty[vh] = glm::vec3(laplaceB / 2.0);
        }

        // Principal directions from the Taubin tensor. The existing scalar
        // fields above are left untouched; only the direction fields are added.
        const std::vector<TensorVertex> tensor = ComputeTaubinTensor(mesh);
        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh)) continue;
            if (tensor[i].Valid)
            {
                result.PrincipalDir1Property[vh] = glm::vec3(tensor[i].Dir1);
                result.PrincipalDir2Property[vh] = glm::vec3(tensor[i].Dir2);
            }
            else
            {
                result.PrincipalDir1Property[vh] = glm::vec3(0.0f);
                result.PrincipalDir2Property[vh] = glm::vec3(0.0f);
            }
        }

        return result;
    }

    // =========================================================================
    // ComputeCurvatureTensor — principal directions + tensor-recovered curvatures
    // =========================================================================

    std::optional<CurvatureTensorResult> ComputeCurvatureTensor(HalfedgeMesh::Mesh& mesh)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        CurvatureTensorResult result;
        result.PrincipalDir1Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir1", glm::vec3(0.0f)));
        result.PrincipalDir2Property = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:principal_dir2", glm::vec3(0.0f)));
        result.MaxPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:max_principal_curvature", 0.0));
        result.MinPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:min_principal_curvature", 0.0));

        // Scalar baseline (H/K-derived principal curvatures) used as the
        // fail-closed fallback for degenerate/boundary/flat vertices.
        const auto areas = ComputeMixedVoronoiAreas(mesh);
        const auto laplacian = ComputeCotanLaplacian(mesh);
        const std::vector<double> vertexAngleSums = ComputeVertexAngleSums(mesh);

        const std::vector<TensorVertex> tensor = ComputeTaubinTensor(mesh);

        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            // Scalar-derived principal curvatures (same formulation as ComputeCurvature).
            double scalarKMax = 0.0;
            double scalarKMin = 0.0;
            if (areas[i] > 1e-12)
            {
                const glm::dvec3 laplaceB = laplacian[i] / areas[i];
                const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));
                const double H = ComputeSignedMeanCurvatureFromLaplaceBeltrami(laplaceB, normal);
                const double defect = ComputeVertexAngleDefect(mesh, vh, vertexAngleSums[i]);
                const double K = defect / areas[i];
                const double sqrtDisc = std::sqrt(std::max(0.0, H * H - K));
                scalarKMax = H + sqrtDisc;
                scalarKMin = H - sqrtDisc;
            }

            if (tensor[i].Valid)
            {
                result.PrincipalDir1Property[vh] = glm::vec3(tensor[i].Dir1);
                result.PrincipalDir2Property[vh] = glm::vec3(tensor[i].Dir2);
                result.MaxPrincipalCurvatureProperty[vh] = tensor[i].MaxPrincipal;
                result.MinPrincipalCurvatureProperty[vh] = tensor[i].MinPrincipal;
            }
            else
            {
                // Fail closed: zero-direction sentinel, keep scalar-derived κ.
                result.PrincipalDir1Property[vh] = glm::vec3(0.0f);
                result.PrincipalDir2Property[vh] = glm::vec3(0.0f);
                result.MaxPrincipalCurvatureProperty[vh] = scalarKMax;
                result.MinPrincipalCurvatureProperty[vh] = scalarKMin;
            }
        }

        return result;
    }
} // namespace Geometry::Curvature
