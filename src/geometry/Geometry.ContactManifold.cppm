module;
#include <glm/glm.hpp>
#include <optional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry.ContactManifold;

import Geometry.Primitives;
import Geometry.GJK; // Should contain GJK_EPA logic in full version
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
        float PenetrationDepth;     // Positive value indicating overlap magnitude
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

        // Small helper for fallback paths: best-effort world-space "center" estimation.
        // This must be cheap and constexpr-friendly; it shouldn't allocate.
        template <typename Shape>
        [[nodiscard]]  glm::vec3 CenterOf(const Shape& s)
        {
            if constexpr (requires { s.Center; })
            {
                return s.Center;
            }
            else if constexpr (std::same_as<Shape, Capsule>)
            {
                return (s.PointA + s.PointB) * 0.5f;
            }
            else if constexpr (std::same_as<Shape, AABB>)
            {
                return (s.Min + s.Max) * 0.5f;
            }
            else if constexpr (std::same_as<Shape, OBB>)
            {
                return s.Center;
            }
            else if constexpr (std::same_as<Shape, ConvexHull>)
            {
                // Robust even for empty hulls.
                if (s.Vertices.empty()) return glm::vec3(0.0f);
                glm::vec3 sum(0.0f);
                for (const glm::vec3& v : s.Vertices) sum += v;
                return sum / static_cast<float>(s.Vertices.size());
            }
            else
            {
                // Unknown shape type. Returning 0 keeps compilation working and preserves
                // the "log warning + unstable manifold" behavior, but without hard errors.
                return glm::vec3(0.0f);
            }
        }

        // --- Fallback ---

        template <typename A, typename B>
        std::optional<ContactManifold> Contact_Fallback(const A& a, const B& b, Extrinsic::Core::Memory::LinearArena& scratch)
        {
            // Use GJK_Intersection to get the simplex
            auto simplexOpt = Internal::GJK_Intersection(a, b, scratch);

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

                // Fallback if EPA fails (e.g. numerical instability or degenerate simplex)
                // Use heuristic center-diff
                Extrinsic::Core::Log::Warn("Physics: GJK hit but EPA failed. Using heuristic fallback.");

                ContactManifold m{};
                glm::vec3 dir = CenterOf(b) - CenterOf(a);
                if (glm::length2(dir) < 1e-10f) m.Normal = glm::vec3(0, 1, 0);
                else m.Normal = glm::normalize(dir);
                m.PenetrationDepth = 0.001f;
                m.ContactPointA = Geometry::Support(a, m.Normal);
                m.ContactPointB = Geometry::Support(b, -m.Normal);
                return m;
            }
            return std::nullopt;
        }

        // Back-compat overload (existing call sites): uses a small local scratch.
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
