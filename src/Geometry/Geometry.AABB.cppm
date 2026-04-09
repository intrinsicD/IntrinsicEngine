module;

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>
#include <array>
#include <span>
#include <vector>
#include <cfloat>

export module Geometry.AABB;

export namespace Geometry
{
    struct AABB
    {
        glm::vec3 Min = glm::vec3(FLT_MAX);
        glm::vec3 Max = glm::vec3(-FLT_MAX);

        [[nodiscard]] constexpr bool IsValid() const
        {
            return (Min.x <= Max.x) && (Min.y <= Max.y) && (Min.z <= Max.z);
        }

        [[nodiscard]] constexpr glm::vec3 GetCenter() const
        {
            return (Min + Max) * 0.5f;
        }

        [[nodiscard]] constexpr glm::vec3 GetExtents() const
        {
            return (Max - Min) * 0.5f;
        }

        [[nodiscard]] constexpr glm::vec3 GetSize() const
        {
            return Max - Min;
        }

        [[nodiscard]] constexpr float GetSurfaceArea() const
        {
            const glm::vec3 size = GetSize();
            return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
        }

        [[nodiscard]] constexpr float GetVolume() const
        {
            const glm::vec3 size = GetSize();
            return size.x * size.y * size.z;
        }

        [[nodiscard]] float GetLongestAxisLength() const
        {
            const glm::vec3 size = GetSize();
            return glm::compMax(size);
        }

        [[nodiscard]] std::array<glm::vec3, 8> GetCorners() const
        {
            return {
                glm::vec3{Min.x, Min.y, Min.z}, glm::vec3{Max.x, Min.y, Min.z},
                glm::vec3{Max.x, Max.y, Min.z}, glm::vec3{Min.x, Max.y, Min.z},
                glm::vec3{Min.x, Min.y, Max.z}, glm::vec3{Max.x, Min.y, Max.z},
                glm::vec3{Max.x, Max.y, Max.z}, glm::vec3{Min.x, Max.y, Max.z}
            };
        }
    };

    // Transform an AABB by a 4x4 matrix, returning the axis-aligned bounding
    // box of the transformed corners. Useful for world-space AABB computation
    // from local-space geometry + model matrix.
    [[nodiscard]] AABB TransformAABB(const AABB& box, const glm::mat4& m)
    {
        AABB result;
        for (const auto& corner : box.GetCorners())
        {
            const glm::vec3 tp = glm::vec3(m * glm::vec4(corner, 1.0f));
            result.Min = glm::min(result.Min, tp);
            result.Max = glm::max(result.Max, tp);
        }
        return result;
    }

    [[nodiscard]] glm::vec3 ClosestPoint(const AABB& aabb, const glm::vec3& point)
    {
        return glm::clamp(point, aabb.Min, aabb.Max);
    }

    AABB Union(std::span<const AABB> aabbs)
    {
        AABB result;
        for (const auto& aabb : aabbs)
        {
            result.Min = glm::min(result.Min, aabb.Min);
            result.Max = glm::max(result.Max, aabb.Max);
        }
        return result;
    };

    AABB Union(const AABB& aabb, const glm::vec3& point)
    {
        AABB result;
        result.Min = glm::min(aabb.Min, point);
        result.Max = glm::max(aabb.Max, point);
        return result;
    }

    AABB Union(const AABB& aabb, const AABB& other)
    {
        AABB result;
        result.Min = glm::min(aabb.Min, other.Min);
        result.Max = glm::max(aabb.Max, other.Max);
        return result;
    }

    AABB Intersection(const AABB& a, const AABB& b)
    {
        AABB result;
        result.Min = glm::max(a.Min, b.Min);
        result.Max = glm::min(a.Max, b.Max);
        return result;
    }

    std::vector<AABB> ToAABB(std::span<const glm::vec3> points)
    {
        std::vector<AABB> result;
        result.reserve(points.size());
        for (const auto& point : points)
        {
            result.emplace_back(point, point);
        }
        return result;
    }

    double Volume(const AABB& aabb)
    {
        return aabb.GetVolume();
    }

    // Returns unsigned distance from point to AABB surface.
    // Returns 0 if point is inside or on the surface.
    double Distance(const AABB& a, const glm::vec3& p)
    {
        const glm::vec3 delta = p - ClosestPoint(a, p);
        return glm::length(delta);
    }

    // Returns signed distance: negative inside, positive outside.
    double SignedDistance(const AABB& a, const glm::vec3& p)
    {
        glm::vec3 d = glm::abs(p - a.GetCenter()) - a.GetExtents();
        return glm::length(glm::max(d, glm::vec3(0.0f))) +
               glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
    }

    double SquaredDistance(const AABB& a, const glm::vec3& p)
    {
        const glm::vec3 delta = p - ClosestPoint(a, p);
        return glm::dot(delta, delta);
    }

    double SquaredDistance(const AABB& a, const AABB& b)
    {
        const glm::vec3 delta = glm::max(glm::max(a.Min - b.Max, b.Min - a.Max), glm::vec3(0.0f));
        return glm::dot(delta, delta);
    }

    double Distance(const AABB& a, const AABB& b)
    {
        return glm::length(glm::max(glm::max(a.Min - b.Max, b.Min - a.Max), glm::vec3(0.0f)));
    }
}
