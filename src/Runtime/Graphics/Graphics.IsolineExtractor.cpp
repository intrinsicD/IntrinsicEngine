module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

module Graphics.IsolineExtractor;

import Geometry.HalfedgeMesh;

using namespace Graphics;
using namespace Geometry;

IsolineExtractor::IsolineResult IsolineExtractor::Extract(
    const Halfedge::Mesh& mesh,
    const std::string& scalarProperty,
    uint32_t isoCount,
    float rangeMin,
    float rangeMax)
{
    IsolineResult result;

    if (isoCount == 0 || rangeMin >= rangeMax)
        return result;

    // Get the scalar property.
    auto prop = mesh.VertexProperties().Get<float>(scalarProperty);
    if (!prop.IsValid())
        return result;

    const auto& values = prop.Vector();

    // Compute iso-values: evenly spaced within (rangeMin, rangeMax),
    // excluding the endpoints to avoid degenerate boundary lines.
    std::vector<float> isoValues(isoCount);
    for (uint32_t i = 0; i < isoCount; ++i)
    {
        isoValues[i] = rangeMin + (rangeMax - rangeMin)
                        * static_cast<float>(i + 1) / static_cast<float>(isoCount + 1);
    }

    // For each face, check each edge for iso-crossings.
    // A triangle has 3 edges. For each iso-value, count crossings:
    // if exactly 2 edges cross, produce a line segment.
    const size_t faceCount = mesh.FacesSize();

    for (size_t fi = 0; fi < faceCount; ++fi)
    {
        FaceHandle f{static_cast<PropertyIndex>(fi)};
        if (mesh.IsDeleted(f))
            continue;

        // Get triangle vertices.
        auto h0 = mesh.Halfedge(f);
        auto h1 = mesh.NextHalfedge(h0);
        auto h2 = mesh.NextHalfedge(h1);

        auto v0 = mesh.ToVertex(h0);
        auto v1 = mesh.ToVertex(h1);
        auto v2 = mesh.ToVertex(h2);

        const float s0 = values[v0.Index];
        const float s1 = values[v1.Index];
        const float s2 = values[v2.Index];

        const glm::vec3& p0 = mesh.Position(v0);
        const glm::vec3& p1 = mesh.Position(v1);
        const glm::vec3& p2 = mesh.Position(v2);

        // For each iso-value, find edge crossings.
        for (const float iso : isoValues)
        {
            // Classify each vertex relative to the iso-value.
            const float d0 = s0 - iso;
            const float d1 = s1 - iso;
            const float d2 = s2 - iso;

            // Find crossing points on the 3 edges.
            glm::vec3 crossings[3];
            int crossCount = 0;

            // Edge v0-v1
            if ((d0 > 0.0f) != (d1 > 0.0f))
            {
                float t = d0 / (d0 - d1);
                crossings[crossCount++] = glm::mix(p0, p1, t);
            }

            // Edge v1-v2
            if ((d1 > 0.0f) != (d2 > 0.0f))
            {
                float t = d1 / (d1 - d2);
                crossings[crossCount++] = glm::mix(p1, p2, t);
            }

            // Edge v2-v0
            if ((d2 > 0.0f) != (d0 > 0.0f))
            {
                float t = d2 / (d2 - d0);
                crossings[crossCount++] = glm::mix(p2, p0, t);
            }

            // Exactly 2 crossings → one line segment through this triangle.
            if (crossCount == 2)
            {
                result.Points.push_back(crossings[0]);
                result.Points.push_back(crossings[1]);
            }
        }
    }

    return result;
}
