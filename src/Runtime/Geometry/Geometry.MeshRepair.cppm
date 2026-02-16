module;

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

export module Geometry:MeshRepair;

import :Properties;
import :HalfedgeMesh;

export namespace Geometry::MeshRepair
{
    // =========================================================================
    // Mesh Repair Operations
    // =========================================================================
    //
    // A collection of mesh repair operations for fixing common defects in
    // real-world triangle meshes: holes, degenerate triangles, and inconsistent
    // face orientations.
    //
    // These operations are designed for robustness on imperfect meshes, using
    // safety iteration limits on all halfedge traversals per the codebase
    // convention.

    // =====================================================================
    // 1. Boundary Loop Detection
    // =====================================================================

    struct BoundaryLoop
    {
        // Ordered list of vertex handles forming the boundary loop
        std::vector<VertexHandle> Vertices;

        // Ordered list of boundary halfedge handles (one per edge in the loop)
        std::vector<HalfedgeHandle> Halfedges;
    };

    // Find all boundary loops in the mesh.
    // Each loop is a cycle of boundary halfedges forming a hole.
    [[nodiscard]] std::vector<BoundaryLoop> FindBoundaryLoops(const Halfedge::Mesh& mesh);

    // =====================================================================
    // 2. Hole Filling
    // =====================================================================
    //
    // Fills holes in a triangle mesh using an advancing-front triangulation
    // approach. For each boundary loop:
    //   1. If the loop has 3 vertices, add a single triangle.
    //   2. Otherwise, iteratively find the ear with the smallest angle and
    //      fill it, producing triangles that minimize deviation from flat.
    //
    // This is a minimum-area advancing-front method that produces reasonable
    // triangulations for most practical holes. For more sophisticated hole
    // filling (e.g., curvature-continuation), see Liepa 2003.

    struct HoleFillingParams
    {
        // Maximum boundary loop size to attempt filling.
        // Very large holes may produce poor results.
        std::size_t MaxLoopSize{500};

        // Whether to apply Laplacian smoothing to filled vertices after filling.
        // (Currently fills use existing boundary vertices only, so this is a no-op.)
        bool SmoothAfterFill{false};
    };

    struct HoleFillingResult
    {
        // Number of holes detected
        std::size_t HolesDetected{0};

        // Number of holes successfully filled
        std::size_t HolesFilled{0};

        // Number of triangles added to fill holes
        std::size_t TrianglesAdded{0};

        // Number of holes skipped (too large or failed)
        std::size_t HolesSkipped{0};
    };

    // Fill all holes in the mesh.
    [[nodiscard]] std::optional<HoleFillingResult> FillHoles(
        Halfedge::Mesh& mesh,
        const HoleFillingParams& params = {});

    // =====================================================================
    // 3. Degenerate Triangle Removal
    // =====================================================================
    //
    // Detects and removes degenerate triangles (zero or near-zero area).
    // A triangle is degenerate if its area is below a threshold, which
    // indicates collinear or coincident vertices.
    //
    // Removal strategy: delete the degenerate face. If this leaves isolated
    // vertices or dangling edges, those are also cleaned up via
    // GarbageCollection.

    struct DegenerateRemovalParams
    {
        // Area threshold below which a triangle is considered degenerate.
        float AreaThreshold{1e-8f};
    };

    struct DegenerateRemovalResult
    {
        // Number of degenerate faces detected
        std::size_t DegenerateFacesFound{0};

        // Number of faces removed
        std::size_t FacesRemoved{0};
    };

    // Remove degenerate triangles from the mesh.
    [[nodiscard]] std::optional<DegenerateRemovalResult> RemoveDegenerateFaces(
        Halfedge::Mesh& mesh,
        const DegenerateRemovalParams& params = {});

    // =====================================================================
    // 4. Consistent Face Orientation
    // =====================================================================
    //
    // Ensures all faces in a connected component have consistent winding
    // order (all CCW or all CW). Uses BFS from a seed face, propagating
    // orientation through shared edges.
    //
    // If a face's orientation disagrees with its already-oriented neighbor,
    // its halfedge cycle is reversed (flipped).
    //
    // For meshes with multiple connected components, each component is
    // oriented independently.

    struct OrientationResult
    {
        // Number of connected components found
        std::size_t ComponentCount{0};

        // Number of faces that were flipped to achieve consistency
        std::size_t FacesFlipped{0};

        // Whether the mesh was already consistently oriented
        bool WasConsistent{true};
    };

    // Make all face orientations consistent within each connected component.
    [[nodiscard]] std::optional<OrientationResult> MakeConsistentOrientation(
        Halfedge::Mesh& mesh);

    // =====================================================================
    // 5. Combined Repair
    // =====================================================================

    struct RepairParams
    {
        bool RemoveDegenerates{true};
        DegenerateRemovalParams DegenerateParams;

        bool FixOrientation{true};

        bool FillHoles{true};
        HoleFillingParams HoleParams;
    };

    struct RepairResult
    {
        DegenerateRemovalResult DegenerateResult;
        OrientationResult OrientResult;
        HoleFillingResult HoleResult;
    };

    // Run all repair operations in sequence.
    // Order: remove degenerates -> fix orientation -> fill holes
    [[nodiscard]] std::optional<RepairResult> Repair(
        Halfedge::Mesh& mesh,
        const RepairParams& params = {});

} // namespace Geometry::MeshRepair
