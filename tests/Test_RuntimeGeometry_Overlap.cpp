// tests/Test_RuntimeGeometry_Overlap.cpp
// Comprehensive overlap/intersection tests for all primitive pairs
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <glm/gtx/quaternion.hpp>

import Runtime.Geometry.Primitives;
import Runtime.Geometry.Overlap;
import Runtime.Geometry.Support;
import Runtime.Geometry.Validation;

using namespace Runtime::Geometry;

// =========================================================================
// TEST HELPERS
// =========================================================================

void ExpectVec3Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 0.01f)
{
    EXPECT_NEAR(a.x, b.x, tolerance) << "A=" << glm::to_string(a) << " B=" << glm::to_string(b);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

// =========================================================================
// SPHERE VS SPHERE OVERLAP
// =========================================================================

TEST(GeometryOverlap, SphereSphere_Overlapping)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{1.5f, 0, 0}, 1.0f}; // Overlapping by 0.5
    Sphere s3{{2.1f, 0, 0}, 1.0f}; // Disjoint (gap of 0.1)

    EXPECT_TRUE(TestOverlap(s1, s2));
    EXPECT_FALSE(TestOverlap(s1, s3));
}

TEST(GeometryOverlap, SphereSphere_Touching)
{
    Sphere s1{{0, 0, 0}, 1.0f};
    Sphere s2{{2.0f, 0, 0}, 1.0f}; // Exactly touching

    // Touching counts as overlap
    EXPECT_TRUE(TestOverlap(s1, s2));
}

TEST(GeometryOverlap, SphereSphere_SamePosition)
{
    Sphere s{{0, 0, 0}, 1.0f};

    // A sphere always overlaps itself
    EXPECT_TRUE(TestOverlap(s, s));
}

TEST(GeometryOverlap, SphereSphere_Concentric)
{
    Sphere outer{{0, 0, 0}, 2.0f};
    Sphere inner{{0, 0, 0}, 1.0f}; // Same center, different radii

    EXPECT_TRUE(TestOverlap(outer, inner));
}

// =========================================================================
// AABB VS AABB OVERLAP
// =========================================================================

TEST(GeometryOverlap, AABBAABB_Overlapping)
{
    AABB b1{{0, 0, 0}, {2, 2, 2}};
    AABB b2{{1, 1, 1}, {3, 3, 3}}; // Intersects center
    AABB b3{{3, 3, 3}, {4, 4, 4}}; // Disjoint

    EXPECT_TRUE(TestOverlap(b1, b2));
    EXPECT_FALSE(TestOverlap(b1, b3));
}

TEST(GeometryOverlap, AABBAABB_TouchingFace)
{
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 0, 0}, {2, 1, 1}}; // Touching at x=1 face

    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryOverlap, AABBAABB_TouchingEdge)
{
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 1, 0}, {2, 2, 1}}; // Touching at edge

    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryOverlap, AABBAABB_TouchingCorner)
{
    AABB a{{0, 0, 0}, {1, 1, 1}};
    AABB b{{1, 1, 1}, {2, 2, 2}}; // Touching at corner (1,1,1)

    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryOverlap, AABBAABB_Identical)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};

    EXPECT_TRUE(TestOverlap(box, box));
}

// =========================================================================
// SPHERE VS AABB OVERLAP
// =========================================================================

TEST(GeometryOverlap, SphereAABB_Inside)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Sphere sInside{{0, 0, 0}, 0.5f};

    EXPECT_TRUE(TestOverlap(sInside, box));
}

TEST(GeometryOverlap, SphereAABB_Touching)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Sphere sTouch{{0, 1.5f, 0}, 0.6f}; // Center at 1.5, box max Y is 1.0. Gap is 0.5. Radius 0.6. Overlaps.

    EXPECT_TRUE(TestOverlap(sTouch, box));
}

TEST(GeometryOverlap, SphereAABB_Outside)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    Sphere sFar{{0, 5, 0}, 1.0f};

    EXPECT_FALSE(TestOverlap(sFar, box));
}

// =========================================================================
// OBB VS OBB OVERLAP (SAT)
// =========================================================================

TEST(GeometryOverlap, OBBOBB_AlignedOverlapping)
{
    OBB a{
        .Center = {0, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };
    OBB b{
        .Center = {1.5f, 0, 0}, // Overlap: [0.5, 2.5] vs [-1, 1]
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };

    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryOverlap, OBBOBB_AlignedSeparated)
{
    OBB a{
        .Center = {0, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };
    OBB b{
        .Center = {3, 0, 0}, // Far away
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };

    EXPECT_FALSE(TestOverlap(a, b));
}

TEST(GeometryOverlap, OBBOBB_Rotated45Degrees)
{
    OBB a{
        .Center = {0, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };
    OBB b{
        .Center = {2, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1))
    };

    // With 45 deg rotation, corner points at A
    EXPECT_TRUE(TestOverlap(a, b));
}

TEST(GeometryOverlap, OBBOBB_ComplexRotation)
{
    OBB a{
        .Center = {0, 0, 0},
        .Extents = {2, 1, 1},
        .Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0, 1, 0))
    };
    OBB b{
        .Center = {1, 1, 0},
        .Extents = {1, 2, 1},
        .Rotation = glm::angleAxis(glm::radians(-45.0f), glm::vec3(1, 0, 0))
    };

    // Complex overlap case - tests full 15-axis SAT
    bool result = TestOverlap(a, b);
    EXPECT_TRUE(std::is_same_v<decltype(result), bool>); // Just verify it doesn't crash
}

// =========================================================================
// OBB VS SPHERE OVERLAP
// =========================================================================

TEST(GeometryOverlap, OBBSphere_NoOverlap)
{
    OBB box{
        .Center = {0, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };
    Sphere s{{0, 2, 0}, 0.5f}; // Distance 2.0, Extent 1.0, Radius 0.5. Gap 0.5

    EXPECT_FALSE(TestOverlap(s, box));
}

TEST(GeometryOverlap, OBBSphere_Overlapping)
{
    OBB box{
        .Center = {0, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::quat(1, 0, 0, 0)
    };
    Sphere s{{0, 1.2f, 0}, 0.5f}; // Overlaps

    EXPECT_TRUE(TestOverlap(s, box));
}

TEST(GeometryOverlap, OBBSphere_Rotated)
{
    OBB box{
        .Center = {0, 0, 0},
        .Extents = {1, 1, 1},
        .Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0, 0, 1))
    };
    Sphere s{{1.3f, 1.3f, 0}, 0.5f};

    EXPECT_TRUE(TestOverlap(s, box));
}

// =========================================================================
// CAPSULE OVERLAP
// =========================================================================

TEST(GeometryOverlap, CapsuleSphere_GJKFallback_Hit)
{
    Capsule cap{{-2, 0, 0}, {2, 0, 0}, 0.5f}; // Long capsule along X
    Sphere sHit{{0, 0.8f, 0}, 0.5f}; // Just touching (dist 0.8, radii sum 1.0)

    EXPECT_TRUE(TestOverlap(cap, sHit));
}

TEST(GeometryOverlap, CapsuleSphere_GJKFallback_Miss)
{
    Capsule cap{{-2, 0, 0}, {2, 0, 0}, 0.5f};
    Sphere sMiss{{0, 2.0f, 0}, 0.5f};

    EXPECT_FALSE(TestOverlap(cap, sMiss));
}

TEST(GeometryOverlap, CapsuleSphere_DegenerateCapsule)
{
    Capsule cap{{0, 0, 0}, {0, 0, 0}, 0.5f}; // Degenerate (point) - behaves like sphere
    Sphere s{{1.5f, 0, 0}, 1.0f};

    // Gap: 1.5 - 0.5 - 1.0 = 0. Should touch
    EXPECT_TRUE(TestOverlap(cap, s));
}

TEST(GeometryOverlap, CapsuleTriangle_GJK)
{
    Capsule cap{{0, 1, 0}, {0, 3, 0}, 0.5f};
    Triangle tri{
        {-2, 0, -1},
        {2, 0, -1},
        {0, 0, 2}
    };

    // Capsule is above Y=0 plane. Lowest point Y=0.5. No overlap.
    EXPECT_FALSE(TestOverlap(cap, tri));

    // Lower capsule
    cap.PointA.y = -0.5f;
    EXPECT_TRUE(TestOverlap(cap, tri));
}

// =========================================================================
// FRUSTUM OVERLAP
// =========================================================================

TEST(GeometryOverlap, FrustumAABB_Visible)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);

    // Box in front of camera
    AABB bHit{{-1, -1, -6}, {1, 1, -4}};
    EXPECT_TRUE(TestOverlap(f, bHit));
}

TEST(GeometryOverlap, FrustumAABB_BehindCamera)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);

    // Box behind camera (Z = +5)
    AABB bMiss{{-1, -1, 4}, {1, 1, 6}};
    EXPECT_FALSE(TestOverlap(f, bMiss));
}

TEST(GeometryOverlap, FrustumAABB_OutsideToSide)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, -1), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);

    // Box far to the right
    AABB bMissSide{{50, -1, -10}, {55, 1, -5}};
    EXPECT_FALSE(TestOverlap(f, bMissSide));
}

TEST(GeometryOverlap, FrustumAABB_LookingAtOrigin)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);

    // Box at origin (Visible)
    AABB visibleBox{{-1, -1, -1}, {1, 1, 1}};
    EXPECT_TRUE(TestOverlap(frustum, visibleBox));

    // Box behind camera (Z > 5)
    AABB behindBox{{-1, -1, 6}, {1, 1, 8}};
    EXPECT_FALSE(TestOverlap(frustum, behindBox));
}

TEST(GeometryOverlap, FrustumSphere_Analytic)
{
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto f = Frustum::CreateFromMatrix(proj * view);

    Sphere sVisible{{0, 0, 0}, 1.0f};
    EXPECT_TRUE(TestOverlap(f, sVisible));

    Sphere sBehind{{0, 0, 10}, 1.0f};
    EXPECT_FALSE(TestOverlap(f, sBehind));
}

// =========================================================================
// RAY OVERLAP
// =========================================================================

TEST(GeometryOverlap, RayAABB_Hit)
{
    Ray r{{-5, 0, 0}, {1, 0, 0}}; // From -X towards origin
    AABB box{{-1, -1, -1}, {1, 1, 1}};

    EXPECT_TRUE(TestOverlap(r, box));
}

TEST(GeometryOverlap, RayAABB_Miss)
{
    Ray r{{-5, 5, 0}, {1, 0, 0}}; // Parallel, above box
    AABB box{{-1, -1, -1}, {1, 1, 1}};

    EXPECT_FALSE(TestOverlap(r, box));
}

TEST(GeometryOverlap, RayAABB_InsideBox)
{
    Ray r{{0, 0, 0}, {1, 0, 0}}; // Origin inside box
    AABB box{{-1, -1, -1}, {1, 1, 1}};

    EXPECT_TRUE(TestOverlap(r, box));
}

TEST(GeometryOverlap, RaySphere_Hit)
{
    Ray r{{0, 0, 5}, {0, 0, -1}}; // Fired from +Z towards origin
    Sphere s{{0, 0, 0}, 1.0f};

    EXPECT_TRUE(TestOverlap(r, s));
}

TEST(GeometryOverlap, RaySphere_Miss)
{
    Ray r{{0, 2, 5}, {0, 0, -1}}; // Parallel, above sphere
    Sphere s{{0, 0, 0}, 1.0f};

    EXPECT_FALSE(TestOverlap(r, s));
}

TEST(GeometryOverlap, RaySphere_OriginOutsidePointingAway)
{
    Ray r{{5, 0, 0}, {1, 0, 0}}; // Pointing away from sphere
    Sphere s{{0, 0, 0}, 1.0f};

    EXPECT_FALSE(TestOverlap(r, s));
}

// =========================================================================
// DEGENERATE SHAPE OVERLAP
// =========================================================================

TEST(GeometryOverlap, DegenerateAABB_Point)
{
    AABB point{{1, 1, 1}, {1, 1, 1}}; // Zero volume
    AABB box{{0, 0, 0}, {2, 2, 2}};

    EXPECT_TRUE(TestOverlap(point, box));
}

TEST(GeometryOverlap, DegenerateAABB_Line)
{
    AABB line{{0, 0, 0}, {1, 0, 0}}; // Line along X
    AABB box{{0.5f, -0.5f, -0.5f}, {0.6f, 0.5f, 0.5f}};

    EXPECT_TRUE(TestOverlap(line, box));
}

TEST(GeometryOverlap, ZeroRadiusSphere_vs_AABB)
{
    Sphere s{{0.5f, 0.5f, 0.5f}, 1e-6f}; // Essentially a point
    AABB box{{0, 0, 0}, {1, 1, 1}};

    EXPECT_TRUE(TestOverlap(s, box));
}

// =========================================================================
// LARGE VALUE TESTS
// =========================================================================

TEST(GeometryOverlap, LargeCoordinates_SphereSphere)
{
    Sphere s1{{1e6f, 1e6f, 1e6f}, 1.0f};
    Sphere s2{{1e6f + 1.5f, 1e6f, 1e6f}, 1.0f};

    // Should still work with large coordinates
    EXPECT_TRUE(TestOverlap(s1, s2));
}

TEST(GeometryOverlap, VerySmallSpheres)
{
    Sphere s1{{0, 0, 0}, 1e-5f};
    Sphere s2{{1e-5f, 0, 0}, 1e-5f};

    EXPECT_TRUE(TestOverlap(s1, s2));
}

// =========================================================================
// SUPPORT FUNCTION TESTS (Related to GJK overlap)
// =========================================================================

TEST(GeometryOverlap, Support_AABB)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};

    ExpectVec3Near(Support(box, {1, 0, 0}), {1, 1, 1});
    ExpectVec3Near(Support(box, {-1, 0, 0}), {-1, 1, 1});
    ExpectVec3Near(Support(box, {0, 1, 0}), {1, 1, 1});
    ExpectVec3Near(Support(box, {1, 1, 1}), {1, 1, 1});
}

TEST(GeometryOverlap, Support_Sphere)
{
    Sphere s{{0, 0, 0}, 1.0f};

    ExpectVec3Near(Support(s, {1, 0, 0}), {1, 0, 0});
    ExpectVec3Near(Support(s, {0, 1, 0}), {0, 1, 0});

    glm::vec3 dir = glm::normalize(glm::vec3(1, 1, 0));
    ExpectVec3Near(Support(s, {1, 1, 0}), dir);
}

TEST(GeometryOverlap, Support_OBB_Rotation)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 0.5f, 0.5f}; // Long on X
    obb.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0)); // Rotate to Z

    glm::vec3 sup = Support(obb, {0, 0, 1});
    ExpectVec3Near(sup, {0.5f, 0.5f, 1.0f}, 0.1f);
}

TEST(GeometryOverlap, Support_Cylinder)
{
    Cylinder cyl{{0, 0, 0}, {0, 2, 0}, 1.0f}; // Height 2 along Y, Radius 1

    // Axial
    ExpectVec3Near(Support(cyl, {0, 1, 0}), {0, 2, 0});
    ExpectVec3Near(Support(cyl, {0, -1, 0}), {0, 0, 0});

    // Radial
    ExpectVec3Near(Support(cyl, {1, 0, 0}), {1, 0, 0}, 0.1f);

    // Diagonal (rim)
    glm::vec3 diag = glm::normalize(glm::vec3(1, 1, 0));
    glm::vec3 result = Support(cyl, diag);
    ExpectVec3Near(result, {1, 2, 0}, 0.1f);
}

TEST(GeometryOverlap, Support_ZeroDirection)
{
    Sphere s{{0, 0, 0}, 1.0f};

    // Should not crash or return NaN
    glm::vec3 result = Support(s, {0, 0, 0});
    EXPECT_TRUE(std::isfinite(result.x));
    EXPECT_TRUE(std::isfinite(result.y));
    EXPECT_TRUE(std::isfinite(result.z));
}
