module;
#include <glm/glm.hpp>
#include <optional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Runtime.Geometry.Contact;

import Runtime.Geometry.Primitives;
import Runtime.Geometry.GJK; // Should contain GJK_EPA logic in full version

export namespace Runtime::Geometry
{
    // Define the Manifold structure again if not exported by Primitives
    // (Ensure it's only defined once in your project structure, preferably in Primitives)
    struct ContactManifold
    {
        glm::vec3 Normal;
        float PenetrationDepth;
        glm::vec3 ContactPointA;
        glm::vec3 ContactPointB;
    };

    namespace Internal
    {
        // --- Analytic Solvers ---

        std::optional<ContactManifold> Contact_Analytic(const Sphere& a, const Sphere& b)
        {
            glm::vec3 diff = b.Center - a.Center;
            float dist2 = glm::length2(diff);
            float radSum = a.Radius + b.Radius;

            if (dist2 > radSum * radSum) return std::nullopt;

            float dist = std::sqrt(dist2);
            ContactManifold m;

            if (dist < 1e-6f) {
                m.Normal = glm::vec3(0, 1, 0);
                m.PenetrationDepth = radSum;
            } else {
                m.Normal = diff / dist;
                m.PenetrationDepth = radSum - dist;
            }

            m.ContactPointA = a.Center + (m.Normal * a.Radius);
            m.ContactPointB = b.Center - (m.Normal * b.Radius);
            return m;
        }

        std::optional<ContactManifold> Contact_Analytic(const Sphere& s, const AABB& b)
        {
            // 1. Closest point on AABB
            glm::vec3 closest = glm::clamp(s.Center, b.Min, b.Max);

            // 2. Vector from Closest to Sphere
            glm::vec3 diff = s.Center - closest;
            float dist2 = glm::length2(diff);

            if (dist2 > s.Radius * s.Radius) return std::nullopt;

            float dist = std::sqrt(dist2);
            ContactManifold m;

            // Handle center inside box
            if (dist < 1e-6f)
            {
                // Sphere center is INSIDE AABB.
                // We need to push out to the nearest face.
                glm::vec3 center = (b.Min + b.Max) * 0.5f;
                glm::vec3 halfExt = (b.Max - b.Min) * 0.5f;
                glm::vec3 d = s.Center - center;
                glm::vec3 overlap = halfExt - glm::abs(d);

                // Find smallest overlap axis
                if (overlap.x < overlap.y && overlap.x < overlap.z) {
                    m.Normal = glm::vec3(d.x > 0 ? 1 : -1, 0, 0);
                    m.PenetrationDepth = overlap.x + s.Radius;
                } else if (overlap.y < overlap.z) {
                    m.Normal = glm::vec3(0, d.y > 0 ? 1 : -1, 0);
                    m.PenetrationDepth = overlap.y + s.Radius;
                } else {
                    m.Normal = glm::vec3(0, 0, d.z > 0 ? 1 : -1);
                    m.PenetrationDepth = overlap.z + s.Radius;
                }

                m.ContactPointB = s.Center - m.Normal * overlap[0]; // Approx
                m.ContactPointA = s.Center; // Deep inside
            }
            else
            {
                m.Normal = diff / dist;
                m.PenetrationDepth = s.Radius - dist;
                m.ContactPointB = closest;
                m.ContactPointA = s.Center - m.Normal * s.Radius;
            }

            return m;
        }

        // --- Fallback ---

        template<typename A, typename B>
        std::optional<ContactManifold> Contact_Fallback(const A& a, const B& b)
        {
            // In a real engine, this calls GJK to find collision,
            // then runs EPA (Expanding Polytope Algorithm) to find depth and normal.

            // Simplified Fallback: Boolean check with dummy depth
            if (GJK_Boolean(a, b))
            {
                // We know they collide, but GJK Boolean doesn't give depth.
                // Returning a dummy manifold prevents physics from breaking,
                // but doesn't resolve it correctly.
                // TODO: Implement full EPA here.
                ContactManifold m;
                m.Normal = glm::vec3(0,1,0);
                m.PenetrationDepth = 0.001f;
                return m;
            }
            return std::nullopt;
        }
    }

    // =========================================================================
    // CONTACT DISPATCHER
    // =========================================================================
    template<ConvexShape A, ConvexShape B>
    [[nodiscard]] std::optional<ContactManifold> ComputeContact(const A& a, const B& b)
    {
        if constexpr (requires { Internal::Contact_Analytic(a, b); }) {
            return Internal::Contact_Analytic(a, b);
        }
        else if constexpr (requires { Internal::Contact_Analytic(b, a); }) {
            auto result = Internal::Contact_Analytic(b, a);
            if (result) {
                result->Normal = -result->Normal;
                std::swap(result->ContactPointA, result->ContactPointB);
            }
            return result;
        }
        else {
            return Internal::Contact_Fallback(a, b);
        }
    }
}