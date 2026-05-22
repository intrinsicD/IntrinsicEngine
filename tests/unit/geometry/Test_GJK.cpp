// tests/Test_GJK.cpp — Dedicated tests for the GJK collision detection algorithm (TODO D18).
//
// Tests GJK_Boolean (overlap detection) and GJK_Intersection (simplex recovery)
// across primitive pairs including edge cases and degenerate configurations.
#include <gtest/gtest.h>

#include <array>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>

import Core;
import Geometry;

using namespace Geometry;

// ============================================================================
// GJK_Boolean — Basic Overlap Detection
// ============================================================================

TEST(GJK, SphereSphere_Overlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(1.5f, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch));
}

TEST(GJK, SphereSphere_Separated)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(5, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_FALSE(Internal::GJK_Boolean(a, b, scratch));
}

TEST(GJK, SphereSphere_Touching)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(2.0f, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    // Touching should be detected as overlap
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch));
}

TEST(GJK, SphereSphere_Concentric)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(0, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch));
}

TEST(GJK, SphereAABB_Overlapping)
{
    Sphere s{glm::vec3(0, 0, 0), 1.5f};
    AABB box{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(s, box, scratch));
}

TEST(GJK, SphereAABB_Separated)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    AABB box{glm::vec3(2, 2, 2), glm::vec3(3, 3, 3)};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_FALSE(Internal::GJK_Boolean(s, box, scratch));
}

TEST(GJK, AABBAABB_Overlapping)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(0.5f, -1, -1), glm::vec3(2, 1, 1)};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch));
}

TEST(GJK, AABBAABB_Separated)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(3, 3, 3), glm::vec3(5, 5, 5)};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_FALSE(Internal::GJK_Boolean(a, b, scratch));
}

// ============================================================================
// GJK_Boolean — OBB and Capsule
// ============================================================================

TEST(GJK, OBBSphere_Overlapping)
{
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), glm::quat(1, 0, 0, 0)};
    Sphere s{glm::vec3(1.5f, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(obb, s, scratch));
}

TEST(GJK, CapsuleSphere_Overlapping)
{
    Capsule cap{glm::vec3(0, 0, 0), glm::vec3(0, 5, 0), 1.0f};
    Sphere s{glm::vec3(1.5f, 2.5f, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(cap, s, scratch));
}

TEST(GJK, CapsuleCapsule_Separated)
{
    Capsule a{glm::vec3(0, 0, 0), glm::vec3(0, 5, 0), 0.5f};
    Capsule b{glm::vec3(5, 0, 0), glm::vec3(5, 5, 0), 0.5f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_FALSE(Internal::GJK_Boolean(a, b, scratch));
}

// ============================================================================
// GJK_Boolean — ConvexHull
// ============================================================================

TEST(GJK, ConvexHullSphere_Overlapping)
{
    ConvexHull hull;
    hull.Vertices = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}
    };
    Sphere s{glm::vec3(0.5f, 0.5f, 0.5f), 0.1f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(hull, s, scratch));
}

TEST(GJK, ConvexHullSphere_Separated)
{
    ConvexHull hull;
    hull.Vertices = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}
    };
    Sphere s{glm::vec3(5, 5, 5), 0.5f};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_FALSE(Internal::GJK_Boolean(hull, s, scratch));
}

// ============================================================================
// GJK_Intersection — Simplex Recovery
// ============================================================================

TEST(GJK, Intersection_ReturnsSimplex_WhenOverlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(1, 0, 0), 2.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    auto result = Internal::GJK_Intersection(a, b, scratch);
    EXPECT_TRUE(result.has_value());
    // Simplex should have 2-4 points
    EXPECT_GE(result->Size, 2);
    EXPECT_LE(result->Size, 4);
}

TEST(GJK, Intersection_ReturnsNullopt_WhenSeparated)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(5, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    auto result = Internal::GJK_Intersection(a, b, scratch);
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// GJK — Back-compat Overloads (no scratch arena)
// ============================================================================

TEST(GJK, BackCompat_Boolean_Overlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(1.0f, 0, 0), 1.0f};
    EXPECT_TRUE(Internal::GJK_Boolean(a, b));
}

TEST(GJK, BackCompat_Intersection_Overlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(1, 0, 0), 2.0f};
    auto result = Internal::GJK_Intersection(a, b);
    EXPECT_TRUE(result.has_value());
}

// ============================================================================
// GJK — Convergence / Iteration Limit
// ============================================================================

TEST(GJK, ConvergesWithinIterationLimit)
{
    // Use a complex pair: two oriented ellipsoids
    Ellipsoid a{glm::vec3(0, 0, 0), glm::vec3(3, 1, 1), glm::quat(1, 0, 0, 0)};
    Ellipsoid b{glm::vec3(2, 0, 0), glm::vec3(1, 3, 1), glm::quat(1, 0, 0, 0)};
    Core::Memory::LinearArena scratch(8 * 1024);
    // Should detect overlap (elongated ellipsoids touching)
    auto result = Internal::GJK_Boolean(a, b, scratch);
    EXPECT_TRUE(result);
}

// ============================================================================
// GJK — Scale Invariance
// ============================================================================

TEST(GJK, ScaleInvariance_VerySmallObjects)
{
    // Two tiny spheres at 1e-3 scale — overlap and separation must be correct.
    constexpr float s = 1e-3f;
    Sphere a{glm::vec3(0, 0, 0), s};
    Sphere b{glm::vec3(1.5f * s, 0, 0), s};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch)) << "Small overlapping spheres must be detected";

    Sphere c{glm::vec3(5.0f * s, 0, 0), s};
    EXPECT_FALSE(Internal::GJK_Boolean(a, c, scratch)) << "Small separated spheres must not overlap";
}

TEST(GJK, ScaleInvariance_VeryLargeObjects)
{
    // Two large spheres at 1e3 scale.
    constexpr float s = 1e3f;
    Sphere a{glm::vec3(0, 0, 0), s};
    Sphere b{glm::vec3(1.5f * s, 0, 0), s};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch)) << "Large overlapping spheres must be detected";

    Sphere c{glm::vec3(5.0f * s, 0, 0), s};
    EXPECT_FALSE(Internal::GJK_Boolean(a, c, scratch)) << "Large separated spheres must not overlap";
}

TEST(GJK, ScaleInvariance_TinyAABBs)
{
    constexpr float s = 1e-4f;
    AABB a{glm::vec3(-s), glm::vec3(s)};
    AABB b{glm::vec3(0.5f * s, -s, -s), glm::vec3(3 * s, s, s)};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch)) << "Tiny overlapping AABBs must be detected";

    AABB c{glm::vec3(5 * s, 5 * s, 5 * s), glm::vec3(7 * s, 7 * s, 7 * s)};
    EXPECT_FALSE(Internal::GJK_Boolean(a, c, scratch)) << "Tiny separated AABBs must not overlap";
}

TEST(GJK, ScaleInvariance_HugeAABBs)
{
    constexpr float s = 1e4f;
    AABB a{glm::vec3(-s), glm::vec3(s)};
    AABB b{glm::vec3(0.5f * s, -s, -s), glm::vec3(3 * s, s, s)};
    Core::Memory::LinearArena scratch(8 * 1024);
    EXPECT_TRUE(Internal::GJK_Boolean(a, b, scratch)) << "Huge overlapping AABBs must be detected";

    AABB c{glm::vec3(5 * s, 5 * s, 5 * s), glm::vec3(7 * s, 7 * s, 7 * s)};
    EXPECT_FALSE(Internal::GJK_Boolean(a, c, scratch)) << "Huge separated AABBs must not overlap";
}

TEST(GJK, ScaleInvariance_Intersection_TinyOverlap)
{
    // GJK_Intersection must also work at small scales.
    constexpr float s = 1e-3f;
    Sphere a{glm::vec3(0, 0, 0), 2.0f * s};
    Sphere b{glm::vec3(s, 0, 0), 2.0f * s};
    Core::Memory::LinearArena scratch(8 * 1024);
    auto result = Internal::GJK_Intersection(a, b, scratch);
    EXPECT_TRUE(result.has_value()) << "Intersection must succeed for small overlapping spheres";
}

// ============================================================================
// EPA — Scale Invariance and Degenerate Face Guard
// ============================================================================

TEST(GJK, EPA_ScaleInvariance_TinyPenetration)
{
    // Two tiny overlapping OBBs — EPA must produce valid depth at small scales.
    // OBBs are better conditioned than tetrahedra for GJK simplex construction.
    constexpr float s = 1e-3f;
    OBB a{glm::vec3(0, 0, 0), glm::vec3(s, s, s), glm::quat(1, 0, 0, 0)};
    OBB b{glm::vec3(0.5f * s, 0, 0), glm::vec3(s, s, s), glm::quat(1, 0, 0, 0)};

    Core::Memory::LinearArena scratch(256 * 1024);
    auto contact = ComputeContact(a, b, scratch);
    ASSERT_TRUE(contact.has_value()) << "EPA must produce contact for tiny overlapping OBBs";
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

TEST(GJK, EPA_ScaleInvariance_LargePenetration)
{
    // Two large overlapping tetrahedra.
    constexpr float s = 1e3f;
    ConvexHull a;
    a.Vertices = {
        {0, 0, 0}, {s, 0, 0}, {0, s, 0}, {0, 0, s}
    };
    ConvexHull b;
    b.Vertices = a.Vertices;
    for (auto& v : b.Vertices) v += glm::vec3(0.15f * s);

    Core::Memory::LinearArena scratch(256 * 1024);
    auto contact = ComputeContact(a, b, scratch);
    ASSERT_TRUE(contact.has_value()) << "EPA must produce contact for large overlapping hulls";
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

// ============================================================================
// GJK — Termination Diagnostics (GEOM-015 Slice 4)
//
// The diagnostic-bearing overloads must surface a TerminationReason and
// iteration count without changing the boolean outcome of the existing
// entry points. Tests below exercise each reason that is deterministically
// reachable on well-conditioned inputs and assert iteration budgets.
// ============================================================================

TEST(GJK, Diagnostics_Converged_OnOverlappingSpheres)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(1.5f, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    Internal::GJKDiagnostics diag;
    const bool overlap = Internal::GJK_Boolean(a, b, scratch, diag);
    EXPECT_TRUE(overlap);
    EXPECT_EQ(diag.reason, Internal::TerminationReason::Converged);
    EXPECT_GE(diag.iterations, 0);
    EXPECT_LT(diag.iterations, Internal::Config::GJK_MAX_ITERATIONS);
}

TEST(GJK, Diagnostics_EarlyOutNegativeSupport_OnSeparatedSpheres)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(5, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    Internal::GJKDiagnostics diag;
    const bool overlap = Internal::GJK_Boolean(a, b, scratch, diag);
    EXPECT_FALSE(overlap);
    EXPECT_EQ(diag.reason, Internal::TerminationReason::EarlyOutNegativeSupport);
    EXPECT_GT(diag.iterations, 0);
    EXPECT_LT(diag.iterations, Internal::Config::GJK_MAX_ITERATIONS);
}

TEST(GJK, Diagnostics_EarlyOutNegativeSupport_OnSeparatedAABBs)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(3, 3, 3), glm::vec3(5, 5, 5)};
    Core::Memory::LinearArena scratch(8 * 1024);
    Internal::GJKDiagnostics diag;
    const bool overlap = Internal::GJK_Boolean(a, b, scratch, diag);
    EXPECT_FALSE(overlap);
    EXPECT_EQ(diag.reason, Internal::TerminationReason::EarlyOutNegativeSupport);
}

TEST(GJK, Diagnostics_Intersection_Converged_OnOverlap)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(1, 0, 0), 2.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    Internal::GJKDiagnostics diag;
    auto result = Internal::GJK_Intersection(a, b, scratch, diag);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(diag.reason, Internal::TerminationReason::Converged);
    EXPECT_LT(diag.iterations, Internal::Config::GJK_MAX_ITERATIONS);
}

TEST(GJK, Diagnostics_Intersection_EarlyOutNegativeSupport_OnSeparated)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(5, 0, 0), 1.0f};
    Core::Memory::LinearArena scratch(8 * 1024);
    Internal::GJKDiagnostics diag;
    auto result = Internal::GJK_Intersection(a, b, scratch, diag);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(diag.reason, Internal::TerminationReason::EarlyOutNegativeSupport);
}

TEST(GJK, Diagnostics_DefaultConstructedFieldsAreSane)
{
    // The struct must default to a non-iterating Converged state so callers
    // that forget to invoke a diagnostic overload still read sensible values.
    Internal::GJKDiagnostics diag;
    EXPECT_EQ(diag.iterations, 0);
    EXPECT_EQ(diag.reason, Internal::TerminationReason::Converged);
}

TEST(GJK, Diagnostics_BooleanWrapper_StillMatchesDiagnosticOverload)
{
    // The thin wrapper must produce the same boolean as the diagnostic
    // overload across overlapping and separated pairs at varied scales.
    struct Pair { Sphere a; Sphere b; bool expectOverlap; };
    const std::array<Pair, 4> cases = {{
        {{glm::vec3(0), 1.0f},          {glm::vec3(1.5f, 0, 0), 1.0f},        true},
        {{glm::vec3(0), 1.0f},          {glm::vec3(5, 0, 0), 1.0f},           false},
        {{glm::vec3(0), 1e-3f},         {glm::vec3(1.5e-3f, 0, 0), 1e-3f},    true},
        {{glm::vec3(0), 1e3f},          {glm::vec3(5e3f, 0, 0), 1e3f},        false},
    }};
    for (const auto& c : cases)
    {
        Core::Memory::LinearArena scratch(8 * 1024);
        const bool wrapperResult = Internal::GJK_Boolean(c.a, c.b, scratch);
        Internal::GJKDiagnostics diag;
        const bool diagResult = Internal::GJK_Boolean(c.a, c.b, scratch, diag);
        EXPECT_EQ(wrapperResult, diagResult);
        EXPECT_EQ(wrapperResult, c.expectOverlap);
    }
}

// ============================================================================
// GJK — Convergence Budget (GEOM-015 Slice 4)
//
// On the standard primitive corpus the iteration budget must comfortably
// exceed observed iteration counts: MaxIterationsHit indicates either a
// support-function regression or a tolerance miscalibration.
// ============================================================================

TEST(GJK, Diagnostics_ConvergenceBudget_NotExhaustedOnStandardCorpus)
{
    // A representative sample of well-conditioned overlapping and separated
    // pairs from the existing test suite. None should approach the iteration
    // budget; an empirically generous upper bound is asserted here so that a
    // future tolerance regression that doubles iteration counts is caught.
    constexpr int kPracticalIterationBudget = 32;
    Core::Memory::LinearArena scratch(8 * 1024);

    auto check = [&](auto a, auto b) {
        Internal::GJKDiagnostics diag;
        (void)Internal::GJK_Boolean(a, b, scratch, diag);
        EXPECT_NE(diag.reason, Internal::TerminationReason::MaxIterationsHit);
        EXPECT_LE(diag.iterations, kPracticalIterationBudget);
    };

    check(Sphere{glm::vec3(0), 1.0f},                   Sphere{glm::vec3(1.5f, 0, 0), 1.0f});
    check(Sphere{glm::vec3(0), 1.0f},                   Sphere{glm::vec3(5, 0, 0), 1.0f});
    check(Sphere{glm::vec3(0), 2.0f},                   Sphere{glm::vec3(0), 1.0f});
    check(Sphere{glm::vec3(0), 1.5f},                   AABB{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)});
    check(AABB{glm::vec3(-1), glm::vec3(1)},            AABB{glm::vec3(0.5f, -1, -1), glm::vec3(2, 1, 1)});
    check(AABB{glm::vec3(-1), glm::vec3(1)},            AABB{glm::vec3(3, 3, 3), glm::vec3(5, 5, 5)});
    check(OBB{glm::vec3(0), glm::vec3(1), glm::quat(1, 0, 0, 0)},
          Sphere{glm::vec3(1.5f, 0, 0), 1.0f});
    check(Capsule{glm::vec3(0), glm::vec3(0, 5, 0), 1.0f},
          Sphere{glm::vec3(1.5f, 2.5f, 0), 1.0f});
    check(Capsule{glm::vec3(0), glm::vec3(0, 5, 0), 0.5f},
          Capsule{glm::vec3(5, 0, 0), glm::vec3(5, 5, 0), 0.5f});
    check(Ellipsoid{glm::vec3(0), glm::vec3(3, 1, 1), glm::quat(1, 0, 0, 0)},
          Ellipsoid{glm::vec3(2, 0, 0), glm::vec3(1, 3, 1), glm::quat(1, 0, 0, 0)});
}

// ============================================================================
// GJK — Parity Across Scales (GEOM-015 Slice 4)
//
// The normalized-workspace contract (Slice 3) says the boolean outcome of
// GJK must be invariant under uniform scaling of the input. The parity
// battery below picks pairs whose unit-scale outcome is known and asserts
// the same outcome at small (~1e-3) and large (~1e3) shape sizes.
// ============================================================================

TEST(GJK, Parity_BooleanOutcomeAcrossScales)
{
    struct UnitCase { glm::vec3 centerB; float radius; bool expectOverlap; };
    const std::array<UnitCase, 4> unitCases = {{
        {glm::vec3(1.5f, 0, 0), 1.0f, true},   // overlapping
        {glm::vec3(2.0f, 0, 0), 1.0f, true},   // touching
        {glm::vec3(3.0f, 0, 0), 1.0f, false},  // separated
        {glm::vec3(5.0f, 0, 0), 1.0f, false},  // far apart
    }};
    const std::array<float, 5> scales = {1e-3f, 1e-1f, 1.0f, 1e1f, 1e3f};

    Core::Memory::LinearArena scratch(8 * 1024);
    for (float s : scales)
    {
        for (const auto& c : unitCases)
        {
            Sphere a{glm::vec3(0), c.radius * s};
            Sphere b{c.centerB * s, c.radius * s};
            const bool result = Internal::GJK_Boolean(a, b, scratch);
            EXPECT_EQ(result, c.expectOverlap)
                << "Scale " << s << " centerB=" << c.centerB.x << " expected " << c.expectOverlap;
        }
    }
}

TEST(GJK, Parity_NearTouchingSeparation_PreviouslyFlippedScales)
{
    // Two unit spheres separated by a small fraction past their touching
    // configuration. At unit scale the normalized-workspace contract makes
    // this decidable; at extreme scales the legacy (pre-Slice-2) policy was
    // known to flip on `Geometry.Support`-side guards. With Slice 2 in
    // place, the GJK driver's normalized workspace must produce a
    // consistent "separated" outcome across the full scale range.
    Core::Memory::LinearArena scratch(8 * 1024);
    const std::array<float, 5> scales = {1e-3f, 1e-1f, 1.0f, 1e1f, 1e3f};
    for (float s : scales)
    {
        // separation = 2 * radius + small_gap; pick a gap large enough to be
        // resolved at unit scale (well above the normalized 1e-6 floor) but
        // small enough to exercise the previously-fragile regime.
        const float radius = 1.0f * s;
        const float gap = 1e-3f * s;
        Sphere a{glm::vec3(0), radius};
        Sphere b{glm::vec3(2.0f * radius + gap, 0, 0), radius};
        Internal::GJKDiagnostics diag;
        const bool overlap = Internal::GJK_Boolean(a, b, scratch, diag);
        EXPECT_FALSE(overlap)
            << "Scale " << s << ": gap=" << gap << " radius=" << radius
            << " should report separated";
        EXPECT_NE(diag.reason, Internal::TerminationReason::MaxIterationsHit)
            << "Scale " << s << ": did not converge within iteration budget";
    }
}

TEST(GJK, Parity_TouchingSpheres_OverlapAcrossScales)
{
    // Touching spheres (centers exactly 2*r apart) are a canonical
    // boundary case. At every scale the driver must report overlap (the
    // surfaces share a point) and converge within the iteration budget.
    Core::Memory::LinearArena scratch(8 * 1024);
    const std::array<float, 5> scales = {1e-3f, 1e-1f, 1.0f, 1e1f, 1e3f};
    for (float s : scales)
    {
        Sphere a{glm::vec3(0), s};
        Sphere b{glm::vec3(2.0f * s, 0, 0), s};
        Internal::GJKDiagnostics diag;
        const bool overlap = Internal::GJK_Boolean(a, b, scratch, diag);
        EXPECT_TRUE(overlap) << "Scale " << s << ": touching spheres must overlap";
        EXPECT_NE(diag.reason, Internal::TerminationReason::MaxIterationsHit)
            << "Scale " << s << ": did not converge within iteration budget";
    }
}
