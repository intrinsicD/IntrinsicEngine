// tests/Test_GJK.cpp — Dedicated tests for the GJK collision detection algorithm (TODO D18).
//
// Tests GJK_Boolean (overlap detection) and GJK_Intersection (simplex recovery)
// across primitive pairs including edge cases and degenerate configurations.
#include <gtest/gtest.h>

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
