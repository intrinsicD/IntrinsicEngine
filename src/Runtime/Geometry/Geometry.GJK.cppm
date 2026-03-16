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
        // Helper to compute A - B support in Minkowski difference
        template <ConvexShape A, ConvexShape B>
        static glm::vec3 Support(const A& a, const B& b, const glm::vec3& dir)
        {
            return Geometry::Support(a, dir) - Geometry::Support(b, -dir);
        }
    };

    struct Simplex
    {
        std::array<glm::vec3, 4> Points{}; // Value-initialize to zeros
        int Size = 0;
        float InvScale = 1.0f; // Normalization factor applied to all points (1/scale)

        void Push(glm::vec3 p) { Points[Size++] = p; }
        glm::vec3& operator[](int i) { return Points[i]; }
        const glm::vec3& operator[](int i) const { return Points[i]; }
    };

    // --- GJK Configuration ---
    // All arithmetic runs in a normalized workspace (~unit scale) so fixed
    // tolerances are well-conditioned regardless of original object size.
    namespace Config
    {
        constexpr float GJK_EPSILON  = 1e-6f;  // Numerical tolerance for GJK convergence
        constexpr int   GJK_MAX_ITERATIONS = 64;
        constexpr float EPA_EPSILON  = 1e-4f;  // Tolerance for EPA penetration depth
        constexpr int   EPA_MAX_ITERATIONS = 32;
    }

    namespace Detail
    {
        [[nodiscard]] inline glm::vec3 TripleProduct(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            return glm::cross(glm::cross(a, b), c);
        }

        [[nodiscard]] inline bool NearlyZero(const glm::vec3& v) noexcept
        {
            return glm::length2(v) <= Config::GJK_EPSILON * Config::GJK_EPSILON;
        }
    }

    // --- GJK IMPLEMENTATION ---

    // Handles the logic of processing the simplex to see if it contains origin.
    // Returns true if intersection found, updates direction for next search.
    // Operates in normalized space — fixed epsilons are always well-conditioned.
    bool NextSimplex(Simplex& points, glm::vec3& direction)
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
                if (abLenSq <= Config::GJK_EPSILON)
                {
                    direction = ao;
                    points.Size = 1;
                    return Detail::NearlyZero(direction);
                }

                const float projection = glm::dot(ao, ab);
                const glm::vec3 perp = Detail::TripleProduct(ab, ao, ab);

                if (projection > 0.0f)
                {
                    if (Detail::NearlyZero(perp))
                    {
                        const float t = projection / abLenSq;
                        if (t >= -Config::GJK_EPSILON && t <= 1.0f + Config::GJK_EPSILON)
                            return true; // Origin lies on the segment AB.

                        direction = ao;
                        if (Detail::NearlyZero(direction))
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
                    if (Detail::NearlyZero(direction))
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

                if (Detail::NearlyZero(abc))
                {
                    points.Size = 2;
                    points[0] = b;
                    points[1] = a;
                    return NextSimplex(points, direction);
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
                        if (Detail::NearlyZero(direction))
                            return true;
                    }
                    else
                    {
                        points[0] = b;
                        points[1] = a;
                        points.Size = 2;
                        return NextSimplex(points, direction);
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
                        return NextSimplex(points, direction);
                    }
                    else
                    {
                        const float planeDot = glm::dot(abc, ao);
                        if (std::abs(planeDot) <= Config::GJK_EPSILON)
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
                if (Detail::NearlyZero(abc) &&
                    Detail::NearlyZero(acd) &&
                    Detail::NearlyZero(adb))
                {
                    points[0] = c;
                    points[1] = b;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction);
                }

                if (glm::dot(abc, ao) > 0.0f)
                {
                    points[0] = c;
                    points[1] = b;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction);
                }
                if (glm::dot(acd, ao) > 0.0f)
                {
                    points[0] = d;
                    points[1] = c;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction);
                }
                if (glm::dot(adb, ao) > 0.0f)
                {
                    points[0] = b;
                    points[1] = d;
                    points[2] = a;
                    points.Size = 3;
                    return NextSimplex(points, direction);
                }
                return true; // Origin is inside all faces!
            }
        }
        return false;
    }

    // =========================================================================
    // Normalized-workspace GJK
    //
    // The first Minkowski-difference support point determines the workspace
    // scale. All subsequent support results are multiplied by 1/scale so that
    // the simplex lives in ~unit space. This makes fixed tolerances
    // (GJK_EPSILON) reliable at any object scale — cross products, dot
    // products, and triple products all operate on O(1) magnitudes.
    //
    // For GJK_Boolean the result is just a bool; no unscaling needed.
    // For GJK_Intersection the returned simplex is in normalized space;
    // EPA_Solver receives it as-is and performs the same normalization on its
    // own support queries, then unscales the final penetration depth.
    // =========================================================================

    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b, Core::Memory::LinearArena& /*scratch*/)
    {
        // Currently allocation-free; scratch is plumbed for consistency with EPA.
        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});

        // Compute normalization scale from the initial support point.
        const float scale = glm::length(support);
        const float invScale = (scale > 1e-30f) ? (1.0f / scale) : 1.0f;

        // Normalize the initial support into ~unit space.
        support *= invScale;

        if (Detail::NearlyZero(support))
            return true;

        Simplex points;
        points.Push(support);

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (true)
        {
            if (Detail::NearlyZero(direction))
                return true;

            // Support query in original space, then normalize.
            support = MinkowskiDifference::Support(a, b, direction) * invScale;

            if (Detail::NearlyZero(support))
                return true;

            if (glm::dot(support, direction) < Config::GJK_EPSILON) return false;

            bool duplicate = false;
            for (int i = 0; i < points.Size; ++i)
            {
                if (glm::length2(points[i] - support) <= Config::GJK_EPSILON * Config::GJK_EPSILON)
                {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                return false;

            points.Push(support);

            if (NextSimplex(points, direction)) return true;

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
        const float invScale = (scale > 1e-30f) ? (1.0f / scale) : 1.0f;

        support *= invScale;

        Simplex points;
        points.InvScale = invScale;
        points.Push(support);

        if (Detail::NearlyZero(support))
            return points;

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (maxIterations-- > 0)
        {
            if (Detail::NearlyZero(direction))
                return points;

            support = MinkowskiDifference::Support(a, b, direction) * invScale;

            if (Detail::NearlyZero(support))
            {
                points.Push(support);
                return points;
            }

            if (glm::dot(support, direction) < Config::GJK_EPSILON) return std::nullopt;

            for (int i = 0; i < points.Size; ++i)
            {
                if (glm::length2(points[i] - support) <= Config::GJK_EPSILON * Config::GJK_EPSILON)
                    return std::nullopt;
            }

            points.Push(support);
            if (NextSimplex(points, direction)) return points;
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
