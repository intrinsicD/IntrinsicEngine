module;

#include <cstddef>
#include <optional>

export module Geometry:Subdivision;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::Subdivision
{
    // =========================================================================
    // Loop Subdivision for Triangle Meshes
    // =========================================================================
    //
    // Implementation of Loop's subdivision scheme (Loop, "Smooth Subdivision
    // Surfaces Based on Triangles", 1987) with Warren's improved weights
    // (Warren, "Weighted Stencils for Catmull-Clark Subdivision Surfaces",
    // 1998).
    //
    // The algorithm subdivides each triangle into four sub-triangles by
    // inserting a new vertex at the midpoint of each edge, then adjusting
    // both old and new vertex positions using weighted averaging rules:
    //
    //   Even vertices (original): weighted average with 1-ring neighbors
    //     v' = (1 - n*β) * v + β * Σ neighbors
    //     where β = 3/(8*n) for n > 3, β = 3/16 for n = 3
    //
    //   Odd vertices (new edge points):
    //     Interior: v' = 3/8*(v₀+v₁) + 1/8*(v₂+v₃)
    //     Boundary: v' = 1/2*(v₀+v₁)
    //
    //   Boundary even vertices:
    //     v' = 1/8*prev + 3/4*v + 1/8*next
    //
    // Loop subdivision produces C²-continuous limit surfaces everywhere
    // except at extraordinary vertices (valence ≠ 6) where it is C¹.

    struct SubdivisionParams
    {
        // Number of subdivision iterations. Each iteration quadruples
        // the face count: n_faces_out = n_faces_in * 4^Iterations.
        std::size_t Iterations{1};
    };

    struct SubdivisionResult
    {
        // Number of subdivision iterations actually performed
        std::size_t IterationsPerformed{0};

        // Final mesh statistics
        std::size_t FinalVertexCount{0};
        std::size_t FinalFaceCount{0};
    };

    // -------------------------------------------------------------------------
    // Subdivide a triangle mesh using Loop's scheme
    // -------------------------------------------------------------------------
    //
    // Returns a new mesh containing the subdivided result. The input mesh
    // is not modified. Returns nullopt if the input mesh is not a pure
    // triangle mesh or is empty.
    [[nodiscard]] std::optional<SubdivisionResult> Subdivide(
        const Halfedge::Mesh& input,
        Halfedge::Mesh& output,
        const SubdivisionParams& params = {});

} // namespace Geometry::Subdivision
