module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Curvature.Impl;

import :Curvature;
import :Properties;
import :HalfedgeMesh;

namespace Geometry::Curvature
{
    // -------------------------------------------------------------------------
    // Helper: cotangent of angle between two vectors
    // -------------------------------------------------------------------------
    static double Cotan(glm::vec3 u, glm::vec3 v)
    {
        auto crossVec = glm::cross(u, v);
        double sinVal = static_cast<double>(glm::length(crossVec));
        double cosVal = static_cast<double>(glm::dot(u, v));

        if (sinVal < 1e-10)
            return 0.0;

        return cosVal / sinVal;
    }

    // -------------------------------------------------------------------------
    // Helper: triangle area
    // -------------------------------------------------------------------------
    static double TriangleArea(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        return 0.5 * static_cast<double>(glm::length(glm::cross(b - a, c - a)));
    }

    // -------------------------------------------------------------------------
    // Helper: angle at vertex a in triangle (a, b, c)
    // -------------------------------------------------------------------------
    static double AngleAtVertex(glm::vec3 a, glm::vec3 b, glm::vec3 c)
    {
        glm::vec3 ab = b - a;
        glm::vec3 ac = c - a;
        float lenAB = glm::length(ab);
        float lenAC = glm::length(ac);

        if (lenAB < 1e-10f || lenAC < 1e-10f)
            return 0.0;

        float cosAngle = glm::dot(ab, ac) / (lenAB * lenAC);
        cosAngle = std::clamp(cosAngle, -1.0f, 1.0f);
        return static_cast<double>(std::acos(cosAngle));
    }

    // =========================================================================
    // Mixed Voronoi area per vertex (Meyer et al., 2003)
    // =========================================================================
    // Identical logic to DEC::BuildHodgeStar0 but returns a simple vector<double>
    // for direct use in curvature computation.

    static std::vector<double> ComputeMixedAreas(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nF = mesh.FacesSize();

        std::vector<double> areas(nV, 0.0);

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

            glm::vec3 eAB = pb - pa;
            glm::vec3 eAC = pc - pa;
            glm::vec3 eBC = pc - pb;

            double area = TriangleArea(pa, pb, pc);
            if (area < 1e-12) continue;

            double dotA = static_cast<double>(glm::dot(eAB, eAC));
            double dotB = static_cast<double>(glm::dot(-eAB, eBC));
            double dotC = static_cast<double>(glm::dot(-eAC, -eBC));

            if (dotA < 0.0)
            {
                areas[va.Index] += area / 2.0;
                areas[vb.Index] += area / 4.0;
                areas[vc.Index] += area / 4.0;
            }
            else if (dotB < 0.0)
            {
                areas[va.Index] += area / 4.0;
                areas[vb.Index] += area / 2.0;
                areas[vc.Index] += area / 4.0;
            }
            else if (dotC < 0.0)
            {
                areas[va.Index] += area / 4.0;
                areas[vb.Index] += area / 4.0;
                areas[vc.Index] += area / 2.0;
            }
            else
            {
                double cotA = Cotan(eAB, eAC);
                double cotB = Cotan(-eAB, eBC);
                double cotC = Cotan(-eAC, -eBC);

                double lenSqAB = static_cast<double>(glm::dot(eAB, eAB));
                double lenSqAC = static_cast<double>(glm::dot(eAC, eAC));
                double lenSqBC = static_cast<double>(glm::dot(eBC, eBC));

                areas[va.Index] += (lenSqAB * cotC + lenSqAC * cotB) / 8.0;
                areas[vb.Index] += (lenSqAB * cotC + lenSqBC * cotA) / 8.0;
                areas[vc.Index] += (lenSqAC * cotB + lenSqBC * cotA) / 8.0;
            }
        }

        return areas;
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

    std::vector<double> ComputeMeanCurvature(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();

        std::vector<double> meanCurvature(nV, 0.0);
        auto areas = ComputeMixedAreas(mesh);

        // Compute cotan weights per edge and accumulate Laplace-Beltrami
        std::vector<glm::dvec3> laplacian(nV, glm::dvec3(0.0));

        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh)) continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

            VertexHandle vi = mesh.FromVertex(h0);
            VertexHandle vj = mesh.ToVertex(h0);

            double cotSum = 0.0;

            // Cotan of angle opposite edge in face of h0
            if (!mesh.IsBoundary(h0))
            {
                VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h0));
                glm::vec3 u = mesh.Position(vi) - mesh.Position(vOpp);
                glm::vec3 v = mesh.Position(vj) - mesh.Position(vOpp);
                cotSum += Cotan(u, v);
            }

            // Cotan of angle opposite edge in face of h1
            if (!mesh.IsBoundary(h1))
            {
                VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h1));
                glm::vec3 u = mesh.Position(vj) - mesh.Position(vOpp);
                glm::vec3 v = mesh.Position(vi) - mesh.Position(vOpp);
                cotSum += Cotan(u, v);
            }

            double w = cotSum / 2.0;

            glm::dvec3 diff = glm::dvec3(mesh.Position(vj)) - glm::dvec3(mesh.Position(vi));

            laplacian[vi.Index] += w * diff;
            laplacian[vj.Index] -= w * diff;
        }

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
                meanCurvature[i] = glm::length(laplaceB) / 2.0;

                // Sign convention: estimate vertex normal and check orientation
                // Positive H = surface curves toward normal (locally convex)
                glm::dvec3 normal(0.0);
                HalfedgeHandle h = mesh.Halfedge(vh);
                HalfedgeHandle start = h;
                do
                {
                    if (!mesh.IsBoundary(h))
                    {
                        VertexHandle v1 = mesh.ToVertex(h);
                        VertexHandle v2 = mesh.ToVertex(mesh.NextHalfedge(h));
                        glm::dvec3 e1 = glm::dvec3(mesh.Position(v1)) - glm::dvec3(mesh.Position(vh));
                        glm::dvec3 e2 = glm::dvec3(mesh.Position(v2)) - glm::dvec3(mesh.Position(vh));
                        normal += glm::cross(e1, e2);
                    }
                    h = mesh.CWRotatedHalfedge(h);
                } while (h != start);

                if (glm::dot(normal, laplaceB) < 0.0)
                    meanCurvature[i] = -meanCurvature[i];
            }
        }

        return meanCurvature;
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

    std::vector<double> ComputeGaussianCurvature(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nF = mesh.FacesSize();

        std::vector<double> gaussianCurvature(nV, 0.0);
        auto areas = ComputeMixedAreas(mesh);

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

                gaussianCurvature[i] = defect / areas[i];
            }
        }

        return gaussianCurvature;
    }

    // =========================================================================
    // ComputeCurvature — Full curvature field
    // =========================================================================

    CurvatureField ComputeCurvature(const Halfedge::Mesh& mesh)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();
        const std::size_t nF = mesh.FacesSize();

        CurvatureField result;
        result.Vertices.resize(nV);
        result.MeanCurvatureNormals.resize(nV, glm::vec3(0.0f));
        result.ValidCount = 0;

        // Shared computation: mixed Voronoi areas
        auto areas = ComputeMixedAreas(mesh);

        // 1. Accumulate cotan-weighted Laplacian for mean curvature
        std::vector<glm::dvec3> laplacian(nV, glm::dvec3(0.0));

        for (std::size_t ei = 0; ei < nE; ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh)) continue;

            HalfedgeHandle h0{static_cast<PropertyIndex>(2u * ei)};
            HalfedgeHandle h1 = mesh.OppositeHalfedge(h0);

            VertexHandle vi = mesh.FromVertex(h0);
            VertexHandle vj = mesh.ToVertex(h0);

            double cotSum = 0.0;

            if (!mesh.IsBoundary(h0))
            {
                VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h0));
                glm::vec3 u = mesh.Position(vi) - mesh.Position(vOpp);
                glm::vec3 v = mesh.Position(vj) - mesh.Position(vOpp);
                cotSum += Cotan(u, v);
            }

            if (!mesh.IsBoundary(h1))
            {
                VertexHandle vOpp = mesh.ToVertex(mesh.NextHalfedge(h1));
                glm::vec3 u = mesh.Position(vj) - mesh.Position(vOpp);
                glm::vec3 v = mesh.Position(vi) - mesh.Position(vOpp);
                cotSum += Cotan(u, v);
            }

            double w = cotSum / 2.0;
            glm::dvec3 diff = glm::dvec3(mesh.Position(vj)) - glm::dvec3(mesh.Position(vi));

            laplacian[vi.Index] += w * diff;
            laplacian[vj.Index] -= w * diff;
        }

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

            ++result.ValidCount;

            if (areas[i] < 1e-12) continue;

            // Mean curvature
            glm::dvec3 laplaceB = laplacian[i] / areas[i];
            double H = glm::length(laplaceB) / 2.0;

            // Sign of mean curvature
            glm::dvec3 normal(0.0);
            HalfedgeHandle h = mesh.Halfedge(vh);
            HalfedgeHandle start = h;
            do
            {
                if (!mesh.IsBoundary(h))
                {
                    VertexHandle v1 = mesh.ToVertex(h);
                    VertexHandle v2 = mesh.ToVertex(mesh.NextHalfedge(h));
                    glm::dvec3 e1 = glm::dvec3(mesh.Position(v1)) - glm::dvec3(mesh.Position(vh));
                    glm::dvec3 e2 = glm::dvec3(mesh.Position(v2)) - glm::dvec3(mesh.Position(vh));
                    normal += glm::cross(e1, e2);
                }
                h = mesh.CWRotatedHalfedge(h);
            } while (h != start);

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
            double kappa1 = H + sqrtDisc;  // max principal curvature
            double kappa2 = H - sqrtDisc;  // min principal curvature

            result.Vertices[i].MeanCurvature = H;
            result.Vertices[i].GaussianCurvature = K;
            result.Vertices[i].MaxPrincipalCurvature = kappa1;
            result.Vertices[i].MinPrincipalCurvature = kappa2;

            // Mean curvature normal (half the Laplace-Beltrami of position)
            result.MeanCurvatureNormals[i] = glm::vec3(laplaceB / 2.0);
        }

        return result;
    }

} // namespace Geometry::Curvature
