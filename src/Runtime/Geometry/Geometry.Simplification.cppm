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
    // Implementation of Garland & Heckbert (1997) "Surface Simplification
    // Using Quadric Error Metrics". This is the gold-standard algorithm for
    // triangle mesh decimation, widely used in LOD generation, scan cleanup,
    // and mesh optimization.
    //
    // Algorithm overview:
    //   1. For each vertex, compute a quadric Q_v = Σ K_f for all incident
    //      faces f, where K_f is the fundamental error quadric of plane f:
    //      K_f = n_f * n_f^T (outer product of face normal, extended to 4D
    //      with the plane equation ax+by+cz+d=0).
    //
    //   2. For each edge (v_i, v_j), the cost of collapsing is:
    //      Q = Q_i + Q_j
    //      The optimal placement v̄ minimizes v̄^T Q v̄.
    //      If Q is invertible, v̄ = Q^{-1} * [0,0,0,1]^T.
    //      Otherwise, v̄ is chosen from {v_i, v_j, midpoint}.
    //
    //   3. All edges are placed in a min-heap ordered by collapse cost.
    //
    //   4. Iteratively pop the minimum-cost edge, collapse it, and update
    //      the quadrics and costs of affected edges.
    //
    //   5. Stop when the target face count is reached or the minimum cost
    //      exceeds the error threshold.

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
