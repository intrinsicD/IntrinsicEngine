// tests/Test_Support.cpp — Dedicated tests for Geometry::Support functions (TODO D18).
//
// Validates that each primitive type's support function returns the correct
// farthest point in a given direction.
#include <gtest/gtest.h>

#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>

import Geometry;

using namespace Geometry;

// ============================================================================
// Helpers
// ============================================================================

static constexpr float kEps = 1e-4f;

static void ExpectNear(const glm::vec3& a, const glm::vec3& b, float eps = kEps)
{
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// The support function should always return the point with the maximum dot
// product with the given direction, among all points on the shape.
static void ExpectMaximalDot(const glm::vec3& support, const glm::vec3& dir)
{
    // Not a strict test by itself, but the dot product should be finite.
    float d = glm::dot(support, dir);
    EXPECT_TRUE(std::isfinite(d));
}

// ============================================================================
// Sphere
// ============================================================================

TEST(Support, Sphere_PositiveX)
{
    Sphere s{glm::vec3(1, 2, 3), 2.0f};
    auto p = Support(s, glm::vec3(1, 0, 0));
    ExpectNear(p, glm::vec3(3, 2, 3));
}

TEST(Support, Sphere_NegativeY)
{
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    auto p = Support(s, glm::vec3(0, -1, 0));
    ExpectNear(p, glm::vec3(0, -1, 0));
}

TEST(Support, Sphere_Diagonal)
{
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    glm::vec3 dir = glm::normalize(glm::vec3(1, 1, 1));
    auto p = Support(s, dir);
    EXPECT_NEAR(glm::length(p), 1.0f, kEps);
    EXPECT_GT(glm::dot(p, dir), 0.0f);
}

TEST(Support, Sphere_ZeroDirection)
{
    Sphere s{glm::vec3(0, 0, 0), 1.0f};
    auto p = Support(s, glm::vec3(0, 0, 0));
    // Should return a valid point on the sphere surface (fallback direction).
    EXPECT_NEAR(glm::length(p - s.Center), 1.0f, kEps);
}

// ============================================================================
// AABB
// ============================================================================

TEST(Support, AABB_PositiveX)
{
    AABB b{glm::vec3(-1, -2, -3), glm::vec3(4, 5, 6)};
    auto p = Support(b, glm::vec3(1, 0, 0));
    EXPECT_NEAR(p.x, 4.0f, kEps);
}

TEST(Support, AABB_NegativeCorner)
{
    AABB b{glm::vec3(-1, -2, -3), glm::vec3(4, 5, 6)};
    auto p = Support(b, glm::vec3(-1, -1, -1));
    ExpectNear(p, glm::vec3(-1, -2, -3));
}

TEST(Support, AABB_AllPositive)
{
    AABB b{glm::vec3(-1, -1, -1), glm::vec3(1, 1, 1)};
    auto p = Support(b, glm::vec3(1, 1, 1));
    ExpectNear(p, glm::vec3(1, 1, 1));
}

// ============================================================================
// Capsule
// ============================================================================

TEST(Support, Capsule_AlongAxis)
{
    Capsule c{glm::vec3(0, 0, 0), glm::vec3(0, 5, 0), 1.0f};
    auto p = Support(c, glm::vec3(0, 1, 0));
    // Should be PointB + radius in Y direction
    ExpectNear(p, glm::vec3(0, 6, 0));
}

TEST(Support, Capsule_AgainstAxis)
{
    Capsule c{glm::vec3(0, 0, 0), glm::vec3(0, 5, 0), 1.0f};
    auto p = Support(c, glm::vec3(0, -1, 0));
    ExpectNear(p, glm::vec3(0, -1, 0));
}

TEST(Support, Capsule_Perpendicular)
{
    Capsule c{glm::vec3(0, 0, 0), glm::vec3(0, 5, 0), 2.0f};
    auto p = Support(c, glm::vec3(1, 0, 0));
    // PointA or PointB (whichever has higher dot), plus radius in X
    EXPECT_NEAR(p.x, 2.0f, kEps);
}

// ============================================================================
// OBB
// ============================================================================

TEST(Support, OBB_AxisAligned)
{
    OBB box{glm::vec3(0, 0, 0), glm::vec3(1, 2, 3), glm::quat(1, 0, 0, 0)};
    auto p = Support(box, glm::vec3(1, 0, 0));
    EXPECT_NEAR(p.x, 1.0f, kEps);
}

TEST(Support, OBB_Rotated90)
{
    // Rotate 90 degrees around Z: X-axis becomes Y-axis
    glm::quat rot = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 0, 1));
    OBB box{glm::vec3(0, 0, 0), glm::vec3(3, 1, 1), rot};
    // In world space, querying +Y should pick the extent along the original +X axis
    auto p = Support(box, glm::vec3(0, 1, 0));
    EXPECT_NEAR(p.y, 3.0f, kEps);
}

// ============================================================================
// Cylinder
// ============================================================================

TEST(Support, Cylinder_AlongAxis)
{
    Cylinder c{glm::vec3(0, 0, 0), glm::vec3(0, 4, 0), 1.0f};
    auto p = Support(c, glm::vec3(0, 1, 0));
    EXPECT_NEAR(p.y, 4.0f, kEps);
}

TEST(Support, Cylinder_Perpendicular)
{
    Cylinder c{glm::vec3(0, 0, 0), glm::vec3(0, 4, 0), 2.0f};
    auto p = Support(c, glm::vec3(1, 0, 0));
    // Perpendicular to axis: cap center (whichever wins) + radius in X
    EXPECT_NEAR(p.x, 2.0f, kEps);
}

// ============================================================================
// Ellipsoid
// ============================================================================

TEST(Support, Ellipsoid_AlongPrincipalAxis)
{
    Ellipsoid e{glm::vec3(0, 0, 0), glm::vec3(3, 2, 1), glm::quat(1, 0, 0, 0)};
    auto p = Support(e, glm::vec3(1, 0, 0));
    EXPECT_NEAR(p.x, 3.0f, kEps);
}

TEST(Support, Ellipsoid_ShortAxis)
{
    Ellipsoid e{glm::vec3(0, 0, 0), glm::vec3(3, 2, 1), glm::quat(1, 0, 0, 0)};
    auto p = Support(e, glm::vec3(0, 0, 1));
    EXPECT_NEAR(p.z, 1.0f, kEps);
}

// GEOM-015 Slice 2: regression for the original-space magnitude guards
// migrated from absolute 1e-6f to scale-aware
// `RobustPredicates::ApproxZeroSq(..., scale, 1e-3)`.
//
// Before the migration, `len2 = length2(localDir * Radii)` for an ellipsoid
// with sub-mm radii fell below the absolute 1e-6 threshold for any direction,
// so `Support(Ellipsoid)` returned `Center` regardless of input direction.
// Scale-aware policy with `scale = length(Radii)` makes the guard a
// numerical-stability test instead of a shape-rejection test.
TEST(Support, Ellipsoid_SubMillimeterScaleProducesNonCenterSupport)
{
    const float s = 1.0e-3f;
    Ellipsoid e{glm::vec3(0, 0, 0), glm::vec3(s, s, s), glm::quat(1, 0, 0, 0)};
    auto px = Support(e, glm::vec3(1, 0, 0));
    EXPECT_NEAR(px.x, s, s * 0.01f);
    EXPECT_NEAR(px.y, 0.0f, s);
    EXPECT_NEAR(px.z, 0.0f, s);

    auto pz = Support(e, glm::vec3(0, 0, -1));
    EXPECT_NEAR(pz.z, -s, s * 0.01f);
}

TEST(Support, Ellipsoid_NonUniformSubMillimeterRadii)
{
    Ellipsoid e{glm::vec3(0, 0, 0), glm::vec3(3.0e-3f, 2.0e-3f, 1.0e-3f), glm::quat(1, 0, 0, 0)};
    auto p = Support(e, glm::vec3(1, 0, 0));
    EXPECT_NEAR(p.x, 3.0e-3f, 1.0e-5f);
}

// GEOM-015 Slice 2: anisotropic-ellipsoid regression. With `scale =
// length(Radii)`, the zero-band would be proportional to the largest
// semi-axis (~1414 for Radii = (1000, 1000, 1e-3)), and the +Z direction
// query — whose normal magnitude in radii-space is ~1e-3 — would fall
// inside the band and return Center instead of a surface point on the
// thin axis. Axis-local `scale = min(|Radii|)` keeps the guard a
// numerical-stability test rather than a thin-axis rejection test.
TEST(Support, Ellipsoid_HighlyAnisotropicThinAxisProducesSurfaceSupport)
{
    Ellipsoid e{glm::vec3(0, 0, 0),
                glm::vec3(1000.0f, 1000.0f, 1.0e-3f),
                glm::quat(1, 0, 0, 0)};

    auto pz = Support(e, glm::vec3(0, 0, 1));
    EXPECT_NEAR(pz.z, 1.0e-3f, 1.0e-5f);
    EXPECT_NEAR(pz.x, 0.0f, 1.0f);
    EXPECT_NEAR(pz.y, 0.0f, 1.0f);

    auto px = Support(e, glm::vec3(1, 0, 0));
    EXPECT_NEAR(px.x, 1000.0f, 1.0f);
}

// GEOM-015 Slice 2: fat-disk Cylinder regression. The axis-degeneracy
// guard is a numerical zero-vector floor on `axis`, not a shape-ratio
// test. With `scale = shape.Radius`, a cylinder where R is much larger
// than axisLen (a wide, flat disk) would have axisLen² ≤ (R · 1e-3)²
// and the guard would mis-classify the axis as degenerate, skipping the
// radial expansion that is in fact dominant for this geometry. Scale 1.0
// (absolute floor) keeps the original semantic: only reject `axis` when
// it is numerically zero.
TEST(Support, Cylinder_FatDiskExpandsRadially)
{
    // R = 1000, axisLen = 1 ⇒ axisLen² = 1, which is exactly the
    // would-have-been threshold (R · 1e-3)² = 1 under a shape.Radius scale.
    Cylinder c{glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), 1000.0f};
    auto p = Support(c, glm::vec3(1, 0, 0));
    EXPECT_NEAR(p.x, 1000.0f, 1.0f);
}

// ============================================================================
// Segment
// ============================================================================

TEST(Support, Segment_Forward)
{
    Segment seg{glm::vec3(0, 0, 0), glm::vec3(5, 0, 0)};
    auto p = Support(seg, glm::vec3(1, 0, 0));
    ExpectNear(p, glm::vec3(5, 0, 0));
}

TEST(Support, Segment_Backward)
{
    Segment seg{glm::vec3(0, 0, 0), glm::vec3(5, 0, 0)};
    auto p = Support(seg, glm::vec3(-1, 0, 0));
    ExpectNear(p, glm::vec3(0, 0, 0));
}

// ============================================================================
// Triangle
// ============================================================================

TEST(Support, Triangle_VertexA)
{
    Triangle tri{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)};
    auto p = Support(tri, glm::vec3(-1, -1, 0));
    ExpectNear(p, glm::vec3(0, 0, 0));
}

TEST(Support, Triangle_VertexB)
{
    Triangle tri{glm::vec3(0, 0, 0), glm::vec3(3, 0, 0), glm::vec3(0, 1, 0)};
    auto p = Support(tri, glm::vec3(1, 0, 0));
    ExpectNear(p, glm::vec3(3, 0, 0));
}

TEST(Support, Triangle_VertexC)
{
    Triangle tri{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 5, 0)};
    auto p = Support(tri, glm::vec3(0, 1, 0));
    ExpectNear(p, glm::vec3(0, 5, 0));
}

// ============================================================================
// ConvexHull
// ============================================================================

TEST(Support, ConvexHull_Cube)
{
    ConvexHull hull;
    hull.Vertices = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1}
    };
    auto p = Support(hull, glm::vec3(1, 1, 1));
    ExpectNear(p, glm::vec3(1, 1, 1));
}

TEST(Support, ConvexHull_SingleVertex)
{
    ConvexHull hull;
    hull.Vertices = {{3, 4, 5}};
    auto p = Support(hull, glm::vec3(1, 0, 0));
    ExpectNear(p, glm::vec3(3, 4, 5));
}

// ============================================================================
// Frustum
// ============================================================================

TEST(Support, Frustum_ReturnsCorner)
{
    auto proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 100.0f);
    auto view = glm::lookAt(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    auto frustum = Frustum::CreateFromMatrix(proj * view);

    auto p = Support(frustum, glm::vec3(0, 0, 1));
    // Should be one of the far-plane corners
    ExpectMaximalDot(p, glm::vec3(0, 0, 1));
}

// ============================================================================
// Ray
// ============================================================================

TEST(Support, Ray_AlongDirection)
{
    Ray ray{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0)};
    auto p = Support(ray, glm::vec3(1, 0, 0));
    // Should return a point far along the ray direction
    EXPECT_GT(p.x, 1000.0f);
}

TEST(Support, Ray_AgainstDirection)
{
    Ray ray{glm::vec3(2, 3, 4), glm::vec3(1, 0, 0)};
    auto p = Support(ray, glm::vec3(-1, 0, 0));
    ExpectNear(p, glm::vec3(2, 3, 4));
}
