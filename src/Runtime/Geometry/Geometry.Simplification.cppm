module;

#include <cstddef>
#include <optional>

export module Geometry:Simplification;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Simplification
{
    // =========================================================================
    // Quadric Error Metric (QEM) Mesh Simplification
    // =========================================================================
    //
    // The engine uses triangle quadrics derived from Trettner & Kobbelt (2020),
    // "Fast and Robust QEF Minimization using Probabilistic Quadrics", with the
    // probabilistic formulation enabled by default and a deterministic triangle-
    // quadric fallback available for exact/legacy behavior.

    // Configuration for mesh simplification
    struct SimplificationParams
    {
        // Target number of faces. The algorithm stops when FaceCount() <= TargetFaces.
        // Set to 0 to use only the error threshold.
        std::size_t TargetFaces{0};

        // Maximum allowed error per collapse. Set to a very large value to
        // use only the face count target.
        double MaxError{1e30};

        // If true, boundary edges are not collapsed (preserves mesh outline).
        bool PreserveBoundary{true};

        // Weight for boundary edge quadrics. Higher values penalize boundary
        // collapse more strongly. Only used when PreserveBoundary is false.
        double BoundaryWeight{100.0};

        // If true, use probabilistic triangle quadrics (Trettner & Kobbelt 2020).
        // If false, use deterministic triangle quadrics from the same framework.
        bool UseProbabilisticQuadrics{true};

        // Relative positional standard deviation used by the probabilistic model.
        // The absolute sigma is computed as:
        //   sigma = ProbabilisticPositionStdDevFactor * mean_input_edge_length.
        // Setting this to 0 makes the probabilistic model collapse to the
        // deterministic triangle-quadric formulation.
        double ProbabilisticPositionStdDevFactor{0.01};

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
        double MaxNormalDeviationDegrees{0.0};
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
