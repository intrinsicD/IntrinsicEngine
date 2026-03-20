module;

#include <cstddef>
#include <optional>

export module Geometry.AdaptiveRemeshing;

import Geometry.MeshOperator;
import Geometry.Properties;
import Geometry.HalfedgeMesh;

export namespace Geometry::AdaptiveRemeshing
{
    // =========================================================================
    // Curvature-Driven Adaptive Remeshing
    // =========================================================================
    //
    // Extends the standard isotropic remeshing algorithm (Botsch & Kobbelt 2004)
    // with a curvature-driven per-vertex sizing field. High-curvature regions
    // receive shorter target edge lengths, producing more triangles where detail
    // is needed. Flat regions receive longer target edge lengths, reducing
    // unnecessary geometry.
    //
    // Sizing field:
    //   L(v) = L_base / (1 + alpha * |H(v)|)
    //
    // where H(v) is the mean curvature at vertex v and alpha is the
    // CurvatureAdaptation parameter. The result is clamped to
    // [MinEdgeLength, MaxEdgeLength].
    //
    // Per-edge threshold = average of endpoint target lengths.
    //
    // Algorithm (per iteration):
    //   1. Compute curvature field -> sizing field
    //   2. Split edges longer than 4/3 × local target
    //   3. Collapse edges shorter than 4/5 × local target
    //      (subject to link condition and geometry preservation)
    //   4. Equalize valence via edge flips toward target (6 interior, 4 boundary)
    //   5. Tangential Laplacian smoothing projected onto normal plane

    struct AdaptiveRemeshingParams
    {
        // Minimum edge length. 0 = auto (0.1 × mean edge length).
        double MinEdgeLength{0.0};

        // Maximum edge length. 0 = auto (5.0 × mean edge length).
        double MaxEdgeLength{0.0};

        // Curvature adaptation strength.
        // 0 = isotropic (uniform target length = mean edge length).
        // Higher values = more refinement in high-curvature regions.
        double CurvatureAdaptation{1.0};

        // Number of remeshing iterations. Each iteration recomputes the sizing field.
        std::size_t Iterations{5};

        // Tangential smoothing factor per iteration (0 < lambda < 1).
        double Lambda{0.5};

        // If true, boundary vertices are not moved and boundary edges
        // are not collapsed or flipped.
        bool PreserveBoundary{true};

        // Upper bound on split+collapse operations in one iteration.
        // 0 disables this limit.
        std::size_t MaxOpsPerIteration{256};

        // Upper bound on mesh growth relative to input topology.
        // Example: 4.0 means stop splitting once V/E exceed 4x initial counts.
        // <= 0 disables this limit.
        double MaxTopologyGrowthFactor{4.0};

        // If true, build a frozen reference surface and project updated vertices
        // back to nearest reference triangles using KD-tree acceleration.
        bool EnableReferenceProjection{false};

        // K for KD-tree candidate shortlist in nearest-triangle projection.
        std::size_t ReferenceProjectionK{16};

        // Maximum allowed projection distance; <= 0 disables distance clamping.
        double MaxReferenceProjectionDistance{0.0};

        // If true, project split midpoints before inserting new vertices.
        bool ProjectSplitVertices{true};

        // If true, project vertices after tangential smoothing each iteration.
        bool ProjectAfterSmoothing{true};
    };

    // AdaptiveRemeshingResult is structurally identical to Remeshing::RemeshingResult.
    // Both alias Geometry::RemeshingOperationResult to avoid a duplicated definition.
    using AdaptiveRemeshingResult = RemeshingOperationResult;

    // -------------------------------------------------------------------------
    // Curvature-driven adaptive remeshing.
    //
    // Modifies the mesh in-place. After remeshing, call
    // mesh.GarbageCollection() to compact the storage.
    //
    // Returns nullopt if the mesh is empty or has fewer than 2 faces.
    // -------------------------------------------------------------------------
    [[nodiscard]] std::optional<AdaptiveRemeshingResult> AdaptiveRemesh(
        Halfedge::Mesh& mesh,
        const AdaptiveRemeshingParams& params = {});

} // namespace Geometry::AdaptiveRemeshing
