module;

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.Queries;

namespace Geometry
{
    RaySegmentResult ClosestRaySegment(const Ray& ray,
                                       const glm::vec3& segA,
                                       const glm::vec3& segB)
    {
        const glm::vec3 d1 = ray.Direction;
        const glm::vec3 d2 = segB - segA;
        const glm::vec3 r = ray.Origin - segA;

        const float a11 = glm::dot(d1, d1);
        const float a12 = glm::dot(d1, d2);
        const float a22 = glm::dot(d2, d2);
        const float b1 = glm::dot(d1, r);
        const float b2 = glm::dot(d2, r);
        const float det = a11 * a22 - a12 * a12;

        RaySegmentResult best{};

        const auto evaluate = [&](float s, float t) {
            const glm::vec3 pRay = ray.Origin + s * d1;
            const glm::vec3 pSeg = segA + t * d2;
            const float distSq = glm::length2(pRay - pSeg);
            if (distSq < best.DistanceSq)
            {
                best.DistanceSq = distSq;
                best.RayT = s;
                best.SegmentT = t;
                best.PointOnRay = pRay;
                best.PointOnSegment = pSeg;
            }
        };

        if (a22 <= 1.0e-20f)
        {
            const float t = ClosestPointParameter(ray, segA);
            evaluate(t, 0.0f);
            return best;
        }

        if (det > 1.0e-20f)
        {
            float s = std::max((a12 * b2 - a22 * b1) / det, 0.0f);
            float t = std::clamp((a11 * b2 - a12 * b1) / det, 0.0f, 1.0f);
            evaluate(s, t);
        }

        {
            const float s = ClosestPointParameter(ray, segA);
            evaluate(s, 0.0f);
        }
        {
            const float s = ClosestPointParameter(ray, segB);
            evaluate(s, 1.0f);
        }
        {
            const float t = ClosestPointParameter(Segment{segA, segB}, ray.Origin);
            evaluate(0.0f, t);
        }

        return best;
    }

    PointSegmentResult ClosestPointSegment(
        const glm::vec3& point, const glm::vec3& segA, const glm::vec3& segB)
    {
        const float t = ClosestPointParameter(Segment{segA, segB}, point);
        const glm::vec3 closest = segA + t * (segB - segA);
        const glm::vec3 delta = point - closest;
        return {glm::dot(delta, delta), t, closest};
    }

    PointRayResult ClosestPointRay(const glm::vec3& point, const Ray& ray)
    {
        const float t = ClosestPointParameter(ray, point);
        const glm::vec3 closest = ray.GetPoint(t);
        const glm::vec3 delta = point - closest;
        return {glm::dot(delta, delta), t, closest};
    }
}
