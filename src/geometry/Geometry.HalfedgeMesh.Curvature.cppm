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
    struct VertexCurvature
    {
        double MeanCurvature{0.0};
        double GaussianCurvature{0.0};
        double MinPrincipalCurvature{0.0};
        double MaxPrincipalCurvature{0.0};
    };

    struct CurvatureField
    {
        VertexProperty<double> MeanCurvatureProperty{};
        VertexProperty<double> GaussianCurvatureProperty{};
        VertexProperty<double> MinPrincipalCurvatureProperty{};
        VertexProperty<double> MaxPrincipalCurvatureProperty{};

        // Equals -H times the oriented unit vertex normal. Its magnitude is |H|.
        VertexProperty<glm::vec3> MeanCurvatureNormalProperty{};

        // Unit tangent directions for maximum and minimum principal curvature.
        // Unsupported degenerate/flat vertices receive zero-vector sentinels.
        VertexProperty<glm::vec3> PrincipalDir1Property{};
        VertexProperty<glm::vec3> PrincipalDir2Property{};
    };

    struct CurvatureTensorResult
    {
        VertexProperty<glm::vec3> PrincipalDir1Property{};
        VertexProperty<glm::vec3> PrincipalDir2Property{};
        VertexProperty<double> MaxPrincipalCurvatureProperty{};
        VertexProperty<double> MinPrincipalCurvatureProperty{};
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
    // H = (κ₁ + κ₂) / 2 and K = κ₁κ₂ after scalar smoothing;
    // the standalone Meyer operators remain available through the two functions
    // above and are not mixed into this result.
    [[nodiscard]] CurvatureField ComputeCurvature(HalfedgeMesh::Mesh& mesh);

    // For each interior edge e, forms the signed hinge contribution
    //   M_e = beta_e (|e|/2) t_e t_e^T,
    // then sums incident contributions over the vertex plus its one-ring
    // neighbours and divides by their mixed area. The tensor is restricted to
    // the oriented vertex tangent plane before a closed-form 2x2 decomposition.
    // A hinge measures bend across its edge while M_e stores the edge tangent,
    // so each eigenvalue is paired with the complementary tangent eigenvector.
    // Three deterministic nonnegative-cotan passes smooth the principal values;
    // directions remain the unsmoothed tensor basis. Outward convex curvature is
    // positive. Reversing orientation negates curvature along each physical
    // direction, swapping the algebraically ordered max/min slots when they
    // differ.
    //
    // Supported boundary vertices use interior hinges in this two-ring support.
    // Deleted, isolated, flat, zero-area, degenerate, or non-finite support fails
    // closed to finite zero values and zero-vector directions. Empty/no-face
    // meshes return nullopt. Storage is O(V + E + F). Runtime is linear for
    // bounded-valence meshes and O(F + E + Σ_v degree(v)²) without that
    // assumption because the reference two-ring quadrature revisits support
    // vertices' incident edges.
    [[nodiscard]] std::optional<CurvatureTensorResult> ComputeCurvatureTensor(
        HalfedgeMesh::Mesh& mesh);

} // namespace Geometry::Curvature
