module;

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/component_wise.hpp>

export module Runtime.Geometry.AABB;

export namespace Runtime::Geometry
{
    struct AABB
    {
        glm::vec3 Min = glm::vec3(FLT_MAX);
        glm::vec3 Max = glm::vec3(-FLT_MAX);

        [[nodiscard]] bool IsVaild() const
        {
            return (Min.x <= Max.x) && (Min.y <= Max.y) && (Min.z <= Max.z);
        }

        [[nodiscard]] glm::vec3 GetCenter() const
        {
            return (Min + Max) * 0.5f;
        }

        [[nodiscard]] glm::vec3 GetExtents() const
        {
            return (Max - Min) * 0.5f;
        }

        [[nodiscard]] glm::vec3 GetSize() const
        {
            return Max - Min;
        }

        [[nodiscard]] float GetSurfaceArea() const
        {
            glm::vec3 size = GetSize();
            return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
        }

        [[nodiscard]] float GetVolume() const
        {
            glm::vec3 size = GetSize();
            return size.x * size.y * size.z;
        }

        [[nodiscard]] float GetLongestAxisLength() const
        {
            glm::vec3 size = GetSize();
            return glm::compMax(size);
        }
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
}
