#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp> // For debugging vector output

import Runtime.Geometry.Primitives;
import Runtime.Geometry.Overlap;
import Runtime.Geometry.Containment;
import Runtime.Geometry.Contact;

using namespace Runtime::Geometry;

// Helper to assert vectors are close
void ExpectVec3Eq(const glm::vec3& a, const glm::vec3& b, float epsilon = 0.001f)
{
    EXPECT_NEAR(a.x, b.x, epsilon) << "Vectors differ. A=" << glm::to_string(a) << " B=" << glm::to_string(b);
    EXPECT_NEAR(a.y, b.y, epsilon);
    EXPECT_NEAR(a.z, b.z, epsilon);
}

// =========================================================================
// 1. PRIMITIVE SUPPORT TESTS (Crucial for GJK correctness)
// =========================================================================

TEST(GeometryPrimitives, AABB_Support)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};

    // Test Cardinals
    ExpectVec3Eq(box.Support({1, 0, 0}), {1, -1, -1}); // Implementation specific: checks > 0
    ExpectVec3Eq(box.Support({-1, 0, 0}), {-1, -1, -1});
    ExpectVec3Eq(box.Support({0, 1, 0}), {-1, 1, -1});

    // Test Diagonal
    ExpectVec3Eq(box.Support({1, 1, 1}), {1, 1, 1});
}

TEST(GeometryPrimitives, Sphere_Support)
{
    Sphere s{{0, 0, 0}, 1.0f};

    ExpectVec3Eq(s.Support({1, 0, 0}), {1, 0, 0});
    ExpectVec3Eq(s.Support({0, 1, 0}), {0, 1, 0});

    // Normalized direction check
    glm::vec3 dir = glm::normalize(glm::vec3(1, 1, 0));
    ExpectVec3Eq(s.Support({1, 1, 0}), dir);
}

TEST(GeometryPrimitives, Cylinder_Support)
{
    Cylinder cyl{{0, 0, 0}, {0, 2, 0}, 1.0f}; // Height 2 along Y, Radius 1

    // Support along Y axis (Cap)
    ExpectVec3Eq(cyl.Support({0, 1, 0}), {0, 2, 0});
    ExpectVec3Eq(cyl.Support({0, -1, 0}), {0, 0, 0});

    // Support perpendicular (Side)
    ExpectVec3Eq(cyl.Support({1, 0, 0}), {1, 0, 0}); // Bottom circle edge

    // Support diagonal (Rim)
    glm::vec3 diag = glm::normalize(glm::vec3(1, 1, 0));
    glm::vec3 result = cyl.Support(diag);

    // It should pick the top cap ({0,2,0}) pushed out by radius in X ({1,0,0})
    ExpectVec3Eq(result, {1, 2, 0});
}

// =========================================================================
// 2. OVERLAP TESTS (Analytic & Fallback)
// =========================================================================

TEST(GeometryOverlap, Analytic_SphereSphere)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{1.5f, 0, 0}, 1.0f}; // Overlapping
    Sphere s3{{2.1f, 0, 0}, 1.0f}; // Disjoint

    EXPECT_TRUE(TestOverlap(s1, s2));
    EXPECT_FALSE(TestOverlap(s1, s3));
}

TEST(GeometryOverlap, Analytic_AABBAABB)
{
    AABB b1{{0, 0, 0}, {2, 2, 2}};
    AABB b2{{1, 1, 1}, {3, 3, 3}}; // Intersects center
    AABB b3{{3, 3, 3}, {4, 4, 4}}; // Disjoint

    EXPECT_TRUE(TestOverlap(b1, b2));
    EXPECT_FALSE(TestOverlap(b1, b3));
}

TEST(GeometryOverlap, Analytic_SphereAABB)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Sphere sInside{{0, 0, 0}, 0.5f};
    Sphere sTouch{{0, 1.5f, 0}, 0.6f}; // Center at 1.5, box max Y is 1.0. Gap is 0.5. Radius 0.6. Overlaps.
    Sphere sFar{{0, 5, 0}, 1.0f};

    EXPECT_TRUE(TestOverlap(sInside, box));
    EXPECT_TRUE(TestOverlap(sTouch, box));
    EXPECT_FALSE(TestOverlap(sFar, box));
}

TEST(GeometryOverlap, Fallback_GJK_CapsuleSphere)
{
    // We assume Overlap_Analytic(Capsule, Sphere) is NOT implemented, so this hits GJK.
    Capsule cap{{-2, 0, 0}, {2, 0, 0}, 0.5f}; // Long capsule along X
    Sphere sHit{{0, 0.8f, 0}, 0.5f}; // Just touching (dist 0.8, radii sum 1.0)
    Sphere sMiss{{0, 2.0f, 0}, 0.5f};

    EXPECT_TRUE(TestOverlap(cap, sHit));
    EXPECT_FALSE(TestOverlap(cap, sMiss));
}

TEST(GeometryOverlap, Fallback_GJK_AABBOBB)
{
    // AABB vs Oriented Box
    AABB aabb{{-1, -1, -1}, {1, 1, 1}};

    OBB obb;
    obb.Center = {2, 0, 0};
    obb.Extents = {0.5f, 0.5f, 0.5f};
    obb.Rotation = glm::quat(glm::vec3(0, 0, 0)); // Identity

    // Touching exactly at x=1 (AABB Max) and x=1.5 (OBB Min). Gap 0.5. No overlap.
    EXPECT_FALSE(TestOverlap(aabb, obb));

    obb.Center = {1.2f, 0, 0}; // Move closer. 1.2 - 0.5 = 0.7 (Min X). AABB Max X = 1.0. Overlap!
    EXPECT_TRUE(TestOverlap(aabb, obb));

    // Rotate OBB 45 degrees
    obb.Center = {2.0f, 0, 0};
    obb.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));
    // Check non-overlap
    EXPECT_FALSE(TestOverlap(aabb, obb));
}

// =========================================================================
// 3. CONTAINMENT TESTS (Strict)
// =========================================================================

TEST(GeometryContainment, AABB_Point)
{
    AABB box{{0, 0, 0}, {10, 10, 10}};

    EXPECT_TRUE(Contains(box, glm::vec3(5,5,5)));
    EXPECT_TRUE(Contains(box, glm::vec3(0,0,0))); // Boundary inclusive
    EXPECT_FALSE(Contains(box, glm::vec3(-1,5,5)));
}

TEST(GeometryContainment, Sphere_Sphere)
{
    Sphere outer{{0, 0, 0}, 10.0f};
    Sphere inner{{2, 0, 0}, 1.0f};
    Sphere intersect{{9, 0, 0}, 2.0f}; // Extends to 11.0. Not contained.

    EXPECT_TRUE(Contains(outer, inner));
    EXPECT_FALSE(Contains(outer, intersect));
}

TEST(GeometryContainment, AABB_AABB)
{
    AABB outer{{0, 0, 0}, {10, 10, 10}};
    AABB inner{{2, 2, 2}, {8, 8, 8}};
    AABB crossing{{8, 8, 8}, {12, 12, 12}};

    EXPECT_TRUE(Contains(outer, inner));
    EXPECT_FALSE(Contains(outer, crossing));
}

TEST(GeometryContainment, Sphere_AABB)
{
    Sphere s{{0, 0, 0}, 2.0f};
    AABB box{{-1, -1, -1}, {1, 1, 1}}; // Diagonal length is sqrt(3) ~ 1.73 < 2.0. Inside.
    AABB bigBox{{-1.5f, -1.5f, -1.5f}, {1.5f, 1.5f, 1.5f}}; // Diag > 2.0. Outside.

    EXPECT_TRUE(Contains(s, box));
    EXPECT_FALSE(Contains(s, bigBox));
}

// =========================================================================
// 4. CONTACT TESTS (Manifold Generation)
// =========================================================================

TEST(GeometryContact, Analytic_SphereSphere)
{
    Sphere sA{{0, 0, 0}, 1.0f};
    Sphere sB{{1.5f, 0, 0}, 1.0f};

    auto result = ComputeContact(sA, sB);
    ASSERT_TRUE(result.has_value());

    ContactManifold m = *result;

    // Normal should point from A to B?
    // Implementation: diff = b - a; m.Normal = diff / dist.
    // Wait, typical physics engine convention: Normal is direction to move A to SEPARATE it from B.
    // If A is at 0 and B is at 1.5. To separate, A must move LEFT (-1, 0, 0).
    // Let's check implementation in previous prompt:
    // m.Normal = diff / dist; (This points A -> B, i.e., (1,0,0)).
    // Check logic: usually penetration * Normal is the correction vector.
    // If Normal is (1,0,0), A moves towards B (deeper).
    // Let's verify expected behavior vs code behavior.

    // Code says: "Normal = diff / dist; // Points A -> B"
    // So if we apply separation, we likely do: PosA -= Normal * Depth * 0.5; PosB += Normal * Depth * 0.5;

    ExpectVec3Eq(m.Normal, {1, 0, 0});
    EXPECT_NEAR(m.PenetrationDepth, 0.5f, 0.001f);

    // Contact Point on A: Center + Radius * Normal = (1,0,0)
    ExpectVec3Eq(m.ContactPointA, {1, 0, 0});
    // Contact Point on B: Center - Radius * Normal = (1.5,0,0) - (1,0,0) = (0.5,0,0)
    ExpectVec3Eq(m.ContactPointB, {0.5f, 0, 0});
}

TEST(GeometryContact, Analytic_SphereAABB_Simple)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Sphere s{{0, 1.8f, 0}, 1.0f}; // Sphere Center Y=1.8. Radius=1. Bottom at 0.8. Box Top at 1.0. Pen=0.2.

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    ContactManifold m = *result;

    // Closest point on box is {0, 1, 0}
    // Diff = Sphere - Closest = {0, 0.8, 0}
    // Normal = {0, 1, 0} (Points Box -> Sphere)
    // Wait, the function was Contact_Analytic(Sphere, AABB).
    // Code: "diff = s.Center - closest;" (Points Box -> Sphere)
    // Normal = diff / dist = (0,1,0).

    ExpectVec3Eq(m.Normal, {0, 1, 0});
    EXPECT_NEAR(m.PenetrationDepth, 0.2f, 0.001f);
    ExpectVec3Eq(m.ContactPointB, {0, 1, 0}); // Closest on box
}

TEST(GeometryContact, Analytic_SphereAABB_Inside)
{
    // Test the "Deep Penetration" logic
    AABB box{{-5, -5, -5}, {5, 5, 5}};
    Sphere s{{4.5f, 0, 0}, 1.0f}; // Center is inside box.
    // Closest face is +X (at 5.0). Distance to face is 0.5.
    // Sphere Radius is 1.0.
    // Logic: overlap.x = 0.5. overlap.y = 5.
    // Smallest overlap is X.
    // Normal should point towards +X.

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    ContactManifold m = *result;

    ExpectVec3Eq(m.Normal, {1, 0, 0});
    // Depth = overlap + radius = 0.5 + 1.0 = 1.5
    EXPECT_NEAR(m.PenetrationDepth, 1.5f, 0.001f);
}

TEST(GeometryContact, Fallback_BooleanCheck)
{
    // Tests that the Fallback mechanism correctly identifies a collision
    // even if it returns a dummy manifold.
    Capsule cap{{-1, 0, 0}, {1, 0, 0}, 0.5f};
    Sphere s{{0, 0.2f, 0}, 0.5f};

    auto result = ComputeContact(cap, s); // No analytic solver provided in previous prompt
    ASSERT_TRUE(result.has_value());

    // Based on the placeholder fallback implementation:
    EXPECT_NEAR(result->PenetrationDepth, 0.001f, 0.0001f); // Dummy depth
    ExpectVec3Eq(result->Normal, {0, 1, 0}); // Dummy normal
}
