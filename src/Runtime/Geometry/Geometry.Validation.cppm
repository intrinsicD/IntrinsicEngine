module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <cmath>
#include <limits>


export module Runtime.Geometry.Validation;
 
import Runtime.Geometry.Primitives;
 
export namespace Runtime::Geometry::Validation
{
    // =========================================================================
    // VALIDATION UTILITIES
    // =========================================================================
 
    constexpr float EPSILON = 1e-6f;
    constexpr float EPSILON_SQ = EPSILON * EPSILON;
 
    // --- Vector Validation ---
 
    inline bool IsFinite(const glm::vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }
 
    inline bool IsNormalized(const glm::vec3& v, float tolerance = 1e-4f)
    {
        float lenSq = glm::dot(v, v);
        return std::abs(lenSq - 1.0f) < tolerance;
    }
 
    inline bool IsZero(const glm::vec3& v, float epsilon = EPSILON)
    {
        return glm::length2(v) < epsilon * epsilon;
    }
 
    // --- Primitive Validation ---
 
    inline bool IsValid(const Sphere& s)
    {
        return IsFinite(s.Center) &&
               s.Radius > 0.0f &&
               s.Radius < std::numeric_limits<float>::max();
    }
 
    inline bool IsValid(const AABB& box)
    {
        return IsFinite(box.Min) &&
               IsFinite(box.Max) &&
               box.Min.x <= box.Max.x &&
               box.Min.y <= box.Max.y &&
               box.Min.z <= box.Max.z;
    }
 
    inline bool IsDegenerate(const AABB& box, float epsilon = EPSILON)
    {
        glm::vec3 size = box.Max - box.Min;
        return size.x < epsilon || size.y < epsilon || size.z < epsilon;
    }
 
    inline bool IsValid(const OBB& obb)
    {
        return IsFinite(obb.Center) &&
               IsFinite(obb.Extents) &&
               obb.Extents.x > 0.0f && obb.Extents.y > 0.0f && obb.Extents.z > 0.0f &&
               std::abs(glm::length(obb.Rotation) - 1.0f) < 1e-4f; // Quaternion normalized
    }
 
    inline bool IsDegenerate(const OBB& obb, float epsilon = EPSILON)
    {
        return obb.Extents.x < epsilon || obb.Extents.y < epsilon || obb.Extents.z < epsilon;
    }
 
    inline bool IsValid(const Capsule& cap)
    {
        return IsFinite(cap.PointA) &&
               IsFinite(cap.PointB) &&
               cap.Radius > 0.0f &&
               cap.Radius < std::numeric_limits<float>::max();
    }
 
    inline bool IsDegenerate(const Capsule& cap, float epsilon = EPSILON)
    {
        return glm::distance2(cap.PointA, cap.PointB) < epsilon * epsilon;
    }
 
    inline bool IsValid(const Cylinder& cyl)
    {
        return IsFinite(cyl.PointA) &&
               IsFinite(cyl.PointB) &&
               cyl.Radius > 0.0f &&
               cyl.Radius < std::numeric_limits<float>::max();
    }
 
    inline bool IsDegenerate(const Cylinder& cyl, float epsilon = EPSILON)
    {
        return glm::distance2(cyl.PointA, cyl.PointB) < epsilon * epsilon;
    }
 
    inline bool IsValid(const Ellipsoid& e)
    {
        return IsFinite(e.Center) &&
               IsFinite(e.Radii) &&
               e.Radii.x > 0.0f && e.Radii.y > 0.0f && e.Radii.z > 0.0f &&
               std::abs(glm::length(e.Rotation) - 1.0f) < 1e-4f;
    }
 
    inline bool IsDegenerate(const Ellipsoid& e, float epsilon = EPSILON)
    {
        return e.Radii.x < epsilon || e.Radii.y < epsilon || e.Radii.z < epsilon;
    }
 
    inline bool IsValid(const Triangle& tri)
    {
        return IsFinite(tri.A) && IsFinite(tri.B) && IsFinite(tri.C);
    }
 
    inline bool IsDegenerate(const Triangle& tri, float epsilon = EPSILON)
    {
        // Check if all three vertices are collinear or coincident
        glm::vec3 ab = tri.B - tri.A;
        glm::vec3 ac = tri.C - tri.A;
        glm::vec3 normal = glm::cross(ab, ac);
        return glm::length2(normal) < epsilon * epsilon;
    }
 
    inline bool IsValid(const Plane& p)
    {
        float lenSq = glm::dot(p.Normal, p.Normal);
        return lenSq > 1e-12f && !std::isnan(p.Distance) && !std::isinf(p.Distance);
    }
 
    inline bool IsValid(const Ray& r)
    {
        return IsFinite(r.Origin) &&
               IsFinite(r.Direction) &&
               !IsZero(r.Direction);
    }
 
    inline bool IsValid(const Segment& seg)
    {
        return IsFinite(seg.A) && IsFinite(seg.B);
    }
 
    inline bool IsDegenerate(const Segment& seg, float epsilon = EPSILON)
    {
        return glm::distance2(seg.A, seg.B) < epsilon * epsilon;
    }
 
    inline bool IsValid(const ConvexHull& hull)
    {
        if (hull.Vertices.empty()) return false;
 
        for (const auto& v : hull.Vertices)
        {
            if (!IsFinite(v)) return false;
        }
 
        for (const auto& p : hull.Planes)
        {
            if (!IsValid(p)) return false;
        }
 
        return true;
    }
 
    inline bool IsValid(const Frustum& f)
    {
        for (const auto& p : f.Planes)
        {
            if (!IsValid(p)) return false;
        }
 
        for (const auto& corner : f.Corners)
        {
            if (!IsFinite(corner)) return false;
        }
 
        return true;
    }
 
    // =========================================================================
    // SANITIZATION UTILITIES (Make primitives valid)
    // =========================================================================
 
    inline Sphere Sanitize(const Sphere& s)
    {
        Sphere result = s;
        if (!IsFinite(result.Center)) result.Center = glm::vec3(0.0f);
        if (result.Radius <= 0.0f || !std::isfinite(result.Radius))
            result.Radius = 1.0f;
        return result;
    }
 
    inline AABB Sanitize(const AABB& box)
    {
        AABB result = box;
        if (!IsFinite(result.Min)) result.Min = glm::vec3(-1.0f);
        if (!IsFinite(result.Max)) result.Max = glm::vec3(1.0f);
 
        // Ensure Min <= Max
        if (result.Min.x > result.Max.x) std::swap(result.Min.x, result.Max.x);
        if (result.Min.y > result.Max.y) std::swap(result.Min.y, result.Max.y);
        if (result.Min.z > result.Max.z) std::swap(result.Min.z, result.Max.z);
 
        // Ensure non-degenerate
        if (IsDegenerate(result))
        {
            glm::vec3 center = (result.Min + result.Max) * 0.5f;
            result.Min = center - glm::vec3(0.5f);
            result.Max = center + glm::vec3(0.5f);
        }
 
        return result;
    }
 
    inline OBB Sanitize(const OBB& obb)
    {
        OBB result = obb;
        if (!IsFinite(result.Center)) result.Center = glm::vec3(0.0f);
        if (!IsFinite(result.Extents) || IsDegenerate(result))
            result.Extents = glm::vec3(1.0f);
 
        // Normalize quaternion
        float qLen = glm::length(result.Rotation);
        if (qLen < 1e-6f)
            result.Rotation = glm::quat(1, 0, 0, 0);
        else
            result.Rotation = result.Rotation / qLen;
 
        return result;
    }
 
    inline Ray Sanitize(const Ray& r)
    {
        Ray result = r;
        if (!IsFinite(result.Origin)) result.Origin = glm::vec3(0.0f);
        if (!IsFinite(result.Direction) || IsZero(result.Direction))
            result.Direction = glm::vec3(0, 0, 1);
        else
            result.Direction = glm::normalize(result.Direction);
 
        return result;
    }
}