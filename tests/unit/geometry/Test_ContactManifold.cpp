// tests/Test_ContactManifold.cpp — Dedicated tests for Geometry::ComputeContact (TODO D18).
//
// Validates the contact manifold dispatcher: analytic solvers (Sphere-Sphere,
// Sphere-AABB), fallback GJK/EPA paths, and the ComputeContact dispatcher
// including symmetric argument ordering.
#include <gtest/gtest.h>

#include <cmath>
#include <type_traits>

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
// A->B Normal Convention (BUG-025)
//
// Every ComputeContact(a, b) path must return a Normal pointing from a to b.
// Pinned per analytic overload, through the reversed-argument dispatcher, and
// through the GJK/EPA fallback for sphere/capsule/OBB pairings in both
// argument orders.
// ============================================================================

namespace
{
    template <typename Shape>
    glm::vec3 ShapeCenter(const Shape& s)
    {
        if constexpr (std::is_same_v<Shape, Capsule>)
            return (s.PointA + s.PointB) * 0.5f;
        else if constexpr (std::is_same_v<Shape, AABB>)
            return (s.Min + s.Max) * 0.5f;
        else
            return s.Center;
    }

    // Convention invariant: Normal is unit length and points from a toward b
    // (positive projection on the center offset).
    template <typename A, typename B>
    void ExpectNormalPointsAToB(const A& a, const B& b, const ContactManifold& m)
    {
        EXPECT_NEAR(glm::length(m.Normal), 1.0f, kEps);
        const glm::vec3 centerDelta = ShapeCenter(b) - ShapeCenter(a);
        EXPECT_GT(glm::dot(m.Normal, centerDelta), 0.0f)
            << "Normal (" << m.Normal.x << ", " << m.Normal.y << ", " << m.Normal.z
            << ") must point from A toward B";
    }
}

TEST(ContactManifold, Convention_SphereAABB_SphereAboveBox)
{
    // The PHYSICS-003 repro: dynamic sphere resting on a static floor slab.
    // With the sphere as A, the convention requires Normal = (0, -1, 0).
    Sphere s{glm::vec3(0, 1.4f, 0), 0.5f};
    AABB box{glm::vec3(-5, 0, -5), glm::vec3(5, 1, 5)};
    auto contact = ComputeContact(s, box);

    ASSERT_TRUE(contact.has_value());
    ExpectNear(contact->Normal, glm::vec3(0, -1, 0));
    EXPECT_NEAR(contact->PenetrationDepth, 0.1f, kEps);
    ExpectNear(contact->ContactPointA, glm::vec3(0, 0.9f, 0)); // sphere surface
    ExpectNear(contact->ContactPointB, glm::vec3(0, 1.0f, 0)); // box top face
}

TEST(ContactManifold, Convention_SphereAABB_CenterInsideBox)
{
    // Deep-penetration branch: sphere center inside the box, nearest to the
    // top face. The sphere escapes upward, so the A->B normal points down.
    Sphere s{glm::vec3(0, 1.5f, 0), 0.25f};
    AABB box{glm::vec3(-2, 0, -2), glm::vec3(2, 2, 2)};
    auto contact = ComputeContact(s, box);

    ASSERT_TRUE(contact.has_value());
    ExpectNear(contact->Normal, glm::vec3(0, -1, 0));
    EXPECT_NEAR(contact->PenetrationDepth, 0.5f + 0.25f, kEps); // overlap + radius
    ExpectNear(contact->ContactPointB, glm::vec3(0, 2.0f, 0));  // nearest box face
    ExpectNear(contact->ContactPointA, glm::vec3(0, 1.25f, 0)); // deepest sphere point
}

TEST(ContactManifold, Convention_AABBSphere_ReversedDispatch)
{
    // Same scene with the box as A: the dispatcher swaps arguments, so the
    // normal must flip to point from the box toward the sphere.
    Sphere s{glm::vec3(0, 1.4f, 0), 0.5f};
    AABB box{glm::vec3(-5, 0, -5), glm::vec3(5, 1, 5)};
    auto contact = ComputeContact(box, s);

    ASSERT_TRUE(contact.has_value());
    ExpectNear(contact->Normal, glm::vec3(0, 1, 0));
    ExpectNear(contact->ContactPointA, glm::vec3(0, 1.0f, 0)); // box top face
    ExpectNear(contact->ContactPointB, glm::vec3(0, 0.9f, 0)); // sphere surface
}

TEST(ContactManifold, Convention_SphereSphere_BothOrders)
{
    Sphere lower{glm::vec3(0, 0, 0), 1.0f};
    Sphere upper{glm::vec3(0, 1.5f, 0), 1.0f};

    auto ab = ComputeContact(lower, upper);
    ASSERT_TRUE(ab.has_value());
    ExpectNear(ab->Normal, glm::vec3(0, 1, 0));

    auto ba = ComputeContact(upper, lower);
    ASSERT_TRUE(ba.has_value());
    ExpectNear(ba->Normal, glm::vec3(0, -1, 0));
}

TEST(ContactManifold, Convention_Fallback_SphereOBB_BothOrders)
{
    // The PHYSICS-003 fallback probe: sphere above an OBB, GJK/EPA path.
    Sphere s{glm::vec3(0, 1.4f, 0), 0.5f};
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(2, 1, 2), glm::quat(1, 0, 0, 0)};

    Core::Memory::LinearArena scratch(256 * 1024);
    auto sphereFirst = ComputeContact(s, obb, scratch);
    ASSERT_TRUE(sphereFirst.has_value());
    ExpectNormalPointsAToB(s, obb, *sphereFirst);
    EXPECT_NEAR(sphereFirst->Normal.y, -1.0f, 0.05f);

    scratch.Reset();
    auto obbFirst = ComputeContact(obb, s, scratch);
    ASSERT_TRUE(obbFirst.has_value());
    ExpectNormalPointsAToB(obb, s, *obbFirst);
    EXPECT_NEAR(obbFirst->Normal.y, 1.0f, 0.05f);
}

TEST(ContactManifold, Convention_Fallback_CapsuleOBB_BothOrders)
{
    // Upright capsule straddling the +X face of an OBB.
    Capsule cap{glm::vec3(1.8f, -0.5f, 0), glm::vec3(1.8f, 0.5f, 0), 0.5f};
    OBB obb{glm::vec3(0, 0, 0), glm::vec3(1.5f, 1.5f, 1.5f), glm::quat(1, 0, 0, 0)};

    Core::Memory::LinearArena scratch(256 * 1024);
    auto capFirst = ComputeContact(cap, obb, scratch);
    ASSERT_TRUE(capFirst.has_value());
    ExpectNormalPointsAToB(cap, obb, *capFirst);

    scratch.Reset();
    auto obbFirst = ComputeContact(obb, cap, scratch);
    ASSERT_TRUE(obbFirst.has_value());
    ExpectNormalPointsAToB(obb, cap, *obbFirst);
}

TEST(ContactManifold, Convention_Fallback_SphereCapsule_BothOrders)
{
    // Sphere beside an upright capsule, offset along +X from the capsule axis.
    Sphere s{glm::vec3(0.8f, 0, 0), 0.5f};
    Capsule cap{glm::vec3(0, -1, 0), glm::vec3(0, 1, 0), 0.5f};

    Core::Memory::LinearArena scratch(256 * 1024);
    auto sphereFirst = ComputeContact(s, cap, scratch);
    ASSERT_TRUE(sphereFirst.has_value());
    ExpectNormalPointsAToB(s, cap, *sphereFirst);

    scratch.Reset();
    auto capFirst = ComputeContact(cap, s, scratch);
    ASSERT_TRUE(capFirst.has_value());
    ExpectNormalPointsAToB(cap, s, *capFirst);
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

TEST(ContactManifold, RayCast_Sphere_CenterOriginHasFiniteFallbackNormal)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    auto hit = RayCast(ray, s);

    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 0.0f, kEps);
    ExpectNear(hit->Point, glm::vec3(0, 0, 0));
    EXPECT_TRUE(std::isfinite(hit->Normal.x));
    EXPECT_TRUE(std::isfinite(hit->Normal.y));
    EXPECT_TRUE(std::isfinite(hit->Normal.z));
    EXPECT_NEAR(glm::length(hit->Normal), 1.0f, kEps);
    ExpectNear(hit->Normal, glm::vec3(-1, 0, 0));
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

TEST(ContactManifold, RayCast_AABB_OnSlabBoundaryAxisParallel)
{
    const AABB box{glm::vec3(0, 0, 0), glm::vec3(1, 1, 1)};

    const auto minPlaneHit =
        RayCast(Ray{glm::vec3(0, 5, 0), glm::vec3(0, -1, 0)}, box);
    ASSERT_TRUE(minPlaneHit.has_value());
    EXPECT_NEAR(minPlaneHit->Distance, 4.0f, kEps);
    ExpectNear(minPlaneHit->Point, glm::vec3(0, 1, 0));

    const auto maxPlaneCornerHit =
        RayCast(Ray{glm::vec3(1, 5, 1), glm::vec3(0, -1, 0)}, box);
    ASSERT_TRUE(maxPlaneCornerHit.has_value());
    EXPECT_NEAR(maxPlaneCornerHit->Distance, 4.0f, kEps);
    ExpectNear(maxPlaneCornerHit->Point, glm::vec3(1, 1, 1));

    EXPECT_FALSE(RayCast(Ray{glm::vec3(0, 5, 2), glm::vec3(0, -1, 0)}, box).has_value());
}

TEST(ContactManifold, RayCast_AABB_InsideBox)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    AABB box{glm::vec3(-2, -2, -2), glm::vec3(2, 2, 2)};
    auto hit = RayCast(ray, box);
    // Origin inside box — should still return a hit
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 2.0f, kEps);
}
