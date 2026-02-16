module;

#include <cstddef>

export module Geometry:Smoothing;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Smoothing
{
    // =========================================================================
    // Mesh smoothing operators
    // =========================================================================
    //
    // Smoothing is applied in-place to the vertex positions of the mesh.
    // All smoothing operators preserve mesh topology (no edge collapses,
    // splits, or flips). Boundary vertices are optionally kept fixed.

    // Configuration for smoothing operations
    struct SmoothingParams
    {
        // Number of smoothing iterations
        std::size_t Iterations{1};

        // Step size for each iteration (0 < Lambda < 1 for stability)
        // Typical values: 0.5 for uniform Laplacian, 0.1–0.5 for cotan Laplacian
        double Lambda{0.5};

        // If true, boundary vertices are not moved
        bool PreserveBoundary{true};
    };

    // -------------------------------------------------------------------------
    // Uniform Laplacian smoothing
    // -------------------------------------------------------------------------
    //
    // Each vertex is moved toward the centroid of its 1-ring neighbors:
    //   x_i ← x_i + λ * (x̄_i - x_i)
    //
    // where x̄_i = (1/|N(i)|) Σ_{j ∈ N(i)} x_j is the uniform average.
    //
    // Simple and fast but not area-aware. Causes shrinkage — use Taubin
    // smoothing to compensate.
    void UniformLaplacian(Halfedge::Mesh& mesh, const SmoothingParams& params);

    // -------------------------------------------------------------------------
    // Cotangent Laplacian smoothing
    // -------------------------------------------------------------------------
    //
    // Explicit smoothing using cotan weights (area-aware):
    //   x_i ← x_i + (λ / A_i) * Σ_j w_ij * (x_j - x_i)
    //
    // where w_ij = (cot α_ij + cot β_ij) / 2 and A_i is the mixed
    // Voronoi area. This is a single forward Euler step of the heat
    // equation on the mesh surface.
    //
    // More geometrically faithful than uniform Laplacian — respects
    // mesh geometry and produces better results on irregular meshes.
    // Still causes shrinkage; combine with Taubin for volume preservation.
    void CotanLaplacian(Halfedge::Mesh& mesh, const SmoothingParams& params);

    // -------------------------------------------------------------------------
    // Taubin smoothing (shrinkage-free)
    // -------------------------------------------------------------------------
    //
    // Two-pass smoothing that eliminates the shrinkage artifact of
    // Laplacian smoothing (Taubin, "A Signal Processing Approach to
    // Fair Surface Design", 1995):
    //
    //   Pass 1: x_i ← x_i + λ * Δx_i     (smoothing, λ > 0)
    //   Pass 2: x_i ← x_i + μ * Δx_i     (un-shrinking, μ < 0)
    //
    // where μ is derived from the passband frequency parameter kPB:
    //   μ = 1 / (kPB - 1/λ)
    //
    // Typical kPB values: 0.01–0.1 (lower = more smoothing, higher = less).
    //
    // This applies the uniform Laplacian as the operator Δ.
    // The params.Lambda field is used as λ, and kPB is a separate parameter.
    struct TaubinParams
    {
        // Number of smoothing iterations (each iteration = one λ + one μ pass)
        std::size_t Iterations{10};

        // Smoothing factor (λ > 0)
        double Lambda{0.5};

        // Passband frequency — controls how much smoothing occurs.
        // Range: (0, 1). Lower values = more aggressive smoothing.
        // Default 0.1 provides moderate smoothing with good volume preservation.
        double PassbandFrequency{0.1};

        // If true, boundary vertices are not moved
        bool PreserveBoundary{true};
    };

    void Taubin(Halfedge::Mesh& mesh, const TaubinParams& params);

} // namespace Geometry::Smoothing
