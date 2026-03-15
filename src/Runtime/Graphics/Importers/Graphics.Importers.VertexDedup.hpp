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
    // Spatial vertex key: positions are quantized to a fixed grid for
    // fuzzy-equality comparison. Two vertices are "equal" if they fall into
    // the same quantized cell.
    struct SpatialVertexKey
    {
        glm::vec3 Position;

        bool operator==(const SpatialVertexKey& other) const
        {
            auto qi = [](float v) { return static_cast<int32_t>(v * 1e5f); };
            return qi(Position.x) == qi(other.Position.x)
                && qi(Position.y) == qi(other.Position.y)
                && qi(Position.z) == qi(other.Position.z);
        }
    };

    struct SpatialVertexKeyHash
    {
        std::size_t operator()(const SpatialVertexKey& k) const
        {
            auto qi = [](float v) { return static_cast<int32_t>(v * 1e5f); };
            std::size_t h = std::hash<int32_t>()(qi(k.Position.x));
            h ^= std::hash<int32_t>()(qi(k.Position.y)) * 2654435761u;
            h ^= std::hash<int32_t>()(qi(k.Position.z)) * 40503u;
            return h;
        }
    };
}
