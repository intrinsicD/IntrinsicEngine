module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Runtime.Geometry.Overlap;

import Runtime.Geometry.Primitives;
import Runtime.Geometry.GJK;

export namespace Runtime::Geometry
{
    namespace Internal
    {
        bool Overlap_Analytic(const Sphere& a, const Sphere& b)
        {
            float distSq = glm::distance2(a.Center, b.Center);
            float radSum = a.Radius + b.Radius;
            return distSq <= (radSum * radSum);
        }

        bool Overlap_Analytic(const AABB& a, const AABB& b)
        {
            return (a.Min.x <= b.Max.x && a.Max.x >= b.Min.x) &&
                (a.Min.y <= b.Max.y && a.Max.y >= b.Min.y) &&
                (a.Min.z <= b.Max.z && a.Max.z >= b.Min.z);
        }

        bool Overlap_Analytic(const Sphere& s, const AABB& b)
        {
            glm::vec3 closest = glm::clamp(s.Center, b.Min, b.Max);
            return glm::distance2(closest, s.Center) <= (s.Radius * s.Radius);
        }

        bool Overlap_Analytic(const Sphere& s, const Capsule& c)
        {
            // 1. Find closest point on segment AB to sphere center
            glm::vec3 ab = c.PointB - c.PointA;
            float t = glm::dot(s.Center - c.PointA, ab) / glm::length2(ab);
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 closestOnSegment = c.PointA + t * ab;

            // 2. Check distance
            float radSum = s.Radius + c.Radius;
            return glm::distance2(s.Center, closestOnSegment) <= (radSum * radSum);
        }

        template <typename A, typename B>
        bool Overlap_Fallback(const A& a, const B& b)
        {
            // Uses the Generic GJK solver imported from Runtime.Geometry.GJK
            return GJK_Boolean(a, b);
        }
    }

    // =========================================================================
    // OVERLAP (Boolean - Fast)
    // =========================================================================
    template <ConvexShape A, ConvexShape B>
    [[nodiscard]] bool TestOverlap(const A& a, const B& b)
    {
        // 1. Analytic (Fastest)
        if constexpr (requires { Internal::Overlap_Analytic(a, b); })
        {
            return Internal::Overlap_Analytic(a, b);
        }
        else if constexpr (requires { Internal::Overlap_Analytic(b, a); })
        {
            return Internal::Overlap_Analytic(b, a);
        }
        // 2. Generic (Fallback)
        else
        {
            return Internal::Overlap_Fallback(a, b);
        }
    }
}
