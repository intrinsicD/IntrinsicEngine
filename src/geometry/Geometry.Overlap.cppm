module;

#include <glm/glm.hpp>
#include <span>

export module Geometry.Overlap;

import Geometry.Primitives;
import Geometry.GJK;
import Geometry.Support;

export namespace Geometry
{
    namespace Internal
    {
        // Ray/AABB overlap treats slab boundaries as inclusive. Axis-parallel
        // rays with zero or negative-zero components are valid when their
        // origin lies inside the corresponding slab.
        bool Overlap_Analytic(const OBB& a, const OBB& b);
        bool Overlap_Analytic(const OBB& a, const Sphere& b);
        bool Overlap_Analytic(const Frustum& f, const AABB& box);
        bool Overlap_Analytic(const Frustum& f, const Sphere& s);
        bool Overlap_Analytic(const Ray& r, const AABB& b);
        bool Overlap_Analytic(const Ray& r, const Sphere& s);
        bool Overlap_Analytic(const Sphere& a, const Sphere& b);
        bool Overlap_Analytic(const AABB& a, const AABB& b);
        bool Overlap_Analytic(const Sphere& s, const AABB& b);
        bool Overlap_Analytic(const Sphere& s, const Capsule& c);

        template <typename A, typename B>
        bool Overlap_Fallback(const A& a, const B& b)
        {
            return GJK_Boolean(a, b);
        }
    }

    template <ConvexShape A, ConvexShape B>
    [[nodiscard]] bool TestOverlap(const A& a, const B& b)
    {
        if constexpr (requires { Internal::Overlap_Analytic(a, b); })
        {
            return Internal::Overlap_Analytic(a, b);
        }
        else if constexpr (requires { Internal::Overlap_Analytic(b, a); })
        {
            return Internal::Overlap_Analytic(b, a);
        }
        else
        {
            return Internal::Overlap_Fallback(a, b);
        }
    }
}
