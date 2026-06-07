module;

#include <glm/glm.hpp>

module Geometry.ConvexHull;

namespace Geometry
{
    glm::vec3 GetVertexCentroid(const ConvexHull& hull)
    {
        if (hull.Vertices.empty())
        {
            return glm::vec3(0.0f);
        }

        glm::vec3 centroid(0.0f);
        for (const glm::vec3& vertex : hull.Vertices)
        {
            centroid += vertex;
        }
        return centroid / static_cast<float>(hull.Vertices.size());
    }

    AABB ComputeAABB(const ConvexHull& hull)
    {
        AABB bounds;
        for (const glm::vec3& vertex : hull.Vertices)
        {
            bounds = Union(bounds, vertex);
        }
        return bounds;
    }
}
