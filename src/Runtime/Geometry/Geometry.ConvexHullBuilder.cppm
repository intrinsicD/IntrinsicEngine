module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:ConvexHullBuilder;

import :Primitives;
import :HalfedgeMesh;

export namespace Geometry::ConvexHullBuilder
{
    // =========================================================================
    // 3D Convex Hull Builder — Quickhull Algorithm
    // =========================================================================
    //
    // Computes the convex hull of a set of 3D points using the Quickhull
    // algorithm (Barber, Dobkin & Huhdanpaa, "The Quickhull Algorithm for
    // Convex Hulls", ACM Transactions on Mathematical Software, 1996).
    //
    // The algorithm proceeds as follows:
    //
    //   1. INITIAL SIMPLEX: Find 6 extreme points (min/max on each axis),
    //      select 2 most distant as baseline, find 3rd most distant from line,
    //      then 4th most distant from the triangle plane to form a tetrahedron
    //      with outward-facing normals.
    //
    //   2. CONFLICT ASSIGNMENT: Partition remaining points into "conflict
    //      lists" — each unprocessed point is assigned to the hull face it
    //      lies farthest above (positive signed distance from the face plane).
    //      Points not above any face are interior and discarded.
    //
    //   3. ITERATIVE EXPANSION: While any face has a non-empty conflict list:
    //      a. Select the conflict point farthest from its owning face (the
    //         "eye point").
    //      b. BFS from the owning face to find all faces visible from the
    //         eye point (positive signed distance from the eye).
    //      c. Extract the "horizon" — the ordered boundary edges between
    //         visible and non-visible faces.
    //      d. Create new triangular faces connecting the eye point to each
    //         horizon edge.
    //      e. Redistribute orphaned conflict points from deleted visible
    //         faces to the newly created faces.
    //      f. Delete visible faces.
    //
    //   4. EXTRACTION: Collect hull vertices (renumbered), face planes (H-Rep),
    //      and optionally a Halfedge::Mesh representation.
    //
    // Complexity:
    //   Expected:   O(n log n) for randomly distributed points.
    //   Worst-case: O(n^2) for adversarial point distributions.
    //
    // Robustness:
    //   - Epsilon-based distance tests to handle near-coplanar and near-
    //     coincident points without infinite loops.
    //   - Degenerate inputs (collinear, coplanar, fewer than 4 non-degenerate
    //     points) return nullopt.
    //   - Safety iteration limits on all loops that traverse hull topology.

    struct ConvexHullParams
    {
        // Distance threshold below which a point is considered on-plane.
        // Points within this distance of a face are treated as coplanar and
        // will not cause that face to be expanded.
        double DistanceEpsilon{1e-8};

        // Whether to compute the H-Rep (half-space planes) for the hull.
        // Always true in practice — SDF, SAT, and containment all need planes.
        bool ComputePlanes{true};

        // Whether to produce a Halfedge::Mesh representation of the hull.
        // Useful for subsequent mesh operations (smoothing, subdivision, etc.).
        bool BuildMesh{false};
    };

    struct ConvexHullResult
    {
        // The computed convex hull with V-Rep (vertices on the hull boundary)
        // and optionally H-Rep (face planes with outward normals).
        Geometry::ConvexHull Hull;

        // Optional Halfedge::Mesh representation (populated if BuildMesh=true).
        Halfedge::Mesh Mesh;

        // Number of input points.
        std::size_t InputPointCount{0};

        // Number of unique vertices on the hull boundary.
        std::size_t HullVertexCount{0};

        // Number of triangular faces on the hull.
        std::size_t HullFaceCount{0};

        // Number of edges on the hull (by Euler: E = V + F - 2).
        std::size_t HullEdgeCount{0};

        // Number of input points that were interior (not on the hull).
        std::size_t InteriorPointCount{0};
    };

    // -------------------------------------------------------------------------
    // Build the 3D convex hull from a set of points
    // -------------------------------------------------------------------------
    //
    // Returns the convex hull with both V-Rep and H-Rep (if requested).
    //
    // Returns nullopt if:
    //   - Fewer than 4 points
    //   - All points are coincident (within epsilon)
    //   - All points are collinear (no triangle can be formed)
    //   - All points are coplanar (no tetrahedron can be formed)
    [[nodiscard]] std::optional<ConvexHullResult> Build(
        std::span<const glm::vec3> points,
        const ConvexHullParams& params = {});

    // -------------------------------------------------------------------------
    // Build the 3D convex hull from a Halfedge::Mesh's vertex positions
    // -------------------------------------------------------------------------
    //
    // Convenience overload that extracts vertex positions from the mesh and
    // delegates to the span-based Build(). Skips deleted vertices.
    [[nodiscard]] std::optional<ConvexHullResult> BuildFromMesh(
        const Halfedge::Mesh& mesh,
        const ConvexHullParams& params = {});

} // namespace Geometry::ConvexHullBuilder
