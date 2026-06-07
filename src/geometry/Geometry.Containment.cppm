module;
#include <optional>
#include <array>
#include <span>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry.Containment;

import Geometry.Primitives;
import Geometry.SDF;
import Geometry.RobustPredicates;

export namespace Geometry
{
    // Concept to check if an object exposes vertices for fallback checks
    template <typename T>
    concept HasVertices = requires(T t)
    {
        // Either std::vector or std::array of vec3
        { t.Vertices } -> std::convertible_to<std::span<const glm::vec3>>;
        // Or specific access logic... for simplicity let's assume direct iteration
    };

    namespace Internal
    {
        // --- Analytic ---

        bool Contains_Analytic(const Sphere& outer, const Sphere& inner);
        bool Contains_Analytic(const AABB& outer, const glm::vec3& p);
        bool Contains_Analytic(const AABB& outer, const AABB& inner);
        bool Contains_Analytic(const Sphere& outer, const AABB& inner);

        // Note: For Culling, we often want "Intersection" not strict "Containment".
        // A Frustum contains an AABB if the AABB is FULLY inside.
        // A Frustum "Sees" an AABB if they Overlap.
        // We implement strict containment here.
        bool Contains_Analytic(const Frustum& f, const AABB& box);
        bool Contains_Analytic(const Frustum& f, const Sphere& s);

        // --- Fallback ---

        template <typename Container, typename Object>
        bool Contains_Fallback(const Container& c, const Object& o)
        {
            // Fallback strategy:
            // 1. If Object has vertices (like Frustum, ConvexHull), check if Container contains ALL vertices.
            // 2. Requires Container to have an Overlap/Contain check for vec3.

            if constexpr (requires { o.Vertices; })
            {
                for (const auto& v : o.Vertices)
                {
                    if (!Contains_Analytic(c, v)) return false; // Requires Point check
                }
                return true;
            }

            // If we can't determine, false is the safe bet for "Strict Containment"
            return false;
        }
    }

    template <typename Outer, typename Inner>
    [[nodiscard]] bool Contains(const Outer& container, const Inner& object)
    {
        if constexpr (requires { Internal::Contains_Analytic(container, object); })
        {
            return Internal::Contains_Analytic(container, object);
        }
        else
        {
            return Internal::Contains_Fallback(container, object);
        }
    }
}
