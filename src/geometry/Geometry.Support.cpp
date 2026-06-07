module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.Support;

import Geometry.RobustPredicates;

namespace Geometry
{
    namespace Internal
    {
        glm::vec3 Normalize(const glm::vec3& v)
        {
            float lenSq = glm::length2(v);
            if (lenSq < 1e-12f) return {0, 1, 0};
            return v * glm::inversesqrt(lenSq);
        }
    }

    glm::vec3 Support(const Sphere& shape, const glm::vec3& direction) noexcept
    {
        auto dir = Internal::Normalize(direction);
        return shape.Center + dir * shape.Radius;
    }

    glm::vec3 Support(const AABB& shape, const glm::vec3& direction) noexcept
    {
        return {
            (direction.x >= 0.0f) ? shape.Max.x : shape.Min.x,
            (direction.y >= 0.0f) ? shape.Max.y : shape.Min.y,
            (direction.z >= 0.0f) ? shape.Max.z : shape.Min.z
        };
    }

    glm::vec3 Support(const Capsule& shape, const glm::vec3& direction) noexcept
    {
        float dotA = glm::dot(direction, shape.PointA);
        float dotB = glm::dot(direction, shape.PointB);
        glm::vec3 segmentSupport = (dotA > dotB) ? shape.PointA : shape.PointB;

        float lenSq = glm::length2(direction);
        if (Geometry::RobustPredicates::ApproxZeroSq(static_cast<double>(lenSq), 1.0, 1.0e-3))
            return segmentSupport;

        return segmentSupport + (direction * (shape.Radius * glm::inversesqrt(lenSq)));
    }

    glm::vec3 Support(const OBB& shape, const glm::vec3& direction) noexcept
    {
        auto dir = Internal::Normalize(direction);
        glm::vec3 localDir = glm::conjugate(shape.Rotation) * dir;

        glm::vec3 localSupport = glm::vec3(
            (localDir.x >= 0.0f) ? shape.Extents.x : -shape.Extents.x,
            (localDir.y >= 0.0f) ? shape.Extents.y : -shape.Extents.y,
            (localDir.z >= 0.0f) ? shape.Extents.z : -shape.Extents.z
        );

        return shape.Center + (shape.Rotation * localSupport);
    }

    glm::vec3 Support(const Cylinder& shape, const glm::vec3& direction) noexcept
    {
        auto dir = Internal::Normalize(direction);
        glm::vec3 axis = shape.PointB - shape.PointA;
        float axisLen2 = glm::length2(axis);

        float dirDotAxis = glm::dot(dir, axis);
        glm::vec3 capCenter = (dirDotAxis > 0.0f) ? shape.PointB : shape.PointA;

        if (!Geometry::RobustPredicates::ApproxZeroSq(static_cast<double>(axisLen2), 1.0, 1.0e-3))
        {
            glm::vec3 axisDir = dir - axis * (dirDotAxis / axisLen2);
            float perpLen2 = glm::length2(axisDir);

            if (!Geometry::RobustPredicates::ApproxZeroSq(static_cast<double>(perpLen2), 1.0, 1.0e-3))
            {
                return capCenter + axisDir * (shape.Radius * glm::inversesqrt(perpLen2));
            }
        }

        return capCenter;
    }

    glm::vec3 Support(const Ellipsoid& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        glm::vec3 localDir = glm::conjugate(shape.Rotation) * dir;
        glm::vec3 normal = localDir * shape.Radii;

        float len2 = glm::length2(normal);
        const glm::vec3 absRadii = glm::abs(shape.Radii);
        const double axisLocalScale = static_cast<double>(std::min({absRadii.x, absRadii.y, absRadii.z}));
        if (Geometry::RobustPredicates::ApproxZeroSq(static_cast<double>(len2), axisLocalScale, 1.0e-3))
            return shape.Center;

        glm::vec3 unitSpherePoint = normal * glm::inversesqrt(len2);
        glm::vec3 localSupport = unitSpherePoint * shape.Radii;
        return shape.Center + (shape.Rotation * localSupport);
    }

    glm::vec3 Support(const Segment& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        float dotA = glm::dot(dir, shape.A);
        float dotB = glm::dot(dir, shape.B);
        return (dotA > dotB) ? shape.A : shape.B;
    }

    glm::vec3 Support(const Triangle& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        float dotA = glm::dot(dir, shape.A);
        float dotB = glm::dot(dir, shape.B);
        float dotC = glm::dot(dir, shape.C);

        if (dotA > dotB && dotA > dotC) return shape.A;
        if (dotB > dotC) return shape.B;
        return shape.C;
    }

    glm::vec3 Support(const ConvexHull& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        float maxDot = -std::numeric_limits<float>::infinity();
        glm::vec3 bestPt(0.0f);

        const glm::vec3* ptr = shape.Vertices.data();
        const std::size_t count = shape.Vertices.size();

        for (std::size_t i = 0; i < count; ++i)
        {
            float dot = glm::dot(ptr[i], dir);
            if (dot > maxDot)
            {
                maxDot = dot;
                bestPt = ptr[i];
            }
        }
        return bestPt;
    }

    glm::vec3 Support(const Frustum& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        int bestIdx = 0;
        float maxDot = glm::dot(shape.Corners[0], dir);

        for (int i = 1; i < 8; ++i)
        {
            float d = glm::dot(shape.Corners[i], dir);
            if (d > maxDot)
            {
                maxDot = d;
                bestIdx = i;
            }
        }
        return shape.Corners[bestIdx];
    }

    glm::vec3 Support(const Ray& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        float alignment = glm::dot(dir, shape.Direction);

        if (alignment > 0.0f)
        {
            return shape.Origin + shape.Direction * 1e5f;
        }

        return shape.Origin;
    }
}
