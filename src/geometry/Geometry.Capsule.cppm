module;

#include <glm/glm.hpp>

export module Geometry.Capsule;

export namespace Geometry
{
    struct Capsule
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

        [[nodiscard]] float GetAxisLength() const;
        [[nodiscard]] float GetVolume() const;
    };

    [[nodiscard]] glm::vec3 ClosestPointOnAxis(const Capsule& capsule, const glm::vec3& point);
    [[nodiscard]] glm::vec3 ClosestPoint(const Capsule& capsule, const glm::vec3& point);
    [[nodiscard]] double SignedDistance(const Capsule& capsule, const glm::vec3& point);
    [[nodiscard]] double Distance(const Capsule& capsule, const glm::vec3& point);
    [[nodiscard]] double SquaredDistance(const Capsule& capsule, const glm::vec3& point);
    [[nodiscard]] double Volume(const Capsule& capsule);
}
