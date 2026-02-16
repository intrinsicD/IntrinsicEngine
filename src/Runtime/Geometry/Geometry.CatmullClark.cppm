module;

#include <cstddef>
#include <optional>

export module Geometry:CatmullClark;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::CatmullClark
{
    // =========================================================================
    // Catmull-Clark Subdivision for Arbitrary Polygon Meshes
    // =========================================================================
    //
    // Implementation of Catmull-Clark subdivision (Catmull & Clark, "Recursively
    // generated B-spline surfaces on arbitrary topological meshes", 1978).
    //
    // Unlike Loop subdivision which requires pure-triangle input, Catmull-Clark
    // operates on arbitrary polygon meshes (triangles, quads, n-gons). After one
    // iteration, all faces become quadrilaterals. Subsequent iterations produce
    // only quads.
    //
    // The algorithm introduces three types of new vertices per iteration:
    //
    //   Face points: centroid of face vertices
    //     F = (1/n) * sum(face vertices)
    //
    //   Edge points: average of edge endpoints and adjacent face points
    //     E = (v0 + v1 + F_left + F_right) / 4     (interior edge)
    //     E = (v0 + v1) / 2                         (boundary edge)
    //
    //   Vertex points: weighted combination of original, face, and edge averages
    //     V' = (Q/n + 2*R/n + S*(n-3)/n)            (interior vertex, n = valence)
    //     where Q = average of adjacent face points
    //           R = average of adjacent edge midpoints
    //           S = original vertex position
    //     Boundary vertex: V' = (1/8)*prev + (3/4)*V + (1/8)*next
    //
    // Each original face with k edges produces k quad faces. Total new faces
    // = sum of face valences = 2 * |E| (counting both sides of each edge).
    //
    // Catmull-Clark subdivision converges to C^2-continuous limit surfaces
    // everywhere except at extraordinary vertices (valence != 4) where it is C^1.

    struct SubdivisionParams
    {
        // Number of subdivision iterations. Each iteration roughly quadruples
        // face count (exactly so for pure-quad input).
        std::size_t Iterations{1};
    };

    struct SubdivisionResult
    {
        // Number of subdivision iterations actually performed
        std::size_t IterationsPerformed{0};

        // Final mesh statistics
        std::size_t FinalVertexCount{0};
        std::size_t FinalEdgeCount{0};
        std::size_t FinalFaceCount{0};

        // Whether all faces in the output are quads
        bool AllQuads{false};
    };

    // -------------------------------------------------------------------------
    // Subdivide a mesh using the Catmull-Clark scheme
    // -------------------------------------------------------------------------
    //
    // Accepts arbitrary polygon meshes (triangles, quads, n-gons, or mixed).
    // After one iteration, all faces are quads. Returns a new mesh in `output`;
    // `input` is not modified.
    //
    // Returns nullopt if:
    //   - Input mesh is empty
    //   - Iterations is zero
    [[nodiscard]] std::optional<SubdivisionResult> Subdivide(
        const Halfedge::Mesh& input,
        Halfedge::Mesh& output,
        const SubdivisionParams& params = {});

} // namespace Geometry::CatmullClark
