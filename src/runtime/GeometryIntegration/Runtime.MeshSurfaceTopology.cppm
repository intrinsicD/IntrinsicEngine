module;

#include <cstdint>
#include <vector>

export module Extrinsic.Runtime.MeshSurfaceTopology;

import Extrinsic.ECS.Components.GeometrySources;

export namespace Extrinsic::Runtime
{
    // Validation status for the canonical mesh face-ring walk. This module
    // names topology only; GPU byte packing remains a private extraction
    // implementation detail.
    enum class MeshSurfaceTopologyStatus : std::uint8_t
    {
        Success,
        WrongDomain,
        MissingPositions,
        MissingHalfedgeTopology,
        MissingFaceTopology,
        EmptyMesh,
        InvalidTopology,
        DegenerateAllFaces,
    };

    [[nodiscard]] const char* DebugNameForMeshSurfaceTopologyStatus(
        MeshSurfaceTopologyStatus status) noexcept;

    // Build the gl_PrimitiveID -> source face-row inverse in exactly the same
    // fan-triangulation order used by runtime surface extraction.
    [[nodiscard]] MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleFaceMap(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outTriangleToFace);

    // Build the canonical surface triangle list and its source-face inverse.
    // Both outputs are cleared on entry and on failure.
    [[nodiscard]] MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleTopology(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outSurfaceIndices,
        std::vector<std::uint32_t>& outTriangleToFace);

    // BUG-137 Slice B — same walk, plus the halfedge that produced each emitted
    // triangle corner. `outCornerHalfedges` is parallel to `outSurfaceIndices`:
    // entry `i` is the halfedge whose target vertex is `outSurfaceIndices[i]`.
    // Corner-domain attributes (notably `h:texcoord`) are indexed by that
    // halfedge, so this is what lets GPU upload split a vertex per distinct UV
    // without the mesh itself carrying duplicated topology.
    // All three outputs are cleared on entry and on failure.
    [[nodiscard]] MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleCornerTopology(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outSurfaceIndices,
        std::vector<std::uint32_t>& outTriangleToFace,
        std::vector<std::uint32_t>& outCornerHalfedges);
}
