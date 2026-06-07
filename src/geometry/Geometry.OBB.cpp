module;

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include <glm/gtx/quaternion.hpp>

module Geometry.OBB;

import Geometry.PCA;

namespace Geometry
{
    bool OBB::IsValid() const
    {
        return Extents.x >= 0.0f && Extents.y >= 0.0f && Extents.z >= 0.0f;
    }

    glm::vec3 OBB::GetSize() const
    {
        return 2.0f * Extents;
    }

    float OBB::GetSurfaceArea() const
    {
        const glm::vec3 size = GetSize();
        return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
    }

    float OBB::GetVolume() const
    {
        const glm::vec3 size = GetSize();
        return size.x * size.y * size.z;
    }

    float OBB::GetLongestAxisLength() const
    {
        const glm::vec3 size = GetSize();
        return glm::compMax(size);
    }

    std::array<glm::vec3, 3> OBB::GetAxes() const
    {
        return {
            glm::rotate(Rotation, glm::vec3(1.0f, 0.0f, 0.0f)),
            glm::rotate(Rotation, glm::vec3(0.0f, 1.0f, 0.0f)),
            glm::rotate(Rotation, glm::vec3(0.0f, 0.0f, 1.0f))
        };
    }

    std::array<glm::vec3, 8> OBB::GetCorners() const
    {
        std::array<glm::vec3, 8> corners;
        glm::vec3 x = glm::rotate(Rotation, glm::vec3(Extents.x, 0, 0));
        glm::vec3 y = glm::rotate(Rotation, glm::vec3(0, Extents.y, 0));
        glm::vec3 z = glm::rotate(Rotation, glm::vec3(0, 0, Extents.z));

        corners[0] = Center + x + y + z;
        corners[1] = Center + x + y - z;
        corners[2] = Center + x - y + z;
        corners[3] = Center + x - y - z;
        corners[4] = Center - x + y + z;
        corners[5] = Center - x + y - z;
        corners[6] = Center - x - y + z;
        corners[7] = Center - x - y - z;
        return corners;
    }

    glm::vec3 OBB::ToLocal(const glm::vec3& worldPos) const
    {
        return glm::conjugate(Rotation) * (worldPos - Center);
    }

    glm::vec3 OBB::ToWorld(const glm::vec3& localPos) const
    {
        return Center + (Rotation * localPos);
    }

    glm::vec3 ClosestPoint(const OBB& obb, const glm::vec3& point)
    {
        const glm::vec3 local = obb.ToLocal(point);
        return obb.ToWorld(glm::clamp(local, -obb.Extents, obb.Extents));
    }

    OBB Union(const OBB& obb, const glm::vec3& point)
    {
        glm::vec3 localP = obb.ToLocal(point);
        glm::vec3 minLocal = -obb.Extents;
        glm::vec3 maxLocal = obb.Extents;

        minLocal = glm::min(minLocal, localP);
        maxLocal = glm::max(maxLocal, localP);

        OBB result;
        result.Rotation = obb.Rotation;
        result.Extents = (maxLocal - minLocal) * 0.5f;
        glm::vec3 localCenterOffset = minLocal + result.Extents;
        result.Center = obb.ToWorld(localCenterOffset);
        return result;
    }

    OBB Union(const OBB& a, const OBB& b)
    {
        if (!b.IsValid()) return a;
        if (!a.IsValid()) return b;

        glm::vec3 minLocal = -a.Extents;
        glm::vec3 maxLocal = a.Extents;

        auto bCorners = b.GetCorners();
        for (const auto& p : bCorners)
        {
            glm::vec3 localP = a.ToLocal(p);
            minLocal = glm::min(minLocal, localP);
            maxLocal = glm::max(maxLocal, localP);
        }

        OBB result;
        result.Rotation = a.Rotation;
        result.Extents = (maxLocal - minLocal) * 0.5f;
        glm::vec3 localCenterOffset = minLocal + result.Extents;
        result.Center = a.ToWorld(localCenterOffset);
        return result;
    }

    OBB Union(std::span<const OBB> obbs)
    {
        if (obbs.empty()) return OBB{};
        if (obbs.size() == 1) return obbs[0];

        OBB result = obbs[0];
        for (size_t i = 1; i < obbs.size(); ++i)
        {
            result = Union(result, obbs[i]);
        }
        return result;
    }

    double Volume(const OBB& obb)
    {
        return obb.GetVolume();
    }

    double SignedDistance(const OBB& a, const glm::vec3& p)
    {
        const glm::vec3 localP = a.ToLocal(p);
        const glm::vec3 d = glm::abs(localP) - a.Extents;
        return glm::length(glm::max(d, glm::vec3(0.0f))) + glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
    }

    double Distance(const OBB& a, const glm::vec3& p)
    {
        const glm::vec3 delta = p - ClosestPoint(a, p);
        return glm::length(delta);
    }

    double SquaredDistance(const OBB& a, const glm::vec3& p)
    {
        const glm::vec3 delta = p - ClosestPoint(a, p);
        return glm::dot(delta, delta);
    }

    AABB ToAABB(const OBB& obb)
    {
        AABB bounds;
        for (const glm::vec3& corner : obb.GetCorners())
        {
            bounds = Union(bounds, corner);
        }
        return bounds;
    }

    OBB ToOOBB(std::span<const glm::vec3> points)
    {
        const auto isFinite = [](const glm::vec3& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        };

        const PCAResult pca = ToPCA(points);
        if (!pca.Valid)
        {
            return OBB{};
        }

        glm::mat3 basis = pca.Eigenvectors;
        glm::vec3 axis0 = glm::normalize(basis[0]);
        glm::vec3 axis1 = glm::normalize(basis[1]);
        glm::vec3 axis2 = glm::normalize(glm::cross(axis0, axis1));
        if (glm::dot(axis2, axis2) <= 1.0e-12f)
        {
            axis2 = glm::vec3{0.0f, 0.0f, 1.0f};
        }
        axis1 = glm::normalize(glm::cross(axis2, axis0));
        basis = glm::mat3{axis0, axis1, axis2};

        glm::vec3 minLocal(std::numeric_limits<float>::max());
        glm::vec3 maxLocal(-std::numeric_limits<float>::max());
        for (const glm::vec3& point : points)
        {
            if (!isFinite(point))
            {
                continue;
            }

            const glm::vec3 local{
                glm::dot(point - pca.Mean, basis[0]),
                glm::dot(point - pca.Mean, basis[1]),
                glm::dot(point - pca.Mean, basis[2]),
            };
            minLocal = glm::min(minLocal, local);
            maxLocal = glm::max(maxLocal, local);
        }

        const glm::vec3 localCenter = 0.5f * (minLocal + maxLocal);

        OBB obb;
        obb.Extents = glm::max(0.5f * (maxLocal - minLocal), glm::vec3{0.0f});
        obb.Center = pca.Mean + basis * localCenter;
        obb.Rotation = glm::normalize(glm::quat_cast(basis));
        return obb;
    }
}
