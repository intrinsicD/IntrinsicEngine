module;

#include <cstddef>
#include <optional>
#include <string>

export module Geometry:Simplification;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Simplification
{
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

    // Configuration for mesh simplification
    struct SimplificationParams
    {
        QuadricOptions Quadric{};

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
    struct SimplificationResult
    {
        // Number of collapses performed
        std::size_t CollapseCount{0};

        // Final face count
        std::size_t FinalFaceCount{0};

        // Maximum error of any performed collapse
        double MaxCollapseError{0.0};
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
    [[nodiscard]] std::optional<SimplificationResult> Simplify(
        Halfedge::Mesh& mesh,
        const SimplificationParams& params);

} // namespace Geometry::Simplification
