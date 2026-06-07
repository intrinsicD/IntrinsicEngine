module;

#include <array>

#include <glm/glm.hpp>

export module Geometry.Frustum;

import Geometry.AABB;
import Geometry.Plane;

export namespace Geometry
{
    struct Frustum
    {
        std::array<glm::vec3, 8> Corners{};
        std::array<Plane, 6> Planes{};

        [[nodiscard]] glm::vec3 GetCenter() const;
        static Frustum CreateFromMatrix(const glm::mat4& viewProj);
    };

    [[nodiscard]] bool Contains(const Frustum& frustum, const glm::vec3& point);
    [[nodiscard]] AABB ComputeAABB(const Frustum& frustum);
}
