module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
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
    using MeshUtils::AngleAtVertex;
    using MeshUtils::ComputeMixedVoronoiAreas;
    using MeshUtils::ComputeCotanLaplacian;
    using MeshUtils::VertexNormal;

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
                // H = ||ΔS x|| / 2
                glm::dvec3 laplaceB = laplacian[i] / areas[i];
                result.Property[vh] = glm::length(laplaceB) / 2.0;

                // Sign convention: check orientation against the area-weighted vertex
                // normal. Positive H = surface curves toward normal (locally convex).
                const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));

                if (glm::dot(normal, laplaceB) < 0.0)
                    result.Property[vh] = -result.Property[vh];
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
        const std::size_t nF = mesh.FacesSize();

        GaussianCurvatureResult result;
        result.Property = VertexProperty<double>(mesh.VertexProperties().GetOrAdd<double>("v:gaussian_curvature", 0.0));

        auto areas = ComputeMixedVoronoiAreas(mesh);

        // Accumulate angle sum per vertex
        std::vector<double> angleSum(nV, 0.0);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            angleSum[va.Index] += AngleAtVertex(pa, pb, pc);
            angleSum[vb.Index] += AngleAtVertex(pb, pc, pa);
            angleSum[vc.Index] += AngleAtVertex(pc, pa, pb);
        }

        // Compute Gaussian curvature
        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] > 1e-12)
            {
                double defect = mesh.IsBoundary(vh)
                                    ? std::numbers::pi - angleSum[i]
                                    : 2.0 * std::numbers::pi - angleSum[i];

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
        const std::size_t nF = mesh.FacesSize();

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

        // 2. Accumulate angle sums for Gaussian curvature
        std::vector<double> angleSum(nV, 0.0);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh)) continue;

            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            angleSum[va.Index] += AngleAtVertex(pa, pb, pc);
            angleSum[vb.Index] += AngleAtVertex(pb, pc, pa);
            angleSum[vc.Index] += AngleAtVertex(pc, pa, pb);
        }

        // 3. Assemble per-vertex curvature
        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;

            if (areas[i] < 1e-12) continue;

            // Mean curvature
            glm::dvec3 laplaceB = laplacian[i] / areas[i];
            double H = glm::length(laplaceB) / 2.0;

            // Sign of mean curvature: check orientation against the area-weighted
            // vertex normal. Positive H = surface curves toward normal (locally convex).
            const glm::dvec3 normal = glm::dvec3(VertexNormal(mesh, vh));

            if (glm::dot(normal, laplaceB) < 0.0)
                H = -H;

            // Gaussian curvature
            double defect = mesh.IsBoundary(vh)
                                ? std::numbers::pi - angleSum[i]
                                : 2.0 * std::numbers::pi - angleSum[i];
            double K = defect / areas[i];

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
