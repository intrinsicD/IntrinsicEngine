module;
#include <glm/glm.hpp>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
#include <utility>

module Geometry.EPA;

namespace Geometry::Internal::Detail
{
    std::optional<SeedDirection> SeedExpansion(std::span<const glm::vec3> simplex)
    {
        const glm::dvec3 a(simplex[0]);
        const glm::dvec3 ab = glm::dvec3(simplex[1]) - a;
        glm::dvec3 normal;
        double predicateScale;
        if (simplex.size() == 2)
        {
            predicateScale = glm::dot(ab, ab);
            const glm::dvec3 absolute = glm::abs(ab);
            glm::dvec3 axis(0.0);
            axis[absolute.x <= absolute.y && absolute.x <= absolute.z ? 0 : absolute.y <= absolute.z ? 1 : 2] = 1.0;
            normal = glm::cross(ab, axis);
        }
        else
        {
            normal = glm::cross(ab, glm::dvec3(simplex[2]) - a);
            predicateScale = glm::length(normal);
        }
        const double length = glm::length(normal);
        if (!std::isfinite(length) || !(length > 0.0) ||
            !std::isfinite(predicateScale) || !(predicateScale > 0.0)) return std::nullopt;
        // GJK tests the triple product for a line and the unnormalized plane
        // dot product for a triangle. Express that inherited slack as a length.
        return SeedDirection{glm::vec3(normal / length), double(Config::GJK_EPSILON) / predicateScale};
    }

    PolytopeFace MakeFace(std::span<const glm::vec3> polytope, int i0, int i1, int i2)
    {
        PolytopeFace face{glm::vec3(0.0f), std::numeric_limits<float>::max(), {i0, i1, i2}};
        const glm::dvec3 a(polytope[i0]);
        const glm::dvec3 cross = glm::cross(glm::dvec3(polytope[i1]) - a, glm::dvec3(polytope[i2]) - a);
        const double length = glm::length(cross);
        if (!std::isfinite(length) || !(length > 0.0)) return face;
        const glm::dvec3 normal = cross / length;
        const float distance = static_cast<float>(glm::dot(normal, a));
        if (!std::isfinite(distance)) return face;
        face.Normal = glm::vec3(normal);
        face.Distance = distance;
        return face;
    }

    PolytopeFace MakeSeedFace(std::span<const glm::vec3> polytope,
                              int i0, int i1, int i2, int opposite)
    {
        const glm::dvec3 a(polytope[i0]);
        const glm::dvec3 cross = glm::cross(glm::dvec3(polytope[i1]) - a, glm::dvec3(polytope[i2]) - a);
        if (glm::dot(cross, glm::dvec3(polytope[opposite]) - a) > 0.0)
            std::swap(i1, i2);
        return MakeFace(polytope, i0, i1, i2);
    }
}
