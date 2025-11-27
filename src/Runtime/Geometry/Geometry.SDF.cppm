module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional> // For std::function if needed, though auto returns are better


export module Runtime.Geometry.SDF;

import Runtime.Geometry.Primitives; // Import your POD structs

export namespace Runtime::Geometry::SDF
{
    // --- 1. Raw Math Functions (Local Space) ---

    inline float Sphere(const glm::vec3& p, float r)
    {
        return glm::length(p) - r;
    }

    inline float Box(const glm::vec3& p, const glm::vec3& extents)
    {
        glm::vec3 q = glm::abs(p) - extents;
        return glm::length(glm::max(q, 0.0f)) + glm::min(glm::max(q.x, glm::max(q.y, q.z)), 0.0f);
    }

    inline float Capsule(const glm::vec3& p, const glm::vec3& a, const glm::vec3& b, float r)
    {
        glm::vec3 pa = p - a, ba = b - a;
        float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
        return glm::length(pa - ba * h) - r;
    }

    inline float Cylinder(const glm::vec3& p, float h, float r)
    {
        glm::vec2 d = glm::abs(glm::vec2(glm::length(glm::vec2(p.x, p.z)), p.y)) - glm::vec2(r, h);
        return glm::min(glm::max(d.x, d.y), 0.0f) + glm::length(glm::max(d, 0.0f));
    }

    // --- 2. World Space Adapters (Factories) ---
    // Returns a lambda [vec3 p -> float] for the given primitive

    auto CreateSDF(const Runtime::Geometry::Sphere& s)
    {
        return [center = s.Center, radius = s.Radius](const glm::vec3& p) {
            return Sphere(p - center, radius);
        };
    }

    auto CreateSDF(const Runtime::Geometry::OBB& b)
    {
        // Capture by value to allow primitive to go out of scope
        return [center = b.Center, extents = b.Extents, rot = b.Rotation](const glm::vec3& p) {
            // Transform World P -> Box Local P
            glm::vec3 localP = glm::conjugate(rot) * (p - center);
            return Box(localP, extents);
        };
    }

    auto CreateSDF(const Runtime::Geometry::Capsule& c)
    {
        return [a = c.PointA, b = c.PointB, r = c.Radius](const glm::vec3& p) {
            return Capsule(p, a, b, r);
        };
    }

    // We can define a generic CreateSDF for Cylinder if we add a Cylinder primitive
    auto CreateSDF(const Runtime::Geometry::Cylinder& c)
    {
        // Cylinder struct uses PointA/PointB logic, but raw SDF expects height/radius and centered at origin.
        // We need to construct a transform frame for the cylinder segment.

        float height = glm::length(c.PointB - c.PointA) * 0.5f;
        glm::vec3 center = (c.PointA + c.PointB) * 0.5f;
        glm::vec3 up = glm::vec3(0, 1, 0);
        glm::vec3 axis = glm::normalize(c.PointB - c.PointA);

        // Rotation alignment (From Up to Axis)
        glm::quat rot = glm::rotation(up, axis);

        return [center, height, radius = c.Radius, rot](const glm::vec3& p) {
             glm::vec3 localP = glm::conjugate(rot) * (p - center);
             return Cylinder(localP, height, radius);
        };
    }
}