module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.PointCloudGeometryPacker;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.GpuWorld;

export namespace Extrinsic::Runtime
{
    // Vertex layout for the runtime point-cloud packer. Point-cloud rendering
    // keeps the retained point 20-byte format (position + neutral UV) consumed
    // by `forward/point.vert` / `forward/point.frag`. UV fields are not used for
    // normal encoding; dedicated normal-buffer residency for point/surfel
    // rendering is owned by a later slice.
    struct PointCloudVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(PointCloudVertex) == 20);

    // Reusable scratch buffer fed to `PackCloud`. Callers own one buffer per
    // extraction-cache instance and reuse it across frames; `PackCloud` clears
    // before refilling so the buffer never grows beyond one cloud payload.
    struct PointCloudPackBuffer
    {
        std::vector<std::byte> VertexBytes;

        void Clear() noexcept;
    };

    // Fail-closed status returned by `PackCloud`. Each value maps to a single
    // diagnostics counter folded into `RuntimeRenderExtractionStats`.
    enum class PointCloudPackStatus : std::uint8_t
    {
        Success,
        WrongDomain,       // Source provenance is not `Domain::PointCloud`.
        MissingPositions,  // `Vertices` PropertySet absent, or `v:position` absent / wrong-typed.
        EmptyCloud,        // No point positions.
        NonFinitePosition, // `v:position` contains NaN / infinity.
    };

    [[nodiscard]] const char* DebugNameForPointCloudPackStatus(PointCloudPackStatus status) noexcept;

    struct PointCloudPackResult
    {
        PointCloudPackStatus Status = PointCloudPackStatus::Success;
        std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Upload{};
    };

    // Pack a promoted ECS point-cloud `GeometrySources` view into one canonical
    // `Graphics::GpuWorld::GeometryUploadDesc`.
    //
    // Point positions (`v:position` on `Vertices`) form the vertex buffer in
    // input order; the retained point pipeline draws the vertex buffer directly
    // so no index buffer is emitted (`SurfaceIndices`/`LineIndices` stay empty).
    // Deleted rows are packed in place (no compaction in this slice; documented
    // as a later concern, matching the graph packer).
    //
    // `LocalSphere` is filled from the point AABB center and half-diagonal so
    // downstream culling/transform sync has a deterministic non-empty local
    // bound; `WorldSphere`/`WorldAabb*` remain zero and are overwritten by
    // per-frame extraction (`ExtractBounds`).
    //
    // `outBuffer` is cleared on entry. The returned descriptor views into
    // `outBuffer`; callers must hand it to `GpuWorld::UploadGeometry` (or copy
    // out) before reusing `outBuffer`. A failure status leaves `outBuffer`
    // cleared and `Upload` empty.
    [[nodiscard]] PointCloudPackResult PackCloud(
        const ECS::Components::GeometrySources::ConstSourceView& view,
        PointCloudPackBuffer& outBuffer);
}
