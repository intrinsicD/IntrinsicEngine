module;

#include <cmath>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry:Ray;

export namespace Geometry
{
    struct Ray
    {
        glm::vec3 Origin{0.0f};
        glm::vec3 Direction{0.0f, 0.0f, 1.0f};

        [[nodiscard]] glm::vec3 GetPoint(float t) const
        {
            return Origin + Direction * t;
        }
    };

    [[nodiscard]] float ClosestPointParameter(const Ray& ray, const glm::vec3& point)
    {
        const float dirLenSq = glm::dot(ray.Direction, ray.Direction);
        if (dirLenSq <= 1e-12f)
        {
            return 0.0f;
        }
        return glm::max(glm::dot(point - ray.Origin, ray.Direction) / dirLenSq, 0.0f);
    }

    [[nodiscard]] glm::vec3 ClosestPoint(const Ray& ray, const glm::vec3& point)
    {
        return ray.GetPoint(ClosestPointParameter(ray, point));
    }

    [[nodiscard]] double SquaredDistance(const Ray& ray, const glm::vec3& point)
    {
        const glm::vec3 delta = point - ClosestPoint(ray, point);
        return static_cast<double>(glm::dot(delta, delta));
    }

    [[nodiscard]] double Distance(const Ray& ray, const glm::vec3& point)
    {
        return std::sqrt(SquaredDistance(ray, point));
    }
}
