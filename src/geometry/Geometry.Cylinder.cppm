module;

#include <glm/glm.hpp>

export module Geometry.Cylinder;

export namespace Geometry
{
    struct Cylinder
    {
        glm::vec3 PointA{0.0f};
        glm::vec3 PointB{0.0f};
        float Radius{0.0f};

        [[nodiscard]] glm::vec3 GetCenter() const
        {
            return (PointA + PointB) * 0.5f;
        }

        [[nodiscard]] glm::vec3 GetAxis() const
        {
            return PointB - PointA;
        }

        [[nodiscard]] float GetHeight() const;
        [[nodiscard]] float GetVolume() const;
    };

    [[nodiscard]] glm::vec3 ClosestPoint(const Cylinder& cylinder, const glm::vec3& point);
    [[nodiscard]] double SignedDistance(const Cylinder& cylinder, const glm::vec3& point);
    [[nodiscard]] double Distance(const Cylinder& cylinder, const glm::vec3& point);
    [[nodiscard]] double SquaredDistance(const Cylinder& cylinder, const glm::vec3& point);
    [[nodiscard]] double Volume(const Cylinder& cylinder);
}
