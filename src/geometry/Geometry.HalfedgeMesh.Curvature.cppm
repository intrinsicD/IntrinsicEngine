module;

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Curvature;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

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
        VertexProperty<double> MeanCurvatureProperty{};             // H — signed mean curvature
        VertexProperty<double> GaussianCurvatureProperty{};         // K — Gaussian curvature
        VertexProperty<double> MinPrincipalCurvatureProperty{};     // κ₂ = H - √(H² - K)
        VertexProperty<double> MaxPrincipalCurvatureProperty{};     // κ₁ = H + √(H² - K)

        // Per-vertex mean curvature normal: Hn = (1/2A_i) Σ_j (cot α_ij + cot β_ij)(x_j - x_i)
        // This is the Laplace-Beltrami of the position function, divided by 2.
        // Its magnitude is |H|, its direction is the surface normal (for smooth surfaces).
        VertexProperty<glm::vec3> MeanCurvatureNormalProperty{};

        // Unit tangent principal-curvature directions from the Taubin tensor.
        // PrincipalDir1 is the direction of the maximum principal curvature (κ₁),
        // PrincipalDir2 the direction of the minimum (κ₂). Published as
        // v:principal_dir1 / v:principal_dir2. Degenerate / boundary / flat
        // vertices receive the zero sentinel (see ComputeCurvatureTensor).
        VertexProperty<glm::vec3> PrincipalDir1Property{};
        VertexProperty<glm::vec3> PrincipalDir2Property{};
    };

    // Result of the per-vertex curvature-tensor (Taubin) estimation: the two
    // principal directions and the tensor-recovered principal curvatures,
    // aligned so PrincipalDir1Property is the direction of MaxPrincipalCurvatureProperty.
    struct CurvatureTensorResult
    {
        VertexProperty<glm::vec3> PrincipalDir1Property{};      // v:principal_dir1 (κ₁ direction)
        VertexProperty<glm::vec3> PrincipalDir2Property{};      // v:principal_dir2 (κ₂ direction)
        VertexProperty<double> MaxPrincipalCurvatureProperty{}; // v:max_principal_curvature (κ₁)
        VertexProperty<double> MinPrincipalCurvatureProperty{}; // v:min_principal_curvature (κ₂)
    };

    // Result of per-vertex scalar curvature computation
    struct MeanCurvatureResult
    {
        VertexProperty<double> Property{};
    };

    struct GaussianCurvatureResult
    {
        VertexProperty<double> Property{};
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
    // Returns nullopt for empty meshes or meshes with no faces.
    [[nodiscard]] std::optional<MeanCurvatureResult> ComputeMeanCurvature(
        HalfedgeMesh::Mesh& mesh);

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
    // Returns nullopt for empty meshes or meshes with no faces.
    [[nodiscard]] std::optional<GaussianCurvatureResult> ComputeGaussianCurvature(
        HalfedgeMesh::Mesh& mesh);

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
    [[nodiscard]] CurvatureField ComputeCurvature(HalfedgeMesh::Mesh& mesh);

    // -------------------------------------------------------------------------
    // Compute the per-vertex curvature tensor and principal directions (Taubin)
    // -------------------------------------------------------------------------
    //
    // Estimates the per-vertex 3×3 curvature tensor following Taubin, "Estimating
    // the tensor of curvature of a surface from a polyhedral approximation"
    // (ICCV 1995): for each 1-ring edge (i,j) it accumulates
    //   M_i = Σ_j w_ij κ_ij T_ij T_ijᵀ,   Σ_j w_ij = 1,
    // with directional curvature κ_ij = 2 nᵢ·(x_j − x_i) / ‖x_j − x_i‖², tangent
    // direction T_ij = normalize((I − nᵢnᵢᵀ)(x_j − x_i)), and area-derived weights.
    // M_i is eigen-decomposed with Geometry::PCA::SymmetricEigen3; the eigenvector
    // aligned with nᵢ is discarded and the two tangent eigenvectors become the
    // principal directions, with κ₁ = 3λ_a − λ_b and κ₂ = 3λ_b − λ_a recovered per
    // Taubin and aligned to the direction each came from.
    //
    // Fail-closed (GEOM-005/GEOM-007): flat 1-rings, boundary (open) vertices, and
    // zero-area 1-rings receive the zero sentinel direction and keep their
    // scalar-derived (H/K) principal curvatures; no NaN/Inf is ever written.
    // Returns nullopt for empty meshes or meshes with no faces.
    [[nodiscard]] std::optional<CurvatureTensorResult> ComputeCurvatureTensor(
        HalfedgeMesh::Mesh& mesh);

} // namespace Geometry::Curvature
