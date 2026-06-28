module;

#include <cmath>
#include <algorithm>
#include <array>
#include <functional>
#include <numbers>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

module Geometry.Triangle;

import Geometry.Segment;

namespace Geometry
{
    namespace
    {
        constexpr float kTriangleDegenerateEpsilon = 1.0e-7f;

        [[nodiscard]] bool IsFinite(const glm::vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        [[nodiscard]] bool IsFinite(const Triangle& triangle)
        {
            return IsFinite(triangle.A) && IsFinite(triangle.B) && IsFinite(triangle.C);
        }

        [[nodiscard]] glm::vec3 RawOppositeEdgeLengths(const Triangle& triangle)
        {
            return {
                glm::length(triangle.C - triangle.B),
                glm::length(triangle.C - triangle.A),
                glm::length(triangle.B - triangle.A),
            };
        }

        [[nodiscard]] float StableAreaFromEdges(glm::vec3 edges)
        {
            std::array<double, 3> sorted{
                static_cast<double>(edges.x),
                static_cast<double>(edges.y),
                static_cast<double>(edges.z),
            };
            std::sort(sorted.begin(), sorted.end(), std::greater<>{});

            const double a = sorted[0];
            const double b = sorted[1];
            const double c = sorted[2];
            if (!(c > 0.0) || a >= b + c)
            {
                return 0.0f;
            }

            const double product = (a + (b + c)) * (c - (a - b)) * (c + (a - b)) * (a + (b - c));
            if (!(product > 0.0) || !std::isfinite(product))
            {
                return 0.0f;
            }

            return static_cast<float>(0.25 * std::sqrt(product));
        }

        [[nodiscard]] bool HasNonDegenerateArea(const Triangle& triangle, const glm::vec3& edges)
        {
            if (!IsFinite(triangle))
            {
                return false;
            }
            if (!(edges.x > kTriangleDegenerateEpsilon) ||
                !(edges.y > kTriangleDegenerateEpsilon) ||
                !(edges.z > kTriangleDegenerateEpsilon))
            {
                return false;
            }
            return StableAreaFromEdges(edges) > kTriangleDegenerateEpsilon;
        }
    }

    glm::vec3 Triangle::GetCentroid() const
    {
        return (A + B + C) / 3.0f;
    }

    glm::vec3 Triangle::GetNormal() const
    {
        const glm::vec3 normal = glm::cross(B - A, C - A);
        const float lenSq = glm::dot(normal, normal);
        if (lenSq <= 1e-12f)
        {
            return glm::vec3(0.0f);
        }
        return normal * glm::inversesqrt(lenSq);
    }

    float Triangle::GetArea() const
    {
        return 0.5f * glm::length(glm::cross(B - A, C - A));
    }

    glm::vec3 Triangle::EdgeLengths() const
    {
        const glm::vec3 edges = RawOppositeEdgeLengths(*this);
        if (!HasNonDegenerateArea(*this, edges))
        {
            return glm::vec3{0.0f};
        }
        return edges;
    }

    float Triangle::Perimeter() const
    {
        const glm::vec3 edges = EdgeLengths();
        return edges.x + edges.y + edges.z;
    }

    glm::vec3 Triangle::Angles() const
    {
        const glm::vec3 edges = RawOppositeEdgeLengths(*this);
        if (!HasNonDegenerateArea(*this, edges))
        {
            return glm::vec3{0.0f};
        }

        const double a = static_cast<double>(edges.x);
        const double b = static_cast<double>(edges.y);
        const double c = static_cast<double>(edges.z);
        const double angleA = static_cast<double>(SafeAcos(static_cast<float>((b * b + c * c - a * a) / (2.0 * b * c))));
        const double angleB = static_cast<double>(SafeAcos(static_cast<float>((a * a + c * c - b * b) / (2.0 * a * c))));
        const double angleC = std::max(0.0, static_cast<double>(std::numbers::pi_v<float>) - angleA - angleB);
        return glm::vec3{
            static_cast<float>(angleA),
            static_cast<float>(angleB),
            static_cast<float>(angleC),
        };
    }

    float Triangle::StableArea() const
    {
        if (!IsFinite(*this))
        {
            return 0.0f;
        }
        return StableAreaFromEdges(RawOppositeEdgeLengths(*this));
    }

    float SafeAcos(float x)
    {
        if (!std::isfinite(x))
        {
            return 0.0f;
        }
        return std::acos(std::clamp(x, -1.0f, 1.0f));
    }

    glm::vec3 ClosestPoint(const Triangle& triangle, const glm::vec3& point)
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

    double SquaredDistance(const Triangle& triangle, const glm::vec3& point)
    {
        const glm::vec3 delta = point - ClosestPoint(triangle, point);
        return static_cast<double>(glm::dot(delta, delta));
    }

    double Distance(const Triangle& triangle, const glm::vec3& point)
    {
        return std::sqrt(SquaredDistance(triangle, point));
    }
}
