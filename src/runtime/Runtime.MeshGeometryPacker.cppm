module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.MeshGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Runtime
{
    // Vertex layout for the runtime mesh surface packer. Matches the
    // `ProceduralVertex` surface layout consumed by the promoted surface,
    // depth, and face-selection shaders. Mesh-domain `v:texcoord` is copied
    // into U/V when present with one finite vector per vertex; otherwise U/V
    // default to zero so raw imported geometry can render while progressive UV
    // generation is still pending. Normals are carried in dedicated fields from
    // `v:normal` when available and default to +Z otherwise, so UVs are never
    // reused for normal encoding.
    struct MeshVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
        float Nx = 0.0f;
        float Ny = 0.0f;
        float Nz = 1.0f;
    };
    static_assert(sizeof(MeshVertex) == 32);

    // Reusable scratch buffers fed to `PackMesh`. Callers own one buffer per
    // extraction-cache instance and reuse it across frames; `PackMesh` clears
    // before refilling so the buffer never grows beyond one mesh payload.
    struct MeshPackBuffer
    {
        std::vector<std::byte> VertexBytes;
        std::vector<std::uint32_t> SurfaceIndices;

        void Clear() noexcept;
    };

    // Fail-closed status returned by `PackMesh`. Each value maps to a single
    // diagnostics counter that Slice B will fold into
    // `RuntimeRenderExtractionStats`.
    enum class MeshPackStatus : std::uint8_t
    {
        Success,
        WrongDomain,                // Source provenance is not `Domain::Mesh`.
        MissingPositions,           // `v:position` absent / wrong-typed on `Vertices`.
        MissingHalfedgeTopology,    // `Halfedges` PropertySet absent, or `h:to_vertex` / `h:next` absent / wrong-typed.
        MissingFaceTopology,        // `Faces` PropertySet absent, or `f:halfedge` absent / wrong-typed.
        EmptyMesh,                  // No vertices, no halfedges, or no faces.
        InvalidTopology,            // Out-of-range halfedge/face index, mismatched halfedge property arrays, or non-closed face ring.
        NonFinitePosition,          // `v:position` contains NaN / infinity.
        MissingTexcoords,           // Legacy diagnostic name for absent / wrong-typed / count-mismatched `v:texcoord`; packing now falls back to zero U/V.
        NonFiniteTexcoord,          // Legacy diagnostic name for NaN / infinity `v:texcoord`; packing now falls back to zero U/V for invalid entries.
        DegenerateAllFaces,         // Every face produced fewer than three triangle indices (all faces deleted or degenerate).
    };

    [[nodiscard]] const char* DebugNameForMeshPackStatus(MeshPackStatus status) noexcept;

    struct MeshPackResult
    {
        MeshPackStatus Status = MeshPackStatus::Success;
        std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Upload{};
    };

    // Pack a promoted ECS mesh `GeometrySources` view into the canonical
    // `Graphics::GpuWorld::GeometryUploadDesc` triangle-list shape.
    //
    // Algorithm: validate `v:position`, `h:to_vertex`, `h:next`, `h:face`,
    // `f:halfedge`; copy count-matched finite `v:texcoord` into U/V or use
    // zero U/V while progressive UV generation is pending; for each face slot
    // resolve its first halfedge and skip the slot when that halfedge's
    // `h:face` no longer claims the face (`PopulateFromMesh` writes
    // `f:halfedge` for every slot including deleted faces, while
    // `HalfedgeMesh::DeleteFace` clears only the halfedges' `h:face` — so
    // the deleted-face ring is still walkable and must be detected via
    // `h:face` ownership). For live face slots walk the ring (capped at
    // `halfedgeCount` to fail closed on malformed `h:next` cycles),
    // fan-triangulate from the first ring vertex, and emit
    // (ring[0], ring[i], ring[i+1]) surface indices. A ring halfedge whose
    // `h:face` disagrees with the current face after the first halfedge has
    // passed the ownership check indicates a corrupt mesh and is rejected as
    // `InvalidTopology`. Vertex bytes are written in input order so surface
    // indices index directly into the source `Vertices` PropertySet, with
    // `MeshVertex::U/V` populated only from `v:texcoord` and `MeshVertex::N*`
    // populated from `v:normal` when present or the +Z default otherwise.
    // `LocalSphere` is
    // filled from the local AABB center and half-diagonal so downstream
    // culling/transform sync has a deterministic non-empty local bound even
    // before `RUNTIME-082`-style adapters publish a tighter sphere;
    // `WorldSphere`/`WorldAabb*` remain zero — runtime extraction overwrites
    // them with the per-frame world transform via `ExtractBounds` (see
    // `Runtime.RenderExtraction`).
    //
    // `outBuffer` is cleared on entry. The returned `Upload.PackedVertexBytes`
    // and `Upload.SurfaceIndices` view into `outBuffer`; callers must hand the
    // descriptor to `GpuWorld::UploadGeometry` (or copy out) before reusing
    // `outBuffer` for another pack call. Returning a failure status leaves
    // `outBuffer` cleared and `Upload` empty.
    [[nodiscard]] MeshPackResult PackMesh(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPackBuffer& outBuffer);

    // Build the inverse of the GPU surface picking payload: for every surface
    // triangle `PackMesh` emits, record the owning face row (the `Faces`
    // PropertySet slot index). `outTriangleToFace[t]` is the face that produced
    // surface triangle `t`, where `t == gl_PrimitiveID` over the `SurfaceOpaque`
    // draw — exactly the 28-bit payload of an `EncodeSelectionId(Face, ...)`
    // selection id written by `assets/shaders/selection/face_id.frag`. Because
    // a face with an n-gon ring fan-triangulates to `n - 2` GPU triangles, the
    // triangle index is NOT the face row; runtime primitive-selection
    // refinement must map through this table before treating a Face hint as a
    // face. The walk replays `PackMesh`'s exact face-skip and ring traversal
    // (they share one internal helper, so the surface triangle order and this
    // map cannot drift). `outTriangleToFace` is cleared on entry; a non-Success
    // status (mirroring `PackMesh`'s validation) leaves it cleared.
    [[nodiscard]] MeshPackStatus BuildSurfaceTriangleFaceMap(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        std::vector<std::uint32_t>& outTriangleToFace);
}
