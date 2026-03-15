// tests/Test_Containment.cpp — Dedicated tests for Geometry::Contains (TODO D18).
//
// Validates analytic containment checks and fallback vertex-enumeration paths
// for various primitive pair combinations.
#include <gtest/gtest.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// Sphere contains Sphere
// ============================================================================

TEST(Containment, SphereSphere_FullyContained)
{
    Sphere outer{glm::vec3(0, 0, 0), 5.0f};
    Sphere inner{glm::vec3(1, 0, 0), 1.0f};
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_PartialOverlap)
{
    Sphere outer{glm::vec3(0, 0, 0), 2.0f};
    Sphere inner{glm::vec3(1.5f, 0, 0), 1.0f};
    // Inner extends beyond outer: 1.5 + 1.0 = 2.5 > 2.0
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_NoOverlap)
{
    Sphere outer{glm::vec3(0, 0, 0), 1.0f};
    Sphere inner{glm::vec3(5, 0, 0), 1.0f};
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_Concentric)
{
    Sphere outer{glm::vec3(0, 0, 0), 3.0f};
    Sphere inner{glm::vec3(0, 0, 0), 2.0f};
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_PointContainment)
{
    Sphere outer{glm::vec3(0, 0, 0), 2.0f};
    Sphere inner{glm::vec3(1, 0, 0), 0.0f}; // Point (zero radius)
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereSphere_PointOutside)
{
    Sphere outer{glm::vec3(0, 0, 0), 1.0f};
    Sphere inner{glm::vec3(5, 0, 0), 0.0f};
    EXPECT_FALSE(Contains(outer, inner));
}

// ============================================================================
// AABB contains point
// ============================================================================

TEST(Containment, AABBPoint_Inside)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(box, glm::vec3(0, 0, 0)));
}

TEST(Containment, AABBPoint_OnBoundary)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(box, glm::vec3(1, 0, 0)));
}

TEST(Containment, AABBPoint_Outside)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_FALSE(Contains(box, glm::vec3(2, 0, 0)));
}

// ============================================================================
// AABB contains AABB
// ============================================================================

TEST(Containment, AABBAABB_FullyContained)
{
    AABB outer{glm::vec3(-5, -5, -5), glm::vec3(5, 5, 5)};
    AABB inner{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, AABBAABB_PartialOverlap)
{
    AABB outer{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    AABB inner{glm::vec3(0, 0, 0), glm::vec3(2, 2, 2)};
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(Containment, AABBAABB_Equal)
{
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(Contains(box, box));
}

// ============================================================================
// Sphere contains AABB
// ============================================================================

TEST(Containment, SphereAABB_FullyContained)
{
    Sphere outer{glm::vec3(0, 0, 0), 5.0f};
    AABB inner{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    // Distance from center to corner = sqrt(3) ≈ 1.73 < 5.0
    EXPECT_TRUE(Contains(outer, inner));
}

TEST(Containment, SphereAABB_PartialOverlap)
{
    Sphere outer{glm::vec3(0, 0, 0), 1.5f};
    AABB inner{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    // Corner distance sqrt(3) ≈ 1.73 > 1.5
    EXPECT_FALSE(Contains(outer, inner));
}

// ============================================================================
// Frustum contains AABB
// ============================================================================

TEST(Containment, FrustumAABB_Inside)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::FromViewProjection(proj * view);

    // Small box near the camera's look-at target
    AABB box{glm::vec3(-0.1f, -0.1f, -0.1f), glm::vec3(0.1f, 0.1f, 0.1f)};
    EXPECT_TRUE(Contains(frustum, box));
}

TEST(Containment, FrustumAABB_Outside)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::FromViewProjection(proj * view);

    // Box behind camera
    AABB box{glm::vec3(-1, -1, 50), glm::vec3(1, 1, 55)};
    EXPECT_FALSE(Contains(frustum, box));
}

// ============================================================================
// Frustum contains Sphere
// ============================================================================

TEST(Containment, FrustumSphere_Inside)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::FromViewProjection(proj * view);

    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    EXPECT_TRUE(Contains(frustum, s));
}

TEST(Containment, FrustumSphere_StraddlingPlane)
{
    auto proj = glm::perspective(glm::radians(45.0f), 1.0f, 1.0f, 10.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::FromViewProjection(proj * view);

    // Very large sphere centered at origin — will straddle planes
    Sphere s{glm::vec3(0, 0, 0), 100.0f};
    EXPECT_FALSE(Contains(frustum, s));
}
