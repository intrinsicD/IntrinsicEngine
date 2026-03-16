module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <vector>
#include <array>
#include <optional>

export module Geometry:GJK;

import :Primitives;
import :Support;
import Core.Memory;

export namespace Geometry::Internal
{
    // --- GJK HELPER STRUCTS ---

    struct MinkowskiDifference
    {
        // We use function pointers or templates in a real generic system, 
        // but for this specific GJK implementation, we assume templates from the caller.
        // Helper to compute A - B support
        template <ConvexShape A, ConvexShape B>
        static glm::vec3 Support(const A& a, const B& b, const glm::vec3& dir)
        {
            // NEW: Call the free functions via ADL
            return Geometry::Support(a, dir) - Geometry::Support(b, -dir);
        }
    };

    struct Simplex
    {
        std::array<glm::vec3, 4> Points{}; // Value-initialize to zeros
        int Size = 0;

        void Push(glm::vec3 p) { Points[Size++] = p; }
        glm::vec3& operator[](int i) { return Points[i]; }
        const glm::vec3& operator[](int i) const { return Points[i]; }
    };

    // --- GJK Configuration ---
    // Centralized constants for tuning collision detection algorithms.
    // Relative tolerances are scaled by the Minkowski-difference extent at
    // runtime so that the algorithm behaves consistently across object scales.
    namespace Config
    {
        constexpr float GJK_RELATIVE_EPSILON = 1e-6f; // Relative tolerance (scaled by object size)
        constexpr float GJK_ABSOLUTE_EPSILON = 1e-12f; // Floor to avoid zero-tolerance on concentric shapes
        constexpr int GJK_MAX_ITERATIONS = 64; // Maximum iterations before giving up
        constexpr float EPA_RELATIVE_EPSILON = 1e-4f; // Relative tolerance for EPA convergence
        constexpr float EPA_ABSOLUTE_EPSILON = 1e-10f; // Floor for EPA
        constexpr int EPA_MAX_ITERATIONS = 32; // Maximum EPA iterations
    }

    namespace Detail
    {
        [[nodiscard]] inline glm::vec3 TripleProduct(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            return glm::cross(glm::cross(a, b), c);
        }

        // Compute a scale-aware epsilon from the initial Minkowski-difference support.
        [[nodiscard]] inline float ComputeScaledEpsilon(float scale, float relEps, float absEps) noexcept
        {
            return std::max(relEps * scale, absEps);
        }

        [[nodiscard]] inline bool NearlyZero(const glm::vec3& v, float epsSq) noexcept
        {
            return glm::length2(v) <= epsSq;
        }
    }

    // --- GJK IMPLEMENTATION (Boolean Overlap) ---


    // Handles the logic of processing the simplex to see if it contains origin.
    // Returns true if intersection found, updates direction for next search.
    // epsSq is the squared scale-aware epsilon for near-zero checks.
    bool NextSimplex(Simplex& points, glm::vec3& direction, float eps, float epsSq)
    {
        switch (points.Size)
        {
        case 2: // Line
            {
                glm::vec3 a = points[1];
                glm::vec3 b = points[0];
                glm::vec3 ab = b - a;
                glm::vec3 ao = -a;

                const float abLenSq = glm::length2(ab);
                if (abLenSq <= epsSq)
                {
                    direction = ao;
                    points.Size = 1;
                    return Detail::NearlyZero(direction, epsSq);
                }

                const float projection = glm::dot(ao, ab);
                const glm::vec3 perp = Detail::TripleProduct(ab, ao, ab);

                if (projection > 0.0f)
                {
                    if (Detail::NearlyZero(perp, epsSq))
                    {
                        const float t = projection / abLenSq;
                        if (t >= -eps && t <= 1.0f + eps)
                            return true; // Origin lies on the segment AB.

                        direction = ao;
                        if (Detail::NearlyZero(direction, epsSq))
                            return true;
                    }
                    else
                    {
                        direction = perp;
                    }
                }
                else
                {
                    points.Size = 1;
                    direction = ao;
                    if (Detail::NearlyZero(direction, epsSq))
                        return true;
                }
                return false;
            }
        case 3: // Triangle
            {
                glm::vec3 a = points[2];
                glm::vec3 b = points[1];
                glm::vec3 c = points[0];
                glm::vec3 ab = b - a;
                glm::vec3 ac = c - a;
                glm::vec3 ao = -a;
                glm::vec3 abc = glm::cross(ab, ac);

                if (Detail::NearlyZero(abc, epsSq))
                {
                    points.Size = 2;
                    points[0] = b;
                    points[1] = a;
                    return NextSimplex(points, direction, eps, epsSq);
                }

                const glm::vec3 acPerp = glm::cross(abc, ac);
                if (glm::dot(acPerp, ao) > 0.0f)
                {
                    if (glm::dot(ac, ao) > 0.0f)
                    {
                        points[0] = c;
                        points[1] = a;
                        points.Size = 2;
                        direction = Detail::TripleProduct(ac, ao, ac);
                        if (Detail::NearlyZero(direction, epsSq))
                            return true;
                    }
                    else
                    {
                        points[0] = b;
                        points[1] = a;
                        points.Size = 2;
                        return NextSimplex(points, direction, eps, epsSq);
                    }
                }
                else
                {
                    const glm::vec3 abPerp = glm::cross(ab, abc);
                    if (glm::dot(abPerp, ao) > 0.0f)
                    {
                        points[0] = b;
                        points[1] = a;
                        points.Size = 2;
                        return NextSimplex(points, direction, eps, epsSq);
                    }
                    else
                    {
                        const float planeDot = glm::dot(abc, ao);
                        if (std::abs(planeDot) <= eps)
                            return true;

                        if (planeDot > 0.0f)
                        {
                            direction = abc;
                        }
                        else
                        {
                            points[0] = b;
                            points[1] = c;
                            points[2] = a;
                            direction = -abc;
                        }
                    }
                }
                return false;
            }
        case 4: // Tetrahedron
            {
                glm::vec3 a = points[3];
                glm::vec3 b = points[2];
                glm::vec3 c = points[1];
                glm::vec3 d = points[0];
                glm::vec3 ab = b - a;
                glm::vec3 ac = c - a;
                glm::vec3 ad = d - a;
                glm::vec3 ao = -a;

                glm::vec3 abc = glm::cross(ab, ac);
                if (glm::dot(abc, ad) > 0.0f)
                    abc = -abc;
                glm::vec3 acd = glm::cross(ac, ad);
                if (glm::dot(acd, ab) > 0.0f)
                    acd = -acd;
                glm::vec3 adb = glm::cross(ad, ab);
                if (glm::dot(adb, ac) > 0.0f)
                    adb = -adb;

                // Degenerate tetrahedron: if all face normals are near-zero the
                // four points are coplanar. Fall back to triangle simplex.
                if (Detail::NearlyZero(abc, epsSq) &&
                    Detail::NearlyZero(acd, epsSq) &&
                    Detail::NearlyZero(adb, epsSq))
                {
                    points[0] = c;
                    points[1] = b;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction, eps, epsSq);
                }

                if (glm::dot(abc, ao) > 0.0f)
                {
                    points[0] = c;
                    points[1] = b;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction, eps, epsSq);
                }
                if (glm::dot(acd, ao) > 0.0f)
                {
                    points[0] = d;
                    points[1] = c;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction, eps, epsSq);
                }
                if (glm::dot(adb, ao) > 0.0f)
                {
                    points[0] = b;
                    points[1] = d;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction, eps, epsSq);
                }
                return true; // Origin is inside all faces!
            }
        }
        return false;
    }

    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b, Core::Memory::LinearArena& /*scratch*/)
    {
        // Currently allocation-free; scratch is plumbed for consistency with EPA.
        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});

        // Derive scale-aware epsilon from the initial support magnitude.
        const float scale = glm::length(support);
        const float eps = Detail::ComputeScaledEpsilon(scale, Config::GJK_RELATIVE_EPSILON, Config::GJK_ABSOLUTE_EPSILON);
        const float epsSq = eps * eps;

        if (Detail::NearlyZero(support, epsSq))
            return true;

        Simplex points;
        points.Push(support);

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (true)
        {
            if (Detail::NearlyZero(direction, epsSq))
                return true;

            support = MinkowskiDifference::Support(a, b, direction);

            if (Detail::NearlyZero(support, epsSq))
                return true;

            if (glm::dot(support, direction) < eps) return false; // No intersection possible

            bool duplicate = false;
            for (int i = 0; i < points.Size; ++i)
            {
                if (glm::length2(points[i] - support) <= epsSq)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                return false;

            points.Push(support);

            if (NextSimplex(points, direction, eps, epsSq)) return true;

            if (points.Size > 0 && --maxIterations == 0)
            {
                return false;
            }
        }
    }

    template <typename A, typename B>
    std::optional<Simplex> GJK_Intersection(const A& a, const B& b, Core::Memory::LinearArena& /*scratch*/)
    {
        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});

        const float scale = glm::length(support);
        const float eps = Detail::ComputeScaledEpsilon(scale, Config::GJK_RELATIVE_EPSILON, Config::GJK_ABSOLUTE_EPSILON);
        const float epsSq = eps * eps;

        Simplex points;
        points.Push(support);

        if (Detail::NearlyZero(support, epsSq))
            return points;

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (maxIterations-- > 0)
        {
            if (Detail::NearlyZero(direction, epsSq))
                return points;

            support = MinkowskiDifference::Support(a, b, direction);

            if (Detail::NearlyZero(support, epsSq))
            {
                points.Push(support);
                return points;
            }

            if (glm::dot(support, direction) < eps) return std::nullopt;

            for (int i = 0; i < points.Size; ++i)
            {
                if (glm::length2(points[i] - support) <= epsSq)
                    return std::nullopt;
            }

            points.Push(support);
            if (NextSimplex(points, direction, eps, epsSq)) return points;
        }
        return std::nullopt;
    }

    // Back-compat overloads (existing call sites): route through the scratch-taking versions.
    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b)
    {
        Core::Memory::LinearArena scratch(8 * 1024);
        return GJK_Boolean(a, b, scratch);
    }

    template <typename A, typename B>
    std::optional<Simplex> GJK_Intersection(const A& a, const B& b)
    {
        Core::Memory::LinearArena scratch(8 * 1024);
        return GJK_Intersection(a, b, scratch);
    }
}
