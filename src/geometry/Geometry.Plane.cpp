module;

#include <cmath>
#include <span>

#include <glm/glm.hpp>

module Geometry.Plane;

import Geometry.PCA;

namespace Geometry
{
    void Plane::Normalize()
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

    Plane Normalized(Plane plane)
    {
        plane.Normalize();
        return plane;
    }

    double SignedDistance(const Plane& plane, const glm::vec3& point)
    {
        return static_cast<double>(glm::dot(plane.Normal, point) + plane.Distance);
    }

    double Distance(const Plane& plane, const glm::vec3& point)
    {
        return std::abs(SignedDistance(plane, point));
    }

    glm::vec3 ClosestPoint(const Plane& plane, const glm::vec3& point)
    {
        return point - plane.Normal * static_cast<float>(SignedDistance(plane, point));
    }

    glm::vec3 ProjectPoint(const Plane& plane, const glm::vec3& point)
    {
        return ClosestPoint(plane, point);
    }

    Plane ToPlane(std::span<const glm::vec3> points)
    {
        const PCAResult pca = ToPCA(points);

        Plane plane;
        plane.Distance = 0.0f;

        if (!pca.Valid)
        {
            return plane;
        }

        glm::vec3 normal = glm::normalize(glm::vec3{pca.Eigenvectors[2]});
        if (!std::isfinite(normal.x) || !std::isfinite(normal.y) || !std::isfinite(normal.z)
            || glm::dot(normal, normal) <= 1.0e-12f)
        {
            normal = glm::vec3{0.0f, 1.0f, 0.0f};
        }

        plane.Normal = normal;
        plane.Distance = -glm::dot(plane.Normal, pca.Mean);
        plane.Normalize();
        return plane;
    }
}
