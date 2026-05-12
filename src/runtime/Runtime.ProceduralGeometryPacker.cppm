module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

export module Extrinsic.Runtime.ProceduralGeometryPacker;

import Extrinsic.ECS.Component.ProceduralGeometryRef;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Runtime.ProceduralGeometry;

export namespace Extrinsic::Runtime
{
    // Vertex layout for the runtime-owned procedural surface packer. Matches the
    // existing minimal-triangle GpuWorld upload contract: position (3 * float32)
    // followed by uv (2 * float32) for 20 bytes per vertex. GRAPHICS-031 owns
    // any future expansion; the layout is fixed for this implementation slice.
    struct ProceduralVertex
    {
        float Px = 0.0f;
        float Py = 0.0f;
        float Pz = 0.0f;
        float U = 0.0f;
        float V = 0.0f;
    };
    static_assert(sizeof(ProceduralVertex) == 20);

    struct ProceduralGeometryPackBuffer
    {
        std::vector<std::byte> VertexBytes;
        std::vector<std::uint32_t> SurfaceIndices;
        std::vector<std::uint32_t> LineIndices;

        void Clear() noexcept
        {
            VertexBytes.clear();
            SurfaceIndices.clear();
            LineIndices.clear();
        }
    };

    [[nodiscard]] const char* DebugNameFor(ProceduralGeometryKind kind) noexcept;

    [[nodiscard]] std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Pack(
        ProceduralGeometryKind kind,
        const ProceduralGeometryParams& params,
        ProceduralGeometryPackBuffer& outBuffer);
}
