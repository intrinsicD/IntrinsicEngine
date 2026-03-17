module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Curvature.Impl;

import :Curvature;
import :Properties;
import :HalfedgeMesh;
import :MeshUtils;

namespace Geometry::Curvature
{
    using MeshUtils::ComputeMixedVoronoiAreas;
    using MeshUtils::ComputeCotanLaplacian;
    using MeshUtils::ComputeVertexAngleDefect;
    using MeshUtils::ComputeVertexAngleSums;
    using MeshUtils::VertexNormal;

    namespace
    {
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

    std::optional<MeanCurvatureResult> ComputeMeanCurvature(Halfedge::Mesh& mesh)
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

    std::optional<GaussianCurvatureResult> ComputeGaussianCurvature(Halfedge::Mesh& mesh)
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

    CurvatureField ComputeCurvature(Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();

        CurvatureField result;
        result.MeanCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:mean_curvature", 0.0));
        result.GaussianCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:gaussian_curvature", 0.0));
        result.MinPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:min_principal_curvature", 0.0));
        result.MaxPrincipalCurvatureProperty = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:max_principal_curvature", 0.0));
        result.MeanCurvatureNormalProperty = VertexProperty<glm::vec3>(mesh.VertexProperties().GetOrAdd<glm::vec3>("v:mean_curvature_normal", glm::vec3(0.0f)));

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

        return result;
    }
} // namespace Geometry::Curvature
