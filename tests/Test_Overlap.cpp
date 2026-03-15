// tests/Test_Overlap.cpp — Primitive overlap / intersection tests.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// Sphere vs Sphere
// ============================================================================

TEST(Overlap, SphereSphereOverlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(1.5f, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(Overlap, SphereSphereSeparated)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(3.0f, 0, 0), 1.0f};
    EXPECT_FALSE(TestOverlap(a, b));
}

TEST(Overlap, SphereSphereTouching)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(2.0f, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(Overlap, SphereSphereContained)
{
    Sphere a{glm::vec3(0, 0, 0), 5.0f};
    Sphere b{glm::vec3(1, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(a, b));
}

// ============================================================================
// AABB vs AABB
// ============================================================================

TEST(Overlap, AABBAABBOverlapping)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(0.5f, -1, -1), glm::vec3(2, 1, 1)};
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(Overlap, AABBAABBSeparated)
{
    AABB a{glm::vec3(-1), glm::vec3(1)};
    AABB b{glm::vec3(2, 2, 2), glm::vec3(3, 3, 3)};
    EXPECT_FALSE(TestOverlap(a, b));
}

// ============================================================================
// Sphere vs AABB
// ============================================================================

TEST(Overlap, SphereAABBOverlapping)
{
    Sphere s{glm::vec3(0, 0, 0), 1.5f};
    AABB box{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)};
    EXPECT_TRUE(TestOverlap(s, box));
}

TEST(Overlap, SphereAABBSeparated)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    AABB box{glm::vec3(2, 2, 2), glm::vec3(3, 3, 3)};
    EXPECT_FALSE(TestOverlap(s, box));
}

// ============================================================================
// Ray vs AABB
// ============================================================================

TEST(Overlap, RayAABBHit)
{
    Ray ray{glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1), glm::vec3(1)};
    EXPECT_TRUE(TestOverlap(ray, box));
}

TEST(Overlap, RayAABBMiss)
{
    Ray ray{glm::vec3(-5, 5, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1), glm::vec3(1)};
    EXPECT_FALSE(TestOverlap(ray, box));
}

TEST(Overlap, RayAABBInsideBox)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1), glm::vec3(1)};
    EXPECT_TRUE(TestOverlap(ray, box));
}

// ============================================================================
// Ray vs Sphere
// ============================================================================

TEST(Overlap, RaySphereHit)
{
    Ray ray{glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(ray, s));
}

TEST(Overlap, RaySphereMiss)
{
    Ray ray{glm::vec3(-5, 5, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_FALSE(TestOverlap(ray, s));
}

// ============================================================================
// Frustum vs Sphere
// ============================================================================

TEST(Overlap, FrustumSphereInside)
{
    // Create a simple frustum from a standard perspective-view matrix.
    const auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const auto frustum = Frustum::FromViewProjection(proj * view);

    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(frustum, s));
}

TEST(Overlap, FrustumSphereOutside)
{
    const auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    const auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const auto frustum = Frustum::FromViewProjection(proj * view);

    // Sphere far behind the camera.
    Sphere s{glm::vec3(0, 0, 200), 1.0f};
    EXPECT_FALSE(TestOverlap(frustum, s));
}

// ============================================================================
// Sphere vs Capsule
// ============================================================================

TEST(Overlap, SphereCapsuleOverlapping)
{
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    Capsule cap{glm::vec3(1.5f, 0, 0), glm::vec3(3, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(s, cap));
}

TEST(Overlap, SphereCapsuleSeparated)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    Capsule cap{glm::vec3(5, 0, 0), glm::vec3(10, 0, 0), 0.5f};
    EXPECT_FALSE(TestOverlap(s, cap));
}

// ============================================================================
// OBB vs Sphere
// ============================================================================

TEST(Overlap, OBBSphereOverlapping)
{
    OBB obb{glm::vec3(0, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1, 1, 1)};
    Sphere s{glm::vec3(1.5f, 0, 0), 1.0f};
    EXPECT_TRUE(TestOverlap(obb, s));
}

TEST(Overlap, OBBSphereSeparated)
{
    OBB obb{glm::vec3(0, 0, 0), glm::quat(1, 0, 0, 0), glm::vec3(1, 1, 1)};
    Sphere s{glm::vec3(5, 5, 5), 0.5f};
    EXPECT_FALSE(TestOverlap(obb, s));
}
