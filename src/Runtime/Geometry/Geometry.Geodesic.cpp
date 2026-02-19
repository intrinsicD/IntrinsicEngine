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

module Geometry:Geodesic.Impl;

import :Geodesic;
import :Properties;
import :HalfedgeMesh;
import :DEC;
import :MeshUtils;

namespace Geometry::Geodesic
{
    using MeshUtils::Cotan;
    using MeshUtils::MeanEdgeLength;

    // =========================================================================
    // Step 2: Compute normalized negative gradient per face
    // =========================================================================
    // For a triangle with vertices (a, b, c) and 0-form values (ua, ub, uc):
    //   ∇u = (1/2A) * Σ u_i * (N × e_i)
    // where N is face normal, e_i is the edge opposite vertex i,
    // and A is the face area.
    //
    // We return X = -∇u / |∇u| (unit vector pointing toward the source).

    struct FaceGradient
    {
        glm::vec3 Direction; // Normalized negative gradient
        bool Valid;
    };

    static std::vector<FaceGradient> ComputeNormalizedGradient(
        const Halfedge::Mesh& mesh,
        const std::vector<double>& u)
    {
        const std::size_t nF = mesh.FacesSize();
        std::vector<FaceGradient> gradients(nF);

        for (std::size_t fi = 0; fi < nF; ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            gradients[fi].Valid = false;

            if (mesh.IsDeleted(fh))
                continue;

            // Get triangle vertices and values
            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            double ua_val = u[va.Index];
            double ub_val = u[vb.Index];
            double uc_val = u[vc.Index];

            // Face normal (unnormalized, length = 2*area)
            glm::vec3 N = glm::cross(pb - pa, pc - pa);
            float areaTimesTwo = glm::length(N);
            if (areaTimesTwo < 1e-10f)
                continue;

            N /= areaTimesTwo; // Unit normal

            // Edges opposite to each vertex
            glm::vec3 ea = pc - pb; // opposite to a
            glm::vec3 eb = pa - pc; // opposite to b
            glm::vec3 ec = pb - pa; // opposite to c

            // Gradient: (1/2A) * Σ u_i * (N × e_i)
            float invTwoA = 1.0f / areaTimesTwo;
            glm::vec3 grad = invTwoA * (
                static_cast<float>(ua_val) * glm::cross(N, ea) +
                static_cast<float>(ub_val) * glm::cross(N, eb) +
                static_cast<float>(uc_val) * glm::cross(N, ec)
            );

            float gradLen = glm::length(grad);
            if (gradLen < 1e-10f)
                continue;

            // Normalized negative gradient: X = -∇u / |∇u|
            gradients[fi].Direction = -grad / gradLen;
            gradients[fi].Valid = true;
        }

        return gradients;
    }

    // =========================================================================
    // Step 3: Compute integrated divergence of the vector field X
    // =========================================================================
    // Per vertex i, the integrated divergence is:
    //   div(X)_i = (1/2) Σ_{f ∈ faces(i)} [
    //       cot(θ_1) * dot(e_1, X_f) + cot(θ_2) * dot(e_2, X_f)
    //   ]
    // where θ_1, θ_2 are the angles at vertex i's two neighboring vertices
    // in face f, and e_1, e_2 are the edge vectors from those neighbors to i.

    static std::vector<double> ComputeDivergence(
        const Halfedge::Mesh& mesh,
        const std::vector<FaceGradient>& X)
    {
        const std::size_t nV = mesh.VerticesSize();
        std::vector<double> div(nV, 0.0);

        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            FaceHandle fh{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(fh) || !X[fi].Valid)
                continue;

            glm::vec3 Xf = X[fi].Direction;

            // Get triangle vertices
            HalfedgeHandle h0 = mesh.Halfedge(fh);
            HalfedgeHandle h1 = mesh.NextHalfedge(h0);
            HalfedgeHandle h2 = mesh.NextHalfedge(h1);

            VertexHandle va = mesh.ToVertex(h0);
            VertexHandle vb = mesh.ToVertex(h1);
            VertexHandle vc = mesh.ToVertex(h2);

            glm::vec3 pa = mesh.Position(va);
            glm::vec3 pb = mesh.Position(vb);
            glm::vec3 pc = mesh.Position(vc);

            // Edges
            glm::vec3 eab = pb - pa;
            glm::vec3 eac = pc - pa;
            glm::vec3 ebc = pc - pb;

            // Cotangents at each vertex
            double cotA = Cotan(eab, eac);
            double cotB = Cotan(-eab, ebc);
            double cotC = Cotan(-eac, -ebc);

            // Divergence contribution to vertex a:
            //   (1/2) [cot(B) * dot(pa - pb, Xf) + cot(C) * dot(pa - pc, Xf)]
            // Note: edges from neighboring vertices TO vertex a
            double dotBA = static_cast<double>(glm::dot(pa - pb, Xf));
            double dotCA = static_cast<double>(glm::dot(pa - pc, Xf));
            div[va.Index] += 0.5 * (cotB * dotBA + cotC * dotCA);

            // Divergence contribution to vertex b
            double dotAB = static_cast<double>(glm::dot(pb - pa, Xf));
            double dotCB = static_cast<double>(glm::dot(pb - pc, Xf));
            div[vb.Index] += 0.5 * (cotA * dotAB + cotC * dotCB);

            // Divergence contribution to vertex c
            double dotAC = static_cast<double>(glm::dot(pc - pa, Xf));
            double dotBC = static_cast<double>(glm::dot(pc - pb, Xf));
            div[vc.Index] += 0.5 * (cotA * dotAC + cotB * dotBC);
        }

        return div;
    }

    // =========================================================================
    // Main geodesic distance computation
    // =========================================================================

    std::optional<GeodesicResult> ComputeDistance(
        const Halfedge::Mesh& mesh,
        std::span<const std::size_t> sourceVertices,
        const GeodesicParams& params)
    {
        if (mesh.IsEmpty() || mesh.FaceCount() == 0 || sourceVertices.empty())
            return std::nullopt;

        const std::size_t nV = mesh.VerticesSize();

        // Build DEC operators
        DEC::DECOperators ops = DEC::BuildOperators(mesh);
        if (!ops.IsValid())
            return std::nullopt;

        // Time step: t = h² (mean edge length squared)
        double h = MeanEdgeLength(mesh);
        double t = params.TimeStep;
        if (t <= 0.0)
            t = h * h;

        // =====================================================================
        // Step 1: Solve (M + t*L) u = δ
        // =====================================================================
        // M = Hodge0 (mass matrix, diagonal)
        // L = Laplacian (positive semidefinite with our sign convention:
        //     positive diagonal, negative off-diagonal)
        // The combined system M + t*L is SPD for t > 0.

        // Build right-hand side: delta function at source vertices
        std::vector<double> rhs(nV, 0.0);
        for (std::size_t s : sourceVertices)
        {
            if (s < nV)
            {
                VertexHandle vh{static_cast<PropertyIndex>(s)};
                if (!mesh.IsDeleted(vh) && !mesh.IsIsolated(vh))
                    rhs[s] = 1.0;
            }
        }

        // Solve (M + t*L) u = delta
        std::vector<double> u(nV, 0.0);
        DEC::CGParams cgParams;
        cgParams.MaxIterations = params.MaxSolverIterations;
        cgParams.Tolerance = params.SolverTolerance;

        auto heatResult = DEC::SolveCGShifted(
            ops.Hodge0, 1.0,
            ops.Laplacian, t,
            rhs, u, cgParams);

        GeodesicResult result;
        result.HeatSolveIterations = heatResult.Iterations;

        // =====================================================================
        // Step 2: Compute normalized negative gradient field
        // =====================================================================
        auto X = ComputeNormalizedGradient(mesh, u);

        // =====================================================================
        // Step 3: Compute divergence and solve Poisson equation
        // =====================================================================
        auto divX = ComputeDivergence(mesh, X);

        // Solve L * phi = div(X)
        // L has a 1-dimensional null space (constant functions).
        // We pin vertex 0 to zero by modifying the system:
        // Set row 0 of the system to the identity equation: phi[0] = 0.
        // This is done by modifying the rhs and zeroing out the initial guess.

        // (We originally found a pin vertex here, but the regularization
        // approach below handles the null space without explicit pinning.)

        // Create a modified system for the Poisson solve:
        // Instead of modifying the sparse matrix (expensive), we use the
        // shift trick: solve (L + epsilon * I) phi = div(X) with a small
        // regularization that breaks the null space. This is simpler and
        // sufficient for the heat method since we shift the result anyway.
        //
        // We use the shifted solver with a small diagonal mass term.
        DEC::DiagonalMatrix regularizer;
        regularizer.Size = nV;
        regularizer.Diagonal.assign(nV, 1e-8);

        std::vector<double> phi(nV, 0.0);
        auto poissonResult = DEC::SolveCGShifted(
            regularizer, 1.0,
            ops.Laplacian, 1.0,
            divX, phi, cgParams);

        result.PoissonSolveIterations = poissonResult.Iterations;
        result.Converged = heatResult.Converged && poissonResult.Converged;

        // =====================================================================
        // Step 4: Shift distances so minimum is 0
        // =====================================================================
        double minDist = 1e30;
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
            if (phi[vi] < minDist) minDist = phi[vi];
        }

        result.Distances.resize(nV, 0.0);
        for (std::size_t vi = 0; vi < nV; ++vi)
        {
            VertexHandle vh{static_cast<PropertyIndex>(vi)};
            if (mesh.IsDeleted(vh) || mesh.IsIsolated(vh)) continue;
            result.Distances[vi] = phi[vi] - minDist;
        }

        return result;
    }

} // namespace Geometry::Geodesic
