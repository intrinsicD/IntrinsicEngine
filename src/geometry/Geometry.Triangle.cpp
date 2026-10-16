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

import Geometry.Validation;
import Geometry.Segment;

namespace Geometry
{
    namespace
    {
        constexpr float kTriangleDegenerateEpsilon = 1.0e-7f;

        using Geometry::Validation::IsFinite;

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

        [[nodiscard]] glm::dvec3 AreaNormal(const Triangle& triangle)
        {
            if (!IsFinite(triangle)) return glm::dvec3(0.0);
            const glm::dvec3 ab = glm::dvec3(triangle.B) - glm::dvec3(triangle.A);
            const glm::dvec3 ac = glm::dvec3(triangle.C) - glm::dvec3(triangle.A);
            const glm::dvec3 bc = glm::dvec3(triangle.C) - glm::dvec3(triangle.B);
            const glm::dvec3 normal = glm::cross(ab, ac);
            const double scaleSquared = std::max({glm::dot(ab, ab), glm::dot(ac, ac), glm::dot(bc, bc)});
            const double epsilon = kTriangleDegenerateEpsilon;
            return glm::dot(normal, normal) > epsilon * epsilon * scaleSquared * scaleSquared
                ? normal : glm::dvec3(0.0);
        }
    }

    glm::vec3 Triangle::GetCentroid() const
    {
        return (A + B + C) / 3.0f;
    }

    glm::vec3 Triangle::GetNormal() const
    {
        const glm::dvec3 normal = AreaNormal(*this);
        const double lenSq = glm::dot(normal, normal);
        return lenSq > 0.0 ? glm::vec3(normal / std::sqrt(lenSq)) : glm::vec3(0.0f);
    }

    float Triangle::GetArea() const
    {
        return 0.5f * glm::length(glm::cross(B - A, C - A));
    }

    glm::vec3 Triangle::EdgeLengths() const
    {
        const glm::vec3 edges = RawOppositeEdgeLengths(*this);
        if (AreaNormal(*this) == glm::dvec3(0.0))
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
        if (AreaNormal(*this) == glm::dvec3(0.0))
        {
            return glm::vec3{0.0f};
        }

        const double a = static_cast<double>(edges.x);
        const double b = static_cast<double>(edges.y);
        const double c = static_cast<double>(edges.z);
        const double angleA = static_cast<double>(
            SafeAcos(static_cast<float>((b * b + c * c - a * a) / (2.0 * b * c))));
        const double angleB = static_cast<double>(
            SafeAcos(static_cast<float>((a * a + c * c - b * b) / (2.0 * a * c))));
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
        const glm::dvec3 a(triangle.A);
        const glm::dvec3 ab = glm::dvec3(triangle.B) - a;
        const glm::dvec3 ac = glm::dvec3(triangle.C) - a;
        const glm::dvec3 ap = glm::dvec3(point) - a;
        if (AreaNormal(triangle) == glm::dvec3(0.0))
        {
            const glm::vec3 abPoint = ClosestPoint(Segment{triangle.A, triangle.B}, point);
            const glm::vec3 bcPoint = ClosestPoint(Segment{triangle.B, triangle.C}, point);
            const glm::vec3 caPoint = ClosestPoint(Segment{triangle.C, triangle.A}, point);
            const double abDistSq = glm::distance2(glm::dvec3(point), glm::dvec3(abPoint));
            const double bcDistSq = glm::distance2(glm::dvec3(point), glm::dvec3(bcPoint));
            const double caDistSq = glm::distance2(glm::dvec3(point), glm::dvec3(caPoint));
            if (abDistSq <= bcDistSq && abDistSq <= caDistSq) return abPoint;
            if (bcDistSq <= caDistSq) return bcPoint;
            return caPoint;
        }

        const double d1 = glm::dot(ab, ap);
        const double d2 = glm::dot(ac, ap);
        if (d1 <= 0.0f && d2 <= 0.0f) return triangle.A;

        const glm::dvec3 bp = glm::dvec3(point) - glm::dvec3(triangle.B);
        const double d3 = glm::dot(ab, bp);
        const double d4 = glm::dot(ac, bp);
        if (d3 >= 0.0f && d4 <= d3) return triangle.B;

        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const double v = d1 / (d1 - d3);
            return glm::vec3(a + ab * v);
        }

        const glm::dvec3 cp = glm::dvec3(point) - glm::dvec3(triangle.C);
        const double d5 = glm::dot(ab, cp);
        const double d6 = glm::dot(ac, cp);
        if (d6 >= 0.0f && d5 <= d6) return triangle.C;

        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const double w = d2 / (d2 - d6);
            return glm::vec3(a + ac * w);
        }

        const double va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const glm::dvec3 bc = glm::dvec3(triangle.C) - glm::dvec3(triangle.B);
            const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return glm::vec3(glm::dvec3(triangle.B) + bc * w);
        }

        const double denom = 1.0f / (va + vb + vc);
        const double v = vb * denom;
        const double w = vc * denom;
        return glm::vec3(a + ab * v + ac * w);
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

    double AreaFromMetric(double a, double b, double c)
    {
        //Numerically stable herons formula for area of triangle with side lengths a, b and c.
        if (a < b) std::swap(a, b);
        if (a < c) std::swap(a, c);
        if (b < c) std::swap(b, c);
        assert(a >= b);
        assert(b >= c);

        double p = std::sqrt(std::abs((a + (b + c)) * (c - (a - b)) * (c + (a - b)) * (a + (b - c)))) / 4;
        assert(!std::isnan(p));
        return p;
    }

    double AreaFromMetric(const glm::vec3& edgeLengths)
    {
        return AreaFromMetric(edgeLengths.x, edgeLengths.y, edgeLengths.z);
    }

    [[nodiscard]] double AreaFromMetric(const Triangle& triangle)
    {
        return AreaFromMetric(triangle.EdgeLengths());
    }
}
