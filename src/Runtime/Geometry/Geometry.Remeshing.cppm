module;

#include <cstddef>
#include <optional>

export module Geometry:Remeshing;

import :MeshOperator;
import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Remeshing
{
    // =========================================================================
    // Isotropic Remeshing
    // =========================================================================
    //
    // Implementation of the standard isotropic remeshing algorithm
    // (Botsch & Kobbelt, "A Remeshing Approach to Multiresolution Modeling",
    // 2004). Produces a mesh where all edges are approximately the same
    // target length, all triangles are approximately equilateral, and all
    // interior vertex valences are approximately 6.
    //
    // Algorithm (per iteration):
    //   1. Split all edges longer than 4/3 × target_length
    //   2. Collapse all edges shorter than 4/5 × target_length
    //      (subject to link condition and geometry preservation)
    //   3. Flip edges where it improves valence toward the target
    //      (6 for interior vertices, 4 for boundary vertices)
    //   4. Tangential Laplacian smoothing: move each vertex toward
    //      its 1-ring centroid, projected onto the local tangent plane
    //
    // The algorithm preserves mesh boundaries and maintains mesh validity
    // throughout. Multiple iterations progressively improve triangle quality.

    struct RemeshingParams
    {
        // Target edge length. All edges will be driven toward this length.
        // If 0, uses the mean edge length of the input mesh.
        double TargetLength{0.0};

        // Number of remeshing iterations. 5-10 iterations is typical.
        std::size_t Iterations{5};

        // Tangential smoothing factor per iteration (0 < λ < 1).
        double SmoothingLambda{0.5};

        // If true, boundary vertices are not moved and boundary edges
        // are not collapsed or flipped.
        bool PreserveBoundary{true};
    };

    // RemeshingResult and AdaptiveRemeshing::AdaptiveRemeshingResult are identical.
    // Both alias Geometry::RemeshingOperationResult to avoid a duplicated definition.
    using RemeshingResult = Geometry::RemeshingOperationResult;

    // -------------------------------------------------------------------------
    // Remesh a triangle mesh to isotropic quality
    // -------------------------------------------------------------------------
    //
    // Modifies the mesh in-place. After remeshing, call
    // mesh.GarbageCollection() to compact the storage.
    //
    // Returns nullopt if the mesh is empty, non-manifold at any vertex,
    // or has fewer than 2 faces.
    [[nodiscard]] std::optional<RemeshingResult> Remesh(
        Halfedge::Mesh& mesh,
        const RemeshingParams& params = {});

} // namespace Geometry::Remeshing
