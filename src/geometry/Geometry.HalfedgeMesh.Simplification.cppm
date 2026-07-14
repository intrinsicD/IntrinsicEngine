module;

#include <cstddef>
#include <optional>
#include <string>

export module Geometry.Simplification;

import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::Simplification
{
    // =========================================================================
    // Error-metric selection (GEOM-014)
    // =========================================================================
    //
    // ClassicalQEM selects the legacy quadric-only contract: the collapse cost
    // is the configured quadric error, with no GEOM-014 feature pinning or
    // normal-consistency penalty.
    //
    // FA_QEM is a scoped, paper-inspired adaptation of Bhosikar, Savalia,
    // Tiwari, and Bhowmick (arXiv:2605.14029, 2026), and is the new default. It
    // does not reproduce that paper's multi-term quadric equations or optimal
    // placement formulation. The engine adaptation augments the configured
    // quadric path with three feature-preserving mechanisms that are otherwise
    // off:
    //   * sharp-feature pinning  — corner vertices where >= 3 feature edges meet
    //     are immovable; feature-line vertices may only collapse along the line,
    //   * normal-consistency penalty — collapses that swing adjacent face
    //     normals cost more (NormalWeight),
    //   * boundary turning-angle pinning — when PreserveBoundary is false, the
    //     product BoundaryWeight*CurvatureWeight tightens the turning-angle
    //     threshold used to promote boundary vertices to immovable corners.
    // FA_QEM never makes a collapse *cheaper* than ClassicalQEM, so the existing
    // quality guards (normal cones, Hausdorff, aspect ratio) keep their meaning.
    enum class Metric
    {
        ClassicalQEM,
        FA_QEM
    };

    enum class QuadricType
    {
        Plane,
        Triangle,
        Point
    };

    enum class QuadricProbabilisticMode
    {
        Deterministic,
        Isotropic,
        Covariance
    };

    enum class QuadricResidence
    {
        Vertices,
        Faces,
        VerticesAndFaces
    };

    enum class CollapsePlacementPolicy
    {
        KeepSurvivor,
        QuadricMinimizer,
        BestOfEndpointsAndMinimizer
    };

    struct QuadricOptions
    {
        QuadricType Type{QuadricType::Plane};
        QuadricProbabilisticMode ProbabilisticMode{QuadricProbabilisticMode::Deterministic};
        QuadricResidence Residence{QuadricResidence::Vertices};
        CollapsePlacementPolicy PlacementPolicy{CollapsePlacementPolicy::KeepSurvivor};

        // When vertex-resident quadrics are assembled from incident faces, divide
        // by incident-count to keep scale comparable across irregular valence.
        bool AverageVertexQuadrics{true};

        // When face-resident quadrics are assembled for a collapse, divide by the
        // number of contributing faces to keep the domain scale predictable.
        bool AverageFaceQuadrics{false};

        // Isotropic uncertainty parameters used by the probabilistic factories.
        double PositionStdDev{0.0};
        double NormalStdDev{0.0};

        // Optional covariance property names for QuadricProbabilisticMode::Covariance.
        // Missing properties are treated as zero covariance.
        std::string VertexPositionCovarianceProperty{"v:quadric_sigma_p"};
        std::string FacePositionCovarianceProperty{"f:quadric_sigma_p"};
        std::string FaceNormalCovarianceProperty{"f:quadric_sigma_n"};
    };

    // =========================================================================
    // QEM Mesh Simplification with Normal Cones & Hausdorff Tracking
    // =========================================================================
    //
    // Greedy edge-collapse simplification using configurable quadric error
    // metrics (plane / triangle / point) that can live on vertices, faces,
    // or both. Collapse legality is enforced by a battery of optional quality
    // guards: normal-cone deviation, Hausdorff distance, aspect ratio, edge
    // length, and valence limits.
    //
    // Error metric (Params::Metric):
    //   * Metric::ClassicalQEM — quadric error only (Garland & Heckbert 1997
    //     family, including the probabilistic-quadric extensions configured via
    //     QuadricOptions). No GEOM-014 feature pinning. This is the legacy
    //     quadric-only contract retained for parity comparison.
    //   * Metric::FA_QEM — the default, scoped paper-inspired adaptation of
    //     Bhosikar et al. (arXiv:2605.14029, 2026). Adds sharp-feature
    //     corner/line pinning, a normal-consistency cost penalty, and boundary
    //     turning-angle pinning; it is not an equation-level implementation of
    //     the paper's multi-term quadric and optimal placement formulation.
    //     See the Metric enum and Params field comments for the weights. FA_QEM
    //     only ever raises a collapse cost, so existing quality guards retain
    //     their meaning.
    // Variants C (intrinsic-error metric) and D (line-quadric) from the GEOM-014
    // survey are intentionally not implemented here; they are follow-up tasks.

    // Configuration for mesh simplification
    struct Params
    {
        QuadricOptions Quadric{};

        // Error-metric selection. FA_QEM (the default since GEOM-014) adds the
        // feature-aware terms documented on the Metric enum. ClassicalQEM keeps
        // the legacy quadric-only cost and disables all GEOM-014 feature paths.
        Metric Metric{Metric::FA_QEM};

        // FA_QEM only. Dihedral angle (degrees) above which an interior edge is
        // a sharp feature edge. A vertex where >= 3 feature edges meet (e.g. a
        // cube corner) is pinned; a vertex on a feature line (exactly 2 feature
        // edges) may only collapse along that line. Boundary edges always count
        // as feature edges.
        double FeatureAngleThresholdDegrees{45.0};

        // FA_QEM only. Weight of the normal-consistency penalty folded into each
        // collapse cost. Larger values more strongly reject collapses that swing
        // the surrounding face normals. Zero disables the penalty.
        double NormalWeight{1.0};

        // FA_QEM only. Multiplied by CurvatureWeight to tighten the boundary
        // turning-angle threshold when PreserveBoundary is false. Zero prevents
        // corner promotion by this weighted threshold; ordinary feature-line
        // classification remains controlled by PreserveSharpFeatures.
        double BoundaryWeight{1.0};

        // FA_QEM only. Multiplied by BoundaryWeight to tighten the boundary
        // turning-angle threshold, so high-turning-angle boundary vertices are
        // promoted to immovable corners before gentler boundary vertices.
        double CurvatureWeight{1.0};

        // FA_QEM only. Pin sharp-feature corner and feature-line vertices so
        // aggressive decimation cannot erase them. No effect under ClassicalQEM.
        bool PreserveSharpFeatures{true};

        // FA_QEM only. When the mesh carries a "v:texcoord" property, treat
        // texcoord-bearing boundary (UV-seam) vertices as immovable even when
        // PreserveBoundary is false. With vertex-resident texcoords a UV seam is
        // a geometric boundary loop, so this pins those loops. No-op when no
        // texcoord property is present.
        bool PreserveUvSeams{true};

        // Target number of faces. The algorithm stops when FaceCount() <= TargetFaces.
        // Set to 0 to use only the error threshold.
        std::size_t TargetFaces{0};

        // Maximum allowed error per collapse. Set to a very large value to
        // use only the face count target.
        double MaxError{1e30};

        // If true, the current open boundary is treated as immutable: boundary
        // edges are not collapsed and no collapse may touch a boundary vertex.
        bool PreserveBoundary{true};

        // If true, forbid collapsing a boundary vertex into an interior vertex.
        // This preserves open-boundary classification even when boundary edges
        // are otherwise allowed to collapse.
        bool ForbidBoundaryInteriorCollapse{true};

        // Minimum number of incident faces required on the removed vertex.
        // Values < 2 are treated as disabled.
        std::size_t MinRemovedVertexIncidentFaces{2};

        // Optional local quality constraints. Zero disables the check.
        std::size_t MaxValence{0};
        double MaxEdgeLength{0.0};
        double MaxAspectRatio{0.0};

        // Maximum allowed normal deviation in degrees. When > 0, normal cones
        // track accumulated deviation per face. Collapses that would exceed
        // half this angle are rejected.
        double MaxNormalDeviationDegrees{0.0};

        // Maximum allowed Hausdorff distance from the simplified surface to
        // the original. When > 0, per-face point lists track the original
        // surface and collapses that violate the bound are rejected.
        double HausdorffError{0.0};
    };

    // Result of simplification
    struct Result
    {
        // Number of collapses performed
        std::size_t CollapseCount{0};

        // Final face count
        std::size_t FinalFaceCount{0};

        // Maximum error of any performed collapse
        double MaxCollapseError{0.0};

        // --- Diagnostics (GEOM-014) ---

        // Directed collapse evaluations rejected by the topology/link
        // condition (IsCollapseOk), including heap construction and local
        // recomputation after successful collapses.
        std::size_t CollapsesRejectedTopology{0};

        // Candidate positions rejected by a quality guard
        // (boundary/valence/normal/Hausdorff/aspect or an FA_QEM feature pin),
        // including heap construction and local recomputation.
        std::size_t CollapsesRejectedQuality{0};

        // FA_QEM: number of sharp-feature corner / feature-line vertices pinned
        // so they could never be removed. Zero under ClassicalQEM.
        std::size_t SharpFeatureVerticesPinned{0};

        // FA_QEM: number of UV-seam (texcoord-bearing boundary) vertices pinned.
        // Zero under ClassicalQEM or when no "v:texcoord" property exists.
        std::size_t SeamVerticesPinned{0};
    };

    // -------------------------------------------------------------------------
    // Simplify a mesh using QEM edge collapse
    // -------------------------------------------------------------------------
    //
    // Modifies the mesh in-place. After simplification, call
    // mesh.GarbageCollection() to compact the storage.
    //
    // Returns nullopt if the mesh cannot be simplified (e.g., too few faces,
    // non-manifold input, or all collapses violate the link condition).
    [[nodiscard]] std::optional<Result> Simplify(
        HalfedgeMesh::Mesh& mesh,
        const Params& params);

} // namespace Geometry::Simplification
