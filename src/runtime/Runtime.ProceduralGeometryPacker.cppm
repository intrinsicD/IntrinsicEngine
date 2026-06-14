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
    // Vertex layout for the runtime-owned procedural surface packer. Matches
    // `MeshVertex` so the promoted surface, depth, and face-selection shaders
    // consume one surface vertex format: position, texture coordinate, and a
    // dedicated normal. U/V carry texture coordinates only.
    struct ProceduralVertex
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
    static_assert(sizeof(ProceduralVertex) == 32);

    struct ProceduralGeometryPackBuffer
    {
        std::vector<std::byte> VertexBytes;
        std::vector<std::uint32_t> SurfaceIndices;
        std::vector<std::uint32_t> LineIndices;

        void Clear() noexcept;
    };

    [[nodiscard]] const char* DebugNameFor(ProceduralGeometryKind kind) noexcept;

    [[nodiscard]] std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Pack(
        ProceduralGeometryKind kind,
        const ProceduralGeometryParams& params,
        ProceduralGeometryPackBuffer& outBuffer);
}
