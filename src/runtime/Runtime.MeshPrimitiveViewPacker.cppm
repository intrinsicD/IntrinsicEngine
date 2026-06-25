module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.MeshPrimitiveViewPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.VertexChannelStreams;

export namespace Extrinsic::Runtime
{
    // Vertex layout for the runtime mesh-primitive-view packer. Primitive
    // views keep the retained line/point 20-byte format (position + neutral
    // UV) consumed by `forward/line.vert` and `forward/point.vert`. Edge views
    // prefer explicit `Edges` rows and can derive a unique wireframe line list
    // from surface halfedge/face topology. UV fields are not used for normal
    // encoding; dedicated normal-buffer residency for point/surfel rendering is
    // owned by a later slice.
    struct MeshPrimitiveVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(MeshPrimitiveVertex) == 20);

    enum class MeshVertexViewRenderMode : std::uint8_t
    {
        FlatCircle,
        SurfaceAlignedCircle,
        ImpostorSphere,
    };

    // Runtime/editor-facing control surface (RUNTIME-088). A mesh entity always
    // renders its filled faces through the `RUNTIME-085` surface-residency
    // bridge; these flags additionally request the optional edge (line-list)
    // and/or vertex (point) views derived from the *same* authoritative mesh
    // `GeometrySources`. The flags live in runtime/editor state, never in ECS
    // components, and never carry graphics/RHI handles. Slice B consumes them
    // in `RenderExtractionCache`; Slice A only defines the vocabulary and the
    // derivation packers.
    struct MeshPrimitiveViewSettings
    {
        bool EnableEdgeView = false;
        bool EnableVertexView = false;
        MeshVertexViewRenderMode VertexRenderMode =
            MeshVertexViewRenderMode::ImpostorSphere;
        float VertexPointRadiusPx = 6.0f;

        [[nodiscard]] bool AnyEnabled() const noexcept
        {
            return EnableEdgeView || EnableVertexView;
        }
    };

    // Reusable scratch buffers fed to the pack functions. Callers own one buffer
    // per extraction-cache instance and reuse it across frames; each pack
    // function clears before refilling so the buffer never grows beyond one view
    // payload. The vertex view leaves `LineIndices` empty.
    struct MeshPrimitiveViewBuffer
    {
        std::vector<std::byte> VertexBytes;
        VertexChannelStreams Channels;
        std::vector<std::uint32_t> LineIndices;

        void Clear() noexcept;
    };

    // Fail-closed status returned by the pack functions. Each value maps to a
    // single diagnostics counter that Slice B will fold into
    // `RuntimeRenderExtractionStats`.
    enum class MeshPrimitiveViewStatus : std::uint8_t
    {
        Success,
        WrongDomain,         // Source provenance is not `Domain::Mesh`.
        MissingPositions,    // `Vertices` PropertySet absent, or `v:position` absent / wrong-typed.
        EmptyMesh,           // No vertex positions.
        MissingEdgeTopology, // Edge view requested but `Edges` / `e:v0` / `e:v1` absent / wrong-typed.
        InvalidEdge,         // Edge endpoint index out of vertex range.
        NonFinitePosition,   // `v:position` contains NaN / infinity.
    };

    [[nodiscard]] const char* DebugNameForMeshPrimitiveViewStatus(MeshPrimitiveViewStatus status) noexcept;

    struct MeshPrimitiveViewResult
    {
        MeshPrimitiveViewStatus Status = MeshPrimitiveViewStatus::Success;
        std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Upload{};
    };

    // Derive an optional retained *edge* (line-list) view from a mesh
    // `GeometrySources` view.
    //
    // Mesh vertex positions (`v:position` on `Vertices`) form the shared vertex
    // buffer in input order so edge endpoints index directly into it (no
    // deleted-row compaction in this slice; deleted vertices/edges are packed in
    // place and documented as a later concern, matching the graph packer). The
    // line lane emits a line-list `LineIndices` of `(e:v0, e:v1)` endpoint pairs
    // read from `Edges` and validates each endpoint is in vertex range. A mesh
    // with zero edges is valid (yields no line indices), mirroring the graph
    // line lane.
    //
    // `LocalSphere` is filled from the vertex AABB center and half-diagonal so
    // downstream culling/transform sync has a deterministic non-empty local
    // bound; `WorldSphere`/`WorldAabb*` remain zero and are overwritten by
    // per-frame extraction (`ExtractBounds`).
    //
    // `outBuffer` is cleared on entry. The returned descriptor views into
    // `outBuffer`; callers must hand it to `GpuWorld::UploadGeometry` (or copy
    // out) before reusing `outBuffer`. A failure status leaves `outBuffer`
    // cleared and `Upload` empty.
    [[nodiscard]] MeshPrimitiveViewResult PackMeshEdgeView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPrimitiveViewBuffer& outBuffer);

    // Derive an optional retained *vertex* (point) view from a mesh
    // `GeometrySources` view.
    //
    // Mesh vertex positions (`v:position` on `Vertices`) form the vertex buffer
    // in input order; the retained point pipeline draws the vertex buffer
    // directly so no index buffer is emitted (`SurfaceIndices`/`LineIndices`
    // stay empty). Deleted rows are packed in place (no compaction in this
    // slice). `LocalSphere`, buffer ownership, and failure semantics match
    // `PackMeshEdgeView`.
    [[nodiscard]] MeshPrimitiveViewResult PackMeshVertexView(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        MeshPrimitiveViewBuffer& outBuffer);
}
