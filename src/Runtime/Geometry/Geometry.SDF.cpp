module;
#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/constants.hpp>

module Geometry.SDF;

namespace Geometry::SDF
{
    namespace
    {
        constexpr float kEpsilon = 1e-6f;

        [[nodiscard]] float Dot2(const glm::vec3& v)
        {
            return glm::dot(v, v);
        }

        [[nodiscard]] float SafeSegmentProjectionT(const glm::vec3& p,
                                                   const glm::vec3& a,
                                                   const glm::vec3& b)
        {
            const glm::vec3 pa = p - a;
            const glm::vec3 ba = b - a;
            const float baLen2 = Dot2(ba);
            if (baLen2 <= kEpsilon)
            {
                return 0.0f;
            }
            return glm::clamp(glm::dot(pa, ba) / baLen2, 0.0f, 1.0f);
        }
    }

    namespace Math
    {
        float Sdf_Sphere(const glm::vec3& p, float r)
        {
            return glm::length(p) - std::max(r, 0.0f);
        }

        float Sdf_Aabb(const glm::vec3& p, const glm::vec3& extents)
        {
            const glm::vec3 safeExtents = glm::max(extents, glm::vec3(0.0f));
            const glm::vec3 q = glm::abs(p) - safeExtents;
            return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
        }

        float Sdf_Capsule(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, float r)
        {
            const glm::vec3 pa = p - a;
            const glm::vec3 ba = b - a;
            const float t = SafeSegmentProjectionT(p, a, b);
            const float radius = std::max(r, 0.0f);
            return glm::length(pa - ba * t) - radius;
        }

        float Sdf_Obb(const glm::vec3& p, const glm::vec3& extents)
        {
            return Sdf_Aabb(p, extents);
        }

        float Sdf_Cylinder(const glm::vec3& p, float h, float r)
        {
            const glm::vec2 d = glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y))
                - glm::vec2(std::max(r, 0.0f), std::max(h, 0.0f));
            return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, 0.0f));
        }

        float Sdf_Ellipsoid(const glm::vec3& p, const glm::vec3& radii)
        {
            const glm::vec3 safeRadii = glm::max(glm::abs(radii), glm::vec3(kEpsilon));
            const float k0 = glm::length(p / safeRadii);
            const float k1 = glm::length(p / (safeRadii * safeRadii));
            if (k1 <= kEpsilon)
            {
                return k0 - 1.0f;
            }
            return k0 * (k0 - 1.0f) / k1;
        }

        float Sdf_Segment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b)
        {
            const glm::vec3 pa = p - a;
            const glm::vec3 ba = b - a;
            const float t = SafeSegmentProjectionT(p, a, b);
            return glm::length(pa - ba * t);
        }

        float Sdf_Triangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
        {
            const glm::vec3 ba = b - a;
            const glm::vec3 pa = p - a;
            const glm::vec3 cb = c - b;
            const glm::vec3 pb = p - b;
            const glm::vec3 ac = a - c;
            const glm::vec3 pc = p - c;
            const glm::vec3 nor = glm::cross(ba, ac);
            const float norLen2 = Dot2(nor);

            if (norLen2 <= kEpsilon)
            {
                return std::min({
                    Sdf_Segment(p, a, b),
                    Sdf_Segment(p, b, c),
                    Sdf_Segment(p, c, a)});
            }

            const float baLen2 = std::max(Dot2(ba), kEpsilon);
            const float cbLen2 = std::max(Dot2(cb), kEpsilon);
            const float acLen2 = std::max(Dot2(ac), kEpsilon);

            const bool outside = (glm::sign(glm::dot(glm::cross(ba, nor), pa))
                + glm::sign(glm::dot(glm::cross(cb, nor), pb))
                + glm::sign(glm::dot(glm::cross(ac, nor), pc))) < 2.0f;

            if (outside)
            {
                const float d0 = Dot2(ba * glm::clamp(glm::dot(ba, pa) / baLen2, 0.0f, 1.0f) - pa);
                const float d1 = Dot2(cb * glm::clamp(glm::dot(cb, pb) / cbLen2, 0.0f, 1.0f) - pb);
                const float d2 = Dot2(ac * glm::clamp(glm::dot(ac, pc) / acLen2, 0.0f, 1.0f) - pc);
                return std::sqrt(std::min({d0, d1, d2}));
            }

            return std::sqrt(glm::dot(nor, pa) * glm::dot(nor, pa) / norLen2);
        }

        float Sdf_Plane(const glm::vec3& p, const glm::vec3& n, float d)
        {
            if (Dot2(n) <= kEpsilon)
            {
                return d;
            }
            return glm::dot(n, p) + d;
        }

        float Sdf_Ray(const glm::vec3& p, const glm::vec3& origin, const glm::vec3& dir)
        {
            const glm::vec3 pa = p - origin;
            const float dirLen2 = Dot2(dir);
            if (dirLen2 <= kEpsilon)
            {
                return glm::length(pa);
            }
            const float h = glm::max(glm::dot(pa, dir) / dirLen2, 0.0f);
            return glm::length(pa - dir * h);
        }
    }

    float SphereSDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Sphere(p - Shape.Center, Shape.Radius);
    }

    float AabbSDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Aabb(p - Center, Extents);
    }

    float ObbSDF::operator()(const glm::vec3& p) const
    {
        const glm::vec3 localP = ConjRotation * (p - Shape.Center);
        return Math::Sdf_Obb(localP, Shape.Extents);
    }

    float CapsuleSDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Capsule(p, Shape.PointA, Shape.PointB, Shape.Radius);
    }

    float CylinderSDF::operator()(const glm::vec3& p) const
    {
        const glm::vec3 localP = ConjRotation * (p - Center);
        return Math::Sdf_Cylinder(localP, Height, Radius);
    }

    float TriangleSDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Triangle(p, Shape.A, Shape.B, Shape.C) - Thickness;
    }

    float ConvexHullSDF::operator()(const glm::vec3& p) const
    {
        if (Planes.empty())
        {
            return std::numeric_limits<float>::max();
        }

        float maxDist = -std::numeric_limits<float>::infinity();
        for (const Plane& plane : Planes)
        {
            maxDist = std::max(maxDist, glm::dot(plane.Normal, p) + plane.Distance);
        }
        return maxDist;
    }

    float PlaneSDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Plane(p, Shape.Normal, Shape.Distance);
    }

    float SegmentSDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Segment(p, Shape.A, Shape.B);
    }

    float RaySDF::operator()(const glm::vec3& p) const
    {
        return Math::Sdf_Ray(p, Shape.Origin, Shape.Direction);
    }

    float EllipsoidSDF::operator()(const glm::vec3& p) const
    {
        const glm::vec3 localP = ConjRotation * (p - Center);
        return Math::Sdf_Ellipsoid(localP, Radii);
    }

    float FrustumSDF::operator()(const glm::vec3& p) const
    {
        float maxDist = -std::numeric_limits<float>::infinity();
        for (const Plane& plane : Planes)
        {
            maxDist = std::max(maxDist, Math::Sdf_Plane(p, plane.Normal, plane.Distance));
        }
        return maxDist;
    }

    SphereSDF CreateSDF(const Sphere& s)
    {
        return SphereSDF{s};
    }

    AabbSDF CreateSDF(const AABB& b)
    {
        const glm::vec3 center = (b.Min + b.Max) * 0.5f;
        const glm::vec3 extents = (b.Max - b.Min) * 0.5f;
        return AabbSDF{center, extents};
    }

    ObbSDF CreateSDF(const OBB& b)
    {
        return ObbSDF{b, glm::conjugate(b.Rotation)};
    }

    CapsuleSDF CreateSDF(const Capsule& c)
    {
        return CapsuleSDF{c};
    }

    CylinderSDF CreateSDF(const Cylinder& c)
    {
        const glm::vec3 axisVec = c.PointB - c.PointA;
        const float axisLen = glm::length(axisVec);
        if (axisLen < kEpsilon)
        {
            return CylinderSDF{c.PointA, 0.0f, c.Radius, glm::quat(1, 0, 0, 0)};
        }

        const float height = axisLen * 0.5f;
        const glm::vec3 center = (c.PointA + c.PointB) * 0.5f;
        const glm::vec3 axis = axisVec / axisLen;
        const glm::vec3 up(0, 1, 0);
        const float d = glm::dot(up, axis);

        glm::quat rot;
        if (d > 0.9999f)
        {
            rot = glm::quat(1, 0, 0, 0);
        }
        else if (d < -0.9999f)
        {
            rot = glm::angleAxis(glm::pi<float>(), glm::vec3(1, 0, 0));
        }
        else
        {
            const glm::vec3 cAxis = glm::cross(up, axis);
            const float qw = std::sqrt((1.0f + d) * 0.5f);
            const float qw2 = std::max(2.0f * qw, kEpsilon);
            rot = glm::quat(qw, cAxis.x / qw2, cAxis.y / qw2, cAxis.z / qw2);
        }

        return CylinderSDF{center, height, c.Radius, glm::conjugate(rot)};
    }

    TriangleSDF CreateSDF(const Triangle& t)
    {
        return TriangleSDF{t};
    }

    ConvexHullSDF CreateSDF(const ConvexHull& hull)
    {
        return ConvexHullSDF{std::span{hull.Planes}};
    }

    EllipsoidSDF CreateSDF(const Ellipsoid& e)
    {
        return EllipsoidSDF{e.Center, e.Radii, glm::conjugate(e.Rotation)};
    }

    FrustumSDF CreateSDF(const Frustum& f)
    {
        return FrustumSDF{f.Planes};
    }

    PlaneSDF CreateSDF(const Plane& p)
    {
        return PlaneSDF{p};
    }

    SegmentSDF CreateSDF(const Segment& s)
    {
        return SegmentSDF{s};
    }

    RaySDF CreateSDF(const Ray& r)
    {
        return RaySDF{r};
    }
}
