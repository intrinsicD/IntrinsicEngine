module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

export module Geometry:Geodesic;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Geodesic
{
    // =========================================================================
    // Geodesic Distance via the Heat Method
    // =========================================================================
    //
    // Implementation of Crane, Weischedel & Wardetzky, "Geodesics in Heat:
    // A New Approach to Computing Distance Using Heat Kernels" (2013).
    //
    // The heat method computes approximate geodesic distances on a triangle
    // mesh in three steps:
    //
    //   1. Heat diffusion: Solve (M + t*L) u = δ_S
    //      where M = Hodge star 0 (mass matrix), L = weak cotan Laplacian,
    //      t = h² (h = mean edge length), and δ_S is a Dirac impulse
    //      at the source vertices.
    //
    //   2. Gradient normalization: For each face, compute the gradient of u,
    //      then normalize to get the unit vector field X = -∇u / |∇u|.
    //      This field points along the shortest paths.
    //
    //   3. Poisson solve: Recover the distance function φ by solving
    //      L φ = div(X), where div(X) is the integrated divergence of X.
    //      The solution is unique up to a constant; we shift so min(φ) = 0.
    //
    // Advantages over Dijkstra / fast marching:
    //   - Handles any triangle mesh topology (including non-convex, genus > 0)
    //   - Same factorization reused for multiple source sets
    //   - Accuracy improves with mesh refinement (converges to true geodesic)
    //
    // The DEC module provides all required operators (Hodge stars, Laplacian,
    // exterior derivatives). The CG solver handles the two linear solves.

    struct GeodesicParams
    {
        // Time step for heat diffusion. If 0, uses h² where h is the mean
        // edge length (recommended default from the paper).
        double TimeStep{0.0};

        // CG solver tolerance
        double SolverTolerance{1e-8};

        // CG solver maximum iterations
        std::size_t MaxSolverIterations{2000};
    };

    struct GeodesicResult
    {
        // Per-vertex geodesic distances from the source set.
        // Size = mesh.VerticesSize(). Deleted/isolated vertices have distance 0.
        std::vector<double> Distances;

        // Number of CG iterations used in each solve step
        std::size_t HeatSolveIterations{0};
        std::size_t PoissonSolveIterations{0};

        // Whether both solves converged
        bool Converged{false};
    };

    // -------------------------------------------------------------------------
    // Compute geodesic distances from a set of source vertices
    // -------------------------------------------------------------------------
    //
    // sourceVertices: indices of source vertices (0-based, into VerticesSize())
    //
    // Returns nullopt if the mesh is empty, has no faces, or the source set
    // is empty.
    [[nodiscard]] std::optional<GeodesicResult> ComputeDistance(
        const Halfedge::Mesh& mesh,
        std::span<const std::size_t> sourceVertices,
        const GeodesicParams& params = {});

} // namespace Geometry::Geodesic
