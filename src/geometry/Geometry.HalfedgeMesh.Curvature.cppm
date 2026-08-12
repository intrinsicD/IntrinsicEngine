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
        // Vertices whose interior tensor support, or boundary interpolation
        // from such support, was numerically valid. A supported flat vertex
        // can still carry exactly zero curvature.
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

        // Unit tangent directions for maximum and minimum principal curvature.
        // Supported flat vertices and unsupported degenerate vertices receive
        // zero-vector sentinels.
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
    // H = (κ₁ + κ₂) / 2 and K = κ₁κ₂ after scalar smoothing;
    // the standalone Meyer operators remain available through the two functions
    // above and are not mixed into this result.
    [[nodiscard]] CurvatureField ComputeCurvature(HalfedgeMesh::Mesh& mesh);

    // For each interior edge e, forms the signed hinge contribution
    //   M_e = beta_e (|e|/2) t_e t_e^T,
    // then sums incident contributions over the vertex plus its one-ring
    // neighbours and divides by their mixed area. A signed symmetric 3x3
    // Jacobi decomposition identifies the tensor-normal eigenvalue by minimum
    // absolute magnitude, matching the PMP reference scalar formulation.
    // A hinge measures bend across its edge while M_e stores the edge tangent,
    // so each eigenvalue is paired with the complementary tangent eigenvector.
    // Boundary scalars are first interpolated uniformly from non-boundary
    // neighbours. Three simultaneous nonnegative-cotan passes then apply
    // `new = 0.5*old + 0.5*weighted-neighbour-average` through the reusable
    // property smoother. Directions remain the unsmoothed tensor basis and use
    // the geometric normal to resolve zero-eigenvalue ambiguity. Outward convex
    // curvature is positive. Reversing orientation negates curvature along each
    // physical direction, swapping the algebraically ordered max/min slots when
    // they differ.
    //
    // Interior centers exclude boundary support samples; supported boundary
    // vertices inherit scalars and line directions from valid interior
    // neighbours. Triangle conditioning is measured by the scale-independent
    // ratio 2A/l_max^2. Degenerate, non-triangular, non-finite, or sub-threshold
    // faces invalidate their incident support and the one-ring tensor centers
    // that consume it. Those vertices retain finite zero sentinels and are
    // excluded as smoothing sources, preventing unreliable spikes from
    // diffusing into the valid field. Supported flat vertices retain zero
    // scalars and directions. Empty/no-face meshes return nullopt. Storage is
    // O(V + E + F). Runtime is linear for
    // bounded-valence meshes and O(F + E + Σ_v degree(v)²) without that
    // assumption because the reference two-ring quadrature revisits support
    // vertices' incident edges.
    [[nodiscard]] std::optional<CurvatureTensorResult> ComputeCurvatureTensor(
        HalfedgeMesh::Mesh& mesh);

} // namespace Geometry::Curvature
