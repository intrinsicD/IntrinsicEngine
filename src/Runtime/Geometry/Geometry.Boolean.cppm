module;

#include <optional>

export module Geometry.Boolean;

import Geometry.HalfedgeMesh;

export namespace Geometry::Boolean
{
    enum class Operation
    {
        Union,
        Intersection,
        Difference
    };

    struct BooleanParams
    {
        // Epsilon used for geometric classification and robust point-in-mesh tests.
        float Epsilon{1e-5f};
    };

    struct BooleanResult
    {
        // True when the operation completed exactly for the current implementation scope.
        bool ExactResult{false};

        // True when the operation fell back to a conservative classifier-based path.
        bool UsedFallback{false};

        // True if the input meshes had an overlapping volume region.
        bool VolumesOverlap{false};

        // Human-readable diagnostic for unsupported overlap topologies.
        const char* Diagnostic{""};
    };

    // Performs mesh CSG over closed triangle-compatible halfedge meshes.
    //
    // Current support envelope:
    // - Disjoint volumes: exact for union/intersection/difference.
    // - Full containment (A inside B or B inside A): exact for union/intersection.
    // - Partial overlap: returns nullopt (requires robust triangle-triangle clipping and remeshing).
    [[nodiscard]] std::optional<BooleanResult> Compute(
        const Halfedge::Mesh& a,
        const Halfedge::Mesh& b,
        Operation op,
        Halfedge::Mesh& out,
        const BooleanParams& params = {});
}
