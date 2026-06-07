module;

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.Cylinder;

import Geometry.Sphere;

namespace Geometry
{
    float Cylinder::GetHeight() const
    {
        return glm::distance(PointA, PointB);
    }

    float Cylinder::GetVolume() const
    {
        return std::numbers::pi_v<float> * Radius * Radius * GetHeight();
    }

    namespace
    {
        [[nodiscard]] glm::vec3 CylinderFallbackPerpendicular(const glm::vec3& axisDir)
        {
            const glm::vec3 up = (std::abs(axisDir.y) < 0.999f)
                ? glm::vec3{0.0f, 1.0f, 0.0f}
                : glm::vec3{1.0f, 0.0f, 0.0f};
            const glm::vec3 tangent = glm::cross(axisDir, up);
            const float lenSq = glm::dot(tangent, tangent);
            if (lenSq <= 1e-12f)
            {
                return glm::vec3{1.0f, 0.0f, 0.0f};
            }
            return tangent * glm::inversesqrt(lenSq);
        }
    }

    glm::vec3 ClosestPoint(const Cylinder& cylinder, const glm::vec3& point)
    {
        const glm::vec3 axis = cylinder.PointB - cylinder.PointA;
        const float axisLen = glm::length(axis);
        if (axisLen <= 1e-6f)
        {
            return ClosestPoint(Sphere{cylinder.PointA, cylinder.Radius}, point);
        }

        const glm::vec3 axisDir = axis / axisLen;
        const glm::vec3 center = cylinder.GetCenter();
        const float halfHeight = axisLen * 0.5f;
        const glm::vec3 rel = point - center;
        const float axial = glm::dot(rel, axisDir);
        const glm::vec3 radial = rel - axisDir * axial;
        const float radialLen = glm::length(radial);

        if (radialLen <= cylinder.Radius && std::abs(axial) <= halfHeight)
        {
            return point;
        }

        const float clampedAxial = glm::clamp(axial, -halfHeight, halfHeight);
        const float clampedRadial = glm::min(radialLen, cylinder.Radius);
        glm::vec3 radialDir = (radialLen > 1e-6f)
            ? (radial / radialLen)
            : CylinderFallbackPerpendicular(axisDir);

        return center + axisDir * clampedAxial + radialDir * clampedRadial;
    }

    double SignedDistance(const Cylinder& cylinder, const glm::vec3& point)
    {
        const glm::vec3 axis = cylinder.PointB - cylinder.PointA;
        const float axisLen = glm::length(axis);
        if (axisLen <= 1e-6f)
        {
            return SignedDistance(Sphere{cylinder.PointA, cylinder.Radius}, point);
        }

        const glm::vec3 axisDir = axis / axisLen;
        const glm::vec3 rel = point - cylinder.GetCenter();
        const float axial = glm::dot(rel, axisDir);
        const glm::vec3 radial = rel - axisDir * axial;
        const glm::vec2 d = glm::abs(glm::vec2(glm::length(radial), axial)) -
                            glm::vec2(cylinder.Radius, axisLen * 0.5f);
        return static_cast<double>(glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, glm::vec2(0.0f))));
    }

    double Distance(const Cylinder& cylinder, const glm::vec3& point)
    {
        return std::max(SignedDistance(cylinder, point), 0.0);
    }

    double SquaredDistance(const Cylinder& cylinder, const glm::vec3& point)
    {
        const double dist = Distance(cylinder, point);
        return dist * dist;
    }

    double Volume(const Cylinder& cylinder)
    {
        return static_cast<double>(cylinder.GetVolume());
    }
}
