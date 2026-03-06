module;

#include <algorithm>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <numbers>

export module Geometry:Capsule;

import :Segment;

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

        [[nodiscard]] float GetAxisLength() const
        {
            return glm::distance(PointA, PointB);
        }

        [[nodiscard]] float GetVolume() const
        {
            const float axisLength = GetAxisLength();
            return std::numbers::pi_v<float> * Radius * Radius * axisLength +
                   (4.0f / 3.0f) * std::numbers::pi_v<float> * Radius * Radius * Radius;
        }
    };

    [[nodiscard]] glm::vec3 ClosestPointOnAxis(const Capsule& capsule, const glm::vec3& point)
    {
        return ClosestPoint(Segment{capsule.PointA, capsule.PointB}, point);
    }

    [[nodiscard]] glm::vec3 ClosestPoint(const Capsule& capsule, const glm::vec3& point)
    {
        const glm::vec3 axisPoint = ClosestPointOnAxis(capsule, point);
        const glm::vec3 delta = point - axisPoint;
        const float distSq = glm::dot(delta, delta);
        const float radiusSq = capsule.Radius * capsule.Radius;
        if (distSq <= radiusSq)
        {
            return point;
        }
        if (distSq <= 1e-12f)
        {
            return axisPoint;
        }
        return axisPoint + delta * (capsule.Radius * glm::inversesqrt(distSq));
    }

    [[nodiscard]] double SignedDistance(const Capsule& capsule, const glm::vec3& point)
    {
        const glm::vec3 axisPoint = ClosestPointOnAxis(capsule, point);
        return static_cast<double>(glm::distance(point, axisPoint) - capsule.Radius);
    }

    [[nodiscard]] double Distance(const Capsule& capsule, const glm::vec3& point)
    {
        return std::max(SignedDistance(capsule, point), 0.0);
    }

    [[nodiscard]] double SquaredDistance(const Capsule& capsule, const glm::vec3& point)
    {
        const double dist = Distance(capsule, point);
        return dist * dist;
    }

    [[nodiscard]] double Volume(const Capsule& capsule)
    {
        return static_cast<double>(capsule.GetVolume());
    }
}
