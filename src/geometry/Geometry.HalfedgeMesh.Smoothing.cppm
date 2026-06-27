module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Smoothing;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.DEC;

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

    // Result of explicit smoothing operations (Uniform, Cotan, Taubin)
    struct SmoothingResult
    {
        // Number of smoothing iterations performed
        std::size_t IterationsPerformed{0};

        // Final vertex count (unchanged — topology is preserved)
        std::size_t VertexCount{0};
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
    //
    // Returns nullopt for empty meshes or zero iterations.
    [[nodiscard]] std::optional<SmoothingResult> UniformLaplacian(
        HalfedgeMesh::Mesh& mesh, const SmoothingParams& params);

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
    //
    // Returns nullopt for empty meshes or zero iterations.
    [[nodiscard]] std::optional<SmoothingResult> CotanLaplacian(
        HalfedgeMesh::Mesh& mesh, const SmoothingParams& params);

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

    // Returns nullopt for empty meshes or zero iterations.
    [[nodiscard]] std::optional<SmoothingResult> Taubin(
        HalfedgeMesh::Mesh& mesh, const TaubinParams& params);

    // -------------------------------------------------------------------------
    // Implicit (backward Euler) Laplacian smoothing
    // -------------------------------------------------------------------------
    //
    // Solves the heat equation implicitly:
    //   (M + λ·dt·L) x_new = M · x_old
    //
    // where M = Hodge star 0 (vertex mass matrix), L = cotan Laplacian
    // (positive semidefinite), λ is the diffusion coefficient, and dt is
    // the timestep.
    //
    // Unlike explicit smoothing (forward Euler), the implicit scheme is
    // unconditionally stable — arbitrarily large timesteps produce smooth
    // results without oscillation or divergence. A single iteration with
    // a large dt can achieve the same effect as many explicit iterations.
    //
    // The system (M + λ·dt·L) is SPD for λ·dt > 0, solved via the
    // DEC module's SolveCGShifted (Jacobi-preconditioned CG).
    //
    // Reference: Desbrun et al., "Implicit Fairing of Irregular Meshes
    // Using Diffusion and Curvature Flow" (1999).

    struct ImplicitSmoothingParams
    {
        // Number of smoothing iterations. Each iteration rebuilds DEC
        // operators so that cotan weights track the evolving geometry.
        std::size_t Iterations{1};

        // Diffusion coefficient. Controls smoothing strength.
        double Lambda{1.0};

        // Timestep. Larger values produce stronger smoothing per iteration.
        // With implicit integration, stability is guaranteed for any dt > 0.
        // If 0, auto-selects dt = h² where h is the mean edge length.
        double TimeStep{0.0};

        // If true, boundary vertices are pinned to their original positions.
        bool PreserveBoundary{true};

        // CG solver tolerance
        double SolverTolerance{1e-8};

        // CG solver maximum iterations
        std::size_t MaxSolverIterations{2000};
    };

    struct ImplicitSmoothingResult
    {
        // Number of smoothing iterations performed
        std::size_t IterationsPerformed{0};

        // CG iterations used in the last solve (per-axis maximum)
        std::size_t LastCGIterations{0};

        // Whether the last CG solve converged
        bool Converged{false};

        // Final vertex count (unchanged — topology is preserved)
        std::size_t VertexCount{0};
    };

    [[nodiscard]] std::optional<ImplicitSmoothingResult> ImplicitLaplacian(
        HalfedgeMesh::Mesh& mesh,
        const ImplicitSmoothingParams& params = {});

    // -------------------------------------------------------------------------
    // Feature-preserving bilateral mesh denoiser
    // -------------------------------------------------------------------------
    //
    // A two-stage, feature-preserving denoiser for triangle (and polygon)
    // meshes. Unlike the Laplacian/Taubin/Implicit operators above — which act
    // directly on vertex positions and blur sharp features — this operator
    // first denoises the *normal field* and only then moves vertices to agree
    // with the cleaned normals, so creases and corners survive.
    //
    //   Stage 1 — bilateral filtering of per-face normals: each face normal is
    //             replaced with a range/spatial-weighted average over its
    //             edge-adjacent neighbours (and itself). The range (normal
    //             difference) weight is what preserves features: across a sharp
    //             edge the normals differ strongly, so neighbours on the other
    //             side of the crease contribute little. (Fleishman, Drori &
    //             Cohen-Or, "Bilateral Mesh Denoising", SIGGRAPH 2003.)
    //   Stage 2 — vertex update by normal projection: each vertex is nudged so
    //             its incident faces lie on the planes implied by the filtered
    //             normals, x_i += (1/|F(i)|) Σ_f n_f (n_f · (c_f − x_i)).
    //             (Sun, Rosin, Martin & Langbein, "Fast and Effective
    //             Feature-Preserving Mesh Denoising", IEEE TVCG 2007; Ohtake
    //             et al. normal-projection integration.)
    //
    // This is the mesh analogue of the point-cloud bilateral filter; the engine
    // previously had bilateral filtering only for point clouds.
    //
    // Fail-closed contract (mirrors VertexNormals): the operator is
    // deterministic, never asserts, never emits NaN/Inf, and on any non-Success
    // status leaves the mesh unmodified. Degenerate (zero-area / non-finite)
    // faces are counted and excluded from weighting rather than poisoning the
    // result.

    struct BilateralDenoiseParams
    {
        // Stage 1: number of face-normal bilateral filtering iterations.
        std::size_t NormalIterations{5};

        // Stage 2: number of vertex position-update iterations.
        std::size_t VertexIterations{10};

        // Spatial (centroid-distance) Gaussian standard deviation. If <= 0, it
        // is auto-selected from the mean centroid distance between 1-ring
        // edge-adjacent faces.
        double SigmaSpatial{0.0};

        // Range (normal-difference) Gaussian standard deviation. If <= 0, it is
        // auto-selected from the mean normal difference between 1-ring
        // edge-adjacent faces (floored to a small positive value so a perfectly
        // flat mesh does not divide by zero).
        double SigmaRange{0.0};

        // If true, boundary vertices are pinned to their original positions.
        bool PreserveBoundary{true};

        // Length below which a face area vector / normal is treated as
        // degenerate. Consistent with VertexNormals::Params.
        double DegenerateNormalLengthEpsilon{1.0e-12};
    };

    // Fail-closed status for the bilateral denoiser. Success means the mesh was
    // processed; every other value means the mesh was left unmodified.
    enum class DenoiseStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        NonManifoldInput,
        DegenerateGeometry,
        NonFiniteInput,
        InvalidParams,
    };

    struct BilateralDenoiseResult
    {
        DenoiseStatus Status{DenoiseStatus::Success};

        // Stage 1 / Stage 2 iterations actually performed.
        std::size_t NormalIterationsPerformed{0};
        std::size_t VertexIterationsPerformed{0};

        // Final vertex count (unchanged — topology is preserved).
        std::size_t VertexCount{0};

        // Diagnostics mirroring VertexNormals::Result.
        std::size_t ProcessedFaceCount{0};
        std::size_t DegenerateFaceCount{0};
        std::size_t NonFiniteFaceCount{0};
        std::size_t SkippedDeletedFaceCount{0};
        std::size_t PinnedBoundaryVertexCount{0};
        std::size_t MovedVertexCount{0};

        // The sigma values actually used (after auto-selection).
        double SigmaSpatialUsed{0.0};
        double SigmaRangeUsed{0.0};
    };

    [[nodiscard]] std::string_view DebugName(DenoiseStatus status) noexcept;

    // Stage 1 only — compute filtered per-face normals without mutating the
    // mesh. `filteredFaceNormals` is resized to mesh.FacesSize(); usable faces
    // receive a unit normal, deleted/degenerate faces receive the zero vector.
    // Returns the fail-closed status (see BilateralDenoiseParams / contract).
    // On a clean, smooth mesh the filtered normals are near-identical to the
    // raw area-weighted face normals.
    [[nodiscard]] BilateralDenoiseResult FilterFaceNormals(
        const HalfedgeMesh::Mesh& mesh,
        const BilateralDenoiseParams& params,
        std::vector<glm::vec3>& filteredFaceNormals);

} // namespace Geometry::Smoothing
