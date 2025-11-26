module;

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <array>
#include <vector>

export module Runtime.Geometry.Primitives;

export namespace Runtime::Geometry
{
    // Common interface concept for Generic Solvers
    template <typename T>
    concept ConvexShape = requires(T s, glm::vec3 d)
    {
        { s.Support(d) } -> std::convertible_to<glm::vec3>;
    };

    struct Sphere
    {
        glm::vec3 Center;
        float Radius;

        [[nodiscard]] glm::vec3 Support(glm::vec3 dir) const
        {
            return Center + glm::normalize(dir) * Radius;
        }
    };

    struct AABB
    {
        glm::vec3 Min;
        glm::vec3 Max;

        [[nodiscard]] glm::vec3 Support(glm::vec3 dir) const
        {
            return {
                dir.x > 0 ? Max.x : Min.x,
                dir.y > 0 ? Max.y : Min.y,
                dir.z > 0 ? Max.z : Min.z
            };
        }
    };

    struct Capsule
    {
        glm::vec3 PointA, PointB;
        float Radius;

        [[nodiscard]] glm::vec3 Support(glm::vec3 dir) const
        {
            float dotA = glm::dot(dir, PointA);
            float dotB = glm::dot(dir, PointB);
            return (dotA > dotB ? PointA : PointB) + glm::normalize(dir) * Radius;
        }
    };

    struct OBB
    {
        glm::vec3 Center;
        glm::vec3 Extents; // Half-width, half-height, half-depth
        glm::quat Rotation;

        [[nodiscard]] glm::vec3 Support(glm::vec3 dir) const
        {
            // Transform direction to local space, find support, transform back
            glm::vec3 localDir = glm::conjugate(Rotation) * dir;
            glm::vec3 localSupport = {
                (localDir.x > 0 ? Extents.x : -Extents.x),
                (localDir.y > 0 ? Extents.y : -Extents.y),
                (localDir.z > 0 ? Extents.z : -Extents.z)
            };
            return Center + (Rotation * localSupport);
        }
    };

    struct Cylinder
    {
        glm::vec3 PointA; // Bottom Center
        glm::vec3 PointB; // Top Center
        float Radius;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            // 1. Find the support of the "axis" (Segment)
            glm::vec3 axis = PointB - PointA;
            // Avoid normalizing axis here to keep it cheap; just compare dots
            glm::vec3 axisSupport = (glm::dot(dir, axis) > 0) ? PointB : PointA;

            // 2. Find the support of the "disk" (Circle perpendicular to axis)
            // We project 'dir' onto the plane perpendicular to the axis.
            // dir_perp = dir - (dir . axis_norm) * axis_norm
            // To avoid sqrt(axis), we use: dir_perp = dir - axis * (dot(d,a) / dot(a,a))
            float lenSq = glm::dot(axis, axis);
            glm::vec3 dirPerp = dir;

            if (lenSq > 1e-6f) // Guard against zero-height cylinder
            {
                float projection = glm::dot(dir, axis) / lenSq;
                dirPerp = dir - axis * projection;
            }

            // 3. Add the radius in that perpendicular direction
            // If dir matches axis perfectly, dirPerp is 0, so no radial expansion (correct)
            float perpLen = glm::length(dirPerp);
            if (perpLen > 1e-6f)
            {
                axisSupport += (dirPerp / perpLen) * Radius;
            }

            return axisSupport;
        }
    };

    struct Ellipsoid
    {
        glm::vec3 Center;
        glm::vec3 Radii; // Scaling factors (Rx, Ry, Rz)
        glm::quat Rotation;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            // Transform direction into local space of the sphere
            glm::vec3 localDir = glm::conjugate(Rotation) * dir;

            // Apply non-uniform scaling to the direction vector
            // to find the point on the unit sphere that maps to the extreme point
            glm::vec3 normal = glm::normalize(localDir * Radii);

            // Scale back up
            glm::vec3 supportLocal = normal * Radii;

            // Transform back to world
            return Center + (Rotation * supportLocal);
        }
    };

    struct Segment
    {
        glm::vec3 A, B;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            float dotA = glm::dot(dir, A);
            float dotB = glm::dot(dir, B);
            return (dotA > dotB) ? A : B;
        }
    };

    struct Triangle
    {
        glm::vec3 A, B, C;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            float dotA = glm::dot(dir, A);
            float dotB = glm::dot(dir, B);
            float dotC = glm::dot(dir, C);

            if (dotA > dotB && dotA > dotC) return A;
            if (dotB > dotC) return B;
            return C;
        }
    };

    struct Frustum
    {
        // Storing the 8 corners is the fastest way for GJK
        std::array<glm::vec3, 8> Corners;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            // Same logic as ConvexHull, but unrolled for speed
            int bestIdx = 0;
            float maxDot = glm::dot(Corners[0], dir);

            for(int i = 1; i < 8; ++i)
            {
                float d = glm::dot(Corners[i], dir);
                if(d > maxDot) { maxDot = d; bestIdx = i; }
            }
            return Corners[bestIdx];
        }
    };

    struct ConvexHull
    {
        // For performance, pointers to data are often better than std::vector copy //TODO
        std::vector<glm::vec3> Vertices;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            // Naive O(N) implementation.
            // For large hulls (N > 50), Hill Climbing is preferred.
            float maxDot = -std::numeric_limits<float>::infinity();
            glm::vec3 bestPt = glm::vec3(0.0f);

            for (const auto& v : Vertices)
            {
                float dot = glm::dot(v, dir);
                if (dot > maxDot)
                {
                    maxDot = dot;
                    bestPt = v;
                }
            }
            return bestPt;
        }
    };

    struct Plane
    {
        glm::vec3 Normal;
        float Distance;
    };

    struct Ray
    {
        glm::vec3 Origin;
        glm::vec3 Direction;
    };
}
