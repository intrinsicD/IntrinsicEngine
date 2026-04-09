#pragma once

// Graphics.Importers.VertexDedup.hpp — Spatial-hash vertex deduplication for
// importers that produce unindexed triangle soup (e.g. STL).
//
// The quantize-based VertexKey provides ~10 micron tolerance for unit-scale
// models. For importers that use attribute-index-based deduplication (OBJ),
// this header is not needed — their key types are format-specific.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

namespace Graphics::Importers
{
    namespace Detail
    {
        // Quantization factor: ~10 micron grid cells for unit-scale models.
        // Shared by SpatialVertexKey equality and SpatialVertexKeyHash so the
        // tolerance is defined in exactly one place.
        inline constexpr float kVertexQuantizationFactor = 1e5f;

        [[nodiscard]] inline int32_t QuantizeCoordinate(float v) noexcept
        {
            return static_cast<int32_t>(v * kVertexQuantizationFactor);
        }
    } // namespace Detail

    // Spatial vertex key: positions are quantized to a fixed grid for
    // fuzzy-equality comparison. Two vertices are "equal" if they fall into
    // the same quantized cell.
    struct SpatialVertexKey
    {
        glm::vec3 Position;

        bool operator==(const SpatialVertexKey& other) const
        {
            return Detail::QuantizeCoordinate(Position.x) == Detail::QuantizeCoordinate(other.Position.x)
                && Detail::QuantizeCoordinate(Position.y) == Detail::QuantizeCoordinate(other.Position.y)
                && Detail::QuantizeCoordinate(Position.z) == Detail::QuantizeCoordinate(other.Position.z);
        }
    };

    struct SpatialVertexKeyHash
    {
        std::size_t operator()(const SpatialVertexKey& k) const
        {
            std::size_t h = std::hash<int32_t>()(Detail::QuantizeCoordinate(k.Position.x));
            h ^= std::hash<int32_t>()(Detail::QuantizeCoordinate(k.Position.y)) * 2654435761u;
            h ^= std::hash<int32_t>()(Detail::QuantizeCoordinate(k.Position.z)) * 40503u;
            return h;
        }
    };
}
