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
import :MeshUtils;

namespace Geometry::Smoothing
{
    using MeshUtils::MeanEdgeLength;
    using MeshUtils::ComputeCotanLaplacian;
    using MeshUtils::ComputeOneRingCentroid;

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

            glm::dvec3 centroid = ComputeOneRingCentroid(mesh, vh);
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

    std::optional<SmoothingResult> UniformLaplacian(Halfedge::Mesh& mesh, const SmoothingParams& params)
    {
        if (mesh.IsEmpty() || params.Iterations == 0)
            return std::nullopt;

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            UniformLaplacianPass(mesh, params.Lambda, params.PreserveBoundary);
        }

        SmoothingResult result;
        result.IterationsPerformed = params.Iterations;
        result.VertexCount = mesh.VertexCount();
        return result;
    }

    // =========================================================================
    // CotanLaplacian
    // =========================================================================

    std::optional<SmoothingResult> CotanLaplacian(Halfedge::Mesh& mesh, const SmoothingParams& params)
    {
        if (mesh.IsEmpty() || params.Iterations == 0)
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        for (std::size_t iter = 0; iter < params.Iterations; ++iter)
        {
            // Cotan-weighted Laplacian with non-negative clamping for explicit
            // smoothing stability (Botsch et al., "Polygon Mesh Processing", §4.2).
            auto laplacian = ComputeCotanLaplacian(mesh, /*clampNonNegative=*/true);

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

        SmoothingResult result;
        result.IterationsPerformed = params.Iterations;
        result.VertexCount = mesh.VertexCount();
        return result;
    }

    // =========================================================================
    // Taubin smoothing
    // =========================================================================
    //
    // Alternating passes with λ (positive, smoothing) and μ (negative, inflation).
    // μ = 1 / (kPB - 1/λ)  where kPB is the passband frequency.
    //
    // The result preserves volume much better than pure Laplacian smoothing.

    std::optional<SmoothingResult> Taubin(Halfedge::Mesh& mesh, const TaubinParams& params)
    {
        if (mesh.IsEmpty() || params.Iterations == 0)
            return std::nullopt;

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

        SmoothingResult result;
        result.IterationsPerformed = params.Iterations;
        result.VertexCount = mesh.VertexCount();
        return result;
    }

    // =========================================================================
    // Implicit Laplacian smoothing (backward Euler)
    // =========================================================================
    //
    // Solves (M + λ·dt·L) x_new = M · x_old per coordinate axis.
    // Same SolveCGShifted pattern as Geodesic::ComputeDistance().

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

            std::vector<double> xSolved = solveAxis(xOld);
            std::vector<double> ySolved = solveAxis(yOld);
            std::vector<double> zSolved = solveAxis(zOld);

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
