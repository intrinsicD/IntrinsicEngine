// tests/Test_RuntimeGeometry_Validation.cpp
// Tests for the Validation utilities module
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <limits>
#include <cmath>

import Runtime.Geometry.Primitives;
import Runtime.Geometry.Validation;

using namespace Runtime::Geometry;
using namespace Runtime::Geometry::Validation;

// =========================================================================
// VECTOR VALIDATION TESTS
// =========================================================================

TEST(GeometryValidation, IsFinite_ValidVector)
{
    glm::vec3 v{1.0f, 2.0f, 3.0f};
    EXPECT_TRUE(IsFinite(v));
}

TEST(GeometryValidation, IsFinite_InfinityX)
{
    glm::vec3 v{std::numeric_limits<float>::infinity(), 0, 0};
    EXPECT_FALSE(IsFinite(v));
}

TEST(GeometryValidation, IsFinite_NaN)
{
    glm::vec3 v{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT_FALSE(IsFinite(v));
}

TEST(GeometryValidation, IsNormalized_UnitVector)
{
    glm::vec3 v{1, 0, 0};
    EXPECT_TRUE(IsNormalized(v));
}

TEST(GeometryValidation, IsNormalized_NotNormalized)
{
    glm::vec3 v{2, 0, 0};
    EXPECT_FALSE(IsNormalized(v));
}

TEST(GeometryValidation, IsNormalized_AlmostNormalized)
{
    glm::vec3 v{0.9999f, 0, 0};
    EXPECT_TRUE(IsNormalized(v, 0.01f)); // With tolerance
    EXPECT_FALSE(IsNormalized(v, 0.0001f)); // Stricter tolerance
}

TEST(GeometryValidation, IsZero_ZeroVector)
{
    glm::vec3 v{0, 0, 0};
    EXPECT_TRUE(IsZero(v));
}

TEST(GeometryValidation, IsZero_NearZero)
{
    glm::vec3 v{1e-8f, 1e-8f, 1e-8f};
    EXPECT_TRUE(IsZero(v, 1e-6f));
}

TEST(GeometryValidation, IsZero_NotZero)
{
    glm::vec3 v{0.1f, 0, 0};
    EXPECT_FALSE(IsZero(v));
}

// =========================================================================
// SPHERE VALIDATION
// =========================================================================

TEST(GeometryValidation, Sphere_Valid)
{
    Sphere s{{0, 0, 0}, 1.0f};
    EXPECT_TRUE(IsValid(s));
}

TEST(GeometryValidation, Sphere_NegativeRadius)
{
    Sphere s{{0, 0, 0}, -1.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_ZeroRadius)
{
    Sphere s{{0, 0, 0}, 0.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_InfiniteRadius)
{
    Sphere s{{0, 0, 0}, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_InfiniteCenter)
{
    Sphere s{{std::numeric_limits<float>::infinity(), 0, 0}, 1.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Sanitize)
{
    Sphere s{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, -5.0f};
    EXPECT_FALSE(IsValid(s));

    Sphere fixed = Sanitize(s);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_GT(fixed.Radius, 0.0f);
    EXPECT_TRUE(IsFinite(fixed.Center));
}

// =========================================================================
// AABB VALIDATION
// =========================================================================

TEST(GeometryValidation, AABB_Valid)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    EXPECT_TRUE(IsValid(box));
}

TEST(GeometryValidation, AABB_Inverted)
{
    AABB box{{1, 1, 1}, {0, 0, 0}};
    EXPECT_FALSE(IsValid(box));
}

TEST(GeometryValidation, AABB_Degenerate_Point)
{
    AABB box{{1, 1, 1}, {1, 1, 1}};
    EXPECT_TRUE(IsValid(box)); // Valid but degenerate
    EXPECT_TRUE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_Degenerate_Line)
{
    AABB box{{0, 0, 0}, {1, 0, 0}};
    EXPECT_TRUE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_Degenerate_Plane)
{
    AABB box{{0, 0, 0}, {1, 1, 0}};
    EXPECT_TRUE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_Sanitize_Inverted)
{
    AABB box{{2, 2, 2}, {1, 1, 1}};
    EXPECT_FALSE(IsValid(box));

    AABB fixed = Sanitize(box);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_LE(fixed.Min.x, fixed.Max.x);
    EXPECT_LE(fixed.Min.y, fixed.Max.y);
    EXPECT_LE(fixed.Min.z, fixed.Max.z);
}

TEST(GeometryValidation, AABB_Sanitize_Degenerate)
{
    AABB box{{0, 0, 0}, {0, 0, 0}};
    EXPECT_TRUE(IsDegenerate(box));

    AABB fixed = Sanitize(box);
    EXPECT_FALSE(IsDegenerate(fixed));
    EXPECT_GT(fixed.Max.x - fixed.Min.x, 0.0f);
}

// =========================================================================
// OBB VALIDATION
// =========================================================================

TEST(GeometryValidation, OBB_Valid)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(IsValid(obb));
}

TEST(GeometryValidation, OBB_NegativeExtent)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {-1, 1, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_FALSE(IsValid(obb));
}

TEST(GeometryValidation, OBB_UnnormalizedQuaternion)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(2, 0, 0, 0); // Length = 2

    EXPECT_FALSE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Degenerate)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 0, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(IsDegenerate(obb));
}

TEST(GeometryValidation, OBB_Sanitize)
{
    OBB obb;
    obb.Center = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    obb.Extents = {0, -1, 0};
    obb.Rotation = glm::quat(5, 0, 0, 0);

    EXPECT_FALSE(IsValid(obb));

    OBB fixed = Sanitize(obb);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_TRUE(IsFinite(fixed.Center));
    EXPECT_FALSE(IsDegenerate(fixed));
    EXPECT_NEAR(glm::length(fixed.Rotation), 1.0f, 1e-4f);
}

// =========================================================================
// CAPSULE VALIDATION
// =========================================================================

TEST(GeometryValidation, Capsule_Valid)
{
    Capsule cap{{0, 0, 0}, {0, 1, 0}, 0.5f};
    EXPECT_TRUE(IsValid(cap));
}

TEST(GeometryValidation, Capsule_NegativeRadius)
{
    Capsule cap{{0, 0, 0}, {0, 1, 0}, -0.5f};
    EXPECT_FALSE(IsValid(cap));
}

TEST(GeometryValidation, Capsule_Degenerate)
{
    Capsule cap{{1, 1, 1}, {1, 1, 1}, 0.5f};
    EXPECT_TRUE(IsDegenerate(cap));
}

TEST(GeometryValidation, Capsule_InfinitePoint)
{
    Capsule cap{{std::numeric_limits<float>::infinity(), 0, 0}, {0, 1, 0}, 0.5f};
    EXPECT_FALSE(IsValid(cap));
}

// =========================================================================
// CYLINDER VALIDATION
// =========================================================================

TEST(GeometryValidation, Cylinder_Valid)
{
    Cylinder cyl{{0, 0, 0}, {0, 1, 0}, 0.5f};
    EXPECT_TRUE(IsValid(cyl));
}

TEST(GeometryValidation, Cylinder_Degenerate)
{
    Cylinder cyl{{1, 1, 1}, {1, 1, 1}, 0.5f};
    EXPECT_TRUE(IsDegenerate(cyl));
}

TEST(GeometryValidation, Cylinder_ZeroRadius)
{
    Cylinder cyl{{0, 0, 0}, {0, 1, 0}, 0.0f};
    EXPECT_FALSE(IsValid(cyl));
}

// =========================================================================
// ELLIPSOID VALIDATION
// =========================================================================

TEST(GeometryValidation, Ellipsoid_Valid)
{
    Ellipsoid e;
    e.Center = {0, 0, 0};
    e.Radii = {1, 2, 1};
    e.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(IsValid(e));
}

TEST(GeometryValidation, Ellipsoid_NegativeRadius)
{
    Ellipsoid e;
    e.Center = {0, 0, 0};
    e.Radii = {1, -2, 1};
    e.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_FALSE(IsValid(e));
}

TEST(GeometryValidation, Ellipsoid_Degenerate)
{
    Ellipsoid e;
    e.Center = {0, 0, 0};
    e.Radii = {1, 0, 1};
    e.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(IsDegenerate(e));
}

// =========================================================================
// TRIANGLE VALIDATION
// =========================================================================

TEST(GeometryValidation, Triangle_Valid)
{
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    EXPECT_TRUE(IsValid(tri));
}

TEST(GeometryValidation, Triangle_Degenerate_Collinear)
{
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    EXPECT_TRUE(IsDegenerate(tri));
}

TEST(GeometryValidation, Triangle_Degenerate_Point)
{
    Triangle tri{{1, 1, 1}, {1, 1, 1}, {1, 1, 1}};
    EXPECT_TRUE(IsDegenerate(tri));
}

TEST(GeometryValidation, Triangle_InfiniteVertex)
{
    Triangle tri{{std::numeric_limits<float>::infinity(), 0, 0}, {1, 0, 0}, {0, 1, 0}};
    EXPECT_FALSE(IsValid(tri));
}

// =========================================================================
// PLANE VALIDATION
// =========================================================================

TEST(GeometryValidation, Plane_Valid)
{
    Plane p{{0, 1, 0}, 5.0f};
    EXPECT_TRUE(IsValid(p));
}

TEST(GeometryValidation, Plane_ZeroNormal)
{
    Plane p{{0, 0, 0}, 5.0f};
    EXPECT_FALSE(p.IsValid());
}

TEST(GeometryValidation, Plane_InfiniteDistance)
{
    Plane p{{0, 1, 0}, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(p.IsValid());
}

TEST(GeometryValidation, Plane_NaNDistance)
{
    Plane p{{0, 1, 0}, std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(p.IsValid());
}

TEST(GeometryValidation, Plane_Normalize_ZeroNormal)
{
    Plane p{{0, 0, 0}, 10.0f};
    EXPECT_FALSE(p.IsValid());

    p.Normalize();
    EXPECT_TRUE(p.IsValid());
    EXPECT_TRUE(IsFinite(p.Normal));
    EXPECT_FALSE(IsZero(p.Normal));
}

// =========================================================================
// RAY VALIDATION
// =========================================================================

TEST(GeometryValidation, Ray_Valid)
{
    Ray r{{0, 0, 0}, {0, 0, 1}};
    EXPECT_TRUE(IsValid(r));
}

TEST(GeometryValidation, Ray_ZeroDirection)
{
    Ray r{{0, 0, 0}, {0, 0, 0}};
    EXPECT_FALSE(IsValid(r));
}

TEST(GeometryValidation, Ray_InfiniteOrigin)
{
    Ray r{{std::numeric_limits<float>::infinity(), 0, 0}, {0, 0, 1}};
    EXPECT_FALSE(IsValid(r));
}

TEST(GeometryValidation, Ray_Sanitize)
{
    Ray r{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, {0, 0, 0}};
    EXPECT_FALSE(IsValid(r));

    Ray fixed = Sanitize(r);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_TRUE(IsFinite(fixed.Origin));
    EXPECT_FALSE(IsZero(fixed.Direction));
    EXPECT_TRUE(IsNormalized(fixed.Direction));
}

// =========================================================================
// SEGMENT VALIDATION
// =========================================================================

TEST(GeometryValidation, Segment_Valid)
{
    Segment seg{{0, 0, 0}, {1, 1, 1}};
    EXPECT_TRUE(IsValid(seg));
}

TEST(GeometryValidation, Segment_Degenerate)
{
    Segment seg{{1, 1, 1}, {1, 1, 1}};
    EXPECT_TRUE(IsDegenerate(seg));
}

TEST(GeometryValidation, Segment_InfinitePoint)
{
    Segment seg{{std::numeric_limits<float>::infinity(), 0, 0}, {1, 1, 1}};
    EXPECT_FALSE(IsValid(seg));
}

// =========================================================================
// CONVEX HULL VALIDATION
// =========================================================================

TEST(GeometryValidation, ConvexHull_Valid)
{
    ConvexHull hull;
    hull.Vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    hull.Planes = {{{1, 0, 0}, 0}};

    EXPECT_TRUE(IsValid(hull));
}

TEST(GeometryValidation, ConvexHull_EmptyVertices)
{
    ConvexHull hull;
    EXPECT_FALSE(IsValid(hull));
}

TEST(GeometryValidation, ConvexHull_InvalidVertex)
{
    ConvexHull hull;
    hull.Vertices = {{std::numeric_limits<float>::quiet_NaN(), 0, 0}};

    EXPECT_FALSE(IsValid(hull));
}

TEST(GeometryValidation, ConvexHull_InvalidPlane)
{
    ConvexHull hull;
    hull.Vertices = {{0, 0, 0}};
    hull.Planes = {{{0, 0, 0}, 5.0f}}; // Zero normal

    EXPECT_FALSE(IsValid(hull));
}

// =========================================================================
// FRUSTUM VALIDATION
// =========================================================================

TEST(GeometryValidation, Frustum_Valid)
{
    Frustum f;
    for (int i = 0; i < 6; i++)
    {
        f.Planes[i] = Plane{{0, 1, 0}, 0};
        f.Planes[i].Normalize();
    }

    for (int i = 0; i < 8; i++)
    {
        f.Corners[i] = glm::vec3(i, i, i);
    }

    EXPECT_TRUE(IsValid(f));
}

TEST(GeometryValidation, Frustum_InvalidPlane)
{
    Frustum f;
    for (int i = 0; i < 6; i++)
    {
        f.Planes[i] = Plane{{0, 1, 0}, 0};
    }
    f.Planes[0] = Plane{{0, 0, 0}, std::numeric_limits<float>::quiet_NaN()};

    for (int i = 0; i < 8; i++)
    {
        f.Corners[i] = glm::vec3(0, 0, 0);
    }

    EXPECT_FALSE(IsValid(f));
}

TEST(GeometryValidation, Frustum_InvalidCorner)
{
    Frustum f;
    for (int i = 0; i < 6; i++)
    {
        f.Planes[i] = Plane{{0, 1, 0}, 0};
        f.Planes[i].Normalize();
    }

    for (int i = 0; i < 8; i++)
    {
        f.Corners[i] = glm::vec3(0, 0, 0);
    }
    f.Corners[0] = glm::vec3(std::numeric_limits<float>::infinity(), 0, 0);

    EXPECT_FALSE(IsValid(f));
}
