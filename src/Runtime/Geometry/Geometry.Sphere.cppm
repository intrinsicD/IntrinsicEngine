module;

#include <algorithm>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <numbers>

export module Geometry:Sphere;

export namespace Geometry
{
    struct Sphere
    {
        glm::vec3 Center{0.0f};
        float Radius{0.0f};

        [[nodiscard]] float GetDiameter() const
        {
            return Radius * 2.0f;
        }

        [[nodiscard]] float GetSurfaceArea() const
        {
            return 4.0f * std::numbers::pi_v<float> * Radius * Radius;
        }

        [[nodiscard]] float GetVolume() const
        {
            return (4.0f / 3.0f) * std::numbers::pi_v<float> * Radius * Radius * Radius;
        }
    };

    [[nodiscard]] glm::vec3 ClosestPoint(const Sphere& sphere, const glm::vec3& point)
    {
        const glm::vec3 delta = point - sphere.Center;
        const float distSq = glm::dot(delta, delta);
        const float radiusSq = sphere.Radius * sphere.Radius;
        if (distSq <= radiusSq)
        {
            return point;
        }
        if (distSq <= 1e-12f)
        {
            return sphere.Center;
        }
        return sphere.Center + delta * (sphere.Radius * glm::inversesqrt(distSq));
    }

    [[nodiscard]] double SignedDistance(const Sphere& sphere, const glm::vec3& point)
    {
        return static_cast<double>(glm::distance(point, sphere.Center) - sphere.Radius);
    }

    [[nodiscard]] double Distance(const Sphere& sphere, const glm::vec3& point)
    {
        return std::max(SignedDistance(sphere, point), 0.0);
    }

    [[nodiscard]] double SquaredDistance(const Sphere& sphere, const glm::vec3& point)
    {
        const double dist = Distance(sphere, point);
        return dist * dist;
    }

    [[nodiscard]] double Volume(const Sphere& sphere)
    {
        return static_cast<double>(sphere.GetVolume());
    }
}
