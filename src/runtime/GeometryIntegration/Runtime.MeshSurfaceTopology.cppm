// Canonical runtime mesh face/corner traversal and GPU-only corner-attribute
// splitting.
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

    // Same walk, plus the halfedge that produced each emitted triangle corner.
    // `outCornerHalfedges` is parallel to `outSurfaceIndices`:
    // entry `i` is the halfedge whose target vertex is `outSurfaceIndices[i]`.
    // Corner-domain attributes (`h:texcoord` and `h:normal`) are indexed by that
    // halfedge, which lets GPU upload split distinct shading tuples without the
    // mesh itself carrying duplicated topology.
    // All three outputs are cleared on entry and on failure.
    [[nodiscard]] MeshSurfaceTopologyStatus BuildMeshSurfaceTriangleCornerTopology(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outSurfaceIndices,
        std::vector<std::uint32_t>& outTriangleToFace,
        std::vector<std::uint32_t>& outCornerHalfedges);

    // The compatibility UV-only split used by callers without corner normals:
    // one slot per distinct `(mesh vertex, UV)` pair.
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

    // GPU vertex table for the complete corner shading tuple. Each emitted
    // slot represents one distinct `(mesh vertex, resolved UV, resolved normal)`
    // value, so authored corner discontinuities never alter owning topology.
    struct MeshCornerAttributeSplit
    {
        std::vector<std::uint32_t> SourceVertexForSlot{};
        std::vector<glm::vec2> TexcoordForSlot{};
        std::vector<glm::vec3> NormalForSlot{};
    };

    // Rewrites `surfaceIndices` into split-slot space. Either corner-property
    // span may be empty; at least one must be present. Missing/out-of-range UVs
    // use the vertex fallback then (0,0). Missing, non-finite, or degenerate
    // normals use the normalized vertex fallback then +Z. Returns false without
    // changing `surfaceIndices` when topology inputs do not line up.
    [[nodiscard]] bool BuildMeshCornerAttributeSplit(
        std::span<const glm::vec2> cornerTexcoords,
        std::span<const glm::vec3> cornerNormals,
        std::span<const std::uint32_t> cornerHalfedges,
        std::span<const glm::vec2> fallbackVertexTexcoords,
        std::span<const glm::vec3> fallbackVertexNormals,
        std::size_t vertexCount,
        std::vector<std::uint32_t>& surfaceIndices,
        MeshCornerAttributeSplit& outSplit);

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

    // Normal-domain counterpart to `PublishMeshCornerTexcoords`. The input is
    // three normals per source triangle; values are copied without changing
    // owning mesh cardinality and published as canonical `h:normal`.
    [[nodiscard]] bool PublishMeshCornerNormals(
        Geometry::HalfedgeMesh::Mesh& mesh,
        std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        std::size_t sourceVertexCount,
        std::span<const glm::vec3> cornerNormals);

    // BUG-137 / BUG-147 — per-corner UVs mapped back onto a *source* mesh's
    // faces, so a chart split can be carried as a UV fact instead of being
    // baked into the entity mesh's topology.
    struct MeshCornerTexcoords
    {
        // Three entries per source triangle, in that triangle's corner order.
        std::vector<glm::vec2> CornerUvs{};
        // One representative UV per source vertex; only meaningful when
        // `HasSeam` is false, where it is also the exact per-vertex answer.
        std::vector<glm::vec2> VertexUvs{};
        std::size_t UnmappedCornerCount{0};
        bool HasSeam{false};
    };

    // Maps a chart-split output mesh's per-vertex UVs back onto the corners of
    // the source faces they came from.
    //
    // An unwrapper emits a fresh output vertex per `(chart, source vertex)`
    // pair, so its output carries the seam as duplicated *topology*. This is
    // the inverse: given the output's per-vertex UVs and the two cross-
    // references an unwrapper reports (`sourceFaceForOutputFace`,
    // `sourceVertexForOutputVertex`), it recovers the per-corner UVs of the
    // unsplit source, from which `PublishMeshCornerTexcoords` writes
    // `h:texcoord` onto a mesh that kept its own topology.
    //
    // Takes the cross-references directly rather than an unwrapper result type,
    // so it stays independent of any particular atlas backend and is testable
    // without one.
    //
    // A face the unwrapper dropped leaves its corners unassigned; those inherit
    // a sibling corner at the same vertex, so the buffer stays finite and no
    // new `(vertex, UV)` pair — and therefore no phantom seam — is invented.
    // The count of such corners is reported in `UnmappedCornerCount`.
    //
    // Returns false when the cross-references do not line up with the meshes.
    [[nodiscard]] bool GatherSplitMeshCornerTexcoords(
        const Geometry::MeshSoup::IndexedMesh& outputMesh,
        std::span<const glm::vec2> outputVertexUvs,
        std::span<const std::uint32_t> sourceFaceForOutputFace,
        std::span<const std::uint32_t> sourceVertexForOutputVertex,
        std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        std::size_t sourceVertexCount,
        MeshCornerTexcoords& out);

    // Fills `VertexUvs`, `HasSeam`, and the unmapped-corner inheritance from an
    // already-populated `CornerUvs` + per-corner assignment mask. Exposed for
    // producers that gather corners some other way (authored payload corners,
    // for example) and still need the same finalization.
    [[nodiscard]] bool FinalizeMeshCornerTexcoords(
        MeshCornerTexcoords& corners,
        std::span<const std::uint8_t> cornerAssigned,
        std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces,
        std::size_t sourceVertexCount);
}
