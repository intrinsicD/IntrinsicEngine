// tests/Test_ContactManifold.cpp — Dedicated tests for Geometry::ComputeContact (TODO D18).
//
// Validates the contact manifold dispatcher: analytic solvers (Sphere-Sphere,
// Sphere-AABB), fallback GJK/EPA paths, and the ComputeContact dispatcher
// including symmetric argument ordering.
#include <gtest/gtest.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

import Core;
import Geometry;

using namespace Geometry;

static constexpr float kEps = 1e-3f;

static void ExpectNear(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// ============================================================================
// Analytic Sphere-Sphere Contact
// ============================================================================

TEST(ContactManifold, SphereSphere_Overlapping)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(3, 0, 0), 2.0f};
    auto contact = ComputeContact(a, b);

    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
    EXPECT_NEAR(contact->PenetrationDepth, 1.0f, kEps); // (2+2) - 3 = 1

    // Normal should point from A to B (+X direction)
    EXPECT_NEAR(contact->Normal.x, 1.0f, kEps);
    EXPECT_NEAR(contact->Normal.y, 0.0f, kEps);
    EXPECT_NEAR(contact->Normal.z, 0.0f, kEps);

    // Contact point on A: A.center + normal * A.radius
    ExpectNear(contact->ContactPointA, glm::vec3(2, 0, 0));
    // Contact point on B: B.center - normal * B.radius
    ExpectNear(contact->ContactPointB, glm::vec3(1, 0, 0));
}

TEST(ContactManifold, SphereSphere_Separated)
{
    Sphere a{glm::vec3(0, 0, 0), 1.0f};
    Sphere b{glm::vec3(5, 0, 0), 1.0f};
    auto contact = ComputeContact(a, b);
    EXPECT_FALSE(contact.has_value());
}

TEST(ContactManifold, SphereSphere_Concentric)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(0, 0, 0), 1.0f};
    auto contact = ComputeContact(a, b);

    ASSERT_TRUE(contact.has_value());
    EXPECT_NEAR(contact->PenetrationDepth, 3.0f, kEps); // 2 + 1
    // Normal should be a valid unit vector (fallback direction)
    EXPECT_NEAR(glm::length(contact->Normal), 1.0f, kEps);
}

TEST(ContactManifold, SphereSphere_DiagonalOverlap)
{
    Sphere a{glm::vec3(0, 0, 0), 2.0f};
    Sphere b{glm::vec3(1, 1, 1), 2.0f};
    auto contact = ComputeContact(a, b);

    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
    // Normal should point from A to B (diagonal direction)
    glm::vec3 expectedDir = glm::normalize(glm::vec3(1, 1, 1));
    EXPECT_NEAR(contact->Normal.x, expectedDir.x, kEps);
    EXPECT_NEAR(contact->Normal.y, expectedDir.y, kEps);
    EXPECT_NEAR(contact->Normal.z, expectedDir.z, kEps);
}

// ============================================================================
// Analytic Sphere-AABB Contact
// ============================================================================

TEST(ContactManifold, SphereAABB_Overlapping)
{
    Sphere s{glm::vec3(0, 0, 0), 1.5f};
    AABB box{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)};
    auto contact = ComputeContact(s, box);

    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

TEST(ContactManifold, SphereAABB_Separated)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    AABB box{glm::vec3(2, 2, 2), glm::vec3(3, 3, 3)};
    auto contact = ComputeContact(s, box);
    EXPECT_FALSE(contact.has_value());
}

TEST(ContactManifold, SphereAABB_CenterInsideBox)
{
    Sphere s{glm::vec3(0, 0, 0), 0.5f};
    AABB box{glm::vec3(-2, -2, -2), glm::vec3(2, 2, 2)};
    auto contact = ComputeContact(s, box);

    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

// ============================================================================
// Symmetric Dispatcher — Argument Order
// ============================================================================

TEST(ContactManifold, AABBSphere_SymmetricDispatch)
{
    // ComputeContact(AABB, Sphere) should work via reversed-argument dispatch
    AABB box{glm::vec3(1, -1, -1), glm::vec3(3, 1, 1)};
    Sphere s{glm::vec3(0, 0, 0), 1.5f};
    auto contact = ComputeContact(box, s);

    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
    // Normal direction should be flipped compared to (Sphere, AABB)
    auto contactAB = ComputeContact(s, box);
    ASSERT_TRUE(contactAB.has_value());
    EXPECT_NEAR(glm::dot(contact->Normal, contactAB->Normal), -1.0f, 0.1f);
}

// ============================================================================
// GJK/EPA Fallback Path
// ============================================================================

TEST(ContactManifold, ConvexHullSphere_Fallback)
{
    ConvexHull hull;
    hull.Vertices = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}
    };
    Sphere s{glm::vec3(0.5f, 0.5f, 0.5f), 0.1f};

    Core::Memory::LinearArena scratch(64 * 1024);
    auto contact = ComputeContact(hull, s, scratch);

    // Hull fully contains the sphere — contact expected
    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

TEST(ContactManifold, OBBCapsule_Fallback)
{
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(2, 2, 2), glm::quat(1, 0, 0, 0)};
    Capsule cap{glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), 0.5f};

    Core::Memory::LinearArena scratch(64 * 1024);
    auto contact = ComputeContact(obb, cap, scratch);

    ASSERT_TRUE(contact.has_value());
    EXPECT_GT(contact->PenetrationDepth, 0.0f);
}

TEST(ContactManifold, CapsuleCapsule_Separated_NoContact)
{
    Capsule a{glm::vec3(0, 0, 0), glm::vec3(0, 3, 0), 0.5f};
    Capsule b{glm::vec3(10, 0, 0), glm::vec3(10, 3, 0), 0.5f};

    auto contact = ComputeContact(a, b);
    EXPECT_FALSE(contact.has_value());
}

// ============================================================================
// RayCast
// ============================================================================

TEST(ContactManifold, RayCast_Sphere_Hit)
{
    Ray ray{glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    auto hit = RayCast(ray, s);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 4.0f, kEps); // Hit at x=-1
    ExpectNear(hit->Point, glm::vec3(-1, 0, 0));
    ExpectNear(hit->Normal, glm::vec3(-1, 0, 0));
}

TEST(ContactManifold, RayCast_Sphere_Miss)
{
    Ray ray{glm::vec3(-5, 5, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    auto hit = RayCast(ray, s);
    EXPECT_FALSE(hit.has_value());
}

TEST(ContactManifold, RayCast_AABB_Hit)
{
    Ray ray{glm::vec3(-5, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    auto hit = RayCast(ray, box);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 4.0f, kEps);
    ExpectNear(hit->Point, glm::vec3(-1, 0, 0));
}

TEST(ContactManifold, RayCast_AABB_Miss)
{
    Ray ray{glm::vec3(-5, 5, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    auto hit = RayCast(ray, box);
    EXPECT_FALSE(hit.has_value());
}

TEST(ContactManifold, RayCast_AABB_InsideBox)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-2, -2, -2), glm::vec3(2, 2, 2)};
    auto hit = RayCast(ray, box);
    // Origin inside box — should still return a hit
    ASSERT_TRUE(hit.has_value());
}
