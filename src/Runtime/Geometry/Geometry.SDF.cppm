module;
#include <array>
#include <span>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Geometry.SDF;

import Geometry.Primitives;

export namespace Geometry::SDF
{
    namespace Math
    {
        [[nodiscard]] float Sdf_Sphere(const glm::vec3& p, float r);
        [[nodiscard]] float Sdf_Aabb(const glm::vec3& p, const glm::vec3& extents);
        [[nodiscard]] float Sdf_Capsule(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, float r);
        [[nodiscard]] float Sdf_Obb(const glm::vec3& p, const glm::vec3& extents);
        [[nodiscard]] float Sdf_Cylinder(const glm::vec3& p, float h, float r);
        [[nodiscard]] float Sdf_Ellipsoid(const glm::vec3& p, const glm::vec3& radii);
        [[nodiscard]] float Sdf_Segment(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b);
        [[nodiscard]] float Sdf_Triangle(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
        [[nodiscard]] float Sdf_Plane(const glm::vec3& p, const glm::vec3& n, float d);
        [[nodiscard]] float Sdf_Ray(const glm::vec3& p, const glm::vec3& origin, const glm::vec3& dir);
    }

    struct SphereSDF
    {
        Sphere Shape;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct AabbSDF
    {
        glm::vec3 Center;
        glm::vec3 Extents;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct ObbSDF
    {
        OBB Shape;
        glm::quat ConjRotation;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct CapsuleSDF
    {
        Capsule Shape;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct CylinderSDF
    {
        glm::vec3 Center;
        float Height;
        float Radius;
        glm::quat ConjRotation;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct TriangleSDF
    {
        Triangle Shape;
        float Thickness = 0.02f;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct ConvexHullSDF
    {
        std::span<const Plane> Planes;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct PlaneSDF
    {
        Plane Shape;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct SegmentSDF
    {
        Segment Shape;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct RaySDF
    {
        Ray Shape;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct EllipsoidSDF
    {
        glm::vec3 Center;
        glm::vec3 Radii;
        glm::quat ConjRotation;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    struct FrustumSDF
    {
        std::array<Plane, 6> Planes;
        [[nodiscard]] float operator()(const glm::vec3& p) const;
    };

    [[nodiscard]] SphereSDF CreateSDF(const Sphere& s);
    [[nodiscard]] AabbSDF CreateSDF(const AABB& b);
    [[nodiscard]] ObbSDF CreateSDF(const OBB& b);
    [[nodiscard]] CapsuleSDF CreateSDF(const Capsule& c);
    [[nodiscard]] CylinderSDF CreateSDF(const Cylinder& c);
    [[nodiscard]] TriangleSDF CreateSDF(const Triangle& t);
    [[nodiscard]] ConvexHullSDF CreateSDF(const ConvexHull& hull);
    [[nodiscard]] EllipsoidSDF CreateSDF(const Ellipsoid& e);
    [[nodiscard]] FrustumSDF CreateSDF(const Frustum& f);
    [[nodiscard]] PlaneSDF CreateSDF(const Plane& p);
    [[nodiscard]] SegmentSDF CreateSDF(const Segment& s);
    [[nodiscard]] RaySDF CreateSDF(const Ray& r);
}
