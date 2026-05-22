module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

export module Geometry.RobustPredicates;

// =============================================================================
// Geometry.RobustPredicates — GEOM-007 Slice 1.
//
// A narrow, dependency-free predicate foundation for downstream geometry
// algorithms that need deterministic decisions in the presence of near-degenerate
// inputs. This module is intentionally NOT re-exported from the `Geometry`
// umbrella (per `docs/architecture/geometry-api-style.md` §"Module style" — it
// is an advanced narrow module and callers must opt in explicitly).
//
// Numerical policy:
// - Public inputs use `glm::vec*<float>` to preserve repository storage rules.
// - Internal evaluation is performed in `double` to widen the certain band.
// - Decisions inside the documented "uncertainty band" are reported with
//   `Certainty::Uncertain` rather than coerced into a hard sign. Callers
//   choosing to treat uncertain results as a particular sign must do so
//   explicitly; the module never silently picks a side.
// - This is a filtered double-precision implementation. Exact / adaptive
//   Shewchuk-style escalation is a Slice 4 follow-up (see
//   `tasks/active/GEOM-007-robust-predicates-intersection-classification.md`).
//   Until then, callers requiring guaranteed signs (e.g. mesh boolean kernels)
//   must add their own snap-rounding / perturbation pre-pass or wait for
//   Slice 4.
//
// Diagnostics:
// - `Sign` distinguishes `Negative` / `Zero` / `Positive`. `Zero` means the
//   filtered evaluation produced a magnitude inside the certain-zero band.
// - `Certainty` distinguishes `Certain` (above the filter bound) from
//   `Uncertain` (inside the filter bound but non-zero in the floating-point
//   computation).
// - `BarycentricRegion` classifies a 2D-projected point against a triangle:
//   `Vertex{A,B,C}`, `Edge{AB,BC,CA}`, `Interior`, `Outside`, `Degenerate`
//   (triangle has zero area within tolerance), `Uncertain` (filter band).
// =============================================================================

export namespace Geometry::RobustPredicates
{
    // -----------------------------------------------------------------------
    // Diagnostic enums.
    // -----------------------------------------------------------------------

    enum class Sign : std::int8_t
    {
        Negative = -1,
        Zero = 0,
        Positive = 1,
    };

    enum class Certainty : std::uint8_t
    {
        Certain = 0,
        Uncertain = 1,
    };

    enum class BarycentricRegion : std::uint8_t
    {
        VertexA = 0,
        VertexB,
        VertexC,
        EdgeAB,
        EdgeBC,
        EdgeCA,
        Interior,
        Outside,
        Degenerate,
        Uncertain,
    };

    // -----------------------------------------------------------------------
    // Result records.
    // -----------------------------------------------------------------------

    // Result of a signed-magnitude predicate (orientation, signed distance).
    struct SignedResult
    {
        double Value{0.0};
        Sign Sign{Sign::Zero};
        Certainty Certainty{Certainty::Certain};
        // Filter bound used for this evaluation, recorded so callers can
        // reproduce the certain-band decision when reporting diagnostics.
        double FilterBound{0.0};
    };

    // Result of an in-plane (3D triangle in its own supporting plane)
    // barycentric classification.  Weights sum to 1 when the triangle is
    // non-degenerate; weights are unspecified when `Region == Degenerate`.
    struct BarycentricResult
    {
        BarycentricRegion Region{BarycentricRegion::Uncertain};
        double WA{0.0};
        double WB{0.0};
        double WC{0.0};
        // Signed plane distance of the query point to the triangle's
        // supporting plane (scaled by 2 * triangle area).  Non-zero values
        // mean the query point is not coplanar; `Region` reports
        // `Uncertain` or `Outside` accordingly.
        SignedResult PlaneDistance{};
    };

    // -----------------------------------------------------------------------
    // Scale-aware comparison helpers.
    // -----------------------------------------------------------------------

    // Conservative ULP-style relative epsilon used when no explicit scale is
    // supplied.  The constant tracks IEEE-754 double precision (~2.22e-16);
    // the multiplier is the standard Shewchuk-style filter slack accounting
    // for the dependent floating-point operations performed by the 2D
    // orientation determinant (5 mul + 1 sub, error bound is ~4 * eps * |a|
    // + higher-order terms; we round up to 8 * eps for safety margin).
    inline constexpr double kDoubleEpsilon = 2.2204460492503131e-16;
    inline constexpr double kOrientationFilter2D = 8.0 * kDoubleEpsilon;
    inline constexpr double kOrientationFilter3D = 16.0 * kDoubleEpsilon;

    // Scale-aware epsilon: returns a tolerance proportional to the magnitude
    // of the supplied scale (e.g. bounding-box diagonal or characteristic
    // length).  Callers pass the relevant local scale rather than relying on
    // a global tolerance.
    [[nodiscard]] inline constexpr double ScaledEpsilon(double scale,
                                                        double relative = 1.0e-9) noexcept
    {
        const double absScale = scale < 0.0 ? -scale : scale;
        // Floor the returned tolerance at the absolute machine epsilon scaled
        // by `relative` so that callers passing scale == 0 still get a
        // non-zero certain-zero band.
        const double floor = relative * 1.0e-7;
        const double scaled = relative * absScale;
        return scaled > floor ? scaled : floor;
    }

    [[nodiscard]] inline bool ApproxEqual(double a, double b, double scale,
                                          double relative = 1.0e-9) noexcept
    {
        const double diff = a - b;
        const double eps = ScaledEpsilon(scale, relative);
        return (diff < 0.0 ? -diff : diff) <= eps;
    }

    [[nodiscard]] inline bool ApproxEqual(const glm::vec3& a, const glm::vec3& b,
                                          double scale,
                                          double relative = 1.0e-9) noexcept
    {
        const double eps = ScaledEpsilon(scale, relative);
        const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
        const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
        const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
        return (dx * dx + dy * dy + dz * dz) <= eps * eps;
    }

    // -----------------------------------------------------------------------
    // 2D orientation predicate.
    // -----------------------------------------------------------------------
    //
    // Returns the signed area of the triangle (a, b, c) doubled.  Positive
    // values indicate counter-clockwise orientation in a right-handed XY
    // plane; negative values indicate clockwise; near-zero values inside the
    // filter band are reported with `Certainty::Uncertain`.
    [[nodiscard]] inline SignedResult Orientation2D(const glm::vec2& a,
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

    // -----------------------------------------------------------------------
    // 3D orientation predicate.
    // -----------------------------------------------------------------------
    //
    // Returns six times the signed volume of the tetrahedron (a, b, c, d).
    // Positive values mean `d` lies above the plane (a, b, c) measured
    // against the right-hand normal (b-a) × (c-a); negative values mean `d`
    // lies below.  Near-zero values inside the filter band are reported with
    // `Certainty::Uncertain`.
    [[nodiscard]] inline SignedResult Orientation3D(const glm::vec3& a,
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

    // -----------------------------------------------------------------------
    // Signed distance of a point to a plane defined by (origin, unit normal).
    // -----------------------------------------------------------------------
    //
    // The caller is responsible for supplying a unit normal; the predicate
    // does not renormalize.  Magnitude is reported in source units; the
    // certainty bound is `ScaledEpsilon` of the point/origin magnitudes.
    [[nodiscard]] inline SignedResult SignedDistanceToPlane(const glm::vec3& planeOrigin,
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

    // -----------------------------------------------------------------------
    // Signed-distance evaluation for a Hessian-form plane.
    // -----------------------------------------------------------------------
    //
    // Hessian convention used by the geometry layer's plane storage
    // (`Geometry::Plane` and `Geometry::Frustum::Plane`):
    //
    //     dot(planeNormal, point) + planeOffset == 0  on the plane.
    //
    // This is the same convention as `Geometry::SDF::Math::Sdf_Plane`. The
    // helper computes the signed value in double precision with the same
    // `SignedResult` + filter-bound diagnostics as
    // `SignedDistanceToPlane(origin, normal, query)` so callers migrating
    // off the float-precision `Sdf_Plane` form get a drop-in replacement
    // (GEOM-007 Slice 3.3.a).
    //
    // Caller responsibilities mirror the origin-form overload:
    // - `planeNormal` is treated as supplied; the helper does not
    //   renormalize. When `|planeNormal|` is not 1 the returned value is
    //   `dot(N, q) + d` (i.e. distance scaled by `|N|`); sign / certainty
    //   diagnostics are unchanged by that scaling.
    // - The certainty band is derived from the sum of absolute term
    //   magnitudes, so on-plane queries report `Sign::Zero` /
    //   `Certainty::Certain` when the inputs are exactly representable.
    [[nodiscard]] inline SignedResult SignedDistanceToHessianPlane(
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

    // -----------------------------------------------------------------------
    // In-plane triangle barycentric classification.
    // -----------------------------------------------------------------------
    //
    // Classifies `query` against triangle (a, b, c) by projecting onto the
    // triangle's supporting plane and computing barycentric weights via
    // signed sub-triangle areas.  Reports:
    //   - `Degenerate` when triangle area is below `ScaledEpsilon` of the
    //     edge lengths;
    //   - `Uncertain` when any barycentric weight is inside the filter band
    //     against zero or against one;
    //   - `VertexA/B/C` when two of three weights are at zero within the
    //     filter band;
    //   - `EdgeAB/BC/CA` when exactly one weight is at zero within the
    //     filter band and the other two are positive;
    //   - `Interior` when all weights are strictly positive outside the
    //     filter band;
    //   - `Outside` when at least one weight is negative outside the filter
    //     band.
    //
    // The plane-distance signed result is recorded on the returned record so
    // callers can disambiguate coplanar vs above/below for 3D point/face
    // incidence at higher layers; this module does not declare a global
    // "coplanar" decision because coplanarity tolerance is caller-specific.
    [[nodiscard]] inline BarycentricResult ClassifyTriangleBarycentric(
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






