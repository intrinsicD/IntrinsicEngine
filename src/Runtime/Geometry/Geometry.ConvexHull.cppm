module;

#include <glm/glm.hpp>
#include <span>
#include <vector>

export module Geometry:ConvexHull;

import :AABB;
import :Plane;

export namespace Geometry
{
    struct ConvexHull
    {
        std::vector<glm::vec3> Vertices{};
        std::vector<Plane> Planes{};

        [[nodiscard]] bool IsEmpty() const
        {
            return Vertices.empty();
        }
    };

    [[nodiscard]] glm::vec3 GetVertexCentroid(const ConvexHull& hull)
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

    [[nodiscard]] AABB ComputeAABB(const ConvexHull& hull)
    {
        AABB bounds;
        for (const glm::vec3& vertex : hull.Vertices)
        {
            bounds = Union(bounds, vertex);
        }
        return bounds;
    }
}

