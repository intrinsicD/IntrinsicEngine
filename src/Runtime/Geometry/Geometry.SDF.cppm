module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <vector>
#include <cmath>

export module Runtime.Geometry.SDF;

import Runtime.Geometry.Primitives;

export namespace Runtime::Geometry::SDF
{
    // --- 1. Raw Math Functions (Pure Math, No State) ---

    namespace Math
    {
        inline float Sdf_Sphere(const glm::vec3& p, float r)
        {
            return glm::length(p) - r;
        }

        inline float Sdf_Aabb(const glm::vec3& p, const glm::vec3& extents)
        {
            glm::vec3 q = glm::abs(p) - extents;
            return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
        }

        inline float Sdf_Capsule(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, float r)
        {
            glm::vec3 pa = p - a, ba = b - a;
            float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
            return glm::length(pa - ba * h) - r;
        }

        inline float Sdf_Obb(const glm::vec3& p, const glm::vec3& extents)
        {
            return Sdf_Aabb(p, extents);
        }

        inline float Sdf_Cylinder(const glm::vec3& p, float h, float r)
        {
            glm::vec2 d = glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y)) - glm::vec2(r, h);
            return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, 0.0f));
        }

        // Ellipsoid Approximation (Not exact, but good for collision)
        // Uses the "Scale world to unit sphere" trick with correction.
        inline float Sdf_Ellipsoid(const glm::vec3& p, const glm::vec3& radii)
        {
            float k0 = glm::length(p / radii);
            float k1 = glm::length(p / (radii * radii));
            return k0 * (k0 - 1.0f) / k1;
        }

        inline float Sdf_Segment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
        {
            glm::vec3 pa = p - a, ba = b - a;
            float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
            return glm::length(pa - ba * h);
        }

        inline float Math_Dot2(const glm::vec3& v) { return glm::dot(v, v); }

        // Inigo Quilez's Exact Triangle SDF
        inline float Sdf_Triangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            glm::vec3 ba = b - a;
            glm::vec3 pa = p - a;
            glm::vec3 cb = c - b;
            glm::vec3 pb = p - b;
            glm::vec3 ac = a - c;
            glm::vec3 pc = p - c;
            glm::vec3 nor = glm::cross(ba, ac);

            return sqrt(
                (glm::sign(glm::dot(glm::cross(ba, nor), pa)) +
                    glm::sign(glm::dot(glm::cross(cb, nor), pb)) +
                    glm::sign(glm::dot(glm::cross(ac, nor), pc)) < 2.0)
                    ? glm::min(glm::min(
                                   Math_Dot2(ba * glm::clamp(glm::dot(ba, pa) / Math_Dot2(ba), 0.0f, 1.0f) - pa),
                                   Math_Dot2(cb * glm::clamp(glm::dot(cb, pb) / Math_Dot2(cb), 0.0f, 1.0f) - pb)),
                               Math_Dot2(ac * glm::clamp(glm::dot(ac, pc) / Math_Dot2(ac), 0.0f, 1.0f) - pc))
                    : glm::dot(nor, pa) * glm::dot(nor, pa) / Math_Dot2(nor));
        }

        inline float Sdf_Plane(const glm::vec3& p, const glm::vec3& n, float d)
        {
            // Assuming N is normalized
            return glm::dot(n, p) + d;
        }

        inline float Sdf_Ray(const glm::vec3& p, const glm::vec3& origin, const glm::vec3& dir)
        {
            // Distance to a line starting at 'origin' and going infinite in 'dir'
            glm::vec3 pa = p - origin;
            // Project p onto dir
            float h = glm::dot(pa, dir);
            // Clamp so we don't look behind the ray origin
            h = glm::max(h, 0.0f);
            return glm::length(pa - dir * h);
        }
    }


    // --- 2. Named Functors (Stateful Adapters) ---

    // Simple Shapes: Store Primitive By Value (Safe & Fast)
    struct SphereSDF
    {
        Sphere Shape;

        float operator()(const glm::vec3& p) const
        {
            return Math::Sdf_Sphere(p - Shape.Center, Shape.Radius);
        }
    };

    struct AabbSDF
    {
        glm::vec3 Center;
        glm::vec3 Extents;

        float operator()(const glm::vec3& p) const
        {
            // No rotation needed! Faster than OBB.
            return Math::Sdf_Aabb(p - Center, Extents);
        }
    };

    struct ObbSDF
    {
        OBB Shape;
        glm::quat ConjRotation;

        float operator()(const glm::vec3& p) const
        {
            // Transform World P -> Box Local P
            glm::vec3 localP = ConjRotation * (p - Shape.Center);
            return Math::Sdf_Obb(localP, Shape.Extents);
        }
    };

    struct CapsuleSDF
    {
        Capsule Shape;

        float operator()(const glm::vec3& p) const
        {
            return Math::Sdf_Capsule(p, Shape.PointA, Shape.PointB, Shape.Radius);
        }
    };

    // Complex Case: Cylinder
    // We CANNOT store the Cylinder primitive directly efficiently.
    // We must store the pre-computed decomposition (Basis/Rotation) for performance.
    struct CylinderSDF
    {
        glm::vec3 Center;
        float Height;
        float Radius;
        glm::quat ConjRotation; // <--- Pre-computed! Not in original Cylinder struct.

        float operator()(const glm::vec3& p) const
        {
            // 1. Transform P to Cylinder Local Space using pre-computed rotation
            glm::vec3 localP = ConjRotation * (p - Center);
            // 2. Evaluate against a vertical cylinder at origin
            return Math::Sdf_Cylinder(localP, Height, Radius);
        }
    };

    struct TriangleSDF
    {
        Triangle Shape;
        float Thickness = 0.02f;

        float operator()(const glm::vec3& p) const
        {
            return Math::Sdf_Triangle(p, Shape.A, Shape.B, Shape.C) - Thickness;
        }
    };

    struct ConvexHullSDF
    {
        std::vector<Plane> Planes;

        float operator()(const glm::vec3& p) const
        {
            // Handle empty hull (degenerate case)
            if (Planes.empty())
            {
                // Empty hull contains nothing - return large positive distance
                return std::numeric_limits<float>::max();
            }

            float maxDist = -std::numeric_limits<float>::infinity();

            for (const auto& plane : Planes)
            {
                float d = glm::dot(plane.Normal, p) + plane.Distance;
                if (d > maxDist) maxDist = d;
            }
            return maxDist;
        }
    };

    struct PlaneSDF
    {
        Plane Shape;

        float operator()(const glm::vec3& p) const
        {
            return Math::Sdf_Plane(p, Shape.Normal, Shape.Distance);
        }
    };

    // --- Segment ---
    struct SegmentSDF
    {
        Segment Shape;

        float operator()(const glm::vec3& p) const
        {
            return Math::Sdf_Segment(p, Shape.A, Shape.B);
        }
    };

    // --- Ray ---
    struct RaySDF
    {
        Ray Shape;

        float operator()(const glm::vec3& p) const
        {
            return Math::Sdf_Ray(p, Shape.Origin, Shape.Direction);
        }
    };

    // --- Ellipsoid ---
    struct EllipsoidSDF
    {
        glm::vec3 Center;
        glm::vec3 Radii;
        glm::quat ConjRotation;

        float operator()(const glm::vec3& p) const
        {
            // Transform to local un-rotated space
            glm::vec3 localP = ConjRotation * (p - Center);
            return Math::Sdf_Ellipsoid(localP, Radii);
        }
    };

    // --- Frustum (Convex intersection of 6 planes) ---
    struct FrustumSDF
    {
        // We copy the planes array (6 * 16 bytes = 96 bytes).
        // This is acceptable to copy by value for cache locality in the hot loop.
        std::array<Plane, 6> Planes;

        float operator()(const glm::vec3& p) const
        {
            // Intersection of volumes = Max of signed distances
            // If inside, all distances are negative. Max is the least negative (closest to boundary).
            // If outside, at least one is positive. Max is the most positive.
            float maxDist = -std::numeric_limits<float>::infinity();

            for (const auto& plane : Planes)
            {
                // GetSignedDistance assumes + is "in front" (usually inside for Frustum planes depending on winding)
                // Standard Gribb/Hartmann extraction usually points normals INSIDE.
                // If Normals point IN: Inside is +dist.
                // If Normals point OUT: Inside is -dist.
                //
                // Let's assume Standard Physics: Normals point OUT of the solid.
                // So Inside = Negative.
                // Our Primitive::Frustum normals usually point OUT.
                float d = Math::Sdf_Plane(p, plane.Normal, plane.Distance);
                if (d > maxDist) maxDist = d;
            }
            return maxDist;
        }
    };

    // --- 3. Factories (The only place where conversion happens) ---

    auto CreateSDF(const Sphere& s)
    {
        return SphereSDF{s}; // Simple copy
    }

    auto CreateSDF(const AABB& b)
    {
        glm::vec3 center = (b.Min + b.Max) * 0.5f;
        glm::vec3 extents = (b.Max - b.Min) * 0.5f;
        return AabbSDF{center, extents};
    }

    auto CreateSDF(const OBB& b)
    {
        return ObbSDF{b, glm::conjugate(b.Rotation)}; // Simple copy
    }

    auto CreateSDF(const Capsule& c)
    {
        return CapsuleSDF{c}; // Simple copy
    }

    auto CreateSDF(const Cylinder& c)
    {
        // --- Heavy Math happens ONCE here, not in the loop ---
        glm::vec3 axisVec = c.PointB - c.PointA;
        float axisLen = glm::length(axisVec);

        // Handle degenerate cylinder (PointA == PointB)
        if (axisLen < 1e-6f)
        {
            // Degenerate to sphere
            return CylinderSDF{c.PointA, 0.0f, c.Radius, glm::quat(1, 0, 0, 0)};
        }

        float height = axisLen * 0.5f;
        glm::vec3 center = (c.PointA + c.PointB) * 0.5f;
        glm::vec3 axis = axisVec / axisLen; // Normalized axis

        // Robust quaternion construction from orthonormal basis
        // We want to rotate Y-axis to align with 'axis'
        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::quat rot;

        float d = glm::dot(up, axis);

        // Check if vectors are parallel (aligned or opposite)
        if (d > 0.9999f)
        {
            // Already aligned with Y-axis
            rot = glm::quat(1, 0, 0, 0);
        }
        else if (d < -0.9999f)
        {
            // Opposite to Y-axis: rotate 180 degrees around X
            rot = glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0));
        }
        else
        {
            // General case: use half-angle formula for numerical stability
            // This is more stable than glm::rotation() for small angles
            glm::vec3 c = glm::cross(up, axis);
            float s = glm::length(c); //TODO: Why is this unused? can i remove it?

            // Quaternion from axis-angle using half-angle formula
            // q.w = cos(theta/2) = sqrt((1 + cos(theta)) / 2)
            // q.xyz = sin(theta/2) * normalized_axis = c / (2 * sqrt((1 + cos(theta)) / 2))
            float qw = std::sqrt((1.0f + d) * 0.5f);
            float qw2 = 2.0f * qw;
            rot = glm::quat(qw, c.x / qw2, c.y / qw2, c.z / qw2);
        }

        return CylinderSDF{center, height, c.Radius, glm::conjugate(rot)};
    }

    auto CreateSDF(const Triangle& t)
    {
        return TriangleSDF{t};
    }

    auto CreateSDF(const ConvexHull& hull)
    {
        return ConvexHullSDF{hull.Planes};
    }

    auto CreateSDF(const Ellipsoid& e)
    {
        return EllipsoidSDF{e.Center, e.Radii, glm::conjugate(e.Rotation)};
    }

    auto CreateSDF(const Frustum& f)
    {
        return FrustumSDF{f.Planes};
    }

    auto CreateSDF(const Plane& p)
    {
        return PlaneSDF{p};
    }

    auto CreateSDF(const Segment& s)
    {
        return SegmentSDF{s};
    }
}
