module;

#include <span>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

export module Geometry.Ellipsoid;

export namespace Geometry
{
    struct Ellipsoid
    {
        glm::vec3 Center{0.0f};
        glm::vec3 Radii{1.0f};
        glm::quat Rotation{1.0f, 0.0f, 0.0f, 0.0f};

        [[nodiscard]] glm::vec3 ToLocal(const glm::vec3& worldPos) const
        {
            return glm::conjugate(Rotation) * (worldPos - Center);
        }

        [[nodiscard]] glm::vec3 ToWorld(const glm::vec3& localPos) const
        {
            return Center + (Rotation * localPos);
        }

        [[nodiscard]] float GetVolume() const;
    };

    [[nodiscard]] bool Contains(const Ellipsoid& ellipsoid, const glm::vec3& point);
    [[nodiscard]] glm::vec3 ProjectToSurfaceAlongCenterRay(const Ellipsoid& ellipsoid, const glm::vec3& point);
    [[nodiscard]] double Volume(const Ellipsoid& ellipsoid);
    [[nodiscard]] Ellipsoid ToEllipsoiod(std::span<const glm::vec3> points);
}
