module;

#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:Parameterization;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Parameterization
{
    // =========================================================================
    // Least Squares Conformal Maps (LSCM) UV Parameterization
    // =========================================================================
    //
    // Implementation of Lévy, Petitjean, Ray & Maillot, "Least Squares
    // Conformal Maps for Automatic Texture Atlas Generation" (2002).
    //
    // Given a triangle mesh with disk topology (exactly one boundary loop),
    // computes UV coordinates that minimize conformal (angle) distortion.
    // Two boundary vertices are pinned to break translational, rotational,
    // and scale degrees of freedom. The resulting normal-equations system is
    // SPD and solved via the DEC module's Jacobi-preconditioned CG.
    //
    // Requirements:
    //   - Triangle mesh (all faces must be triangles)
    //   - Disk topology: exactly one boundary loop
    //   - At least 3 vertices
    //
    // The method minimizes:
    //   E_LSCM = Σ_t A_t | ∂f/∂s + i·∂f/∂t |²
    // where (s,t) are local orthonormal triangle coordinates and f maps to
    // the UV plane.

    struct ParameterizationParams
    {
        // Indices of two pinned boundary vertices. If SIZE_MAX, auto-selected
        // to maximize arc-length separation along the boundary loop.
        std::size_t PinVertex0{std::numeric_limits<std::size_t>::max()};
        std::size_t PinVertex1{std::numeric_limits<std::size_t>::max()};

        // UV coordinates for the pinned vertices (used for both auto and manual).
        glm::vec2 PinUV0{0.0f, 0.0f};
        glm::vec2 PinUV1{1.0f, 0.0f};

        // CG solver parameters
        double SolverTolerance{1e-8};
        std::size_t MaxSolverIterations{5000};
    };

    struct ParameterizationResult
    {
        // Per-vertex UV coordinates. Size = mesh.VerticesSize().
        // Deleted/isolated vertices have UV = (0, 0).
        std::vector<glm::vec2> UVs;

        // Quality metrics
        double MeanConformalDistortion{0.0};   // σ_max / σ_min averaged over faces
        double MaxConformalDistortion{0.0};     // Worst-case triangle distortion
        std::size_t FlippedTriangleCount{0};    // UV triangles with negative signed area

        // Solver diagnostics
        std::size_t CGIterations{0};
        bool Converged{false};
    };

    // -------------------------------------------------------------------------
    // Compute LSCM parameterization.
    //
    // Returns nullopt if:
    //   - Mesh is empty or has no faces
    //   - Mesh is not a triangle mesh
    //   - Mesh does not have disk topology (not exactly one boundary loop)
    //   - Fewer than 3 vertices
    //   - Specified pin vertices are invalid or identical
    // -------------------------------------------------------------------------
    [[nodiscard]] std::optional<ParameterizationResult> ComputeLSCM(
        const Halfedge::Mesh& mesh,
        const ParameterizationParams& params = {});

} // namespace Geometry::Parameterization
