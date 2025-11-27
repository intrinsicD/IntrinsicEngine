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

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            return Center + glm::normalize(dir) * Radius;
        }
    };

    struct AABB
    {
        glm::vec3 Min;
        glm::vec3 Max;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
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

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            const float dotA = glm::dot(dir, PointA);
            const float dotB = glm::dot(dir, PointB);
            return (dotA > dotB ? PointA : PointB) + glm::normalize(dir) * Radius;
        }
    };

    struct OBB
    {
        glm::vec3 Center;
        glm::vec3 Extents; // Half-width, half-height, half-depth
        glm::quat Rotation;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            // Transform direction to local space, find support, transform back
            const glm::vec3 localDir = glm::conjugate(Rotation) * dir;
            const glm::vec3 localSupport = {
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
            const glm::vec3 axis = PointB - PointA;
            // Avoid normalizing axis here to keep it cheap; just compare dots
            glm::vec3 axisSupport = (glm::dot(dir, axis) > 0) ? PointB : PointA;

            // 2. Find the support of the "disk" (Circle perpendicular to axis)
            // We project 'dir' onto the plane perpendicular to the axis.
            // dir_perp = dir - (dir . axis_norm) * axis_norm
            // To avoid sqrt(axis), we use: dir_perp = dir - axis * (dot(d,a) / dot(a,a))
            const float lenSq = glm::dot(axis, axis);
            glm::vec3 dirPerp = dir;

            if (lenSq > 1e-6f) // Guard against zero-height cylinder
            {
                const float projection = glm::dot(dir, axis) / lenSq;
                dirPerp = dir - axis * projection;
            }

            // 3. Add the radius in that perpendicular direction
            // If dir matches axis perfectly, dirPerp is 0, so no radial expansion (correct)
            const float perpLen = glm::length(dirPerp);
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
            const glm::vec3 localDir = glm::conjugate(Rotation) * dir;

            // Apply non-uniform scaling to the direction vector
            // to find the point on the unit sphere that maps to the extreme point
            const glm::vec3 normal = glm::normalize(localDir * Radii);

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
            const float dotA = glm::dot(dir, A);
            const float dotB = glm::dot(dir, B);
            return (dotA > dotB) ? A : B;
        }
    };

    struct Triangle
    {
        glm::vec3 A, B, C;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            const float dotA = glm::dot(dir, A);
            const float dotB = glm::dot(dir, B);
            const float dotC = glm::dot(dir, C);

            if (dotA > dotB && dotA > dotC) return A;
            if (dotB > dotC) return B;
            return C;
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
            auto bestPt = glm::vec3(0.0f);

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

        void Normalize()
        {
            const float len = glm::length(Normal);
            Normal /= len;
            Distance /= len;
        }

        // Positive distance = Point is in front of plane (inside frustum usually)
        [[nodiscard]] float GetSignedDistance(const glm::vec3& point) const
        {
            return glm::dot(Normal, point) + Distance;
        }
    };

    struct Frustum
    {
        // Storing the 8 corners is the fastest way for GJK
        std::array<glm::vec3, 8> Corners;
        std::array<Plane, 6> Planes;

        [[nodiscard]] glm::vec3 Support(const glm::vec3& dir) const
        {
            // Same logic as ConvexHull, but unrolled for speed
            int bestIdx = 0;
            float maxDot = glm::dot(Corners[0], dir);

            for (int i = 1; i < 8; ++i)
            {
                float d = glm::dot(Corners[i], dir);
                if (d > maxDot)
                {
                    maxDot = d;
                    bestIdx = i;
                }
            }
            return Corners[bestIdx];
        }

        // --- Construction Helper ---
        // Call this once per frame per camera
        static Frustum CreateFromMatrix(const glm::mat4& viewProj)
        {
            Frustum f;

            // 1. Extract Planes (Gribb/Hartmann method)
            // Left
            f.Planes[0].Normal.x = viewProj[0][3] + viewProj[0][0];
            f.Planes[0].Normal.y = viewProj[1][3] + viewProj[1][0];
            f.Planes[0].Normal.z = viewProj[2][3] + viewProj[2][0];
            f.Planes[0].Distance = viewProj[3][3] + viewProj[3][0];

            // Right
            f.Planes[1].Normal.x = viewProj[0][3] - viewProj[0][0];
            f.Planes[1].Normal.y = viewProj[1][3] - viewProj[1][0];
            f.Planes[1].Normal.z = viewProj[2][3] - viewProj[2][0];
            f.Planes[1].Distance = viewProj[3][3] - viewProj[3][0];

            // Bottom
            f.Planes[2].Normal.x = viewProj[0][3] + viewProj[0][1];
            f.Planes[2].Normal.y = viewProj[1][3] + viewProj[1][1];
            f.Planes[2].Normal.z = viewProj[2][3] + viewProj[2][1];
            f.Planes[2].Distance = viewProj[3][3] + viewProj[3][1];

            // Top
            f.Planes[3].Normal.x = viewProj[0][3] - viewProj[0][1];
            f.Planes[3].Normal.y = viewProj[1][3] - viewProj[1][1];
            f.Planes[3].Normal.z = viewProj[2][3] - viewProj[2][1];
            f.Planes[3].Distance = viewProj[3][3] - viewProj[3][1];

            // Near
            f.Planes[4].Normal.x = viewProj[0][3] + viewProj[0][2];
            f.Planes[4].Normal.y = viewProj[1][3] + viewProj[1][2];
            f.Planes[4].Normal.z = viewProj[2][3] + viewProj[2][2];
            f.Planes[4].Distance = viewProj[3][3] + viewProj[3][2];

            // Far
            f.Planes[5].Normal.x = viewProj[0][3] - viewProj[0][2];
            f.Planes[5].Normal.y = viewProj[1][3] - viewProj[1][2];
            f.Planes[5].Normal.z = viewProj[2][3] - viewProj[2][2];
            f.Planes[5].Distance = viewProj[3][3] - viewProj[3][2];

            for (auto& p : f.Planes) p.Normalize();

            // 2. Compute Corners (Inverse Transform NDC cube)
            glm::mat4 inv = glm::inverse(viewProj);
            glm::vec4 ndc[8] = {
                {-1, -1, -1, 1}, {1, -1, -1, 1}, {1, 1, -1, 1}, {-1, 1, -1, 1}, // Near
                {-1, -1, 1, 1}, {1, -1, 1, 1}, {1, 1, 1, 1}, {-1, 1, 1, 1} // Far
            };

            for (int i = 0; i < 8; ++i)
            {
                glm::vec4 res = inv * ndc[i];
                f.Corners[i] = glm::vec3(res) / res.w;
            }

            return f;
        }
    };

    struct Ray
    {
        glm::vec3 Origin;
        glm::vec3 Direction;
    };
}
