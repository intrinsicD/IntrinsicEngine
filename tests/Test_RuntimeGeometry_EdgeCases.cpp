// tests/Test_RuntimeGeometry_EdgeCases.cpp
// Comprehensive tests for edge cases, degenerate shapes, and numerical stability
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <limits>
#include <cmath>

import Runtime.Geometry.Primitives;
import Runtime.Geometry.Support;
import Runtime.Geometry.Overlap;
import Runtime.Geometry.Contact;
import Runtime.Geometry.Containment;
import Runtime.Geometry.SDF;
import Runtime.Geometry.SDF.General;
import Runtime.Geometry.Validation;

using namespace Runtime::Geometry;

// --- Test Helpers ---

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
// DEGENERATE SHAPE TESTS
// =========================================================================

TEST(GeometryEdgeCases, ZeroRadiusSphere)
{
    Sphere s{{0, 0, 0}, 0.0f};

    // Validation should detect this
    EXPECT_FALSE(Validation::IsValid(s));

    // Sanitize should fix it
    Sphere fixed = Validation::Sanitize(s);
    EXPECT_TRUE(Validation::IsValid(fixed));
    EXPECT_GT(fixed.Radius, 0.0f);
}

TEST(GeometryEdgeCases, DegenerateAABB_Point)
{
    AABB box{{1, 1, 1}, {1, 1, 1}}; // Zero volume

    EXPECT_TRUE(Validation::IsDegenerate(box));

    // Should still be "valid" structure-wise
    EXPECT_TRUE(Validation::IsValid(box));

    // Sanitize should make it non-degenerate
    AABB fixed = Validation::Sanitize(box);
    EXPECT_FALSE(Validation::IsDegenerate(fixed));
}

TEST(GeometryEdgeCases, DegenerateAABB_Line)
{
    AABB box{{0, 0, 0}, {1, 0, 0}}; // Degenerate on Y and Z

    EXPECT_TRUE(Validation::IsDegenerate(box));
}

TEST(GeometryEdgeCases, InvertedAABB)
{
    AABB box{{1, 1, 1}, {-1, -1, -1}}; // Min > Max

    EXPECT_FALSE(Validation::IsValid(box));

    // Sanitize should swap
    AABB fixed = Validation::Sanitize(box);
    EXPECT_TRUE(Validation::IsValid(fixed));
    EXPECT_LE(fixed.Min.x, fixed.Max.x);
    EXPECT_LE(fixed.Min.y, fixed.Max.y);
    EXPECT_LE(fixed.Min.z, fixed.Max.z);
}

TEST(GeometryEdgeCases, DegenerateOBB_ZeroExtents)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {0, 0, 0};
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(Validation::IsDegenerate(obb));

    // Support function should still work (returns center)
    glm::vec3 sup = Support(obb, {1, 0, 0});
    ExpectVec3Near(sup, obb.Center);
}

TEST(GeometryEdgeCases, DegenerateCapsule_Point)
{
    Capsule cap{{0, 0, 0}, {0, 0, 0}, 0.5f}; // PointA == PointB

    EXPECT_TRUE(Validation::IsDegenerate(cap));

    // Support should degrade to sphere behavior
    glm::vec3 sup = Support(cap, {1, 0, 0});
    ExpectVec3Near(sup, {0.5f, 0, 0}, 0.01f);
}

TEST(GeometryEdgeCases, DegenerateCylinder_Point)
{
    Cylinder cyl{{0, 0, 0}, {0, 0, 0}, 1.0f}; // PointA == PointB

    EXPECT_TRUE(Validation::IsDegenerate(cyl));

    // SDF factory should handle this gracefully
    auto sdf = SDF::CreateSDF(cyl);
    float dist = sdf({2, 0, 0});

    // Should behave like sphere with radius 1.0
    EXPECT_NEAR(dist, 1.0f, 0.1f);
}

TEST(GeometryEdgeCases, DegenerateTriangle_Collinear)
{
    // All points on a line
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};

    EXPECT_TRUE(Validation::IsDegenerate(tri));

    // Support should still work (picks furthest point)
    glm::vec3 sup = Support(tri, {1, 0, 0});
    ExpectVec3Near(sup, {2, 0, 0});
}

TEST(GeometryEdgeCases, DegenerateTriangle_Point)
{
    // All three points identical
    Triangle tri{{1, 1, 1}, {1, 1, 1}, {1, 1, 1}};

    EXPECT_TRUE(Validation::IsDegenerate(tri));

    glm::vec3 sup = Support(tri, {1, 0, 0});
    ExpectVec3Near(sup, {1, 1, 1});
}

// =========================================================================
// ZERO VECTOR TESTS
// =========================================================================

TEST(GeometryEdgeCases, Support_ZeroDirection_Sphere)
{
    Sphere s{{0, 0, 0}, 1.0f};
    glm::vec3 result = Support(s, {0, 0, 0});

    // Should return a valid point (fallback direction)
    ExpectVec3Finite(result);
}

TEST(GeometryEdgeCases, Support_ZeroDirection_AABB)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    glm::vec3 result = Support(box, {0, 0, 0});

    // AABB support doesn't need normalization, should pick a corner
    ExpectVec3Finite(result);
}

TEST(GeometryEdgeCases, Support_ZeroDirection_Capsule)
{
    Capsule cap{{-1, 0, 0}, {1, 0, 0}, 0.5f};
    glm::vec3 result = Support(cap, {0, 0, 0});

    // Should return one of the segment endpoints without crashing
    ExpectVec3Finite(result);
}

TEST(GeometryEdgeCases, Ray_ZeroDirection)
{
    Ray r{{0, 0, 0}, {0, 0, 0}};

    EXPECT_FALSE(Validation::IsValid(r));

    // Sanitize should fix it
    Ray fixed = Validation::Sanitize(r);
    EXPECT_TRUE(Validation::IsValid(fixed));
    EXPECT_FALSE(Validation::IsZero(fixed.Direction));
}

// =========================================================================
// NUMERICAL STABILITY TESTS
// =========================================================================

TEST(GeometryEdgeCases, Plane_ZeroNormal)
{
    Plane p{{0, 0, 0}, 5.0f};

    EXPECT_FALSE(p.IsValid());

    // Normalize should handle this gracefully
    p.Normalize();
    EXPECT_TRUE(p.IsValid());
    ExpectVec3Finite(p.Normal);
}

TEST(GeometryEdgeCases, Plane_VeryLargeDistance)
{
    Plane p{{1, 0, 0}, 1e10f};

    p.Normalize();
    EXPECT_TRUE(p.IsValid());
    EXPECT_TRUE(std::isfinite(p.Distance));
}

TEST(GeometryEdgeCases, Cylinder_NearParallelAxis_UpAligned)
{
    // Cylinder already aligned with Y axis
    Cylinder cyl{{0, 0, 0}, {0, 1, 0}, 0.5f};

    auto sdf = SDF::CreateSDF(cyl);
    float dist = sdf({1, 0.5f, 0});

    // Should work without numerical issues
    EXPECT_TRUE(std::isfinite(dist));
}

TEST(GeometryEdgeCases, Cylinder_NearParallelAxis_DownAligned)
{
    // Cylinder pointing down (opposite to Y)
    Cylinder cyl{{0, 1, 0}, {0, 0, 0}, 0.5f};

    auto sdf = SDF::CreateSDF(cyl);
    float dist = sdf({1, 0.5f, 0});

    EXPECT_TRUE(std::isfinite(dist));
}

TEST(GeometryEdgeCases, Cylinder_NearlyParallelAxis)
{
    // Nearly aligned with Y (0.9999 dot product)
    Cylinder cyl{{0, 0, 0}, {0.0001f, 1.0f, 0}, 0.5f};

    auto sdf = SDF::CreateSDF(cyl);
    float dist = sdf({1, 0.5f, 0});

    EXPECT_TRUE(std::isfinite(dist));
}

TEST(GeometryEdgeCases, OBB_IdentityQuaternion_NotNormalized)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(2, 0, 0, 0); // Not normalized!

    EXPECT_FALSE(Validation::IsValid(obb));

    // Sanitize should normalize it
    OBB fixed = Validation::Sanitize(obb);
    EXPECT_TRUE(Validation::IsValid(fixed));
    EXPECT_NEAR(glm::length(fixed.Rotation), 1.0f, 1e-4f);
}

TEST(GeometryEdgeCases, ConvexHull_EmptyPlanes)
{
    ConvexHull hull;
    hull.Planes.clear();
    hull.Vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};

    auto sdf = SDF::CreateSDF(hull);
    float dist = sdf({10, 10, 10});

    // Should return large positive (point is outside empty hull)
    EXPECT_GT(dist, 1000.0f);
}

// =========================================================================
// OVERLAP EDGE CASES
// =========================================================================

TEST(GeometryEdgeCases, Overlap_SameSphere)
{
    Sphere s{{0, 0, 0}, 1.0f};

    // A sphere always overlaps itself
    EXPECT_TRUE(TestOverlap(s, s));
}

TEST(GeometryEdgeCases, Overlap_SphereTouching)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{2.0f, 0, 0}, 1.0f}; // Exactly touching

    // Should return true (touching counts as overlap)
    EXPECT_TRUE(TestOverlap(s1, s2));
}

TEST(GeometryEdgeCases, Overlap_CapsuleDegenerate_Vs_Sphere)
{
    Capsule cap{{0, 0, 0}, {0, 0, 0}, 0.5f}; // Degenerate (point)
    Sphere s{{1.5f, 0, 0}, 1.0f};

    // Gap: 1.5 - 0.5 - 1.0 = 0. Should touch
    bool result = TestOverlap(cap, s);
    EXPECT_TRUE(result);
}

TEST(GeometryEdgeCases, Overlap_AABBTouchingFace)
{
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 0, 0}, {2, 1, 1}}; // Touching at x=1

    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryEdgeCases, Overlap_AABBTouchingEdge)
{
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 1, 0}, {2, 2, 1}}; // Touching at edge

    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryEdgeCases, Overlap_AABBTouchingCorner)
{
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 1, 1}, {2, 2, 2}}; // Touching at corner (1,1,1)

    EXPECT_TRUE(TestOverlap(a, b));
}

// =========================================================================
// CONTACT EDGE CASES
// =========================================================================

TEST(GeometryEdgeCases, Contact_ConcentricSpheres)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{0, 0, 0}, 0.5f}; // Same center

    auto result = ComputeContact(s1, s2);
    ASSERT_TRUE(result.has_value());

    // Normal should be valid (fallback direction)
    ExpectVec3Finite(result->Normal);
    EXPECT_NEAR(glm::length(result->Normal), 1.0f, 0.01f);

    // Penetration depth should be sum of radii
    EXPECT_NEAR(result->PenetrationDepth, 1.5f, 0.01f);
}

TEST(GeometryEdgeCases, Contact_SphereCenterInsideAABB)
{
    AABB box{{-5, -5, -5}, {5, 5, 5}};
    Sphere s{{4.5f, 0, 0}, 1.0f}; // Center inside, extends outside

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    // Normal should point toward nearest face (+X)
    ExpectVec3Near(result->Normal, {1, 0, 0}, 0.01f);

    // Penetration: distance to face (0.5) + radius (1.0) = 1.5
    EXPECT_NEAR(result->PenetrationDepth, 1.5f, 0.1f);
}

TEST(GeometryEdgeCases, Contact_SphereDeepInsideAABB)
{
    AABB box{{-10, -10, -10}, {10, 10, 10}};
    Sphere s{{0, 0, 0}, 1.0f}; // Deep inside

    auto result = ComputeContact(s, box);
    ASSERT_TRUE(result.has_value());

    // Should pick smallest axis
    ExpectVec3Finite(result->Normal);
    EXPECT_GT(result->PenetrationDepth, 1.0f);
}

// =========================================================================
// SDF EDGE CASES
// =========================================================================

TEST(GeometryEdgeCases, SDF_SphereCenterQuery)
{
    Sphere s{{0, 0, 0}, 1.0f};
    auto sdf = SDF::CreateSDF(s);

    // At center, distance should be -radius
    EXPECT_NEAR(sdf({0, 0, 0}), -1.0f, 0.01f);
}

TEST(GeometryEdgeCases, SDF_AABBCenterQuery)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    auto sdf = SDF::CreateSDF(box);

    // At center, distance should be negative
    float dist = sdf({0, 0, 0});
    EXPECT_LT(dist, 0.0f);
}

TEST(GeometryEdgeCases, SDF_AABBCornerQuery)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    auto sdf = SDF::CreateSDF(box);

    // At corner, distance should be 0
    EXPECT_NEAR(sdf({1, 1, 1}), 0.0f, 0.01f);
}

TEST(GeometryEdgeCases, SDF_CapsuleThinSegment)
{
    // Very thin capsule (almost a line)
    Capsule cap{{-10, 0, 0}, {10, 0, 0}, 0.001f};
    auto sdf = SDF::CreateSDF(cap);

    // Point on segment
    EXPECT_NEAR(sdf({5, 0, 0}), -0.001f, 0.0001f);

    // Point perpendicular
    EXPECT_NEAR(sdf({0, 1, 0}), 1.0f - 0.001f, 0.01f);
}

TEST(GeometryEdgeCases, SDF_TriangleOnPlane)
{
    Triangle tri{{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    auto sdf = SDF::CreateSDF(tri);

    // Point at centroid (should be very close to surface)
    glm::vec3 centroid = (tri.A + tri.B + tri.C) / 3.0f;
    float dist = sdf(centroid);

    EXPECT_NEAR(dist, 0.0f, 0.1f); // Within thickness tolerance
}

// =========================================================================
// SDF GENERAL SOLVER EDGE CASES
// =========================================================================

TEST(GeometryEdgeCases, SDFSolver_NoConvergence)
{
    // Two spheres far apart - solver should return nullopt
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{100, 0, 0}, 1.0f};

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    auto result = SDF::Contact_General_SDF(sdf1, sdf2, {50, 0, 0});

    EXPECT_FALSE(result.has_value());
}

TEST(GeometryEdgeCases, SDFSolver_BadInitialGuess)
{
    // Good overlap, but bad initial guess
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{1.5f, 0, 0}, 1.0f};

    auto sdf1 = SDF::CreateSDF(s1);
    auto sdf2 = SDF::CreateSDF(s2);

    // Start guess very far away
    auto result = SDF::Contact_General_SDF(sdf1, sdf2, {1000, 1000, 1000});

    // Should still converge (or return nullopt)
    // Depends on implementation - just check it doesn't crash
    EXPECT_TRUE(std::isfinite(result.has_value() ? result->PenetrationDepth : 0.0f));
}

// =========================================================================
// CONTAINMENT EDGE CASES
// =========================================================================

TEST(GeometryEdgeCases, Contains_SphereTouching)
{
    Sphere outer{{0, 0, 0}, 2.0f};
    Sphere inner{{0, 0, 0}, 2.0f}; // Same radius

    // Should NOT contain (they're equal, not one inside other)
    EXPECT_FALSE(Contains(outer, inner));
}

TEST(GeometryEdgeCases, Contains_AABBIdentical)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};

    // A box contains itself
    EXPECT_TRUE(Contains(box, box));
}

TEST(GeometryEdgeCases, Contains_PointOnBoundary)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    glm::vec3 point{1, 1, 1}; // On corner

    // Inclusive containment
    EXPECT_TRUE(Contains(box, point));
}

// =========================================================================
// FRUSTUM EDGE CASES
// =========================================================================

TEST(GeometryEdgeCases, Frustum_DegeneratePlanes)
{
    Frustum f;

    // Create frustum with one degenerate plane
    for (int i = 0; i < 6; i++)
    {
        f.Planes[i] = Plane{{0, 1, 0}, 0};
    }

    f.Planes[0] = Plane{{0, 0, 0}, 5.0f}; // Degenerate

    EXPECT_FALSE(Validation::IsValid(f));
}

// =========================================================================
// LARGE VALUE TESTS
// =========================================================================

TEST(GeometryEdgeCases, LargeCoordinates)
{
    Sphere s{{1e6f, 1e6f, 1e6f}, 1.0f};
    Sphere s2{{1e6f + 1.5f, 1e6f, 1e6f}, 1.0f};

    // Should still work with large coordinates
    EXPECT_TRUE(TestOverlap(s, s2));
}

TEST(GeometryEdgeCases, VerySmallRadius)
{
    Sphere s{{0, 0, 0}, 1e-5f};

    auto result = Support(s, {1, 0, 0});
    ExpectVec3Finite(result);
}

TEST(GeometryEdgeCases, VeryLargeRadius)
{
    Sphere s{{0, 0, 0}, 1e6f};

    auto result = Support(s, {1, 0, 0});
    EXPECT_NEAR(result.x, 1e6f, 1e3f);
}
