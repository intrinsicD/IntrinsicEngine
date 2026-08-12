// Exposes discrete scalar and edge-dihedral tensor curvature operators so mesh
// analysis and runtime publication share canonical per-vertex fields.
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
    // Dimensionless twice-area / longest-edge-squared floor for float-position
    // triangles. This is sqrt(float epsilon), rounded conservatively: below it,
    // inverse-area cotan/tensor terms amplify position roundoff beyond the same
    // sqrt-epsilon budget, so principal values/directions are not published.
    inline constexpr double kMinimumReliableTriangleQuality = 3.5e-4;

    struct VertexCurvature
    {
        double MeanCurvature{0.0};
        double GaussianCurvature{0.0};
        double MinPrincipalCurvature{0.0};
        double MaxPrincipalCurvature{0.0};
    };

    struct CurvatureDiagnostics
    {
        // Vertices whose Framework24 two-ring tensor support was numerically
        // valid. A supported flat vertex can still carry zero curvature.
        std::size_t SupportedVertexCount{0};
        std::size_t NonZeroPrincipalVertexCount{0};
        double MinimumPrincipalValue{0.0};
        double MaximumPrincipalValue{0.0};
        std::size_t DegenerateFaceCount{0};
        std::size_t IllConditionedFaceCount{0};
        std::size_t UnsupportedFaceCount{0};
        double MinimumTriangleQuality{0.0};
        double TriangleQualityThreshold{
            kMinimumReliableTriangleQuality};
    };

    struct CurvatureField
    {
        VertexProperty<double> MeanCurvatureProperty{};
        VertexProperty<double> GaussianCurvatureProperty{};
        VertexProperty<double> MinPrincipalCurvatureProperty{};
        VertexProperty<double> MaxPrincipalCurvatureProperty{};

        // Equals -H times the oriented unit vertex normal. Its magnitude is |H|.
        VertexProperty<glm::vec3> MeanCurvatureNormalProperty{};

        // Framework24 tensor eigenvectors paired directly with maximum and
        // minimum principal curvature. Supported flat vertices retain a
        // finite eigensolver basis; unsupported vertices receive zeros.
        VertexProperty<glm::vec3> PrincipalDir1Property{};
        VertexProperty<glm::vec3> PrincipalDir2Property{};
        CurvatureDiagnostics Diagnostics{};
    };

    struct CurvatureTensorResult
    {
        VertexProperty<glm::vec3> PrincipalDir1Property{};
        VertexProperty<glm::vec3> PrincipalDir2Property{};
        VertexProperty<double> MaxPrincipalCurvatureProperty{};
        VertexProperty<double> MinPrincipalCurvatureProperty{};
        CurvatureDiagnostics Diagnostics{};
    };

    struct MeanCurvatureResult
    {
        VertexProperty<double> Property{};
    };

    struct GaussianCurvatureResult
    {
        VertexProperty<double> Property{};
    };

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

    // Computes the coherent edge-dihedral principal system described below.
    // H = (κ₁ + κ₂) / 2 and K = κ₁κ₂ hold exactly at every vertex; the
    // standalone Meyer operators remain available through the two functions
    // above and are not mixed into this result.
    [[nodiscard]] CurvatureField ComputeCurvature(HalfedgeMesh::Mesh& mesh);

    // Deterministic port of Framework24 CurvatureTaubin(mesh, 3, true,
    // Policy::Sequential). For every interior edge e it forms
    //   M_e = beta_e (|e|/2) t_e t_e^T.
    // Each vertex sums the incident hinges and legacy Voronoi areas of itself
    // plus its adjacent vertices (Framework24's two-ring support). A symmetric
    // 3x3 decomposition removes the smallest-absolute eigenvalue as the normal
    // mode and pairs each remaining algebraically ordered value directly with
    // its tensor eigenvector. Open-boundary vertices use the same tensor path.
    //
    // Principal scalar values then receive three nonnegative-cotan full-
    // neighbour replacement passes. Updates are intentionally in-place and
    // execute in stable vertex-index order, matching Framework24's sequential
    // policy while avoiding its default ParallelUnsequential data race.
    // Directions are not smoothed. Parity assumes Framework24's default
    // all-false v_feature property; this API has no private feature-mask input.
    // The legacy area formula, cotan clamp, sign, eigenvalue ordering, and
    // direct direction pairing are preserved exactly; consequently its values
    // are not interchangeable with the standalone Meyer operators above.
    // Curvature units are inverse caller-coordinate units. This function does
    // not reproduce Framework24 MeshIo's separate centering/AABB-normalization
    // side effect.
    //
    // Triangle conditioning uses the scale-independent ratio 2A/l_max^2.
    // Degenerate, non-triangular, non-finite, or sub-threshold faces invalidate
    // affected support, which retains finite zero sentinels and cannot enter
    // smoothing. Supported flat vertices keep zero scalars and a finite tensor
    // basis. Empty/no-face meshes return nullopt. Storage is O(V + E + F);
    // work is linear for bounded-valence meshes and includes the explicit
    // two-ring traversal plus three edge-neighbour passes.
    [[nodiscard]] std::optional<CurvatureTensorResult> ComputeCurvatureTensor(
        HalfedgeMesh::Mesh& mesh);

} // namespace Geometry::Curvature
