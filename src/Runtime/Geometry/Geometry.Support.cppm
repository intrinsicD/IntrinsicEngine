module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Runtime.Geometry.Support;

import Runtime.Geometry.Primitives;

export namespace Runtime::Geometry
{
    namespace Internal
    {
        inline glm::vec3 Normalize(const glm::vec3& v)
        {
            float lenSq = glm::length2(v);
            if (lenSq < 1e-12f) return {0, 1, 0}; // Fallback direction
            return v * glm::inversesqrt(lenSq);
        }
    }

    // -------------------------------------------------------------------------
    // Sphere
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Sphere& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        return shape.Center + dir * shape.Radius;
    }

    // -------------------------------------------------------------------------
    // AABB (Axis-Aligned Bounding Box)
    // -------------------------------------------------------------------------
    glm::vec3 Support(const AABB& shape, const glm::vec3& direction)
    {
        // Simple sign check. No normalization necessary
        // If direction.x > 0, the furthest point is Max.x, else Min.x
        return {
            (direction.x >= 0.0f) ? shape.Max.x : shape.Min.x,
            (direction.y >= 0.0f) ? shape.Max.y : shape.Min.y,
            (direction.z >= 0.0f) ? shape.Max.z : shape.Min.z
        };
    }

    // -------------------------------------------------------------------------
    // Capsule
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Capsule& shape, const glm::vec3& direction)
    {
        // 1. Find support on the inner segment (Line A-B)
        float dotA = glm::dot(direction, shape.PointA);
        float dotB = glm::dot(direction, shape.PointB);
        glm::vec3 segmentSupport = (dotA > dotB) ? shape.PointA : shape.PointB;

        // 2. Expand by radius in the dir of the search
        // We must normalize dir to add exactly 'Radius' distance.
        // Guard against zero vector to prevent NaN.
        float lenSq = glm::length2(direction);
        if (lenSq < 1e-6f) return segmentSupport;

        return segmentSupport + (direction * (shape.Radius * glm::inversesqrt(lenSq)));
    }

    // -------------------------------------------------------------------------
    // OBB (Oriented Bounding Box)
    // -------------------------------------------------------------------------
    glm::vec3 Support(const OBB& shape, const glm::vec3& direction)
    {
        // 1. Transform direction into Box's Local Space
        // Conjugate of quaternion = Inverse rotation
        auto dir = Internal::Normalize(direction);
        glm::vec3 localDir = glm::conjugate(shape.Rotation) * dir;

        // 2. Find Support in Local Space (Same as AABB logic using Extents)
        // Local Max is (+Extents), Local Min is (-Extents)
        glm::vec3 localSupport = glm::vec3(
            (localDir.x >= 0.0f) ? shape.Extents.x : -shape.Extents.x,
            (localDir.y >= 0.0f) ? shape.Extents.y : -shape.Extents.y,
            (localDir.z >= 0.0f) ? shape.Extents.z : -shape.Extents.z
        );

        // 3. Transform Local Support point back to World Space
        return shape.Center + (shape.Rotation * localSupport);
    }

    // -------------------------------------------------------------------------
    // Cylinder
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Cylinder& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        glm::vec3 axis = shape.PointB - shape.PointA;
        float axisLen2 = glm::length2(axis);

        // 1. Axial Support (Which cap?)
        // Project direction onto axis
        float dirDotAxis = glm::dot(dir, axis);
        glm::vec3 capCenter = (dirDotAxis > 0.0f) ? shape.PointB : shape.PointA;

        // 2. Radial Support (Perpendicular expansion)
        // Project dir onto the plane defined by the axis normal.
        // Orthonormal projection: v_perp = v - (v . n) * n
        // We avoid sqrt(axisLen) by using dot/len2 projection formula.

        if (axisLen2 > 1e-6f)
        {
            glm::vec3 axisDir = dir - axis * (dirDotAxis / axisLen2);
            float perpLen2 = glm::length2(axisDir);

            if (perpLen2 > 1e-6f)
            {
                // Add radius in the perpendicular dir
                return capCenter + axisDir * (shape.Radius * glm::inversesqrt(perpLen2));
            }
        }

        // If dir is exactly parallel to axis, purely axial support.
        return capCenter;
    }

    // -------------------------------------------------------------------------
    // Ellipsoid
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Ellipsoid& shape, const glm::vec3& direction)
    {
        // 1. Transform World Direction -> Local Space
        auto dir = Internal::Normalize(direction);
        glm::vec3 localDir = glm::conjugate(shape.Rotation) * dir;

        // 2. Apply Non-Uniform Scaling (Affine Transform) to dir
        // The normal on an ellipsoid corresponds to a scaled normal on the unit sphere.
        // N_sphere = N_ellipsoid * Radii
        glm::vec3 normal = localDir * shape.Radii;

        float len2 = glm::length2(normal);
        if (len2 < 1e-6f) return shape.Center;

        // Normalize to get point on Unit Sphere
        glm::vec3 unitSpherePoint = normal * glm::inversesqrt(len2);

        // 3. Scale back up to Ellipsoid Surface
        glm::vec3 localSupport = unitSpherePoint * shape.Radii;

        // 4. Transform Local Point -> World Space
        return shape.Center + (shape.Rotation * localSupport);
    }

    // -------------------------------------------------------------------------
    // Segment
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Segment& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        float dotA = glm::dot(dir, shape.A);
        float dotB = glm::dot(dir, shape.B);
        return (dotA > dotB) ? shape.A : shape.B;
    }

    // -------------------------------------------------------------------------
    // Triangle
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Triangle& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        float dotA = glm::dot(dir, shape.A);
        float dotB = glm::dot(dir, shape.B);
        float dotC = glm::dot(dir, shape.C);

        // Branchless-ish max
        if (dotA > dotB && dotA > dotC) return shape.A;
        if (dotB > dotC) return shape.B;
        return shape.C;
    }

    // -------------------------------------------------------------------------
    // Convex Hull
    // -------------------------------------------------------------------------
    glm::vec3 Support(const ConvexHull& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        // Naive O(N) iteration.
        // For large hulls (N > 64), consider Hill Climbing adjacency lookup.
        float maxDot = -std::numeric_limits<float>::infinity();
        glm::vec3 bestPt(0.0f);

        for (const auto& v : shape.Vertices)
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

    // -------------------------------------------------------------------------
    // Frustum
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Frustum& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        // A Frustum is just a Convex Hull with exactly 8 vertices.
        // We unroll the loop for performance.
        int bestIdx = 0;
        float maxDot = glm::dot(shape.Corners[0], dir);

        for (int i = 1; i < 8; ++i)
        {
            float d = glm::dot(shape.Corners[i], dir);
            if (d > maxDot)
            {
                maxDot = d;
                bestIdx = i;
            }
        }
        return shape.Corners[bestIdx];
    }

    // -------------------------------------------------------------------------
    // Ray (Semi-Infinite)
    // -------------------------------------------------------------------------
    glm::vec3 Support(const Ray& shape, const glm::vec3& direction)
    {
        auto dir = Internal::Normalize(direction);
        // Warning: GJK with infinite shapes is numerically unstable/undefined.
        // Usually, we return a point "very far away" if the dir aligns with the ray.

        float alignment = glm::dot(dir, shape.Direction);

        if (alignment > 0.0f)
        {
            // Pointing along the ray -> Return point at "Infinity"
            // We use a large finite number to keep float math valid.
            return shape.Origin + shape.Direction * 1e5f;
        }

        // Pointing against the ray -> Return Origin
        return shape.Origin;
    }
}
