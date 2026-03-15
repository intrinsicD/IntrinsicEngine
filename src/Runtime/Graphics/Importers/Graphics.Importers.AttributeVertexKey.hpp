#pragma once

// =============================================================================
// AttributeVertexKey — attribute-index-based vertex deduplication key.
//
// Used by formats that reference positions, normals, and texture coordinates
// by separate indices (e.g., OBJ). Each unique (p, n, t) triple becomes a
// single output vertex.
//
// For spatial-position-based deduplication (e.g., STL triangle soup), see
// Graphics.Importers.VertexDedup.hpp instead.
// =============================================================================

#include <cstddef>
#include <functional>

namespace Graphics::Importers
{
    struct AttributeVertexKey
    {
        int p = -1, n = -1, t = -1;
        bool operator==(const AttributeVertexKey& other) const
        {
            return p == other.p && n == other.n && t == other.t;
        }
    };

    struct AttributeVertexKeyHash
    {
        std::size_t operator()(const AttributeVertexKey& k) const
        {
            return std::hash<int>()(k.p) ^ (std::hash<int>()(k.n) << 1) ^ (std::hash<int>()(k.t) << 2);
        }
    };
}
