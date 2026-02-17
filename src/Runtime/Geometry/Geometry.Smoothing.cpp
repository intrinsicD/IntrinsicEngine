module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>

module Geometry:Smoothing.Impl;

import :Smoothing;
import :Properties;
import :HalfedgeMesh;
import :DEC;

namespace Geometry::Smoothing
{
    // -------------------------------------------------------------------------
    // Helper: cotangent of angle between two vectors
    // -------------------------------------------------------------------------
    static double Cotan(glm::vec3 u, glm::vec3 v)
    {
        auto crossVec = glm::cross(u, v);
        double sinVal = static_cast<double>(glm::length(crossVec));
        double cosVal = static_cast<double>(glm::dot(u, v));
        if (sinVal < 1e-10) return 0.0;
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
    // Helper: single pass of uniform Laplacian smoothing
    // -------------------------------------------------------------------------
    // Applies: x_i ← x_i + factor * (centroid_of_neighbors - x_i)
    // factor can be positive (smoothing) or negative (inflation).

    static void UniformLaplacianPass(
        Halfedge::Mesh& mesh,
        double factor,
        bool preserveBoundary)
    {
        const std::size_t nV = mesh.VerticesSize();

        // Compute target positions in a separate buffer to avoid order-dependent updates
        std::vector<glm::dvec3> newPositions(nV);
        std::vector<bool> movable(nV, false);

        for (std::size_t i = 0; i < nV; ++i)
        {
            VertexHandle vh{static_cast<PropertyIndex>(i)};
            newPositions[i] = glm::dvec3(mesh.Position(vh));

            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
            if (preserveBoundary && mesh.IsBoundary(vh)) continue;

            // Compute centroid of 1-ring neighbors
            glm::dvec3 centroid(0.0);
            std::size_t count = 0;

            HalfedgeHandle h = mesh.Halfedge(vh);
            HalfedgeHandle start = h;
            do
            {
                centroid += glm::dvec3(mesh.Position(mesh.ToVertex(h)));
                ++count;
                h = mesh.CWRotatedHalfedge(h);
            } while (h != start);

            if (count == 0) continue;

            centroid /= static_cast<double>(count);
            glm::dvec3 displacement = centroid - glm::dvec3(mesh.Position(vh));

            newPositions[i] = glm::dvec3(mesh.Position(vh)) + factor * displacement;
            movable[i] = true;
        }

        // Apply new positions
        for (std::size_t i = 0; i < nV; ++i)
        {
            if (movable[i])
            {
                VertexHandle vh{static_cast<PropertyIndex>(i)};
                mesh.Position(vh) = glm::vec3(newPositions[i]);
            }
        }
    }

    // =========================================================================
    // UniformLaplacian
    // =========================================================================

    void UniformLaplacian(Halfedge::Mesh& mesh, const SmoothingParams& params)
    {
        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            UniformLaplacianPass(mesh, params.Lambda, params.PreserveBoundary);
        }
    }

    // =========================================================================
    // CotanLaplacian
    // =========================================================================

    void CotanLaplacian(Halfedge::Mesh& mesh, const SmoothingParams& params)
    {
        const std::size_t nV = mesh.VerticesSize();
        const std::size_t nE = mesh.EdgesSize();
        const std::size_t nF = mesh.FacesSize();

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Compute mixed Voronoi areas
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

            // Accumulate weighted Laplacian displacement per vertex
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

                double w = std::max(0.0, cotSum) / 2.0;
                glm::dvec3 diff = glm::dvec3(mesh.Position(vj)) - glm::dvec3(mesh.Position(vi));

                laplacian[vi.Index] += w * diff;
                laplacian[vj.Index] -= w * diff;
            }

            // Apply displacement: x_i ← x_i + λ * Σ w_ij (x_j - x_i)
            // Note: we deliberately omit the 1/A_i area normalization used in
            // the true Laplace-Beltrami operator. Area normalization is correct
            // for curvature computation but causes instability in explicit
            // smoothing when vertex areas are small. The unnormalized form
            // is standard practice for explicit mesh smoothing (see Botsch et al.,
            // "Polygon Mesh Processing", §4.2).
            for (std::size_t i = 0; i < nV; ++i)
            {
                VertexHandle vh{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
                if (params.PreserveBoundary && mesh.IsBoundary(vh)) continue;

                glm::dvec3 displacement = params.Lambda * laplacian[i];
                mesh.Position(vh) = glm::vec3(glm::dvec3(mesh.Position(vh)) + displacement);
            }
        }
    }

    // =========================================================================
    // Taubin smoothing
    // =========================================================================
    //
    // Alternating passes with λ (positive, smoothing) and μ (negative, inflation).
    // μ = 1 / (kPB - 1/λ)  where kPB is the passband frequency.
    //
    // The result preserves volume much better than pure Laplacian smoothing.

    void Taubin(Halfedge::Mesh& mesh, const TaubinParams& params)
    {
        assert(params.Lambda > 0.0);
        assert(params.PassbandFrequency > 0.0 && params.PassbandFrequency < 1.0);

        // Compute μ from passband frequency
        double mu = 1.0 / (params.PassbandFrequency - 1.0 / params.Lambda);
        assert(mu < 0.0); // μ must be negative for inflation

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Pass 1: Smoothing with λ
            UniformLaplacianPass(mesh, params.Lambda, params.PreserveBoundary);

            // Pass 2: Un-shrinking with μ
            UniformLaplacianPass(mesh, mu, params.PreserveBoundary);
        }
    }

    // =========================================================================
    // Implicit Laplacian smoothing (backward Euler)
    // =========================================================================
    //
    // Solves (M + λ·dt·L) x_new = M · x_old per coordinate axis.
    // Same SolveCGShifted pattern as Geodesic::ComputeDistance().

    static double MeanEdgeLength(const Halfedge::Mesh& mesh)
    {
        double sum = 0.0;
        std::size_t count = 0;
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            EdgeHandle eh{static_cast<PropertyIndex>(ei)};
            if (mesh.IsDeleted(eh)) continue;

            HalfedgeHandle h{static_cast<PropertyIndex>(2u * ei)};
            glm::vec3 a = mesh.Position(mesh.FromVertex(h));
            glm::vec3 b = mesh.Position(mesh.ToVertex(h));
            sum += static_cast<double>(glm::distance(a, b));
            ++count;
        }
        return (count > 0) ? (sum / static_cast<double>(count)) : 0.0;
    }

    std::optional<ImplicitSmoothingResult> ImplicitLaplacian(
        Halfedge::Mesh& mesh,
        const ImplicitSmoothingParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0 || params.Iterations == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();
        ImplicitSmoothingResult result;
        result.VertexCount = mesh.VertexCount();

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Build DEC operators (rebuilt each iteration to track evolving geometry)
            DEC::DECOperators ops = DEC::BuildOperators(mesh);
            if (!ops.IsValid())
                return std::nullopt;

            // Compute timestep
            double h = MeanEdgeLength(mesh);
            double dt = (params.TimeStep > 0.0) ? params.TimeStep : h * h;
            double beta = params.Lambda * dt;

            // CG parameters
            DEC::CGParams cgParams;
            cgParams.MaxIterations = params.MaxSolverIterations;
            cgParams.Tolerance = params.SolverTolerance;

            // Extract current positions per axis
            std::vector<double> xOld(nV, 0.0), yOld(nV, 0.0), zOld(nV, 0.0);
            for (std::size_t i = 0; i < nV; ++i)
            {
                VertexHandle vh{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
                glm::vec3 pos = mesh.Position(vh);
                xOld[i] = static_cast<double>(pos.x);
                yOld[i] = static_cast<double>(pos.y);
                zOld[i] = static_cast<double>(pos.z);
            }

            // For each axis, solve (M + beta*L) x_new = M * x_old
            std::vector<double> rhs(nV, 0.0);
            std::vector<double> xNew(nV, 0.0);
            std::size_t maxCGIter = 0;
            bool allConverged = true;

            auto solveAxis = [&](const std::vector<double>& oldCoord) {
                // RHS = M * x_old
                for (std::size_t i = 0; i < nV; ++i)
                    rhs[i] = ops.Hodge0.Diagonal[i] * oldCoord[i];

                // Initial guess = old position
                for (std::size_t i = 0; i < nV; ++i)
                    xNew[i] = oldCoord[i];

                auto cgResult = DEC::SolveCGShifted(
                    ops.Hodge0, 1.0,
                    ops.Laplacian, beta,
                    rhs, xNew, cgParams);

                if (cgResult.Iterations > maxCGIter)
                    maxCGIter = cgResult.Iterations;
                if (!cgResult.Converged)
                    allConverged = false;

                // Pin boundary vertices
                if (params.PreserveBoundary)
                {
                    for (std::size_t i = 0; i < nV; ++i)
                    {
                        VertexHandle vh{static_cast<PropertyIndex>(i)};
                        if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
                        if (mesh.IsBoundary(vh))
                            xNew[i] = oldCoord[i];
                    }
                }

                return xNew;
            };

            auto xResult = solveAxis(xOld);
            std::vector<double> xSolved = xResult;

            auto yResult = solveAxis(yOld);
            std::vector<double> ySolved = yResult;

            auto zResult = solveAxis(zOld);
            std::vector<double> zSolved = zResult;

            // Write back positions
            for (std::size_t i = 0; i < nV; ++i)
            {
                VertexHandle vh{static_cast<PropertyIndex>(i)};
                if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
                mesh.Position(vh) = glm::vec3(
                    static_cast<float>(xSolved[i]),
                    static_cast<float>(ySolved[i]),
                    static_cast<float>(zSolved[i]));
            }

            result.IterationsPerformed = iter + 1;
            result.LastCGIterations = maxCGIter;
            result.Converged = allConverged;
        }

        return result;
    }

} // namespace Geometry::Smoothing
