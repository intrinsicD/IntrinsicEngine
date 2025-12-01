module;
#include <optional>
#include <array>
#include <span>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Runtime.Geometry.Containment;

import Runtime.Geometry.Primitives;
import Runtime.Geometry.SDF;

export namespace Runtime::Geometry
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

        bool Contains_Analytic(const Sphere& outer, const Sphere& inner)
        {
            float dist = glm::distance(outer.Center, inner.Center);
            if (inner.Radius == 0.0f)
            {
                // Point containment
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
            // All 8 corners of AABB must be inside Sphere
            // Optimization: Find the corner furthest from sphere center
            glm::vec3 furthest = inner.Max;
            if (outer.Center.x > inner.Min.x + (inner.Max.x - inner.Min.x) * 0.5f) furthest.x = inner.Min.x;
            // ... (requires checking quadrant).

            // Simpler: Check all 8 corners
            std::array<glm::vec3, 8> corners = {
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

        // Note: For Culling, we often want "Intersection" not strict "Containment".
        // A Frustum contains an AABB if the AABB is FULLY inside.
        // A Frustum "Sees" an AABB if they Overlap.
        // We implement strict containment here.
        bool Contains_Analytic(const Frustum& f, const AABB& box)
        {
            // For STRICT containment, ALL 8 corners of the AABB must be inside ALL 6 planes.
            // Optimization: Test the point most likely to be OUTSIDE (Negative Vertex)

            for (const auto& plane : f.Planes)
            {
                // Find the point most likely to be "outside" (behind) the plane
                glm::vec3 negativeVertex = box.Max;
                if (plane.Normal.x >= 0) negativeVertex.x = box.Min.x;
                if (plane.Normal.y >= 0) negativeVertex.y = box.Min.y;
                if (plane.Normal.z >= 0) negativeVertex.z = box.Min.z;

                if (SDF::Math::Sdf_Plane(negativeVertex, plane.Normal, plane.Distance) < 0)
                {
                    return false;
                }
            }
            return true;
        }

        bool Contains_Analytic(const Frustum& f, const Sphere& s)
        {
            for (const auto& p : f.Planes)
            {
                // If the sphere touches or crosses the plane (dist > -r), it's not fully contained
                if (SDF::Math::Sdf_Plane(s.Center, p.Normal, p.Distance) < s.Radius) return false;
            }
            return true;
        }

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
