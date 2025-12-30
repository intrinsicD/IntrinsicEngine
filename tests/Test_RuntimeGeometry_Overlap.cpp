// tests/Test_RuntimeGeometry_Contact.cpp
// Comprehensive contact manifold and raycast tests
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>

import Geometry;

using namespace Geometry;

// =========================================================================
// TEST HELPERS
// =========================================================================

inline void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f)
{
    EXPECT_NEAR(a.x, b.x, tolerance) << "A=" << glm::to_string(a) << " B=" << glm::to_string(b);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

inline void ExpectVec3Finite(const glm::vec3& v)
{
    EXPECT_TRUE(std::isfinite(v.x)) << "x is not finite: " << v.x;
    EXPECT_TRUE(std::isfinite(v.y)) << "y is not finite: " << v.y;
    EXPECT_TRUE(std::isfinite(v.z)) << "z is not finite: " << v.z;
}

// =========================================================================
// RAYCAST TESTS
// =========================================================================

TEST(GeometryContact, RayCast_RayVsSphere_Hit)
{
    Sphere s{{0, 0, 0}, 1.0f};
    Ray r{{0, 0, 5}, {0, 0, -1}}; // Fired from +Z towards origin

    auto hit = RayCast(r, s);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 4.0f, 0.001f); // Should hit at z=1
    ExpectVec3Near(hit->Point, {0, 0, 1});
    ExpectVec3Near(hit->Normal, {0, 0, 1});
}

TEST(GeometryContact, RayCast_RayVsSphere_Miss)
{
    Sphere s{{0, 0, 0}, 1.0f};
    Ray r{{0, 2, 5}, {0, 0, -1}}; // Fired parallel, above sphere

    auto hit = RayCast(r, s);
    EXPECT_FALSE(hit.has_value());
}

TEST(GeometryContact, RayCast_RayVsSphere_InsideSphere)
{
    Sphere s{{0, 0, 0}, 2.0f};
    Ray r{{0, 0, 0}, {1, 0, 0}}; // Origin inside sphere

    auto hit = RayCast(r, s);
    ASSERT_TRUE(hit.has_value());
    EXPECT_GE(hit->Distance, 0.0f); // Should return exit point
}

TEST(GeometryContact, RayCast_RayVsAABB_Hit)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Ray r{{-5, 0, 0}, {1, 0, 0}}; // From -X towards box

    auto hit = RayCast(r, box);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 4.0f, 0.001f); // Hits at x=-1
    ExpectVec3Near(hit->Point, {-1, 0, 0});
    ExpectVec3Near(hit->Normal, {-1, 0, 0});
}

TEST(GeometryContact, RayCast_RayVsAABB_Inside)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Ray r{{0, 0, 0}, {1, 0, 0}}; // Origin at center

    auto hit = RayCast(r, box);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 1.0f, 0.001f); // Hits inside face at x=1
    ExpectVec3Near(hit->Point, {1, 0, 0});
}

TEST(GeometryContact, RayCast_RayVsAABB_Miss)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Ray r{{-5, 5, 0}, {1, 0, 0}}; // Parallel, above box

    auto hit = RayCast(r, box);
    EXPECT_FALSE(hit.has_value());
}

TEST(GeometryContact, RayCast_RayVsAABB_NegativeDirection)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Ray r{{5, 0, 0}, {-1, 0, 0}}; // From +X, pointing towards box

    auto hit = RayCast(r, box);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->Distance, 4.0f, 0.001f);
}

// =========================================================================
// CONTACT MANIFOLD - SPHERE VS SPHERE
// =========================================================================

TEST(GeometryContact, Contact_SphereSphere_Overlapping)
{
    Sphere sA{{0, 0, 0}, 1.0f};
    Sphere sB{{1.5f, 0, 0}, 1.0f}; // Overlap by 0.5

    auto result = ComputeContact(sA, sB);
    ASSERT_TRUE(result.has_value());

    ContactManifold m = *result;

    // Normal points from A to B
    ExpectVec3Near(m.Normal, {1, 0, 0});
    EXPECT_NEAR(m.PenetrationDepth, 0.5f, 0.001f);

    // Contact Point on A: Center + Radius * Normal = (1,0,0)
    ExpectVec3Near(m.ContactPointA, {1, 0, 0});
    // Contact Point on B: Center - Radius * Normal = (1.5,0,0) - (1,0,0) = (0.5,0,0)
    ExpectVec3Near(m.ContactPointB, {0.5f, 0, 0});
}

TEST(GeometryContact, Contact_SphereSphere_Touching)
{
    Sphere sA{{0, 0, 0}, 1.0f};
    Sphere sB{{2.0f, 0, 0}, 1.0f}; // Exactly touching

    auto result = ComputeContact(sA, sB);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->PenetrationDepth, 0.0f, 0.001f);
    ExpectVec3Near(result->Normal, {1, 0, 0});
}

TEST(GeometryContact, Contact_SphereSphere_NoOverlap)
{
    Sphere sA{{0, 0, 0}, 1.0f};
    Sphere sB{{3.0f, 0, 0}, 1.0f}; // Gap of 1.0

    auto result = ComputeContact(sA, sB);
    EXPECT_FALSE(result.has_value());
}

TEST(GeometryContact, Contact_SphereSphere_Concentric)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{0, 0, 0}, 0.5f}; // Same center

    auto result = ComputeContact(s1, s2);
    ASSERT_TRUE(result.has_value());

    // Normal should be valid (fallback direction for degenerate case)
    ExpectVec3Finite(result->Normal);
    EXPECT_NEAR(glm::length(result->Normal), 1.0f, 0.01f);

    // Penetration depth should be sum of radii
    EXPECT_NEAR(result->PenetrationDepth, 1.5f, 0.01f);
}

// =========================================================================
// CONTACT MANIFOLD - SPHERE VS AABB
// =========================================================================

TEST(GeometryContact, Contact_SphereAABB_Simple)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Sphere s{{0, 1.8f, 0}, 1.0f}; // Sphere Center Y=1.8, Radius=1, Bottom at 0.8, Box Top at 1.0, Pen=0.2

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    ContactManifold m = *result;

    // Normal points from Sphere to AABB (upward)
    ExpectVec3Near(m.Normal, {0, 1, 0});
    EXPECT_NEAR(m.PenetrationDepth, 0.2f, 0.001f);
    ExpectVec3Near(m.ContactPointB, {0, 1, 0}); // Closest on box
}

TEST(GeometryContact, Contact_SphereAABB_CenterInside)
{
    AABB box{{-5, -5, -5}, {5, 5, 5}};
    Sphere s{{4.5f, 0, 0}, 1.0f}; // Center inside box, extends outside

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    ContactManifold m = *result;

    // Normal should point toward nearest face (+X)
    ExpectVec3Near(m.Normal, {1, 0, 0}, 0.01f);

    // Penetration: distance to face (0.5) + radius (1.0) = 1.5
    EXPECT_NEAR(m.PenetrationDepth, 1.5f, 0.1f);
}

TEST(GeometryContact, Contact_SphereAABB_DeepInside)
{
    AABB box{{-10, -10, -10}, {10, 10, 10}};
    Sphere s{{0, 0, 0}, 1.0f}; // Deep inside at center

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    // Should pick smallest axis
    ExpectVec3Finite(result->Normal);
    EXPECT_GT(result->PenetrationDepth, 1.0f);
}

TEST(GeometryContact, Contact_SphereAABB_Corner)
{
    AABB box{{0, 0, 0}, {2, 2, 2}};
    Sphere s{{3, 3, 3}, 1.0f}; // Near corner (2,2,2)

    auto result = ComputeContact(s, box);

    // Distance from sphere center to corner (2,2,2) is sqrt(3) ≈ 1.732
    // Sphere radius is 1.0, so gap is 0.732 - no overlap
    EXPECT_FALSE(result.has_value());

    // Move closer
    s.Center = {2.5f, 2.5f, 2.5f};
    result = ComputeContact(s, box);

    // Distance is now sqrt(0.75) ≈ 0.866, overlap = 1.0 - 0.866 = 0.134
    ASSERT_TRUE(result.has_value());
}

// =========================================================================
// CONTACT MANIFOLD - FALLBACK (GJK)
// =========================================================================

TEST(GeometryContact, Contact_Fallback_BooleanCheck)
{
    // Tests that the Fallback mechanism correctly identifies a collision
    // even if it returns a dummy manifold.
    Capsule cap{{-1, 0, 0}, {1, 0, 0}, 0.5f};
    Sphere s{{0, 0.2f, 0}, 0.5f};

    auto result = ComputeContact(cap, s);
    ASSERT_TRUE(result.has_value());

    // Based on the placeholder fallback implementation:
    // Returns dummy values since EPA is not implemented
    EXPECT_NEAR(result->PenetrationDepth, 0.001f, 0.0001f);
    ExpectVec3Near(result->Normal, {0, 1, 0});
}

TEST(GeometryContact, Contact_ConvexHull_Sphere)
{
    // Create a simple cube hull
    ConvexHull hull;
    hull.Vertices = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}
    };

    Sphere s{{2.5f, 0, 0}, 1.0f}; // Outside hull

    auto result = ComputeContact(hull, s);
    EXPECT_FALSE(result.has_value());

    // Move closer to overlap
    s.Center = {1.5f, 0, 0};
    result = ComputeContact(hull, s);
    ASSERT_TRUE(result.has_value());
}

// =========================================================================
// SDF CONTACT SOLVER TESTS
// =========================================================================

TEST(GeometryContact, SDFSolver_SphereSphere)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{1.5f, 0, 0}, 1.0f}; // Overlap by 0.5

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    glm::vec3 guess = (s1.Center + s2.Center) * 0.5f;

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, guess);

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.05f);
    ExpectVec3Near(result->Normal, {1, 0, 0}, 0.1f);
}

TEST(GeometryContact, SDFSolver_OBBSphere_Rotated)
{
    OBB box;
    box.Center = {0, 0, 0};
    box.Extents = {1, 1, 1};
    box.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1));

    Sphere s{{1.0f, 0, 0}, 0.5f};

    auto sdfBox = SDF::CreateSDF(box);
    auto sdfSphere = SDF::CreateSDF(s);

    glm::vec3 guess = (box.Center + s.Center) * 0.5f;
    auto result = SDF::Contact_General_SDF(sdfBox, sdfSphere, guess);

    ASSERT_TRUE(result.has_value());

    // Normal should point roughly +X (Box -> Sphere)
    EXPECT_GT(result->Normal.x, 0.5f);
}

TEST(GeometryContact, SDFSolver_CapsuleBox)
{
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.5f}; // Vertical capsule

    OBB box;
    box.Center = {0.8f, 0, 0};
    box.Extents = {0.5f, 0.5f, 0.5f};
    box.Rotation = glm::quat(1, 0, 0, 0);

    auto sdfCap = SDF::CreateSDF(cap);
    auto sdfBox = SDF::CreateSDF(box);

    auto result = SDF::Contact_General_SDF(sdfCap, sdfBox, {0.4f, 0, 0});

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.2f, 0.05f);
    EXPECT_NEAR(std::abs(result->Normal.x), 1.0f, 0.1f);
}

TEST(GeometryContact, SDFSolver_NoOverlap)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{3.0f, 0, 0}, 1.0f}; // Gap of 1.0

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, {1.5f, 0, 0});
    EXPECT_FALSE(result.has_value());
}

TEST(GeometryContact, SDFSolver_SphereTriangle)
{
    Triangle tri{
        {-2, 0, -2},
        {2, 0, -2},
        {0, 0, 2}
    };

    Sphere s{{0, 0.5f, 0}, 1.0f}; // Sphere touching triangle plane

    auto sdfTri = SDF::CreateSDF(tri);
    auto sdfSphere = SDF::CreateSDF(s);

    auto result = SDF::Contact_General_SDF(sdfTri, sdfSphere, {0, 0.2f, 0});

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.1f);
    EXPECT_NEAR(std::abs(result->Normal.y), 1.0f, 0.1f);
}

TEST(GeometryContact, SDFSolver_CylinderPlane)
{
    Cylinder cyl{{0, 1, 0}, {0, 3, 0}, 0.5f};
    Plane floor{{0, 1, 0}, 0.0f}; // Y=0 facing up

    // Move cylinder down so it penetrates
    cyl.PointA.y = -0.2f;
    cyl.PointB.y = 2.0f;

    auto sdfCyl = SDF::CreateSDF(cyl);
    auto sdfPlane = SDF::CreateSDF(floor);

    auto result = SDF::Contact_General_SDF(sdfCyl, sdfPlane, {0, 0, 0});

    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->PenetrationDepth, 0.2f, 0.05f);
}

TEST(GeometryContact, SDFSolver_BadInitialGuess)
{
    // Good overlap, but bad initial guess
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{1.5f, 0, 0}, 1.0f};

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    // Start guess very far away
    auto result = SDF::Contact_General_SDF(sdf1, sdf2, {1000, 1000, 1000});

    // May or may not converge, but shouldn't crash
    if (result.has_value())
    {
        ExpectVec3Finite(result->Normal);
        EXPECT_TRUE(std::isfinite(result->PenetrationDepth));
    }
}

// =========================================================================
// EDGE CASES
// =========================================================================

TEST(GeometryContact, Contact_ZeroRadiusSphere)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{0.5f, 0, 0}, 0.0f}; // Point sphere

    // Should handle gracefully (may or may not return contact)
    auto result = ComputeContact(s1, s2);

    if (result.has_value())
    {
        ExpectVec3Finite(result->Normal);
        EXPECT_GE(result->PenetrationDepth, 0.0f);
    }
}

TEST(GeometryContact, Contact_DegenerateAABB)
{
    AABB box{{1, 1, 1}, {1, 1, 1}}; // Point
    Sphere s{{1, 1, 1}, 0.5f};

    auto result = ComputeContact(s, box);

    // Should detect overlap
    ASSERT_TRUE(result.has_value());
}

TEST(GeometryContact, Contact_LargeCoordinates)
{
    Sphere s1{{1e6f, 1e6f, 1e6f}, 1.0f};
    Sphere s2{{1e6f + 1.5f, 1e6f, 1e6f}, 1.0f};

    auto result = ComputeContact(s1, s2);
    ASSERT_TRUE(result.has_value());

    EXPECT_NEAR(result->PenetrationDepth, 0.5f, 0.01f);
}

// =========================================================================
// NORMAL CONVENTION VERIFICATION
// =========================================================================

TEST(GeometryContact, NormalConvention_PointsAtoB)
{
    // Verify that normals consistently point from A to B

    Sphere sA{{-0.4, 0, 0}, 0.5f};
    Sphere sB{{0.4, 0, 0}, 0.5f};

    auto result = ComputeContact(sA, sB);
    ASSERT_TRUE(result.has_value());

    // Normal should point from A (-1,0,0) to B (1,0,0) → (+1,0,0)
    ExpectVec3Near(result->Normal, {1, 0, 0});
    EXPECT_NEAR(result->PenetrationDepth, 0.2f, 0.001f);

    // Swap order
    result = ComputeContact(sB, sA);
    ASSERT_TRUE(result.has_value());

    // Normal should point from B (1,0,0) to A (-1,0,0) → (-1,0,0)
    ExpectVec3Near(result->Normal, {-1, 0, 0});
}

TEST(GeometryContact, ContactPoints_OnSurface)
{
    Sphere sA{{0, 0, 0}, 1.0f};
    Sphere sB{{1.5f, 0, 0}, 1.0f};

    auto result = ComputeContact(sA, sB);
    ASSERT_TRUE(result.has_value());

    // Contact points should be on the surfaces
    float distA = glm::distance(result->ContactPointA, sA.Center);
    float distB = glm::distance(result->ContactPointB, sB.Center);

    EXPECT_NEAR(distA, sA.Radius, 0.01f);
    EXPECT_NEAR(distB, sB.Radius, 0.01f);
}