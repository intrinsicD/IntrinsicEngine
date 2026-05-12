module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

module Extrinsic.Runtime.ProceduralGeometryPacker;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::array<ProceduralVertex, 3> kTriangleVertices{{
            {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f},
            { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f},
            { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f},
        }};

        constexpr std::array<std::uint32_t, 3> kTriangleSurfaceIndices{{0u, 1u, 2u}};

        [[nodiscard]] std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc>
        PackTriangle(const ProceduralGeometryParams& params, ProceduralGeometryPackBuffer& outBuffer)
        {
            // Default-constructed params (VertexCount == 0 / IndexCount == 0) are
            // treated as a request for the canonical reference triangle so the
            // GRAPHICS-029 reference scene can attach an empty Params{} and still
            // receive the minimal visible surface. Authored overrides must match
            // the fixed 3-vertex / 3-index triangle topology.
            const bool isDefault = params.VertexCount == 0u && params.IndexCount == 0u;
            const bool isExplicitTriangle = params.VertexCount == 3u && params.IndexCount == 3u;
            if (!isDefault && !isExplicitTriangle)
            {
                return std::nullopt;
            }

            outBuffer.Clear();
            outBuffer.VertexBytes.resize(sizeof(ProceduralVertex) * kTriangleVertices.size());
            std::memcpy(outBuffer.VertexBytes.data(), kTriangleVertices.data(), outBuffer.VertexBytes.size());
            outBuffer.SurfaceIndices.assign(kTriangleSurfaceIndices.begin(), kTriangleSurfaceIndices.end());

            Extrinsic::Graphics::GpuWorld::GeometryUploadDesc desc{};
            desc.PackedVertexBytes = std::span<const std::byte>{outBuffer.VertexBytes};
            desc.SurfaceIndices = std::span<const std::uint32_t>{outBuffer.SurfaceIndices};
            desc.LineIndices = std::span<const std::uint32_t>{outBuffer.LineIndices};
            desc.VertexCount = static_cast<std::uint32_t>(kTriangleVertices.size());
            desc.LocalBounds = {};
            desc.DebugName = DebugNameFor(ProceduralGeometryKind::Triangle);
            return desc;
        }
    }

    const char* DebugNameFor(ProceduralGeometryKind kind) noexcept
    {
        switch (kind)
        {
            case ProceduralGeometryKind::Triangle:
                return "Procedural.Triangle";
        }
        return "Procedural.Unknown";
    }

    std::optional<Extrinsic::Graphics::GpuWorld::GeometryUploadDesc> Pack(
        ProceduralGeometryKind kind,
        const ProceduralGeometryParams& params,
        ProceduralGeometryPackBuffer& outBuffer)
    {
        switch (kind)
        {
            case ProceduralGeometryKind::Triangle:
                return PackTriangle(params, outBuffer);
        }
        return std::nullopt;
    }
}
