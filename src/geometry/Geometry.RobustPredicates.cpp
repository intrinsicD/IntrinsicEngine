module;

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

module Geometry.RobustPredicates;

namespace Geometry::RobustPredicates
{
[[nodiscard]] bool ApproxEqual(double a, double b, double scale,
                                          double relative) noexcept
    {
        const double diff = a - b;
        const double eps = ScaledEpsilon(scale, relative);
        return (diff < 0.0 ? -diff : diff) <= eps;
    }

[[nodiscard]] bool ApproxEqual(const glm::vec3& a, const glm::vec3& b,
                                          double scale,
                                          double relative) noexcept
    {
        const double eps = ScaledEpsilon(scale, relative);
        const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
        const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
        const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
        return (dx * dx + dy * dy + dz * dz) <= eps * eps;
    }

[[nodiscard]] SignedResult ApproxZeroSqDiagnostic(double valueSq,
                                                             double scale,
                                                             double relative) noexcept
    {
        const double eps = ScaledEpsilon(scale, relative);
        const double bound = eps * eps;
        const double v = valueSq < 0.0 ? 0.0 : valueSq; // squared magnitudes are non-negative by contract
        SignedResult result{};
        result.Value = v;
        result.FilterBound = bound;
        if (v == 0.0)
        {
            result.Sign = Sign::Zero;
            result.Certainty = Certainty::Certain;
        }
        else if (v > bound)
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Certain;
        }
        else
        {
            // Inside the certain-zero band; non-zero in floating-point but
            // not reliably distinguishable from zero at this scale.
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Uncertain;
        }
        return result;
    }

[[nodiscard]] SignedResult ApproxZeroLenDiagnostic(double length,
                                                              double scale,
                                                              double relative) noexcept
    {
        const double eps = ScaledEpsilon(scale, relative);
        const double v = length < 0.0 ? -length : length;
        SignedResult result{};
        result.Value = v;
        result.FilterBound = eps;
        if (v == 0.0)
        {
            result.Sign = Sign::Zero;
            result.Certainty = Certainty::Certain;
        }
        else if (v > eps)
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Certain;
        }
        else
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Uncertain;
        }
        return result;
    }

[[nodiscard]] bool ApproxZeroSq(double valueSq, double scale,
                                           double relative) noexcept
    {
        const double eps = ScaledEpsilon(scale, relative);
        const double v = valueSq < 0.0 ? 0.0 : valueSq;
        return v <= eps * eps;
    }

[[nodiscard]] bool ApproxZeroLen(double length, double scale,
                                            double relative) noexcept
    {
        const double eps = ScaledEpsilon(scale, relative);
        const double v = length < 0.0 ? -length : length;
        return v <= eps;
    }

[[nodiscard]] SignedResult Orientation2D(const glm::vec2& a,
                                                    const glm::vec2& b,
                                                    const glm::vec2& c) noexcept
    {
        const double ax = static_cast<double>(a.x);
        const double ay = static_cast<double>(a.y);
        const double bx = static_cast<double>(b.x);
        const double by = static_cast<double>(b.y);
        const double cx = static_cast<double>(c.x);
        const double cy = static_cast<double>(c.y);

        const double detLeft = (ax - cx) * (by - cy);
        const double detRight = (ay - cy) * (bx - cx);
        const double det = detLeft - detRight;

        // Shewchuk-style static filter: bound the round-off as a function of
        // the magnitudes of the contributing products.
        const double absLeft = detLeft < 0.0 ? -detLeft : detLeft;
        const double absRight = detRight < 0.0 ? -detRight : detRight;
        const double filter = (absLeft + absRight) * kOrientationFilter2D;

        SignedResult result{};
        result.Value = det;
        result.FilterBound = filter;
        if (det == 0.0)
        {
            // Exactly representable zero is decidable regardless of filter.
            result.Sign = Sign::Zero;
            result.Certainty = Certainty::Certain;
        }
        else if (det > filter)
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Certain;
        }
        else if (det < -filter)
        {
            result.Sign = Sign::Negative;
            result.Certainty = Certainty::Certain;
        }
        else
        {
            // Inside the filter band: cannot decide non-zero sign reliably.
            result.Sign = (det > 0.0) ? Sign::Positive : Sign::Negative;
            result.Certainty = Certainty::Uncertain;
        }
        return result;
    }

[[nodiscard]] SignedResult Orientation3D(const glm::vec3& a,
                                                    const glm::vec3& b,
                                                    const glm::vec3& c,
                                                    const glm::vec3& d) noexcept
    {
        const double bax = static_cast<double>(b.x) - static_cast<double>(a.x);
        const double bay = static_cast<double>(b.y) - static_cast<double>(a.y);
        const double baz = static_cast<double>(b.z) - static_cast<double>(a.z);

        const double cax = static_cast<double>(c.x) - static_cast<double>(a.x);
        const double cay = static_cast<double>(c.y) - static_cast<double>(a.y);
        const double caz = static_cast<double>(c.z) - static_cast<double>(a.z);

        const double dax = static_cast<double>(d.x) - static_cast<double>(a.x);
        const double day = static_cast<double>(d.y) - static_cast<double>(a.y);
        const double daz = static_cast<double>(d.z) - static_cast<double>(a.z);

        // (b-a) × (c-a) cofactors.
        const double nxPos = bay * caz;
        const double nxNeg = baz * cay;
        const double nyPos = baz * cax;
        const double nyNeg = bax * caz;
        const double nzPos = bax * cay;
        const double nzNeg = bay * cax;

        const double nx = nxPos - nxNeg;
        const double ny = nyPos - nyNeg;
        const double nz = nzPos - nzNeg;

        const double det = nx * dax + ny * day + nz * daz;

        const double permanent =
              (std::fabs(nxPos) + std::fabs(nxNeg)) * std::fabs(dax)
            + (std::fabs(nyPos) + std::fabs(nyNeg)) * std::fabs(day)
            + (std::fabs(nzPos) + std::fabs(nzNeg)) * std::fabs(daz);
        const double filter = permanent * kOrientationFilter3D;

        SignedResult result{};
        result.Value = det;
        result.FilterBound = filter;
        if (det == 0.0)
        {
            result.Sign = Sign::Zero;
            result.Certainty = Certainty::Certain;
        }
        else if (det > filter)
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Certain;
        }
        else if (det < -filter)
        {
            result.Sign = Sign::Negative;
            result.Certainty = Certainty::Certain;
        }
        else
        {
            result.Sign = (det > 0.0) ? Sign::Positive : Sign::Negative;
            result.Certainty = Certainty::Uncertain;
        }
        return result;
    }

[[nodiscard]] SignedResult SignedDistanceToPlane(const glm::vec3& planeOrigin,
                                                            const glm::vec3& planeNormal,
                                                            const glm::vec3& query) noexcept
    {
        const double dx = static_cast<double>(query.x) - static_cast<double>(planeOrigin.x);
        const double dy = static_cast<double>(query.y) - static_cast<double>(planeOrigin.y);
        const double dz = static_cast<double>(query.z) - static_cast<double>(planeOrigin.z);

        const double nx = static_cast<double>(planeNormal.x);
        const double ny = static_cast<double>(planeNormal.y);
        const double nz = static_cast<double>(planeNormal.z);

        const double dot = dx * nx + dy * ny + dz * nz;

        const double permanent =
              std::fabs(dx) * std::fabs(nx)
            + std::fabs(dy) * std::fabs(ny)
            + std::fabs(dz) * std::fabs(nz);
        // Three multiplies + two adds; bound at ~4 eps of the permanent.
        const double filter = permanent * (4.0 * kDoubleEpsilon);

        SignedResult result{};
        result.Value = dot;
        result.FilterBound = filter;
        if (dot == 0.0)
        {
            result.Sign = Sign::Zero;
            result.Certainty = Certainty::Certain;
        }
        else if (dot > filter)
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Certain;
        }
        else if (dot < -filter)
        {
            result.Sign = Sign::Negative;
            result.Certainty = Certainty::Certain;
        }
        else
        {
            result.Sign = (dot > 0.0) ? Sign::Positive : Sign::Negative;
            result.Certainty = Certainty::Uncertain;
        }
        return result;
    }

[[nodiscard]] SignedResult SignedDistanceToHessianPlane(
        const glm::vec3& planeNormal,
        float planeOffset,
        const glm::vec3& query) noexcept
    {
        const double nx = static_cast<double>(planeNormal.x);
        const double ny = static_cast<double>(planeNormal.y);
        const double nz = static_cast<double>(planeNormal.z);

        const double qx = static_cast<double>(query.x);
        const double qy = static_cast<double>(query.y);
        const double qz = static_cast<double>(query.z);

        const double d = static_cast<double>(planeOffset);

        const double tx = qx * nx;
        const double ty = qy * ny;
        const double tz = qz * nz;
        const double value = tx + ty + tz + d;

        // Three multiplies plus three adds; bound at ~5 eps of the permanent
        // (matching the slack policy used by `SignedDistanceToPlane`, with
        // one extra term accounting for the constant `d` add).
        const double permanent =
              std::fabs(tx)
            + std::fabs(ty)
            + std::fabs(tz)
            + std::fabs(d);
        const double filter = permanent * (5.0 * kDoubleEpsilon);

        SignedResult result{};
        result.Value = value;
        result.FilterBound = filter;
        if (value == 0.0)
        {
            result.Sign = Sign::Zero;
            result.Certainty = Certainty::Certain;
        }
        else if (value > filter)
        {
            result.Sign = Sign::Positive;
            result.Certainty = Certainty::Certain;
        }
        else if (value < -filter)
        {
            result.Sign = Sign::Negative;
            result.Certainty = Certainty::Certain;
        }
        else
        {
            result.Sign = (value > 0.0) ? Sign::Positive : Sign::Negative;
            result.Certainty = Certainty::Uncertain;
        }
        return result;
    }

[[nodiscard]] BarycentricResult ClassifyTriangleBarycentric(
        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
        const glm::vec3& query) noexcept
    {
        BarycentricResult result{};

        const glm::vec3 ab = b - a;
        const glm::vec3 ac = c - a;
        const glm::vec3 normal = glm::cross(ab, ac);
        const double doubleAreaSq = static_cast<double>(glm::dot(normal, normal));

        // Edge-length-based scale for tolerances.
        const double edgeScale =
            static_cast<double>(glm::length(ab))
            + static_cast<double>(glm::length(ac))
            + static_cast<double>(glm::length(c - b));
        const double areaScale = edgeScale * edgeScale;
        const double degenEps = ScaledEpsilon(areaScale, 1.0e-14);

        if (doubleAreaSq <= degenEps * degenEps)
        {
            result.Region = BarycentricRegion::Degenerate;
            return result;
        }

        const double invDoubleArea = 1.0 / std::sqrt(doubleAreaSq);
        const glm::vec3 unitNormal = normal * static_cast<float>(invDoubleArea);
        result.PlaneDistance = SignedDistanceToPlane(a, unitNormal, query);

        // Project barycentric computation onto the plane using signed sub-
        // triangle areas evaluated through cross products.
        const glm::vec3 v0 = b - a;
        const glm::vec3 v1 = c - a;
        const glm::vec3 v2 = query - a;

        const double d00 = static_cast<double>(glm::dot(v0, v0));
        const double d01 = static_cast<double>(glm::dot(v0, v1));
        const double d11 = static_cast<double>(glm::dot(v1, v1));
        const double d20 = static_cast<double>(glm::dot(v2, v0));
        const double d21 = static_cast<double>(glm::dot(v2, v1));

        const double denom = d00 * d11 - d01 * d01;
        if (denom <= degenEps * degenEps)
        {
            result.Region = BarycentricRegion::Degenerate;
            return result;
        }

        const double v = (d11 * d20 - d01 * d21) / denom;
        const double w = (d00 * d21 - d01 * d20) / denom;
        const double u = 1.0 - v - w;

        result.WA = u;
        result.WB = v;
        result.WC = w;

        // Use a relative tolerance based on the largest absolute weight so
        // callers see consistent classification across triangle scales.
        const double weightScale = std::max({1.0,
                                             std::fabs(u),
                                             std::fabs(v),
                                             std::fabs(w)});
        const double eps = ScaledEpsilon(weightScale, 1.0e-9);

        const bool uZero = std::fabs(u) <= eps;
        const bool vZero = std::fabs(v) <= eps;
        const bool wZero = std::fabs(w) <= eps;
        const bool uNeg = u < -eps;
        const bool vNeg = v < -eps;
        const bool wNeg = w < -eps;

        if (uNeg || vNeg || wNeg)
        {
            result.Region = BarycentricRegion::Outside;
            return result;
        }

        const int zeroCount = (uZero ? 1 : 0) + (vZero ? 1 : 0) + (wZero ? 1 : 0);
        if (zeroCount >= 2)
        {
            // Two weights zero ⇒ third equals one ⇒ vertex incidence.
            if (!uZero) result.Region = BarycentricRegion::VertexA;
            else if (!vZero) result.Region = BarycentricRegion::VertexB;
            else result.Region = BarycentricRegion::VertexC;
            return result;
        }
        if (zeroCount == 1)
        {
            if (uZero) result.Region = BarycentricRegion::EdgeBC; // opposite A
            else if (vZero) result.Region = BarycentricRegion::EdgeCA; // opposite B
            else result.Region = BarycentricRegion::EdgeAB; // opposite C
            return result;
        }
        result.Region = BarycentricRegion::Interior;
        return result;
    }
}
