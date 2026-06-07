module;

#include <algorithm>
#include <numbers>

#include <glm/glm.hpp>

module Geometry.Capsule;

import Geometry.Segment;

namespace Geometry
{
    float Capsule::GetAxisLength() const
    {
        return glm::distance(PointA, PointB);
    }

    float Capsule::GetVolume() const
    {
        const float axisLength = GetAxisLength();
        return std::numbers::pi_v<float> * Radius * Radius * axisLength +
               (4.0f / 3.0f) * std::numbers::pi_v<float> * Radius * Radius * Radius;
    }

    glm::vec3 ClosestPointOnAxis(const Capsule& capsule, const glm::vec3& point)
    {
        return ClosestPoint(Segment{capsule.PointA, capsule.PointB}, point);
    }

    glm::vec3 ClosestPoint(const Capsule& capsule, const glm::vec3& point)
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

    double SignedDistance(const Capsule& capsule, const glm::vec3& point)
    {
        const glm::vec3 axisPoint = ClosestPointOnAxis(capsule, point);
        return static_cast<double>(glm::distance(point, axisPoint) - capsule.Radius);
    }

    double Distance(const Capsule& capsule, const glm::vec3& point)
    {
        return std::max(SignedDistance(capsule, point), 0.0);
    }

    double SquaredDistance(const Capsule& capsule, const glm::vec3& point)
    {
        const double dist = Distance(capsule, point);
        return dist * dist;
    }

    double Volume(const Capsule& capsule)
    {
        return static_cast<double>(capsule.GetVolume());
    }
}
