module;

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <numbers>

export module Geometry:Ellipsoid;

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

        [[nodiscard]] float GetVolume() const
        {
            return (4.0f / 3.0f) * std::numbers::pi_v<float> * Radii.x * Radii.y * Radii.z;
        }
    };

    [[nodiscard]] bool Contains(const Ellipsoid& ellipsoid, const glm::vec3& point)
    {
        const glm::vec3 local = ellipsoid.ToLocal(point);
        const glm::vec3 safeRadii = glm::max(ellipsoid.Radii, glm::vec3(1e-6f));
        const glm::vec3 scaled = local / safeRadii;
        return glm::dot(scaled, scaled) <= 1.0f;
    }

    [[nodiscard]] glm::vec3 ProjectToSurfaceAlongCenterRay(const Ellipsoid& ellipsoid, const glm::vec3& point)
    {
        const glm::vec3 local = ellipsoid.ToLocal(point);
        const glm::vec3 safeRadii = glm::max(ellipsoid.Radii, glm::vec3(1e-6f));
        const glm::vec3 scaled = local / safeRadii;
        const float lenSq = glm::dot(scaled, scaled);
        if (lenSq <= 1e-12f)
        {
            return ellipsoid.ToWorld(glm::vec3{safeRadii.x, 0.0f, 0.0f});
        }
        return ellipsoid.ToWorld(local * glm::inversesqrt(lenSq));
    }

    [[nodiscard]] double Volume(const Ellipsoid& ellipsoid)
    {
        return static_cast<double>(ellipsoid.GetVolume());
    }
}

