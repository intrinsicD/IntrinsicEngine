#pragma once

// Graphics.Importers.TriangulationUtils.hpp — Shared polygon triangulation
// utilities for mesh importers.
//
// Polygon fan triangulation is the standard approach for convex polygons
// in file formats like OBJ and PLY. This header centralizes the algorithm
// to avoid duplication across importers.

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Graphics::Importers
{
    // Fan-triangulate a convex polygon given as an ordered list of vertex indices.
    // Appends (N-2) triangles to `outIndices` where N = faceIndices.size().
    // No-op for degenerate faces with fewer than 3 vertices.
    inline void TriangulateFan(std::span<const uint32_t> faceIndices,
                               std::vector<uint32_t>& outIndices)
    {
        if (faceIndices.size() < 3)
            return;

        for (std::size_t i = 1; i + 1 < faceIndices.size(); ++i)
        {
            outIndices.push_back(faceIndices[0]);
            outIndices.push_back(faceIndices[i]);
            outIndices.push_back(faceIndices[i + 1]);
        }
    }
}
