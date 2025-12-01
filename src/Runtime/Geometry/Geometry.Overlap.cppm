module;
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <span>

export module Runtime.Geometry.Overlap;

import Runtime.Geometry.Primitives;
import Runtime.Geometry.SDF;
import Runtime.Geometry.GJK;

export namespace Runtime::Geometry
{
    namespace Internal
    {
        // --- Separating Axis Theorem (SAT) Helpers ---

        // Test a specific axis for separation. Returns true if separated.
        bool SAT_TestAxis(const glm::vec3& axis, const std::span<const glm::vec3>& vertsA,
                          const std::span<const glm::vec3>& vertsB)
        {
            float minA = std::numeric_limits<float>::max(), maxA = std::numeric_limits<float>::lowest();
            float minB = std::numeric_limits<float>::max(), maxB = std::numeric_limits<float>::lowest();

            // Project A
            for (const auto& v : vertsA)
            {
                float p = glm::dot(v, axis);
                if (p < minA) minA = p;
                if (p > maxA) maxA = p;
            }

            // Project B
            for (const auto& v : vertsB)
            {
                float p = glm::dot(v, axis);
                if (p < minB) minB = p;
                if (p > maxB) maxB = p;
            }

            // Check for gap
            return (minA > maxB || minB > maxA);
        }

        // --- OBB vs OBB (SAT) ---
        bool Overlap_Analytic(const OBB& a, const OBB& b)
        {
            const float epsilon = 1e-6f;

            // 1. Compute rotation matrices
            glm::mat3 R_A = glm::toMat3(a.Rotation);
            glm::mat3 R_B = glm::toMat3(b.Rotation);

            // 2. Compute translation vector in A's frame
            glm::vec3 t = b.Center - a.Center;
            t = glm::vec3(glm::dot(t, R_A[0]), glm::dot(t, R_A[1]), glm::dot(t, R_A[2]));

            // 3. Compute relative rotation matrix R (B in A's frame) and AbsR
            // R[i][j] = dot(A_i, B_j)
            glm::mat3 R, AbsR;
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    R[i][j] = glm::dot(R_A[i], R_B[j]);
                    AbsR[i][j] = std::abs(R[i][j]) + epsilon;
                }
            }

            // 4. Test axes L = A0, A1, A2
            for (int i = 0; i < 3; i++)
            {
                float ra = a.Extents[i];
                float rb = b.Extents[0] * AbsR[i][0] + b.Extents[1] * AbsR[i][1] + b.Extents[2] * AbsR[i][2];
                if (std::abs(t[i]) > ra + rb) return false;
            }

            // 5. Test axes L = B0, B1, B2
            for (int i = 0; i < 3; i++)
            {
                float ra = a.Extents[0] * AbsR[0][i] + a.Extents[1] * AbsR[1][i] + a.Extents[2] * AbsR[2][i];
                float rb = b.Extents[i];
                float t_proj = t[0] * R[0][i] + t[1] * R[1][i] + t[2] * R[2][i];
                if (std::abs(t_proj) > ra + rb) return false;
            }

            // 6. Test 9 Cross products (Edges)

            // L = A0 x B0
            if (std::abs(t[2] * R[1][0] - t[1] * R[2][0]) >
                a.Extents[1] * AbsR[2][0] + a.Extents[2] * AbsR[1][0] +
                b.Extents[1] * AbsR[0][2] + b.Extents[2] * AbsR[0][1])
                return false;

            // L = A0 x B1
            if (std::abs(t[2] * R[1][1] - t[1] * R[2][1]) >
                a.Extents[1] * AbsR[2][1] + a.Extents[2] * AbsR[1][1] +
                b.Extents[0] * AbsR[0][2] + b.Extents[2] * AbsR[0][0])
                return false;

            // L = A0 x B2
            if (std::abs(t[2] * R[1][2] - t[1] * R[2][2]) >
                a.Extents[1] * AbsR[2][2] + a.Extents[2] * AbsR[1][2] +
                b.Extents[0] * AbsR[0][1] + b.Extents[1] * AbsR[0][0])
                return false;

            // L = A1 x B0
            if (std::abs(t[0] * R[2][0] - t[2] * R[0][0]) >
                a.Extents[0] * AbsR[2][0] + a.Extents[2] * AbsR[0][0] +
                b.Extents[1] * AbsR[1][2] + b.Extents[2] * AbsR[1][1])
                return false;

            // L = A1 x B1
            if (std::abs(t[0] * R[2][1] - t[2] * R[0][1]) >
                a.Extents[0] * AbsR[2][1] + a.Extents[2] * AbsR[0][1] +
                b.Extents[0] * AbsR[1][2] + b.Extents[2] * AbsR[1][0])
                return false;

            // L = A1 x B2
            if (std::abs(t[0] * R[2][2] - t[2] * R[0][2]) >
                a.Extents[0] * AbsR[2][2] + a.Extents[2] * AbsR[0][2] +
                b.Extents[0] * AbsR[1][1] + b.Extents[1] * AbsR[1][0])
                return false;

            // L = A2 x B0
            if (std::abs(t[1] * R[0][0] - t[0] * R[1][0]) >
                a.Extents[0] * AbsR[1][0] + a.Extents[1] * AbsR[0][0] +
                b.Extents[1] * AbsR[2][2] + b.Extents[2] * AbsR[2][1])
                return false;

            // L = A2 x B1
            if (std::abs(t[1] * R[0][1] - t[0] * R[1][1]) >
                a.Extents[0] * AbsR[1][1] + a.Extents[1] * AbsR[0][1] +
                b.Extents[0] * AbsR[2][2] + b.Extents[2] * AbsR[2][0])
                return false;

            // L = A2 x B2
            if (std::abs(t[1] * R[0][2] - t[0] * R[1][2]) >
                a.Extents[0] * AbsR[1][2] + a.Extents[1] * AbsR[0][2] +
                b.Extents[0] * AbsR[2][1] + b.Extents[1] * AbsR[2][0])
                return false;

            return true;
        }

        bool Overlap_Analytic(const OBB& a, const Sphere& b)
        {
            // 1. Transform World Sphere Center to OBB Local Space
            // Conjugate of a quaternion represents the inverse rotation (if normalized)
            glm::vec3 localSphereCenter = glm::conjugate(a.Rotation) * (b.Center - a.Center);

            // 2. Find closest point on the OBB (which aligns to axes in local space)
            // The OBB local extents define an AABB from -Extents to +Extents
            glm::vec3 closestPoint = glm::clamp(localSphereCenter, -a.Extents, a.Extents);

            // 3. Distance Check
            // If distance between sphere center and closest point on box < radius, they intersect
            float dist2 = glm::length2(localSphereCenter - closestPoint);
            return dist2 <= (b.Radius * b.Radius);
        }

        // --- Frustum vs AABB ---
        bool Overlap_Analytic(const Frustum& f, const AABB& box)
        {
            // If the box is completely BEHIND any single plane, it is culled.
            for (const auto& plane : f.Planes)
            {
                // Find the point on the AABB most likely to be "behind" the plane
                // based on the normal direction.
                // We want the point furthest in the direction of the normal.
                glm::vec3 positiveVertex = box.Min;
                if (plane.Normal.x >= 0) positiveVertex.x = box.Max.x;
                if (plane.Normal.y >= 0) positiveVertex.y = box.Max.y;
                if (plane.Normal.z >= 0) positiveVertex.z = box.Max.z;

                if (SDF::Math::Sdf_Plane(positiveVertex, plane.Normal, plane.Distance) < 0)
                {
                    // The "most inside" point is actually outside.
                    // Therefore the whole box is outside.
                    return false;
                }
            }
            return true;
        }

        // --- Frustum vs Sphere (Plane-Based) ---
        bool Overlap_Analytic(const Frustum& f, const Sphere& s)
        {
            for (const auto& plane : f.Planes)
            {
                if (SDF::Math::Sdf_Plane(s.Center, plane.Normal, plane.Distance) < -s.Radius)
                {
                    return false;
                }
            }
            return true;
        }

        // --- Ray vs AABB (Slab Method) ---
        bool Overlap_Analytic(const Ray& r, const AABB& b)
        {
            glm::vec3 invDir = 1.0f / r.Direction;
            glm::vec3 t0s = (b.Min - r.Origin) * invDir;
            glm::vec3 t1s = (b.Max - r.Origin) * invDir;

            glm::vec3 tsmaller = glm::min(t0s, t1s);
            glm::vec3 tbigger = glm::max(t0s, t1s);

            float tmin = glm::max(tsmaller.x, glm::max(tsmaller.y, tsmaller.z));
            float tmax = glm::min(tbigger.x, glm::min(tbigger.y, tbigger.z));

            return (tmax >= tmin && tmax >= 0.0f);
        }

        // --- Ray vs Sphere ---
        bool Overlap_Analytic(const Ray& r, const Sphere& s)
        {
            glm::vec3 m = r.Origin - s.Center;
            float b = glm::dot(m, r.Direction);
            float c = glm::dot(m, m) - s.Radius * s.Radius;

            // Exit if r’s origin outside s (c > 0) and r pointing away (b > 0)
            if (c > 0.0f && b > 0.0f) return false;

            float discr = b * b - c;
            // A negative discriminant corresponds to ray missing sphere
            if (discr < 0.0f) return false;

            return true;
        }

        bool Overlap_Analytic(const Sphere& a, const Sphere& b)
        {
            float distSq = glm::distance2(a.Center, b.Center);
            float radSum = a.Radius + b.Radius;
            return distSq <= (radSum * radSum);
        }

        bool Overlap_Analytic(const AABB& a, const AABB& b)
        {
            return (a.Min.x <= b.Max.x && a.Max.x >= b.Min.x) &&
                (a.Min.y <= b.Max.y && a.Max.y >= b.Min.y) &&
                (a.Min.z <= b.Max.z && a.Max.z >= b.Min.z);
        }

        bool Overlap_Analytic(const Sphere& s, const AABB& b)
        {
            glm::vec3 closest = glm::clamp(s.Center, b.Min, b.Max);
            return glm::distance2(closest, s.Center) <= (s.Radius * s.Radius);
        }

        bool Overlap_Analytic(const Sphere& s, const Capsule& c)
        {
            // 1. Find closest point on segment AB to sphere center
            glm::vec3 ab = c.PointB - c.PointA;
            float abLen2 = glm::length2(ab);

            // Degenerate capsule: treat as sphere
            if (abLen2 < 1e-6f) {
                return Overlap_Analytic(s, Sphere{c.PointA, c.Radius});
            }

            float t = glm::dot(s.Center - c.PointA, ab) / abLen2;
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 closestOnSegment = c.PointA + t * ab;

            // 2. Check distance
            float radSum = s.Radius + c.Radius;
            return glm::distance2(s.Center, closestOnSegment) <= (radSum * radSum);
        }

        template <typename A, typename B>
        bool Overlap_Fallback(const A& a, const B& b)
        {
            // Uses the Generic GJK solver imported from Runtime.Geometry.GJK
            return GJK_Boolean(a, b);
        }
    }

    // =========================================================================
    // OVERLAP (Boolean - Fast)
    // =========================================================================
    template <ConvexShape A, ConvexShape B>
    [[nodiscard]] bool TestOverlap(const A& a, const B& b)
    {
        // 1. Analytic (Fastest)
        if constexpr (requires { Internal::Overlap_Analytic(a, b); })
        {
            return Internal::Overlap_Analytic(a, b);
        }
        else if constexpr (requires { Internal::Overlap_Analytic(b, a); })
        {
            return Internal::Overlap_Analytic(b, a);
        }
        // 2. Generic (Fallback)
        else
        {
            return Internal::Overlap_Fallback(a, b);
        }
    }
}
