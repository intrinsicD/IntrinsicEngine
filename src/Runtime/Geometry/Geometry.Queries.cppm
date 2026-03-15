module;

#include <cmath>
#include <limits>
#include <algorithm>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry:Queries;

import :Ray;
import :Segment;

export namespace Geometry
{
    // =========================================================================
    // Ray-Segment Closest Points
    // =========================================================================
    // Computes the pair of closest points between a ray (semi-infinite) and a
    // line segment, returning the squared distance and the parameter/point on
    // each primitive.  Used by Runtime.Selection.cpp for graph-edge picking.

    struct RaySegmentResult
    {
        float DistanceSq = std::numeric_limits<float>::infinity();
        float RayT = 0.0f;       // Parameter on ray (>= 0)
        float SegmentT = 0.0f;   // Parameter on segment [0, 1]
        glm::vec3 PointOnRay{0.0f};
        glm::vec3 PointOnSegment{0.0f};
    };

    [[nodiscard]] inline RaySegmentResult ClosestRaySegment(const Ray& ray,
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

        // Helper: evaluate a candidate (s, t) and update best if closer.
        auto evaluate = [&](float s, float t) {
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

        // Degenerate segment (zero length): reduce to ray-point.
        if (a22 <= 1.0e-20f)
        {
            const float t = ClosestPointParameter(ray, segA);
            evaluate(t, 0.0f);
            return best;
        }

        // General case: solve the 2×2 linear system.
        if (det > 1.0e-20f)
        {
            float s = std::max((a12 * b2 - a22 * b1) / det, 0.0f);
            float t = std::clamp((a11 * b2 - a12 * b1) / det, 0.0f, 1.0f);
            evaluate(s, t);
        }

        // Test boundary candidates to handle clamping.
        // Segment endpoint A (t=0): closest point on ray to segA.
        {
            const float s = ClosestPointParameter(ray, segA);
            evaluate(s, 0.0f);
        }
        // Segment endpoint B (t=1): closest point on ray to segB.
        {
            const float s = ClosestPointParameter(ray, segB);
            evaluate(s, 1.0f);
        }
        // Ray origin (s=0): closest point on segment to ray origin.
        {
            const float t = ClosestPointParameter(Segment{segA, segB}, ray.Origin);
            evaluate(0.0f, t);
        }

        return best;
    }
}
