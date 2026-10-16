// Contact manifold generation between primitives: analytic pair routines with a
// GJK+EPA fallback that uses a caller-provided scratch arena for EPA.
module;
#include <glm/glm.hpp>
#include <optional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry.ContactManifold;

import Geometry.Primitives;
import Geometry.GJK;
import Geometry.EPA;
import Geometry.Support;
import Extrinsic.Core.Memory;
import Extrinsic.Core.Logging;

export namespace Geometry
{
    // =========================================================================
    // CONTACT MANIFOLD NORMAL CONVENTION
    // =========================================================================
    //
    // All contact normals follow the convention: Normal points from A to B
    //
    // To resolve collision:
    //   - Move A in direction: -Normal * (PenetrationDepth * 0.5)
    //   - Move B in direction: +Normal * (PenetrationDepth * 0.5)
    //
    // Contact points:
    //   - ContactPointA: Point on surface of object A (in world space)
    //   - ContactPointB: Point on surface of object B (in world space)
    //
    // Example:
    //   ComputeContact(sphereA, sphereB) returns:
    //     - Normal: unit vector from A.center to B.center
    //     - PenetrationDepth: (A.radius + B.radius) - distance(A, B)
    //     - ContactPointA: A.center + Normal * A.radius
    //     - ContactPointB: B.center - Normal * B.radius
    // =========================================================================

    struct ContactManifold
    {
        glm::vec3 Normal;           // Unit vector pointing from A to B
        float PenetrationDepth;     // Nonnegative overlap magnitude; zero means touching
        glm::vec3 ContactPointA;    // Contact point on surface of object A
        glm::vec3 ContactPointB;    // Contact point on surface of object B
    };

    struct RayHit
    {
        float Distance;             // Distance along ray to hit point (t parameter)
        glm::vec3 Point;            // World-space hit point
        glm::vec3 Normal;           // Finite surface normal at hit point (points outward from surface)
    };

    namespace Internal
    {
        // --- Analytic Solvers ---

        std::optional<ContactManifold> Contact_Analytic(const Sphere& a, const Sphere& b);
        std::optional<ContactManifold> Contact_Analytic(const Sphere& s, const AABB& b);

        // --- Ray Cast Solvers ---
        //
        // Ray/AABB uses inclusive slab boundaries, so rays exactly on Min/Max
        // planes are valid queries. A ray starting inside the box reports the
        // exit distance. Ray/Sphere returns a deterministic finite fallback
        // normal when the hit point is numerically at the sphere center.

        std::optional<RayHit> RayCast_Analytic(const Ray& r, const Sphere& s);
        std::optional<RayHit> RayCast_Analytic(const Ray& r, const AABB& box);

        // --- Fallback ---

        template <typename A, typename B>
        std::optional<ContactManifold> Contact_Fallback(const A& a, const B& b, Extrinsic::Core::Memory::LinearArena& scratch)
        {
            // Use GJK_Intersection to get the simplex
            auto simplexOpt = Internal::GJK_Intersection(a, b);

            if (simplexOpt)
            {
                // Collision Detected! Use EPA to resolve depth/normal.
                auto epa = Internal::EPA_Solver(a, b, *simplexOpt, scratch);

                if (epa)
                {
                    ContactManifold m{};
                    m.Normal = epa->Normal; // A->B per EPAResult contract
                    m.PenetrationDepth = epa->Depth;

                    // EPA gives us the deepest point of A inside B (roughly).
                    // Backing out along the A->B normal by the penetration
                    // depth lands on the surface of B.
                    m.ContactPointA = epa->ContactPoint;
                    m.ContactPointB = m.ContactPointA - (m.Normal * m.PenetrationDepth);

                    return m;
                }

                Extrinsic::Core::Log::Warn("Physics: GJK hit but EPA failed; no contact manifold produced.");
            }
            return std::nullopt;
        }

        // Convenience query with local scratch; repeated callers should supply an arena.
        template <typename A, typename B>
        std::optional<ContactManifold> Contact_Fallback(const A& a, const B& b)
        {
            Extrinsic::Core::Memory::LinearArena scratch(64 * 1024);
            return Contact_Fallback(a, b, scratch);
        }
    }

    // =========================================================================
    // CONTACT DISPATCHER
    // =========================================================================
    template <ConvexShape A, ConvexShape B>
    [[nodiscard]] std::optional<ContactManifold> ComputeContact(const A& a, const B& b)
    {
        if constexpr (requires { Internal::Contact_Analytic(a, b); })
        {
            return Internal::Contact_Analytic(a, b);
        }
        else if constexpr (requires { Internal::Contact_Analytic(b, a); })
        {
            auto result = Internal::Contact_Analytic(b, a);
            if (result)
            {
                result->Normal = -result->Normal;
                std::swap(result->ContactPointA, result->ContactPointB);
            }
            return result;
        }
        else
        {
            return Internal::Contact_Fallback(a, b);
        }
    }

    // Scratch-aware dispatcher overload (preferred for hot paths)
    template <ConvexShape A, ConvexShape B>
    [[nodiscard]] std::optional<ContactManifold> ComputeContact(const A& a, const B& b, Extrinsic::Core::Memory::LinearArena& scratch)
    {
        if constexpr (requires { Internal::Contact_Analytic(a, b); })
        {
            return Internal::Contact_Analytic(a, b);
        }
        else if constexpr (requires { Internal::Contact_Analytic(b, a); })
        {
            auto result = Internal::Contact_Analytic(b, a);
            if (result)
            {
                result->Normal = -result->Normal;
                std::swap(result->ContactPointA, result->ContactPointB);
            }
            return result;
        }
        else
        {
            return Internal::Contact_Fallback(a, b, scratch);
        }
    }

    // --- RayCast Dispatcher ---
    template <typename Shape>
    [[nodiscard]] std::optional<RayHit> RayCast(const Ray& ray, const Shape& shape)
    {
        if constexpr (requires { Internal::RayCast_Analytic(ray, shape); })
        {
            return Internal::RayCast_Analytic(ray, shape);
        }
        else
        {
            // Raycast fallback not typically done via GJK directly efficiently,
            // but one could raymarch the SDF defined by GJK.
            return std::nullopt;
        }
    }
}
