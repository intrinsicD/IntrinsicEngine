module;

#include <array>
#include <span>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

export module Geometry.OBB;

import Geometry.AABB;

export namespace Geometry
{
    struct OBB
    {
        glm::vec3 Center{0.0f};
        glm::vec3 Extents{1.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};

        [[nodiscard]] bool IsValid() const;
        [[nodiscard]] glm::vec3 GetSize() const;
        [[nodiscard]] float GetSurfaceArea() const;
        [[nodiscard]] float GetVolume() const;
        [[nodiscard]] float GetLongestAxisLength() const;
        [[nodiscard]] std::array<glm::vec3, 3> GetAxes() const;
        [[nodiscard]] std::array<glm::vec3, 8> GetCorners() const;
        [[nodiscard]] glm::vec3 ToLocal(const glm::vec3& worldPos) const;
        [[nodiscard]] glm::vec3 ToWorld(const glm::vec3& localPos) const;
    };

    [[nodiscard]] glm::vec3 ClosestPoint(const OBB& obb, const glm::vec3& point);
    OBB Union(const OBB& obb, const glm::vec3& point);
    OBB Union(const OBB& a, const OBB& b);
    OBB Union(std::span<const OBB> obbs);
    double Volume(const OBB& obb);
    double SignedDistance(const OBB& a, const glm::vec3& p);
    double Distance(const OBB& a, const glm::vec3& p);
    double SquaredDistance(const OBB& a, const glm::vec3& p);
    [[nodiscard]] AABB ToAABB(const OBB& obb);
    [[nodiscard]] OBB ToOOBB(std::span<const glm::vec3> points);
}
