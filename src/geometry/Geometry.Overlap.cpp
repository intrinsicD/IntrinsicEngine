module;

#include <cmath>
#include <limits>
#include <span>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

module Geometry.Overlap;

import Geometry.RobustPredicates;

namespace Geometry
{
    namespace Internal
    {
        bool SAT_TestAxis(const glm::vec3& axis, const std::span<const glm::vec3>& vertsA,
                          const std::span<const glm::vec3>& vertsB)
        {
            float minA = std::numeric_limits<float>::max(), maxA = std::numeric_limits<float>::lowest();
            float minB = std::numeric_limits<float>::max(), maxB = std::numeric_limits<float>::lowest();

            for (const auto& v : vertsA)
            {
                float p = glm::dot(v, axis);
                if (p < minA) minA = p;
                if (p > maxA) maxA = p;
            }

            for (const auto& v : vertsB)
            {
                float p = glm::dot(v, axis);
                if (p < minB) minB = p;
                if (p > maxB) maxB = p;
            }

            return (minA > maxB || minB > maxA);
        }

        bool Overlap_Analytic(const OBB& a, const OBB& b)
        {
            const float epsilon = 1e-6f;

            glm::mat3 R_A = glm::toMat3(a.Rotation);
            glm::mat3 R_B = glm::toMat3(b.Rotation);

            glm::vec3 t = b.Center - a.Center;
            t = glm::vec3(glm::dot(t, R_A[0]), glm::dot(t, R_A[1]), glm::dot(t, R_A[2]));

            glm::mat3 R, AbsR;
            for (int i = 0; i < 3; i++)
            {
                for (int j = 0; j < 3; j++)
                {
                    R[i][j] = glm::dot(R_A[i], R_B[j]);
                    AbsR[i][j] = std::abs(R[i][j]) + epsilon;
                }
            }

            for (int i = 0; i < 3; i++)
            {
                float ra = a.Extents[i];
                float rb = b.Extents[0] * AbsR[i][0] + b.Extents[1] * AbsR[i][1] + b.Extents[2] * AbsR[i][2];
                if (std::abs(t[i]) > ra + rb) return false;
            }

            for (int i = 0; i < 3; i++)
            {
                float ra = a.Extents[0] * AbsR[0][i] + a.Extents[1] * AbsR[1][i] + a.Extents[2] * AbsR[2][i];
                float rb = b.Extents[i];
                float t_proj = t[0] * R[0][i] + t[1] * R[1][i] + t[2] * R[2][i];
                if (std::abs(t_proj) > ra + rb) return false;
            }

            if (std::abs(t[2] * R[1][0] - t[1] * R[2][0]) >
                a.Extents[1] * AbsR[2][0] + a.Extents[2] * AbsR[1][0] +
                b.Extents[1] * AbsR[0][2] + b.Extents[2] * AbsR[0][1])
                return false;

            if (std::abs(t[2] * R[1][1] - t[1] * R[2][1]) >
                a.Extents[1] * AbsR[2][1] + a.Extents[2] * AbsR[1][1] +
                b.Extents[0] * AbsR[0][2] + b.Extents[2] * AbsR[0][0])
                return false;

            if (std::abs(t[2] * R[1][2] - t[1] * R[2][2]) >
                a.Extents[1] * AbsR[2][2] + a.Extents[2] * AbsR[1][2] +
                b.Extents[0] * AbsR[0][1] + b.Extents[1] * AbsR[0][0])
                return false;

            if (std::abs(t[0] * R[2][0] - t[2] * R[0][0]) >
                a.Extents[0] * AbsR[2][0] + a.Extents[2] * AbsR[0][0] +
                b.Extents[1] * AbsR[1][2] + b.Extents[2] * AbsR[1][1])
                return false;

            if (std::abs(t[0] * R[2][1] - t[2] * R[0][1]) >
                a.Extents[0] * AbsR[2][1] + a.Extents[2] * AbsR[0][1] +
                b.Extents[0] * AbsR[1][2] + b.Extents[2] * AbsR[1][0])
                return false;

            if (std::abs(t[0] * R[2][2] - t[2] * R[0][2]) >
                a.Extents[0] * AbsR[2][2] + a.Extents[2] * AbsR[0][2] +
                b.Extents[0] * AbsR[1][1] + b.Extents[1] * AbsR[1][0])
                return false;

            if (std::abs(t[1] * R[0][0] - t[0] * R[1][0]) >
                a.Extents[0] * AbsR[1][0] + a.Extents[1] * AbsR[0][0] +
                b.Extents[1] * AbsR[2][2] + b.Extents[2] * AbsR[2][1])
                return false;

            if (std::abs(t[1] * R[0][1] - t[0] * R[1][1]) >
                a.Extents[0] * AbsR[1][1] + a.Extents[1] * AbsR[0][1] +
                b.Extents[0] * AbsR[2][2] + b.Extents[2] * AbsR[2][0])
                return false;

            if (std::abs(t[1] * R[0][2] - t[0] * R[1][2]) >
                a.Extents[0] * AbsR[1][2] + a.Extents[1] * AbsR[0][2] +
                b.Extents[0] * AbsR[2][1] + b.Extents[1] * AbsR[2][0])
                return false;

            return true;
        }

        bool Overlap_Analytic(const OBB& a, const Sphere& b)
        {
            glm::vec3 localSphereCenter = glm::conjugate(a.Rotation) * (b.Center - a.Center);
            glm::vec3 closestPoint = glm::clamp(localSphereCenter, -a.Extents, a.Extents);
            float dist2 = glm::length2(localSphereCenter - closestPoint);
            return dist2 <= (b.Radius * b.Radius);
        }

        bool Overlap_Analytic(const Frustum& f, const AABB& box)
        {
            namespace RP = Geometry::RobustPredicates;
            for (const auto& plane : f.Planes)
            {
                glm::vec3 maxPoint;
                if (plane.Normal.x > 0) maxPoint.x = box.Max.x; else maxPoint.x = box.Min.x;
                if (plane.Normal.y > 0) maxPoint.y = box.Max.y; else maxPoint.y = box.Min.y;
                if (plane.Normal.z > 0) maxPoint.z = box.Max.z; else maxPoint.z = box.Min.z;

                const auto signed_ = RP::SignedDistanceToHessianPlane(plane.Normal, plane.Distance, maxPoint);
                if (signed_.Sign == RP::Sign::Negative && signed_.Certainty == RP::Certainty::Certain)
                {
                    return false;
                }
            }
            return true;
        }

        bool Overlap_Analytic(const Frustum& f, const Sphere& s)
        {
            namespace RP = Geometry::RobustPredicates;
            for (const auto& plane : f.Planes)
            {
                const auto signed_ = RP::SignedDistanceToHessianPlane(plane.Normal, plane.Distance, s.Center);
                const double radius = static_cast<double>(s.Radius);
                if (signed_.Value < -radius - signed_.FilterBound)
                {
                    return false;
                }
            }
            return true;
        }

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

        bool Overlap_Analytic(const Ray& r, const Sphere& s)
        {
            glm::vec3 m = r.Origin - s.Center;
            float b = glm::dot(m, r.Direction);
            float c = glm::dot(m, m) - s.Radius * s.Radius;

            if (c > 0.0f && b > 0.0f) return false;

            float discr = b * b - c;
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
            glm::vec3 ab = c.PointB - c.PointA;
            float abLen2 = glm::length2(ab);

            if (abLen2 < 1e-6f)
            {
                return Overlap_Analytic(s, Sphere{c.PointA, c.Radius});
            }

            float t = glm::dot(s.Center - c.PointA, ab) / abLen2;
            t = glm::clamp(t, 0.0f, 1.0f);
            glm::vec3 closestOnSegment = c.PointA + t * ab;

            float radSum = s.Radius + c.Radius;
            return glm::distance2(s.Center, closestOnSegment) <= (radSum * radSum);
        }
    }
}
