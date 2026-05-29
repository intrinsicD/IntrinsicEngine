module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.GraphGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Runtime
{
    // Vertex layout for the runtime graph packer. Identical to
    // `MeshVertex` (position + UV, 20 bytes) so the retained line
    // (`line.vert`) and point (`point.vert` / `point_retained.frag`,
    // GRAPHICS-071) pipelines consume graph node geometry without a second
    // vertex format. UV is zeroed; graph-driven UV/attribute propagation is
    // owned by later slices.
    struct GraphVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(GraphVertex) == 20);

    // Reusable scratch buffers fed to `PackGraph`. Callers own one buffer per
    // extraction-cache instance and reuse it across frames; `PackGraph` clears
    // before refilling so the buffer never grows beyond one graph payload.
    struct GraphPackBuffer
    {
        std::vector<std::byte> VertexBytes;
        std::vector<std::uint32_t> LineIndices;

        void Clear() noexcept
        {
            VertexBytes.clear();
            LineIndices.clear();
        }
    };

    // Fail-closed status returned by `PackGraph`. Each value maps to a single
    // diagnostics counter that Slice B will fold into
    // `RuntimeRenderExtractionStats`.
    enum class GraphPackStatus : std::uint8_t
    {
        Success,
        WrongDomain,         // `ConstSourceView::ActiveDomain != Domain::Graph`.
        NoRenderLane,        // Neither line nor point lane requested.
        MissingNodes,        // `Nodes` PropertySet absent, or `v:position` absent / wrong-typed.
        EmptyGraph,          // No node positions.
        MissingEdgeTopology, // Line lane requested but `Edges` / `e:v0` / `e:v1` absent / wrong-typed.
        InvalidEdge,         // Edge endpoint index out of node range.
        NonFinitePosition,   // `v:position` contains NaN / infinity.
    };

    [[nodiscard]] const char* DebugNameForGraphPackStatus(GraphPackStatus status) noexcept;

    struct GraphPackResult
    {
        GraphPackStatus Status = GraphPackStatus::Success;
        std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Upload{};
    };

    // Pack a promoted ECS graph `GeometrySources` view into one canonical
    // `Graphics::GpuWorld::GeometryUploadDesc`.
    //
    // Node positions (`v:position` on `Nodes`) form the shared vertex buffer
    // in input order so edge endpoints index directly into it (no deleted-row
    // compaction in this slice; deleted nodes/edges are packed in place and
    // documented as a later concern). The point lane (`wantPoints`) draws the
    // vertex buffer directly; the line lane (`wantLines`) emits a line-list
    // `LineIndices` of `(e:v0, e:v1)` endpoint pairs and validates each
    // endpoint is in range. A graph with zero edges is valid for the line
    // lane (isolated nodes) and yields no line indices.
    //
    // `LocalSphere` is filled from the node AABB center and half-diagonal so
    // downstream culling/transform sync has a deterministic non-empty local
    // bound; `WorldSphere`/`WorldAabb*` remain zero and are overwritten by
    // per-frame extraction (`ExtractBounds`).
    //
    // `outBuffer` is cleared on entry. The returned descriptor views into
    // `outBuffer`; callers must hand it to `GpuWorld::UploadGeometry` (or copy
    // out) before reusing `outBuffer`. A failure status leaves `outBuffer`
    // cleared and `Upload` empty.
    [[nodiscard]] GraphPackResult PackGraph(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        bool wantLines,
        bool wantPoints,
        GraphPackBuffer& outBuffer);
}

