module;

#include <cmath>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.GJK;

namespace Geometry::Internal
{
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
                // (a) normalized convergence tolerance: segment-degeneracy guard.
                // Comparison is intentionally against GJK_EPSILON (not EPS²) — at
                // unit-scale workspace this matches a length cutoff of ~1e-3.
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
                        // (c) barycentric clamp: dimensionless [0, 1] tolerance on
                        // the segment parameter t = (ao · ab) / |ab|². Not a
                        // length / magnitude — the absolute value of GJK_EPSILON
                        // is being reused as a clamp slack regardless of scale.
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
                        // (a) normalized convergence tolerance: in-plane projection
                        // guard. abc and ao are both in normalized workspace, so
                        // |abc · ao| ≤ EPS ⇒ origin lies on the supporting plane.
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
}
