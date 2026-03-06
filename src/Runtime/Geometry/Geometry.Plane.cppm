module;

#include <cmath>
#include <glm/glm.hpp>

export module Geometry:Plane;

export namespace Geometry
{
    struct Plane
    {
        glm::vec3 Normal{0.0f, 1.0f, 0.0f};
        float Distance{0.0f};

        void Normalize()
        {
            const float len = glm::length(Normal);
            if (len < 1e-6f)
            {
                Normal = glm::vec3(0.0f, 1.0f, 0.0f);
                Distance = 0.0f;
                return;
            }
            Normal /= len;
            Distance /= len;
        }
    };

    [[nodiscard]] Plane Normalized(Plane plane)
    {
        plane.Normalize();
        return plane;
    }

    [[nodiscard]] double SignedDistance(const Plane& plane, const glm::vec3& point)
    {
        return static_cast<double>(glm::dot(plane.Normal, point) + plane.Distance);
    }

    [[nodiscard]] double Distance(const Plane& plane, const glm::vec3& point)
    {
        return std::abs(SignedDistance(plane, point));
    }

    [[nodiscard]] glm::vec3 ClosestPoint(const Plane& plane, const glm::vec3& point)
    {
        return point - plane.Normal * static_cast<float>(SignedDistance(plane, point));
    }

    [[nodiscard]] glm::vec3 ProjectPoint(const Plane& plane, const glm::vec3& point)
    {
        return ClosestPoint(plane, point);
    }
}
