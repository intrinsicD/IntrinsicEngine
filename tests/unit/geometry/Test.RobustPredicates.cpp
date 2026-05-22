#include <gtest/gtest.h>

#include <cmath>

#include <glm/glm.hpp>

import Geometry.RobustPredicates;

namespace
{
    using Geometry::RobustPredicates::BarycentricRegion;
    using Geometry::RobustPredicates::BarycentricResult;
    using Geometry::RobustPredicates::Certainty;
    using Geometry::RobustPredicates::ClassifyTriangleBarycentric;
    using Geometry::RobustPredicates::Orientation2D;
    using Geometry::RobustPredicates::Orientation3D;
    using Geometry::RobustPredicates::ScaledEpsilon;
    using Geometry::RobustPredicates::SignedDistanceToPlane;
    using Geometry::RobustPredicates::SignedDistanceToHessianPlane;
    using Geometry::RobustPredicates::Sign;
    using Geometry::RobustPredicates::SignedResult;
}

// -----------------------------------------------------------------------------
// Scale-aware epsilon helper.
// -----------------------------------------------------------------------------

TEST(RobustPredicatesScaledEpsilon, ZeroScaleStillReturnsPositiveFloor)
{
    EXPECT_GT(ScaledEpsilon(0.0), 0.0);
    EXPECT_GT(ScaledEpsilon(-0.0), 0.0);
}

TEST(RobustPredicatesScaledEpsilon, GrowsWithScale)
{
    EXPECT_LT(ScaledEpsilon(1.0), ScaledEpsilon(1.0e6));
}

// -----------------------------------------------------------------------------
// 2D orientation predicate.
// -----------------------------------------------------------------------------

TEST(RobustPredicatesOrientation2D, ProperCounterClockwiseIsPositiveCertain)
{
    const auto r = Orientation2D({0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f});
    EXPECT_EQ(r.Sign, Sign::Positive);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
    EXPECT_GT(r.Value, 0.0);
}

TEST(RobustPredicatesOrientation2D, ProperClockwiseIsNegativeCertain)
{
    const auto r = Orientation2D({0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 0.0f});
    EXPECT_EQ(r.Sign, Sign::Negative);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
    EXPECT_LT(r.Value, 0.0);
}

TEST(RobustPredicatesOrientation2D, CollinearExactlyIsZeroCertain)
{
    // Exactly representable collinear configuration.
    const auto r = Orientation2D({0.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 0.0f});
    EXPECT_EQ(r.Sign, Sign::Zero);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
    EXPECT_EQ(r.Value, 0.0);
}

TEST(RobustPredicatesOrientation2D, DuplicatedPointIsZero)
{
    const auto r = Orientation2D({1.0f, 2.0f}, {1.0f, 2.0f}, {3.0f, 4.0f});
    EXPECT_EQ(r.Sign, Sign::Zero);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

TEST(RobustPredicatesOrientation2D, LargeScaleIsDecidable)
{
    constexpr float k = 1.0e6f;
    const auto r = Orientation2D({0.0f, 0.0f}, {k, 0.0f}, {0.0f, k});
    EXPECT_EQ(r.Sign, Sign::Positive);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

TEST(RobustPredicatesOrientation2D, SmallScaleIsDecidable)
{
    constexpr float k = 1.0e-6f;
    const auto r = Orientation2D({0.0f, 0.0f}, {k, 0.0f}, {0.0f, k});
    EXPECT_EQ(r.Sign, Sign::Positive);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

TEST(RobustPredicatesOrientation2D, NonDegenerateInputReportsActiveFilter)
{
    // The Uncertain band is structurally unreachable from float-widened
    // inputs that evaluate exactly in double, so we instead verify the
    // filter bound is active (non-zero) for non-degenerate inputs.  This
    // pins the Shewchuk-style static filter behaviour without depending on
    // float→double cancellation landing inside the band.
    const auto certainDecidable = Orientation2D({0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f});
    EXPECT_GT(certainDecidable.FilterBound, 0.0);
    EXPECT_GT(std::fabs(certainDecidable.Value), certainDecidable.FilterBound);

    // Collinear inputs produce an exact zero determinant; the filter bound
    // is itself zero because every contributing product is exactly zero,
    // so the result is certain even with the filter active.
    const auto collinear = Orientation2D({0.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 0.0f});
    EXPECT_EQ(collinear.Value, 0.0);
    EXPECT_EQ(collinear.Sign, Sign::Zero);
    EXPECT_EQ(collinear.Certainty, Certainty::Certain);
}

// -----------------------------------------------------------------------------
// 3D orientation predicate.
// -----------------------------------------------------------------------------

TEST(RobustPredicatesOrientation3D, PointAbovePlaneIsPositiveCertain)
{
    // (a, b, c) lie in XY plane with CCW winding; d above means positive
    // signed volume under the right-hand rule.
    const auto r = Orientation3D(
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f});
    EXPECT_EQ(r.Sign, Sign::Positive);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

TEST(RobustPredicatesOrientation3D, PointBelowPlaneIsNegativeCertain)
{
    const auto r = Orientation3D(
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, -1.0f});
    EXPECT_EQ(r.Sign, Sign::Negative);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

TEST(RobustPredicatesOrientation3D, CoplanarExactlyIsZeroCertain)
{
    // All four points on XY plane.
    const auto r = Orientation3D(
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 0.0f});
    EXPECT_EQ(r.Sign, Sign::Zero);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

TEST(RobustPredicatesOrientation3D, DegenerateBaseTriangleStillProducesResult)
{
    // (a, b, c) collinear ⇒ determinant is exactly zero regardless of d.
    const auto r = Orientation3D(
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f});
    EXPECT_EQ(r.Sign, Sign::Zero);
    // Sign is decidable here because every cofactor is exactly zero.
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

// -----------------------------------------------------------------------------
// Signed plane distance.
// -----------------------------------------------------------------------------

TEST(RobustPredicatesSignedDistanceToPlane, AboveAndBelowPlane)
{
    const glm::vec3 origin{0.0f};
    const glm::vec3 normal{0.0f, 0.0f, 1.0f};

    const auto above = SignedDistanceToPlane(origin, normal, {1.0f, 2.0f, 0.5f});
    EXPECT_EQ(above.Sign, Sign::Positive);
    EXPECT_EQ(above.Certainty, Certainty::Certain);
    EXPECT_NEAR(above.Value, 0.5, 1.0e-12);

    const auto below = SignedDistanceToPlane(origin, normal, {1.0f, 2.0f, -0.5f});
    EXPECT_EQ(below.Sign, Sign::Negative);
    EXPECT_EQ(below.Certainty, Certainty::Certain);
    EXPECT_NEAR(below.Value, -0.5, 1.0e-12);
}

TEST(RobustPredicatesSignedDistanceToPlane, OnPlaneIsZero)
{
    const auto r = SignedDistanceToPlane(
        {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 2.0f, 0.0f});
    EXPECT_EQ(r.Sign, Sign::Zero);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
}

// -----------------------------------------------------------------------------
// Hessian-form plane signed distance (GEOM-007 Slice 3.3.a).
// -----------------------------------------------------------------------------

TEST(RobustPredicatesSignedDistanceToHessianPlane, ExactZeroOnPlaneIsCertainZero)
{
    // Plane: z + 1 == 0  (i.e. z == -1).
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    constexpr float d = 1.0f;
    const auto r = SignedDistanceToHessianPlane(n, d, {3.0f, -2.0f, -1.0f});
    EXPECT_EQ(r.Sign, Sign::Zero);
    EXPECT_EQ(r.Certainty, Certainty::Certain);
    EXPECT_NEAR(r.Value, 0.0, 1.0e-12);
}

TEST(RobustPredicatesSignedDistanceToHessianPlane, AboveAndBelowReportCertainSign)
{
    // Plane: z - 0.25 == 0  (i.e. z == 0.25).
    const glm::vec3 n{0.0f, 0.0f, 1.0f};
    constexpr float d = -0.25f;

    const auto above = SignedDistanceToHessianPlane(n, d, {1.0f, -3.0f, 0.75f});
    EXPECT_EQ(above.Sign, Sign::Positive);
    EXPECT_EQ(above.Certainty, Certainty::Certain);
    EXPECT_NEAR(above.Value, 0.5, 1.0e-12);

    const auto below = SignedDistanceToHessianPlane(n, d, {1.0f, -3.0f, -0.75f});
    EXPECT_EQ(below.Sign, Sign::Negative);
    EXPECT_EQ(below.Certainty, Certainty::Certain);
    EXPECT_NEAR(below.Value, -1.0, 1.0e-12);
}

TEST(RobustPredicatesSignedDistanceToHessianPlane, MatchesOriginFormForUnitNormal)
{
    // For unit `N`, the Hessian form `dot(N, q) + d == 0` is the origin
    // form with `origin = -N * d`. Slice 3.3.b / 3.3.c rely on this
    // parity for the Overlap / Containment frustum migration.
    const glm::vec3 n = glm::normalize(glm::vec3{0.3f, 0.7f, 0.65f});
    constexpr float d = -1.25f;
    const glm::vec3 origin = -n * d;

    const glm::vec3 samples[] = {
        {2.0f, -1.0f, 0.5f},
        {-3.0f, 4.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {7.5f, -2.25f, 11.0f},
    };

    for (const auto& q : samples)
    {
        const auto hessian = SignedDistanceToHessianPlane(n, d, q);
        const auto origForm = SignedDistanceToPlane(origin, n, q);
        EXPECT_EQ(hessian.Sign, origForm.Sign);
        // Tolerance must absorb the float-precision rounding of
        // `origin = -n * d` (the float multiply does not reconstruct the
        // double-precision Hessian sum exactly), not just double-precision
        // accumulation noise.
        EXPECT_NEAR(hessian.Value, origForm.Value, 1.0e-5);
    }
}

TEST(RobustPredicatesSignedDistanceToHessianPlane, ScaledNormalScalesValueButNotSign)
{
    // The helper does not renormalize; callers passing a non-unit normal
    // see the value scaled by `|N|`, but the sign / certainty diagnostics
    // are preserved. This matches `SDF::Math::Sdf_Plane` semantics.
    const glm::vec3 unit{0.0f, 1.0f, 0.0f};
    constexpr float d = -2.0f;
    const glm::vec3 scaled = unit * 4.0f;
    const float dScaled = d * 4.0f;

    const glm::vec3 q{5.0f, 3.0f, -1.0f};
    const auto a = SignedDistanceToHessianPlane(unit, d, q);
    const auto b = SignedDistanceToHessianPlane(scaled, dScaled, q);
    EXPECT_EQ(a.Sign, b.Sign);
    EXPECT_EQ(a.Sign, Sign::Positive);
    EXPECT_NEAR(b.Value, a.Value * 4.0, 1.0e-9);
}

TEST(RobustPredicatesSignedDistanceToHessianPlane, LargeAndSmallScaleAreDecidable)
{
    // The filtered double-precision evaluation must keep its certain band
    // wide enough to decide non-degenerate queries at both very large and
    // very small object scales — the frustum-culling and strict-
    // containment callsites in Slices 3.3.b / 3.3.c span that range.
    {
        const glm::vec3 n{0.0f, 0.0f, 1.0f};
        constexpr float d = -1.0e6f;
        const auto above = SignedDistanceToHessianPlane(n, d, {0.0f, 0.0f, 1.0e6f + 1.0f});
        EXPECT_EQ(above.Sign, Sign::Positive);
        EXPECT_EQ(above.Certainty, Certainty::Certain);
    }
    {
        const glm::vec3 n{1.0f, 0.0f, 0.0f};
        constexpr float d = -1.0e-3f;
        const auto below = SignedDistanceToHessianPlane(n, d, {1.0e-4f, 0.0f, 0.0f});
        EXPECT_EQ(below.Sign, Sign::Negative);
        EXPECT_EQ(below.Certainty, Certainty::Certain);
    }
}

// -----------------------------------------------------------------------------
// Triangle barycentric classification.
// -----------------------------------------------------------------------------

namespace
{
    constexpr glm::vec3 kA{0.0f, 0.0f, 0.0f};
    constexpr glm::vec3 kB{1.0f, 0.0f, 0.0f};
    constexpr glm::vec3 kC{0.0f, 1.0f, 0.0f};
}

TEST(RobustPredicatesBarycentric, CentroidIsInterior)
{
    const glm::vec3 centroid = (kA + kB + kC) / 3.0f;
    const auto r = ClassifyTriangleBarycentric(kA, kB, kC, centroid);
    EXPECT_EQ(r.Region, BarycentricRegion::Interior);
    EXPECT_NEAR(r.WA + r.WB + r.WC, 1.0, 1.0e-12);
    EXPECT_GT(r.WA, 0.0);
    EXPECT_GT(r.WB, 0.0);
    EXPECT_GT(r.WC, 0.0);
}

TEST(RobustPredicatesBarycentric, VertexIncidence)
{
    EXPECT_EQ(ClassifyTriangleBarycentric(kA, kB, kC, kA).Region,
              BarycentricRegion::VertexA);
    EXPECT_EQ(ClassifyTriangleBarycentric(kA, kB, kC, kB).Region,
              BarycentricRegion::VertexB);
    EXPECT_EQ(ClassifyTriangleBarycentric(kA, kB, kC, kC).Region,
              BarycentricRegion::VertexC);
}

TEST(RobustPredicatesBarycentric, EdgeMidpointIncidence)
{
    EXPECT_EQ(ClassifyTriangleBarycentric(kA, kB, kC, 0.5f * (kA + kB)).Region,
              BarycentricRegion::EdgeAB);
    EXPECT_EQ(ClassifyTriangleBarycentric(kA, kB, kC, 0.5f * (kB + kC)).Region,
              BarycentricRegion::EdgeBC);
    EXPECT_EQ(ClassifyTriangleBarycentric(kA, kB, kC, 0.5f * (kC + kA)).Region,
              BarycentricRegion::EdgeCA);
}

TEST(RobustPredicatesBarycentric, ClearlyOutsideIsOutside)
{
    const auto r = ClassifyTriangleBarycentric(kA, kB, kC, {2.0f, 2.0f, 0.0f});
    EXPECT_EQ(r.Region, BarycentricRegion::Outside);
}

TEST(RobustPredicatesBarycentric, DegenerateTriangleReportsDegenerate)
{
    // Three collinear vertices ⇒ zero area triangle.
    const auto r = ClassifyTriangleBarycentric(
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {2.0f, 0.0f, 0.0f},
        {0.5f, 0.0f, 0.0f});
    EXPECT_EQ(r.Region, BarycentricRegion::Degenerate);
}

TEST(RobustPredicatesBarycentric, AboveTriangleStillProjectsToInteriorRegion)
{
    // Lifting the query off the plane should still classify the in-plane
    // projection; the plane-distance result records the offset.
    const glm::vec3 centroid = (kA + kB + kC) / 3.0f;
    const glm::vec3 lifted = centroid + glm::vec3{0.0f, 0.0f, 0.25f};
    const auto r = ClassifyTriangleBarycentric(kA, kB, kC, lifted);
    EXPECT_EQ(r.Region, BarycentricRegion::Interior);
    EXPECT_EQ(r.PlaneDistance.Sign, Sign::Positive);
    EXPECT_NEAR(r.PlaneDistance.Value, 0.25, 1.0e-6);
}

TEST(RobustPredicatesBarycentric, LargeAndSmallScaleAreDecidable)
{
    constexpr float kLarge = 1.0e6f;
    const auto rLarge = ClassifyTriangleBarycentric(
        {0.0f, 0.0f, 0.0f}, {kLarge, 0.0f, 0.0f}, {0.0f, kLarge, 0.0f},
        {kLarge * 0.25f, kLarge * 0.25f, 0.0f});
    EXPECT_EQ(rLarge.Region, BarycentricRegion::Interior);

    constexpr float kSmall = 1.0e-4f;
    const auto rSmall = ClassifyTriangleBarycentric(
        {0.0f, 0.0f, 0.0f}, {kSmall, 0.0f, 0.0f}, {0.0f, kSmall, 0.0f},
        {kSmall * 0.25f, kSmall * 0.25f, 0.0f});
    EXPECT_EQ(rSmall.Region, BarycentricRegion::Interior);
}


