module;

#include <cstdint>
#include <limits>

#include <glm/glm.hpp>

export module Geometry.IntersectionClassification;

import Geometry.RobustPredicates;

// =============================================================================
// Geometry.IntersectionClassification — GEOM-007 Slice 2.
//
// Records-only sibling of `Geometry.RobustPredicates`. Defines the shared
// vocabulary for primitive-vs-primitive intersection outcomes so future
// algorithms (and Slice 3 callsite adoption inside `Geometry.Raycast`,
// `Geometry.Overlap`, `Geometry.Containment`, etc.) can report degeneracy,
// touching, overlap, coplanarity, and numerically uncertain cases without
// re-inventing per-callsite enums.
//
// Scope and limitations:
// - This module ships *records only*. It does NOT implement any intersection
//   algorithm. Slice 3 of GEOM-007 will migrate one existing callsite at a
//   time onto these records with parity tests.
// - Numerical decisions remain delegated to `Geometry.RobustPredicates`:
//   when an implementation cannot decide an outcome inside the filter band,
//   it should set `Kind::Uncertain` and leave the geometric fields at their
//   default values rather than guess.
// - Public storage stays in `glm::vec*<float>` per the geometry api-style
//   policy. Parameters along rays/segments are reported as `double` so that
//   callers that need to compare against `RobustPredicates::ScaledEpsilon`
//   do not lose precision on the result-record boundary.
// - The module is intentionally NOT re-exported from the `Geometry` umbrella
//   for the same reason as `Geometry.RobustPredicates`: it is an advanced
//   narrow surface and callers opt in with an explicit `import`.
// =============================================================================

export namespace Geometry::Intersection
{
    // ------------------------------------------------------------------------
    // Shared intersection-kind enum.
    // ------------------------------------------------------------------------
    //
    // The convention is:
    // - `None`          — primitives are disjoint with certainty.
    // - `Proper`        — primitives meet transversally in their respective
    //                     interiors (a single point for segment/segment in
    //                     general position, a single point in the triangle
    //                     interior for ray/triangle, etc.).
    // - `Touching`      — primitives meet at exactly one boundary feature of
    //                     at least one operand (segment endpoint, triangle
    //                     vertex/edge). Callers consult feature-specific
    //                     fields on the record to learn which feature.
    // - `Overlap`       — primitives share a positive-measure subset (a sub-
    //                     segment for segment/segment; a sub-segment for
    //                     triangle/triangle when both are coplanar and their
    //                     intersection is one-dimensional).
    // - `Coplanar`      — primitives lie in a common plane; the implementation
    //                     fell back to a 2D classification reported in the
    //                     record's feature fields. Used by segment/triangle,
    //                     ray/triangle, and triangle/triangle records.
    // - `Coincident`    — primitives occupy the same point set (two triangles
    //                     whose vertex sets coincide modulo orientation; a
    //                     segment lying exactly along a triangle edge; etc.).
    // - `DegenerateInput` — at least one operand was degenerate (zero-length
    //                     segment, zero-area triangle, zero-length ray
    //                     direction).
    // - `Uncertain`     — the underlying predicate evaluation fell inside the
    //                     `Geometry.RobustPredicates` filter band; callers
    //                     must treat the record's geometric fields as
    //                     unspecified.
    enum class Kind : std::uint8_t
    {
        None = 0,
        Proper,
        Touching,
        Overlap,
        Coplanar,
        Coincident,
        DegenerateInput,
        Uncertain,
    };

    // ------------------------------------------------------------------------
    // Segment / ray feature enums (which boundary feature is touched).
    // ------------------------------------------------------------------------

    enum class SegmentFeature : std::uint8_t
    {
        None = 0,    // No feature is implicated (e.g. proper interior hit).
        Start,       // Parameter == 0 within tolerance.
        End,         // Parameter == 1 within tolerance.
        Interior,    // Parameter strictly inside (0, 1).
    };

    enum class RayFeature : std::uint8_t
    {
        None = 0,    // No feature is implicated.
        Origin,      // Parameter == 0 within tolerance.
        Interior,    // Parameter > 0.
    };

    // ------------------------------------------------------------------------
    // Triangle feature enum.
    // ------------------------------------------------------------------------
    //
    // Mirrors `Geometry::RobustPredicates::BarycentricRegion` but trimmed to
    // the in-triangle features that are meaningful at intersection boundary
    // contact. `Outside`/`Degenerate`/`Uncertain` are intentionally absent;
    // they are encoded by `Kind` at the top level of the record.
    enum class TriangleFeature : std::uint8_t
    {
        None = 0,
        VertexA,
        VertexB,
        VertexC,
        EdgeAB,
        EdgeBC,
        EdgeCA,
        Interior,
    };

    // ------------------------------------------------------------------------
    // Sentinel for unspecified scalar fields.
    // ------------------------------------------------------------------------

    inline constexpr double kUnspecified = std::numeric_limits<double>::quiet_NaN();

    // ------------------------------------------------------------------------
    // Segment / segment.
    // ------------------------------------------------------------------------
    //
    // Reports the intersection of segment A (parametrised by `ParamA ∈ [0, 1]`
    // along A's direction) and segment B (similarly `ParamB`).
    // - For `Kind::Proper`, both parameters and `Point` are populated and the
    //   feature fields describe which interior is being touched
    //   (typically both `Interior`).
    // - For `Kind::Touching`, the feature fields identify the specific
    //   endpoint(s) involved and `Point` is that endpoint's location.
    // - For `Kind::Overlap`, the segments are collinear and the
    //   `OverlapStart`/`OverlapEnd` parameters along A delimit the shared
    //   sub-segment with `OverlapStart <= OverlapEnd`.
    // - For `Kind::None|DegenerateInput|Uncertain`, scalar fields default
    //   to `kUnspecified` and the `Point` defaults to the origin.
    struct SegmentSegmentResult
    {
        Kind Kind{Kind::Uncertain};
        SegmentFeature OnA{SegmentFeature::None};
        SegmentFeature OnB{SegmentFeature::None};
        double ParamA{kUnspecified};
        double ParamB{kUnspecified};
        glm::vec3 Point{0.0f};
        // Valid only when `Kind == Overlap`; ordered along A.
        double OverlapStart{kUnspecified};
        double OverlapEnd{kUnspecified};
    };

    // ------------------------------------------------------------------------
    // Segment / triangle (3D).
    // ------------------------------------------------------------------------
    //
    // - For `Kind::Proper`, `SegmentParam ∈ (0, 1)`, the barycentric weights
    //   are strictly positive, `Feature == Interior`, and `Point` is the
    //   in-triangle hit.
    // - For `Kind::Touching`, the segment touches either a triangle vertex or
    //   edge: `Feature` identifies which feature, the barycentric weights and
    //   `Point` describe the contact location, and `SegmentFeature` records
    //   whether the contact is at the segment's start, end, or interior.
    // - For `Kind::Coplanar`, the segment and triangle share the supporting
    //   plane: callers should inspect the (coplanar) sub-segment of intersection
    //   reported by `OverlapStart`/`OverlapEnd` (parameters along the segment).
    //   When no coplanar overlap exists, `OverlapStart`/`OverlapEnd` are left
    //   `kUnspecified` and the record degenerates to `Kind::None`.
    struct SegmentTriangleResult
    {
        Kind Kind{Kind::Uncertain};
        SegmentFeature OnSegment{SegmentFeature::None};
        TriangleFeature OnTriangle{TriangleFeature::None};
        double SegmentParam{kUnspecified};
        double WA{kUnspecified};
        double WB{kUnspecified};
        double WC{kUnspecified};
        glm::vec3 Point{0.0f};
        double OverlapStart{kUnspecified};
        double OverlapEnd{kUnspecified};
    };

    // ------------------------------------------------------------------------
    // Ray / triangle (3D).
    // ------------------------------------------------------------------------
    //
    // Same shape as `SegmentTriangleResult` minus the segment-end concept:
    // - `RayParam >= 0` for any non-`None` outcome other than `Coplanar`.
    // - `Feature == Interior` for `Kind::Proper`.
    // - `Kind::Touching` is used when the ray hits a triangle vertex/edge
    //   (the `Feature` field disambiguates).
    // - `Kind::Coplanar` indicates the ray lies in the triangle's supporting
    //   plane; callers should follow up with a 2D classification.
    struct RayTriangleResult
    {
        Kind Kind{Kind::Uncertain};
        RayFeature OnRay{RayFeature::None};
        TriangleFeature OnTriangle{TriangleFeature::None};
        double RayParam{kUnspecified};
        double WA{kUnspecified};
        double WB{kUnspecified};
        double WC{kUnspecified};
        glm::vec3 Point{0.0f};
    };

    // ------------------------------------------------------------------------
    // Triangle / triangle (3D).
    // ------------------------------------------------------------------------
    //
    // - For `Kind::Proper`, the triangles meet transversally and the contact
    //   is a line segment from `ContactStart` to `ContactEnd`; both endpoints
    //   are populated and `IsCoplanar == false`.
    // - For `Kind::Touching`, the triangles share exactly one point (a vertex
    //   touching another vertex/edge/face); `ContactStart == ContactEnd ==
    //   Point` and the feature fields identify which vertex/edge participated.
    // - For `Kind::Overlap`, the triangles are coplanar and share a positive-
    //   area region; callers consult `IsCoplanar = true` and may follow up
    //   with a 2D arrangement on the supporting plane (Slice 4+).
    // - For `Kind::Coincident`, the triangles share their entire surface modulo
    //   orientation.
    // - For `Kind::Coplanar` with no positive-area overlap, the triangles are
    //   coplanar but disjoint or touch at lower-dimensional features described
    //   by `FeatureA`/`FeatureB`.
    struct TriangleTriangleResult
    {
        Kind Kind{Kind::Uncertain};
        TriangleFeature OnA{TriangleFeature::None};
        TriangleFeature OnB{TriangleFeature::None};
        glm::vec3 ContactStart{0.0f};
        glm::vec3 ContactEnd{0.0f};
        bool IsCoplanar{false};
    };

    // ------------------------------------------------------------------------
    // Point / triangle (3D) incidence.
    // ------------------------------------------------------------------------
    //
    // A thin wrapper around `Geometry::RobustPredicates::BarycentricResult`
    // that also records the side of the supporting plane the query point is
    // on. The wrapper exists so callsites that want a single "point vs
    // triangle" record do not need to import both modules just to express
    // the most common incidence query.
    struct PointTriangleResult
    {
        // `Kind::Proper`     — strictly above or below the supporting plane
        //                      and projects to an interior region.
        // `Kind::Touching`   — on the plane and incident to a vertex/edge
        //                      or interior of the triangle.
        // `Kind::None`       — on the plane (or off) but projects outside.
        // `Kind::Coplanar`   — on the plane and incident to triangle interior
        //                      (degenerates with `Kind::Touching` when
        //                      `Feature == Interior` — implementations may
        //                      pick either; `Coplanar` is the recommended
        //                      tagging when callers want to disambiguate the
        //                      planar case from a 3D touch).
        // `Kind::DegenerateInput` / `Kind::Uncertain` mirror the predicate
        // diagnostics surfaced by `Geometry.RobustPredicates`.
        Kind Kind{Kind::Uncertain};
        TriangleFeature Feature{TriangleFeature::None};
        RobustPredicates::Sign PlaneSide{RobustPredicates::Sign::Zero};
        RobustPredicates::Certainty PlaneSideCertainty{RobustPredicates::Certainty::Uncertain};
        double SignedPlaneDistance{kUnspecified};
        double WA{kUnspecified};
        double WB{kUnspecified};
        double WC{kUnspecified};
    };

    // ------------------------------------------------------------------------
    // Small helpers (header-only, trivial).
    // ------------------------------------------------------------------------

    // Returns true when the supplied `Kind` describes a non-empty intersection
    // (Proper, Touching, Overlap, Coplanar, or Coincident). `None` is empty;
    // `DegenerateInput` and `Uncertain` are *not* claimed as intersections,
    // so callers must consult them separately.
    [[nodiscard]] inline constexpr bool HasIntersection(Kind kind) noexcept
    {
        switch (kind)
        {
            case Kind::Proper:
            case Kind::Touching:
            case Kind::Overlap:
            case Kind::Coplanar:
            case Kind::Coincident:
                return true;
            case Kind::None:
            case Kind::DegenerateInput:
            case Kind::Uncertain:
                return false;
        }
        return false;
    }

    // Returns true when the supplied `Kind` indicates an outcome that the
    // implementation could not decide and that callers must treat as
    // requiring escalation (e.g. Slice 4 exact predicates, snap rounding,
    // or symbolic perturbation).
    [[nodiscard]] inline constexpr bool IsAmbiguous(Kind kind) noexcept
    {
        return kind == Kind::Uncertain;
    }

    // Translates a `BarycentricRegion` from `Geometry.RobustPredicates` into
    // the trimmed `TriangleFeature` enum used by intersection records. The
    // out-of-triangle regions are mapped to `TriangleFeature::None`; callers
    // should set `Kind::None` separately in that case. `Uncertain` /
    // `Degenerate` also map to `TriangleFeature::None` and require the
    // caller to encode the diagnostic via `Kind`.
    [[nodiscard]] inline constexpr TriangleFeature TriangleFeatureFromBarycentric(
        RobustPredicates::BarycentricRegion region) noexcept
    {
        using Reg = RobustPredicates::BarycentricRegion;
        switch (region)
        {
            case Reg::VertexA:  return TriangleFeature::VertexA;
            case Reg::VertexB:  return TriangleFeature::VertexB;
            case Reg::VertexC:  return TriangleFeature::VertexC;
            case Reg::EdgeAB:   return TriangleFeature::EdgeAB;
            case Reg::EdgeBC:   return TriangleFeature::EdgeBC;
            case Reg::EdgeCA:   return TriangleFeature::EdgeCA;
            case Reg::Interior: return TriangleFeature::Interior;
            case Reg::Outside:
            case Reg::Degenerate:
            case Reg::Uncertain:
                return TriangleFeature::None;
        }
        return TriangleFeature::None;
    }
}





