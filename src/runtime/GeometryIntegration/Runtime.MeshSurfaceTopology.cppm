module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Extrinsic.Runtime.MeshSurfaceTopology;

import Extrinsic.ECS.Components.GeometrySources;
import Geometry.HalfedgeMesh;
import Geometry.MeshSoup;

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

    // BUG-137 — the de-indexed vertex table an indexed GPU buffer needs to
    // carry corner-domain UVs: one slot per distinct `(mesh vertex, UV)` pair.
    //
    // This lives here, next to the corner walk, because more than one consumer
    // must produce the *same* split. Renderer upload and property-texture bake
    // cross-check each other through GPU residency (vertex count, index count,
    // and index fingerprint), so a second, independently written split would
    // silently disagree and fail that check rather than render wrongly.
    struct MeshCornerTexcoordSplit
    {
        // Parallel arrays, one entry per GPU vertex slot. `size()` is the
        // de-indexed vertex count, which is >= the mesh vertex count.
        std::vector<std::uint32_t> SourceVertexForSlot{};
        std::vector<glm::vec2> TexcoordForSlot{};
    };

    // Rewrites `surfaceIndices` from mesh-vertex space into split-slot space
    // and fills `outSplit`. `cornerHalfedges` must be parallel to
    // `surfaceIndices` (see `BuildMeshSurfaceTriangleCornerTopology`).
    //
    // A corner whose halfedge falls outside `cornerTexcoords` falls back to
    // `fallbackVertexTexcoords`, and a non-finite UV is repaired to (0,0), so
    // the result is always finite. Returns false — leaving `surfaceIndices`
    // untouched — when the inputs do not line up or an index is out of range.
    [[nodiscard]] bool BuildMeshCornerTexcoordSplit(
        std::span<const glm::vec2> cornerTexcoords,
        std::span<const std::uint32_t> cornerHalfedges,
        std::span<const glm::vec2> fallbackVertexTexcoords,
        std::size_t vertexCount,
        std::vector<std::uint32_t>& surfaceIndices,
        MeshCornerTexcoordSplit& outSplit);

    // Writes `h:texcoord` for every halfedge of `mesh`, whose faces and
    // vertices must be index-identical to `sourceFaces` / `sourceVertexCount`
    // — the relationship `Geometry::Mesh::Conversion::ToHalfedgeMesh` produces
    // from a mesh soup, which adds vertices and faces in source order.
    //
    // `cornerUvs` holds three entries per source face, in that face's corner
    // order. Each halfedge is matched to its corner slot by its target vertex.
    // Boundary halfedges carry no corner of their own, so they repeat a UV
    // already present at their target vertex rather than a default that would
    // read as an extra seam.
    //
    // Returns false — writing nothing — when the meshes do not correspond or a
    // halfedge's target vertex is not a corner of its own face.
    //
    // Shared because both asset materialization and the editor's scratch-mesh
    // round trip must produce the *same* corner mapping: a second, independent
    // implementation would silently disagree about which corner owns which UV.
    [[nodiscard]] bool PublishMeshCornerTexcoords(
        Geometry::HalfedgeMesh::Mesh& mesh,
        std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        std::size_t sourceVertexCount,
        std::span<const glm::vec2> cornerUvs);
}
