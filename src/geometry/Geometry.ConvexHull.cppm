module;

#include <span>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.ConvexHull;

import Geometry.AABB;
import Geometry.Plane;

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

    [[nodiscard]] glm::vec3 GetVertexCentroid(const ConvexHull& hull);
    [[nodiscard]] AABB ComputeAABB(const ConvexHull& hull);
}
