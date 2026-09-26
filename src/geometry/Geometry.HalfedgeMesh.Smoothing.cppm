// Exposes topology-preserving mesh filters and scalar/vector graph filters
// independently of runtime property storage and domain provenance.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Smoothing;

export import Geometry.Smoothing.Types;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.DEC;

export namespace Geometry::Smoothing
{
    // Undirected edges appear once; inactive/deleted source rows are compacted by the caller.
    struct PropertyEdge { std::size_t A{}, B{}; double Weight{1.0}; };
    struct PropertyFilterResult
    {
        bool Success{};
        std::string Diagnostic{};
        std::vector<double> Values{};
        std::size_t OperatorApplications{};
    };
    [[nodiscard]] bool ValidatePropertyFilterParams(const PropertyFilterParams&) noexcept;
    // Validated preparation shared by FilterProperty and GPU backends of the explicit filters.
    // Fixed and isolated rows keep their input values. Degree holds the initial weighted degree;
    // Rate is the maximum degree for combinatorial Laplacians and 1 for random walk. Spectral
    // heat applies HeatSplits steps of the series sum_k HeatCoefficients[k] (I - L/Rate)^k,
    // normalized by HeatMass.
    struct PropertyFilterPlan
    {
        std::size_t Count{}, Channels{};
        std::vector<bool> Fixed{}, Isolated{};
        std::vector<double> Degree{};
        double Rate{};
        std::size_t HeatSplits{};
        std::array<double, 19> HeatCoefficients{};
        double HeatMass{};
    };
    // Nullopt, with a diagnostic, for invalid parameters, shapes, live values, fixed rows,
    // masses or neighborhoods.
    [[nodiscard]] std::optional<PropertyFilterPlan> PlanPropertyFilter(
        std::span<const double> values, std::size_t channels,
        std::span<const PropertyEdge> edges, const PropertyFilterParams& params,
        std::span<const std::size_t> fixedRows, std::span<const double> lumpedMass,
        std::string& diagnostic);
    // Restores fixed and isolated rows to their input and rejects non-finite results; shared by
    // FilterProperty and GPU backends so both publish under the same rules.
    [[nodiscard]] PropertyFilterResult CompletePropertyFilter(const PropertyFilterPlan& plan,
        std::span<const double> input, std::vector<double> values, std::string backend);
    // Values are row-major, with 1..4 channels. Failure never returns partial values.
    // Fixed rows retain their input values; implicit solves eliminate them exactly.
    // LumpedMass is implicit-only and requires one positive finite mass per row.
    [[nodiscard]] PropertyFilterResult FilterProperty(
        std::span<const double> values, std::size_t channels,
        std::span<const PropertyEdge> edges, const PropertyFilterParams& params,
        std::span<const std::size_t> fixedRows = {},
        std::span<const double> lumpedMass = {});
    // Symmetric union kNN; excludes self IDs and retains distinct coincident samples.
    [[nodiscard]] std::optional<std::vector<PropertyEdge>> BuildPropertyNeighborhood(
        std::span<const glm::vec3> positions, std::uint32_t k,
        PropertyWeight weight, double spatialSigma);

    // -------------------------------------------------------------------------
    // Feature-preserving bilateral mesh denoiser
    // -------------------------------------------------------------------------
    //
    // A two-stage, feature-preserving denoiser for triangle (and polygon)
    // meshes. Unlike the property filters above — which act
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

    // Two-stage feature-preserving denoiser, applied in place: runs Stage 1
    // (FilterFaceNormals) then Stage 2 (normal-projection vertex update,
    // x_i += (1/|F(i)|) Σ_f n_f (n_f · (c_f − x_i)) over VertexIterations,
    // boundary vertices pinned when PreserveBoundary). Topology is preserved.
    // Fail-closed: on any non-Success status the mesh is left unmodified and no
    // NaN/Inf is written (see DenoiseStatus / BilateralDenoiseParams).
    [[nodiscard]] BilateralDenoiseResult DenoiseBilateral(
        HalfedgeMesh::Mesh& mesh, const BilateralDenoiseParams& params);

} // namespace Geometry::Smoothing
