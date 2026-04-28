module;

#include <cmath>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

export module Geometry.Triangle;

import Geometry.Segment;

export namespace Geometry
{
    struct Triangle
    {
        glm::vec3 A{0.0f};
        glm::vec3 B{0.0f};
        glm::vec3 C{0.0f};

        [[nodiscard]] glm::vec3 GetCentroid() const
        {
            return (A + B + C) / 3.0f;
        }

        [[nodiscard]] glm::vec3 GetNormal() const
        {
            const glm::vec3 normal = glm::cross(B - A, C - A);
            const float lenSq = glm::dot(normal, normal);
            if (lenSq <= 1e-12f)
            {
                return glm::vec3(0.0f);
            }
            return normal * glm::inversesqrt(lenSq);
        }

        [[nodiscard]] float GetArea() const
        {
            return 0.5f * glm::length(glm::cross(B - A, C - A));
        }
    };

    [[nodiscard]] glm::vec3 ClosestPoint(const Triangle& triangle, const glm::vec3& point)
    {
        const glm::vec3 ab = triangle.B - triangle.A;
        const glm::vec3 ac = triangle.C - triangle.A;
        const glm::vec3 ap = point - triangle.A;
        const glm::vec3 normal = glm::cross(ab, ac);
        if (glm::dot(normal, normal) <= 1e-12f)
        {
            const glm::vec3 abPoint = ClosestPoint(Segment{triangle.A, triangle.B}, point);
            const glm::vec3 bcPoint = ClosestPoint(Segment{triangle.B, triangle.C}, point);
            const glm::vec3 caPoint = ClosestPoint(Segment{triangle.C, triangle.A}, point);
            const float abDistSq = glm::distance2(point, abPoint);
            const float bcDistSq = glm::distance2(point, bcPoint);
            const float caDistSq = glm::distance2(point, caPoint);
            if (abDistSq <= bcDistSq && abDistSq <= caDistSq) return abPoint;
            if (bcDistSq <= caDistSq) return bcPoint;
            return caPoint;
        }

        const float d1 = glm::dot(ab, ap);
        const float d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return triangle.A;

        const glm::vec3 bp = point - triangle.B;
        const float d3 = glm::dot(ab, bp);
        const float d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return triangle.B;

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / (d1 - d3);
            return triangle.A + ab * v;
        }

        const glm::vec3 cp = point - triangle.C;
        const float d5 = glm::dot(ab, cp);
        const float d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return triangle.C;

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / (d2 - d6);
            return triangle.A + ac * w;
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const glm::vec3 bc = triangle.C - triangle.B;
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return triangle.B + bc * w;
        }

        const float denom = 1.0f / (va + vb + vc);
        const float v = vb * denom;
        const float w = vc * denom;
        return triangle.A + ab * v + ac * w;
    }

    [[nodiscard]] double SquaredDistance(const Triangle& triangle, const glm::vec3& point)
    {
        const glm::vec3 delta = point - ClosestPoint(triangle, point);
        return static_cast<double>(glm::dot(delta, delta));
    }

    [[nodiscard]] double Distance(const Triangle& triangle, const glm::vec3& point)
    {
        return std::sqrt(SquaredDistance(triangle, point));
    }
}
