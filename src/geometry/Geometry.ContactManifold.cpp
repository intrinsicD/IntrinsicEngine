module;

#include <cmath>
#include <optional>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.ContactManifold;

namespace Geometry::Internal
{
    std::optional<ContactManifold> Contact_Analytic(const Sphere& a, const Sphere& b)
    {
        glm::vec3 diff = b.Center - a.Center;
        float dist2 = glm::length2(diff);
        float radSum = a.Radius + b.Radius;

        if (dist2 > radSum * radSum) return std::nullopt;

        float dist = std::sqrt(dist2);
        ContactManifold m{};

        if (dist < 1e-6f)
        {
            m.Normal = glm::vec3(0, 1, 0);
            m.PenetrationDepth = radSum;
        }
        else
        {
            m.Normal = diff / dist;
            m.PenetrationDepth = radSum - dist;
        }

        m.ContactPointA = a.Center + (m.Normal * a.Radius);
        m.ContactPointB = b.Center - (m.Normal * b.Radius);
        return m;
    }

    std::optional<ContactManifold> Contact_Analytic(const Sphere& s, const AABB& b)
    {
        glm::vec3 closest = glm::clamp(s.Center, b.Min, b.Max);
        // A->B convention (BUG-025): the sphere is A, so the normal points
        // from the sphere center toward the box.
        glm::vec3 diff = closest - s.Center;
        float dist2 = glm::length2(diff);

        if (dist2 > s.Radius * s.Radius) return std::nullopt;

        float dist = std::sqrt(dist2);
        ContactManifold m{};

        if (dist < 1e-6f)
        {
            // Sphere center inside the box: escape along the cheapest axis.
            // The A->B normal opposes the sphere's escape direction.
            glm::vec3 center = (b.Min + b.Max) * 0.5f;
            glm::vec3 halfExt = (b.Max - b.Min) * 0.5f;
            glm::vec3 d = s.Center - center;
            glm::vec3 overlap = halfExt - glm::abs(d);

            float minOverlap;
            if (overlap.x < overlap.y && overlap.x < overlap.z)
            {
                m.Normal = glm::vec3(d.x > 0 ? -1 : 1, 0, 0);
                minOverlap = overlap.x;
            }
            else if (overlap.y < overlap.z)
            {
                m.Normal = glm::vec3(0, d.y > 0 ? -1 : 1, 0);
                minOverlap = overlap.y;
            }
            else
            {
                m.Normal = glm::vec3(0, 0, d.z > 0 ? -1 : 1);
                minOverlap = overlap.z;
            }

            m.PenetrationDepth = minOverlap + s.Radius;
            m.ContactPointB = s.Center - m.Normal * minOverlap;
            m.ContactPointA = s.Center + m.Normal * s.Radius;
        }
        else
        {
            m.Normal = diff / dist;
            m.PenetrationDepth = s.Radius - dist;
            m.ContactPointB = closest;
            m.ContactPointA = s.Center + m.Normal * s.Radius;
        }

        return m;
    }

    std::optional<RayHit> RayCast_Analytic(const Ray& r, const Sphere& s)
    {
        glm::vec3 m = r.Origin - s.Center;
        float b = glm::dot(m, r.Direction);
        float c = glm::dot(m, m) - s.Radius * s.Radius;

        if (c > 0.0f && b > 0.0f) return std::nullopt;

        float discr = b * b - c;
        if (discr < 0.0f) return std::nullopt;

        float t = -b - std::sqrt(discr);
        if (t < 0.0f) t = 0.0f;

        RayHit hit{};
        hit.Distance = t;
        hit.Point = r.Origin + r.Direction * t;
        hit.Normal = glm::normalize(hit.Point - s.Center);
        return hit;
    }

    std::optional<RayHit> RayCast_Analytic(const Ray& r, const AABB& box)
    {
        glm::vec3 invDir = 1.0f / r.Direction;
        glm::vec3 t0s = (box.Min - r.Origin) * invDir;
        glm::vec3 t1s = (box.Max - r.Origin) * invDir;

        glm::vec3 tsmaller = glm::min(t0s, t1s);
        glm::vec3 tbigger = glm::max(t0s, t1s);

        float tmin = glm::max(tsmaller.x, glm::max(tsmaller.y, tsmaller.z));
        float tmax = glm::min(tbigger.x, glm::min(tbigger.y, tbigger.z));

        if (tmax < tmin || tmax < 0.0f) return std::nullopt;

        RayHit hit{};
        hit.Distance = tmin > 0 ? tmin : tmax;
        hit.Point = r.Origin + r.Direction * hit.Distance;

        const float epsilon = 1e-4f;
        if (std::abs(hit.Point.x - box.Min.x) < epsilon) hit.Normal = {-1, 0, 0};
        else if (std::abs(hit.Point.x - box.Max.x) < epsilon) hit.Normal = {1, 0, 0};
        else if (std::abs(hit.Point.y - box.Min.y) < epsilon) hit.Normal = {0, -1, 0};
        else if (std::abs(hit.Point.y - box.Max.y) < epsilon) hit.Normal = {0, 1, 0};
        else if (std::abs(hit.Point.z - box.Min.z) < epsilon) hit.Normal = {0, 0, -1};
        else hit.Normal = {0, 0, 1};

        return hit;
    }
}
