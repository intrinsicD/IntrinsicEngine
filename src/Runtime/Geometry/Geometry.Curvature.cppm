module;

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:Curvature;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Curvature
{
    // =========================================================================
    // Discrete curvature computation on triangle meshes
    // =========================================================================
    //
    // All curvature operators follow the Meyer et al. (2003) "Discrete
    // Differential-Geometry Operators for Triangulated 2-Manifolds" formulation,
    // which provides second-order accurate estimates on arbitrary triangle meshes.
    //
    // These operators build on the DEC infrastructure (cotan Laplacian, mixed
    // Voronoi areas) but are assembled independently for efficiency — the DEC
    // module builds the full operator matrices, while curvature computation
    // only needs per-vertex scalar/vector quantities.

    // Per-vertex curvature data. Indexed by vertex handle.
    struct VertexCurvature
    {
        double MeanCurvature{0.0};         // H — signed mean curvature
        double GaussianCurvature{0.0};     // K — Gaussian curvature
        double MinPrincipalCurvature{0.0}; // κ₂ = H - √(H² - K)
        double MaxPrincipalCurvature{0.0}; // κ₁ = H + √(H² - K)
    };

    // Result of curvature computation for the entire mesh.
    struct CurvatureField
    {
        // Per-vertex curvature values (indexed by vertex index, including deleted slots)
        std::vector<VertexCurvature> Vertices;

        // Per-vertex mean curvature normal: Hn = (1/2A_i) Σ_j (cot α_ij + cot β_ij)(x_j - x_i)
        // This is the Laplace-Beltrami of the position function, divided by 2.
        // Its magnitude is |H|, its direction is the surface normal (for smooth surfaces).
        std::vector<glm::vec3> MeanCurvatureNormals;

        // Convenience: total number of valid (non-deleted) vertices in the field
        std::size_t ValidCount{0};
    };

    // -------------------------------------------------------------------------
    // Compute mean curvature via Laplace-Beltrami operator
    // -------------------------------------------------------------------------
    //
    // H(v_i) = (1 / 2) * || (1/A_i) * Σ_j w_ij (x_j - x_i) ||
    //
    // where w_ij = (cot α_ij + cot β_ij) / 2 are the cotan weights
    // and A_i is the mixed Voronoi area of vertex i.
    //
    // The sign of H is determined by the dot product of the mean curvature
    // normal with the estimated vertex normal. Positive H means the surface
    // curves toward the normal (convex locally).
    //
    // Returns per-vertex mean curvature values. Size = mesh.VerticesSize().
    // Deleted vertices get value 0.
    [[nodiscard]] std::vector<double> ComputeMeanCurvature(const Halfedge::Mesh& mesh);

    // -------------------------------------------------------------------------
    // Compute Gaussian curvature via angle defect
    // -------------------------------------------------------------------------
    //
    // K(v_i) = (2π - Σ_j θ_j) / A_i
    //
    // where θ_j are the angles at vertex i in each incident triangle,
    // and A_i is the mixed Voronoi area. For boundary vertices, the
    // formula is K(v_i) = (π - Σ_j θ_j) / A_i.
    //
    // This is the Gauss-Bonnet discrete Gaussian curvature.
    // For closed surfaces: Σ K_i * A_i = 2π * χ(M) (Euler characteristic).
    //
    // Returns per-vertex Gaussian curvature values. Size = mesh.VerticesSize().
    [[nodiscard]] std::vector<double> ComputeGaussianCurvature(const Halfedge::Mesh& mesh);

    // -------------------------------------------------------------------------
    // Compute all curvature quantities at once
    // -------------------------------------------------------------------------
    //
    // Computes mean curvature, Gaussian curvature, principal curvatures,
    // and mean curvature normals in a single pass (shares cotan weight
    // and area computation).
    //
    // Principal curvatures are derived from H and K:
    //   κ₁ = H + √(max(0, H² - K))   (maximum principal curvature)
    //   κ₂ = H - √(max(0, H² - K))   (minimum principal curvature)
    //
    // Note: H² - K < 0 can occur due to numerical error on coarse meshes.
    // We clamp to zero in that case.
    [[nodiscard]] CurvatureField ComputeCurvature(const Halfedge::Mesh& mesh);

} // namespace Geometry::Curvature
