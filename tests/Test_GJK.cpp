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
