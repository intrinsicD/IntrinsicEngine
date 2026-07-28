module;

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

module Extrinsic.Runtime.GeometryPlanBuilders;

import Extrinsic.Graphics.GeometryResidency;
import Extrinsic.Graphics.GpuWorld;

namespace Extrinsic::Runtime
{
    namespace
    {
        constexpr std::uint64_t kFnvOffset64 = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime64 = 1099511628211ull;

        constexpr std::array<ProceduralVertex, 3> kTriangleVertices{{
            {-0.5f, -0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
            { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
            { 0.0f,  0.5f, 0.0f, 0.5f, 1.0f, 0.0f, 0.0f, 1.0f},
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
            desc.DebugName = DebugNameForProceduralGeometryKind(
                ProceduralGeometryKind::Triangle);
            return desc;
        }
    }

    std::uint64_t HashProceduralGeometryParams(
        const ProceduralGeometryParams& params) noexcept
    {
        std::uint64_t hash = kFnvOffset64;
        unsigned char bytes[sizeof(ProceduralGeometryParams)];
        std::memcpy(bytes, &params, sizeof(ProceduralGeometryParams));
        for (const unsigned char byte : bytes)
        {
            hash ^= static_cast<std::uint64_t>(byte);
            hash *= kFnvPrime64;
        }
        return hash;
    }

    const char* DebugNameForProceduralGeometryKind(
        const ProceduralGeometryKind kind) noexcept
    {
        switch (kind)
        {
            case ProceduralGeometryKind::Triangle:
                return "Procedural.Triangle";
        }
        return "Procedural.Unknown";
    }

    void ProceduralGeometryPackBuffer::Clear() noexcept
    {
        VertexBytes.clear();
        SurfaceIndices.clear();
        LineIndices.clear();
    }

    std::optional<Graphics::GeometryUploadPlan> BuildProceduralGeometryPlan(
        const ProceduralGeometryKind kind,
        const ProceduralGeometryParams& params,
        const GeometryPlanBuildRequest& request,
        ProceduralGeometryPackBuffer& outBuffer)
    {
        std::optional<Graphics::GpuWorld::GeometryUploadDesc> upload{};
        switch (kind)
        {
        case ProceduralGeometryKind::Triangle:
            upload = PackTriangle(params, outBuffer);
            break;
        }
        if (!upload.has_value())
            return std::nullopt;
        return Graphics::MakeGeometryUploadPlan(
            request.Key,
            request.Generation,
            *upload,
            request.UpdateClass,
            request.UpdateChannels,
            request.StorageHint);
    }
}
