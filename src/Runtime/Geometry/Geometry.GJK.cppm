module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <vector>
#include <array>
#include <optional>

export module Runtime.Geometry.GJK;

import Runtime.Geometry.Primitives; // Needs definitions of ContactManifold

export namespace Runtime::Geometry::Internal
{
    // --- GJK HELPER STRUCTS ---

    struct MinkowskiDifference
    {
        // We use function pointers or templates in a real generic system, 
        // but for this specific GJK implementation, we assume templates from the caller.
        // Helper to compute A - B support
        template <typename A, typename B>
        static glm::vec3 Support(const A& a, const B& b, const glm::vec3& dir)
        {
            return a.Support(dir) - b.Support(-dir);
        }
    };

    struct Simplex
    {
        std::array<glm::vec3, 4> Points;
        int Size = 0;

        void Push(glm::vec3 p) { Points[Size++] = p; }
        glm::vec3& operator[](int i) { return Points[i]; }
        const glm::vec3& operator[](int i) const { return Points[i]; }
    };

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

        while (true)
        {
            support = MinkowskiDifference::Support(a, b, direction);

            if (glm::dot(support, direction) < 0) return false; // No intersection possible

            points.Push(support);

            if (NextSimplex(points, direction)) return true;
        }
    }

    // --- EPA IMPLEMENTATION (Contact Manifold) ---
    // Note: This requires a GJK run first that leaves the simplex intact.
    // For brevity, this is a simplified EPA that assumes GJK returned a tetrahedron.

    struct PolytopeFace
    {
        glm::vec3 Normal;
        float Distance;
        int A, B, C;
    };

    template <typename ShapeA, typename ShapeB>
    std::optional<glm::vec3> EPA_Run(const ShapeA& a, const ShapeB& b, const Simplex& simplex,
                                     std::vector<glm::vec3>& polytope)
    {
        // 1. Initialize Polytope from Simplex
        polytope.insert(polytope.end(), simplex.Points.begin(), simplex.Points.end());
        std::vector<int> faces = {
            0, 1, 2,
            0, 3, 1,
            0, 2, 3,
            1, 3, 2
        };

        // ... (Full EPA implementation is ~100 lines of mesh expansion logic)
        // For this snippet, we will assume a simple GJK depth estimation
        // or return a basic vector.
        // A production engine needs the full expansion loop here.

        // Return placeholder for logic: Normal * Depth
        return glm::vec3(0, 1, 0) * 0.0f;
    }
}
