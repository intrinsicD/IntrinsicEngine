module;

#include <array>
#include <cfloat>
#include <span>
#include <vector>

#include <glm/glm.hpp>

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

        [[nodiscard]] float GetLongestAxisLength() const;
        [[nodiscard]] std::array<glm::vec3, 8> GetCorners() const;
    };

    [[nodiscard]] AABB TransformAABB(const AABB& box, const glm::mat4& m);
    [[nodiscard]] glm::vec3 ClosestPoint(const AABB& aabb, const glm::vec3& point);
    AABB Union(std::span<const AABB> aabbs);
    AABB Union(const AABB& aabb, const glm::vec3& point);
    AABB Union(const AABB& aabb, const AABB& other);
    AABB Intersection(const AABB& a, const AABB& b);
    std::vector<AABB> ToAABB(std::span<const glm::vec3> points);
    double Volume(const AABB& aabb);
    double Distance(const AABB& a, const glm::vec3& p);
    double SignedDistance(const AABB& a, const glm::vec3& p);
    double SquaredDistance(const AABB& a, const glm::vec3& p);
    double SquaredDistance(const AABB& a, const AABB& b);
    double Distance(const AABB& a, const AABB& b);
}
