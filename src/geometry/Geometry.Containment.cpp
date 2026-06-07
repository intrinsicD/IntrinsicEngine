module;

#include <array>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.Containment;

namespace Geometry::Internal
{
    bool Contains_Analytic(const Sphere& outer, const Sphere& inner)
    {
        float dist = glm::distance(outer.Center, inner.Center);
        if (inner.Radius == 0.0f)
        {
            return dist <= outer.Radius;
        }
        return (dist + inner.Radius) < outer.Radius;
    }

    bool Contains_Analytic(const AABB& outer, const glm::vec3& p)
    {
        return p.x >= outer.Min.x && p.x <= outer.Max.x &&
            p.y >= outer.Min.y && p.y <= outer.Max.y &&
            p.z >= outer.Min.z && p.z <= outer.Max.z;
    }

    bool Contains_Analytic(const AABB& outer, const AABB& inner)
    {
        return inner.Min.x >= outer.Min.x && inner.Max.x <= outer.Max.x &&
            inner.Min.y >= outer.Min.y && inner.Max.y <= outer.Max.y &&
            inner.Min.z >= outer.Min.z && inner.Max.z <= outer.Max.z;
    }

    bool Contains_Analytic(const Sphere& outer, const AABB& inner)
    {
        const std::array<glm::vec3, 8> corners = {
            glm::vec3(inner.Min.x, inner.Min.y, inner.Min.z),
            glm::vec3(inner.Min.x, inner.Min.y, inner.Max.z),
            glm::vec3(inner.Min.x, inner.Max.y, inner.Min.z),
            glm::vec3(inner.Min.x, inner.Max.y, inner.Max.z),
            glm::vec3(inner.Max.x, inner.Min.y, inner.Min.z),
            glm::vec3(inner.Max.x, inner.Min.y, inner.Max.z),
            glm::vec3(inner.Max.x, inner.Max.y, inner.Min.z),
            glm::vec3(inner.Max.x, inner.Max.y, inner.Max.z)
        };

        float r2 = outer.Radius * outer.Radius;
        for (const auto& p : corners)
        {
            if (glm::distance2(p, outer.Center) > r2) return false;
        }
        return true;
    }

    bool Contains_Analytic(const Frustum& f, const AABB& box)
    {
        namespace RP = Geometry::RobustPredicates;
        for (const auto& plane : f.Planes)
        {
            glm::vec3 negativeVertex = box.Max;
            if (plane.Normal.x >= 0) negativeVertex.x = box.Min.x;
            if (plane.Normal.y >= 0) negativeVertex.y = box.Min.y;
            if (plane.Normal.z >= 0) negativeVertex.z = box.Min.z;

            const auto signed_ = RP::SignedDistanceToHessianPlane(
                plane.Normal, plane.Distance, negativeVertex);
            const bool certainlyInside =
                signed_.Certainty == RP::Certainty::Certain &&
                signed_.Sign != RP::Sign::Negative;
            if (!certainlyInside)
            {
                return false;
            }
        }
        return true;
    }

    bool Contains_Analytic(const Frustum& f, const Sphere& s)
    {
        namespace RP = Geometry::RobustPredicates;
        for (const auto& plane : f.Planes)
        {
            const auto signed_ = RP::SignedDistanceToHessianPlane(
                plane.Normal, plane.Distance, s.Center);
            const double radius = static_cast<double>(s.Radius);
            if (signed_.Value < radius + signed_.FilterBound)
            {
                return false;
            }
        }
        return true;
    }
}
