module;

#include <glm/glm.hpp>

export module Geometry.Triangle;

export namespace Geometry
{
    struct Triangle
    {
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};
        glm::vec3 C{0.0f};

        [[nodiscard]] glm::vec3 GetCentroid() const;
        [[nodiscard]] glm::vec3 GetNormal() const;
        [[nodiscard]] float GetArea() const;
    };

    [[nodiscard]] glm::vec3 ClosestPoint(const Triangle& triangle, const glm::vec3& point);
    [[nodiscard]] double SquaredDistance(const Triangle& triangle, const glm::vec3& point);
    [[nodiscard]] double Distance(const Triangle& triangle, const glm::vec3& point);
}
