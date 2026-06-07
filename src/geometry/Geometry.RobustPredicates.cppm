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

    [[nodiscard]] bool ApproxEqual(double a, double b, double scale,
                                          double relative = 1.0e-9) noexcept;

    [[nodiscard]] bool ApproxEqual(const glm::vec3& a, const glm::vec3& b,
                                          double scale,
                                          double relative = 1.0e-9) noexcept;

    // -----------------------------------------------------------------------
    // Scale-aware zero-magnitude tests (GEOM-015 Slice 1).
    // -----------------------------------------------------------------------
    //
    // GJK / Support-style code repeatedly asks "is this squared length /
    // length effectively zero at the local primitive scale?". These helpers
    // formalize that question against the same `ScaledEpsilon` policy used
    // by the other predicates in this module and return a
    // `SignedResult`-shaped diagnostic so callers can record the filter
    // bound that was actually used. A bare boolean overload is provided for
    // the common branch-style callsites (e.g. zero-vector guards in support
    // primitives) that do not need the diagnostic record.
    //
    // Semantics:
    // - `ApproxZeroSq(valueSq, scale, relative)` decides whether a squared
    //   magnitude is inside the certain-zero band. The filter bound is
    //   `eps * eps` so the comparison is performed in squared space and
    //   matches the conventional `length2(v) <= EPS * EPS` pattern.
    // - `ApproxZeroLen(length, scale, relative)` is the linear-magnitude
    //   form and uses `eps` directly. Negative `length` inputs are treated
    //   as their absolute value (callers occasionally pass signed
    //   magnitudes).
    // - When the value is exactly representable zero the result reports
    //   `Sign::Zero` / `Certainty::Certain` regardless of scale; otherwise
    //   the result reports `Sign::Positive` plus `Certainty::Certain` when
    //   the magnitude is strictly above the filter, and `Sign::Positive`
    //   plus `Certainty::Uncertain` when it sits inside the band but is
    //   non-zero in floating-point.
    [[nodiscard]] SignedResult ApproxZeroSqDiagnostic(double valueSq,
                                                             double scale,
                                                             double relative = 1.0e-9) noexcept;

    [[nodiscard]] SignedResult ApproxZeroLenDiagnostic(double length,
                                                              double scale,
                                                              double relative = 1.0e-9) noexcept;

    // Bare boolean overloads for the common branch-style callsites. A value
    // is considered approximately zero iff it lies in the certain-zero band
    // (`Sign::Zero` or `Certainty::Uncertain` from the diagnostic forms).
    [[nodiscard]] bool ApproxZeroSq(double valueSq, double scale,
                                           double relative = 1.0e-9) noexcept;

    [[nodiscard]] bool ApproxZeroLen(double length, double scale,
                                            double relative = 1.0e-9) noexcept;

    // -----------------------------------------------------------------------
    // 2D orientation predicate.
    // -----------------------------------------------------------------------
    //
    // Returns the signed area of the triangle (a, b, c) doubled.  Positive
    // values indicate counter-clockwise orientation in a right-handed XY
    // plane; negative values indicate clockwise; near-zero values inside the
    // filter band are reported with `Certainty::Uncertain`.
    [[nodiscard]] SignedResult Orientation2D(const glm::vec2& a,
                                                    const glm::vec2& b,
                                                    const glm::vec2& c) noexcept;

    // -----------------------------------------------------------------------
    // 3D orientation predicate.
    // -----------------------------------------------------------------------
    //
    // Returns six times the signed volume of the tetrahedron (a, b, c, d).
    // Positive values mean `d` lies above the plane (a, b, c) measured
    // against the right-hand normal (b-a) × (c-a); negative values mean `d`
    // lies below.  Near-zero values inside the filter band are reported with
    // `Certainty::Uncertain`.
    [[nodiscard]] SignedResult Orientation3D(const glm::vec3& a,
                                                    const glm::vec3& b,
                                                    const glm::vec3& c,
                                                    const glm::vec3& d) noexcept;

    // -----------------------------------------------------------------------
    // Signed distance of a point to a plane defined by (origin, unit normal).
    // -----------------------------------------------------------------------
    //
    // The caller is responsible for supplying a unit normal; the predicate
    // does not renormalize.  Magnitude is reported in source units; the
    // certainty bound is `ScaledEpsilon` of the point/origin magnitudes.
    [[nodiscard]] SignedResult SignedDistanceToPlane(const glm::vec3& planeOrigin,
                                                            const glm::vec3& planeNormal,
                                                            const glm::vec3& query) noexcept;

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
    [[nodiscard]] SignedResult SignedDistanceToHessianPlane(
        const glm::vec3& planeNormal,
        float planeOffset,
        const glm::vec3& query) noexcept;

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
    [[nodiscard]] BarycentricResult ClassifyTriangleBarycentric(
        const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
        const glm::vec3& query) noexcept;
}






