module;

#include <span>

#include <glm/glm.hpp>

export module Geometry.Plane;

export namespace Geometry
{
    struct Plane
    {
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        float Distance{0.0f};

        void Normalize();
    };

    [[nodiscard]] Plane Normalized(Plane plane);
    [[nodiscard]] double SignedDistance(const Plane& plane, const glm::vec3& point);
    [[nodiscard]] double Distance(const Plane& plane, const glm::vec3& point);
    [[nodiscard]] glm::vec3 ClosestPoint(const Plane& plane, const glm::vec3& point);
    [[nodiscard]] glm::vec3 ProjectPoint(const Plane& plane, const glm::vec3& point);
    [[nodiscard]] Plane ToPlane(std::span<const glm::vec3> points);
}
