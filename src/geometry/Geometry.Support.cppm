module;

#include <concepts>

#include <glm/glm.hpp>

export module Geometry.Support;

import Geometry.Primitives;

export namespace Geometry
{
    glm::vec3 Support(const Sphere& shape, const glm::vec3& direction) noexcept;
    glm::vec3 Support(const AABB& shape, const glm::vec3& direction) noexcept;
    glm::vec3 Support(const Capsule& shape, const glm::vec3& direction) noexcept;
    glm::vec3 Support(const OBB& shape, const glm::vec3& direction) noexcept;
    glm::vec3 Support(const Cylinder& shape, const glm::vec3& direction) noexcept;
    glm::vec3 Support(const Ellipsoid& shape, const glm::vec3& direction);
    glm::vec3 Support(const Segment& shape, const glm::vec3& direction);
    glm::vec3 Support(const Triangle& shape, const glm::vec3& direction);
    glm::vec3 Support(const ConvexHull& shape, const glm::vec3& direction);
    glm::vec3 Support(const Frustum& shape, const glm::vec3& direction);
    glm::vec3 Support(const Ray& shape, const glm::vec3& direction);

    template <typename T>
    concept ConvexShape = requires(const T& s, const glm::vec3& d)
    {
        { Support(s, d) } -> std::convertible_to<glm::vec3>;
    };

    namespace Internal
    {
        glm::vec3 Normalize(const glm::vec3& v);
    }
}
