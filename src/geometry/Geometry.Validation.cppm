module;

#include <glm/glm.hpp>

export module Geometry.Validation;

import Geometry.Primitives;

export namespace Geometry::Validation
{
    constexpr float EPSILON = 1e-6f;
    constexpr float EPSILON_SQ = EPSILON * EPSILON;

    [[nodiscard]] bool IsFinite(float v) noexcept;
    [[nodiscard]] bool IsFinite(double v) noexcept;
    [[nodiscard]] bool IsFinite(const glm::vec2& v) noexcept;
    [[nodiscard]] bool IsFinite(const glm::vec3& v) noexcept;
    [[nodiscard]] bool IsFinite(const glm::dvec3& v) noexcept;

    [[nodiscard]] bool IsNormalized(const glm::vec3& v, float tolerance = 1e-4f);
    [[nodiscard]] bool IsZero(const glm::vec3& v, float epsilon = EPSILON);

    [[nodiscard]] bool IsValid(const Sphere& s);
    [[nodiscard]] bool IsValid(const AABB& box);
    [[nodiscard]] bool IsDegenerate(const AABB& box, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const OBB& obb);
    [[nodiscard]] bool IsDegenerate(const OBB& obb, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const Capsule& cap);
    [[nodiscard]] bool IsDegenerate(const Capsule& cap, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const Cylinder& cyl);
    [[nodiscard]] bool IsDegenerate(const Cylinder& cyl, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const Ellipsoid& e);
    [[nodiscard]] bool IsDegenerate(const Ellipsoid& e, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const Triangle& tri);
    [[nodiscard]] bool IsDegenerate(const Triangle& tri, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const Plane& p);
    [[nodiscard]] bool IsValid(const Ray& r);
    [[nodiscard]] bool IsValid(const Segment& seg);
    [[nodiscard]] bool IsDegenerate(const Segment& seg, float epsilon = EPSILON);
    [[nodiscard]] bool IsValid(const ConvexHull& hull);
    [[nodiscard]] bool IsValid(const Frustum& f);

    [[nodiscard]] Sphere Sanitize(const Sphere& s);
    [[nodiscard]] AABB Sanitize(const AABB& box);
    [[nodiscard]] OBB Sanitize(const OBB& obb);
    [[nodiscard]] Ray Sanitize(const Ray& r);
}
