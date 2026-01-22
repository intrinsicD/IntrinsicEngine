module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <vector>
#include <array>
#include <optional>

export module Geometry:GJK;

import :Primitives; // Needs definitions of ContactManifold
import :Support; // Needs definitions of ContactManifold

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
    // Centralized constants for tuning collision detection algorithms
    namespace Config
    {
        constexpr float GJK_EPSILON = 1e-6f; // Numerical tolerance for GJK convergence
        constexpr int GJK_MAX_ITERATIONS = 64; // Maximum iterations before giving up
        constexpr float EPA_EPSILON = 1e-4f; // Tolerance for EPA penetration depth
        constexpr int EPA_MAX_ITERATIONS = 32; // Maximum EPA iterations
    }

    // --- GJK IMPLEMENTATION (Boolean Overlap) ---


    // Handles the logic of processing the simplex to see if it contains origin
    // Returns true if intersection found, updates direction for next search
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

                if (glm::dot(ab, ao) > 0)
                {
                    direction = glm::cross(glm::cross(ab, ao), ab);
                    if (glm::length2(direction) < Config::GJK_EPSILON)
                    {
                        direction = ao; // Fallback to avoid zero direction on nearly collinear points
                    }
                }
                else
                {
                    points.Size = 1;
                    direction = ao;
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

                if (glm::length2(abc) < Config::GJK_EPSILON)
                {
                    direction = ao;
                    points.Size = 2;
                    points[1] = c;
                    return false;
                }

                if (glm::dot(glm::cross(abc, ac), ao) > 0)
                {
                    if (glm::dot(ac, ao) > 0)
                    {
                        points[1] = c;
                        points.Size = 2;
                        direction = glm::cross(glm::cross(ac, ao), ac);
                    }
                    else
                    {
                        // Star Case (recursion-ish)
                        points[1] = b;
                        points.Size = 2;
                        return NextSimplex(points, direction);
                    }
                }
                else
                {
                    if (glm::dot(glm::cross(ab, abc), ao) > 0)
                    {
                        // Star Case
                        points.Size = 2; // [a, b]
                        return NextSimplex(points, direction);
                    }
                    else
                    {
                        if (glm::dot(abc, ao) > 0)
                        {
                            direction = abc;
                        }
                        else
                        {
                            points[1] = c;
                            points[2] = b; // Flip winding
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
                glm::vec3 acd = glm::cross(ac, ad);
                glm::vec3 adb = glm::cross(ad, ab);

                if (glm::dot(abc, ao) > 0)
                {
                    points.Size = 3;
                    points[0] = c;
                    points[1] = b;
                    points[2] = a; // Remove d
                    return NextSimplex(points, direction);
                }
                if (glm::dot(acd, ao) > 0)
                {
                    points.Size = 3;
                    points[0] = d;
                    points[1] = c;
                    points[2] = a; // Remove b
                    return NextSimplex(points, direction);
                }
                if (glm::dot(adb, ao) > 0)
                {
                    points.Size = 3;
                    points[0] = b;
                    points[1] = d;
                    points[2] = a; // Remove c
                    return NextSimplex(points, direction);
                }
                return true; // Origin is inside all faces!
            }
        }
        return false;
    }

    template <typename A, typename B>
    bool GJK_Boolean(const A& a, const B& b)
    {
        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});

        Simplex points;
        points.Push(support);

        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (true)
        {
            support = MinkowskiDifference::Support(a, b, direction);

            if (glm::dot(support, direction) < 0) return false; // No intersection possible

            points.Push(support);

            if (NextSimplex(points, direction)) return true;

            if (points.Size > 0 && --maxIterations == 0)
            {
                return false;
            }
        }
    }

    template <typename A, typename B>
    std::optional<Simplex> GJK_Intersection(const A& a, const B& b)
    {
        glm::vec3 support = MinkowskiDifference::Support(a, b, {1, 0, 0});
        Simplex points;
        points.Push(support);
        glm::vec3 direction = -support;
        int maxIterations = Config::GJK_MAX_ITERATIONS;

        while (maxIterations-- > 0)
        {
            support = MinkowskiDifference::Support(a, b, direction);
            if (glm::dot(support, direction) < 0) return std::nullopt;
            points.Push(support);
            if (NextSimplex(points, direction)) return points; // Return the simplex containing origin
        }
        return std::nullopt;
    }
}
