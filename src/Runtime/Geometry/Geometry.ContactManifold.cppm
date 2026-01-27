module;
#include <glm/glm.hpp>
#include <optional>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry:Contact;

import :Primitives;
import :GJK; // Should contain GJK_EPA logic in full version
import :EPA;
import :Support;
import Core;

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
        glm::vec3 Normal;           // Surface normal at hit point (points outward from surface)
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
            ContactManifold m{};

            if (dist < 1e-6f)
            {
                m.Normal = glm::vec3(0, 1, 0);
                m.PenetrationDepth = radSum;
            }
            else
            {
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
            ContactManifold m{};

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
                if (overlap.x < overlap.y && overlap.x < overlap.z)
                {
                    m.Normal = glm::vec3(d.x > 0 ? 1 : -1, 0, 0);
                    m.PenetrationDepth = overlap.x + s.Radius;
                }
                else if (overlap.y < overlap.z)
                {
                    m.Normal = glm::vec3(0, d.y > 0 ? 1 : -1, 0);
                    m.PenetrationDepth = overlap.y + s.Radius;
                }
                else
                {
                    m.Normal = glm::vec3(0, 0, d.z > 0 ? 1 : -1);
                    m.PenetrationDepth = overlap.z + s.Radius;
                }

                float minOverlap = (overlap.x < overlap.y && overlap.x < overlap.z)
                                       ? overlap.x
                                       : (overlap.y < overlap.z)
                                       ? overlap.y
                                       : overlap.z;
                m.ContactPointB = s.Center - m.Normal * minOverlap;
                m.ContactPointA = s.Center - m.Normal * s.Radius;
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

        // --- Ray Cast Solvers ---

        std::optional<RayHit> RayCast_Analytic(const Ray& r, const Sphere& s)
        {
            glm::vec3 m = r.Origin - s.Center;
            float b = glm::dot(m, r.Direction);
            float c = glm::dot(m, m) - s.Radius * s.Radius;

            if (c > 0.0f && b > 0.0f) return std::nullopt;

            float discr = b * b - c;
            if (discr < 0.0f) return std::nullopt;

            float t = -b - std::sqrt(discr);
            if (t < 0.0f) t = 0.0f; // Inside sphere

            RayHit hit{};
            hit.Distance = t;
            hit.Point = r.Origin + r.Direction * t;
            hit.Normal = glm::normalize(hit.Point - s.Center);
            return hit;
        }

        std::optional<RayHit> RayCast_Analytic(const Ray& r, const AABB& box)
        {
            glm::vec3 invDir = 1.0f / r.Direction;
            glm::vec3 t0s = (box.Min - r.Origin) * invDir;
            glm::vec3 t1s = (box.Max - r.Origin) * invDir;

            glm::vec3 tsmaller = glm::min(t0s, t1s);
            glm::vec3 tbigger = glm::max(t0s, t1s);

            float tmin = glm::max(tsmaller.x, glm::max(tsmaller.y, tsmaller.z));
            float tmax = glm::min(tbigger.x, glm::min(tbigger.y, tbigger.z));

            if (tmax < tmin || tmax < 0.0f) return std::nullopt;

            // Compute normal
            RayHit hit{};
            hit.Distance = tmin > 0 ? tmin : tmax; // if tmin < 0, origin is inside
            hit.Point = r.Origin + r.Direction * hit.Distance;

            // Simple normal logic based on hit point proximity to faces
            const float epsilon = 1e-4f;
            if (std::abs(hit.Point.x - box.Min.x) < epsilon) hit.Normal = {-1, 0, 0};
            else if (std::abs(hit.Point.x - box.Max.x) < epsilon) hit.Normal = {1, 0, 0};
            else if (std::abs(hit.Point.y - box.Min.y) < epsilon) hit.Normal = {0, -1, 0};
            else if (std::abs(hit.Point.y - box.Max.y) < epsilon) hit.Normal = {0, 1, 0};
            else if (std::abs(hit.Point.z - box.Min.z) < epsilon) hit.Normal = {0, 0, -1};
            else hit.Normal = {0, 0, 1};

            return hit;
        }

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
        std::optional<ContactManifold> Contact_Fallback(const A& a, const B& b, Core::Memory::LinearArena& scratch)
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
                    m.Normal = epa->Normal;
                    m.PenetrationDepth = epa->Depth;

                    // EPA gives us ContactPoint on A (roughly).
                    // B = A + Normal * Depth
                    m.ContactPointA = epa->ContactPoint;
                    m.ContactPointB = m.ContactPointA + (m.Normal * m.PenetrationDepth);

                    return m;
                }

                // Fallback if EPA fails (e.g. numerical instability or degenerate simplex)
                // Use heuristic center-diff
                Core::Log::Warn("Physics: GJK hit but EPA failed. Using heuristic fallback.");

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
            Core::Memory::LinearArena scratch(64 * 1024);
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
    [[nodiscard]] std::optional<ContactManifold> ComputeContact(const A& a, const B& b, Core::Memory::LinearArena& scratch)
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
