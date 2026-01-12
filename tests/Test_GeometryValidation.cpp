#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <cmath>
#include <limits>

import Geometry;

using namespace Geometry;
using namespace Geometry::Validation;

// -----------------------------------------------------------------------------
// Vector Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, IsFinite_ValidVector)
{
    EXPECT_TRUE(IsFinite(glm::vec3(1.0f, 2.0f, 3.0f)));
    EXPECT_TRUE(IsFinite(glm::vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(IsFinite(glm::vec3(-1e20f, 1e20f, 0.0f)));
}

TEST(GeometryValidation, IsFinite_NaN)
{
    float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsFinite(glm::vec3(nan, 0.0f, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, nan, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, 0.0f, nan)));
}

TEST(GeometryValidation, IsFinite_Infinity)
{
    float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsFinite(glm::vec3(inf, 0.0f, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, -inf, 0.0f)));
}

TEST(GeometryValidation, IsNormalized_UnitVectors)
{
    EXPECT_TRUE(IsNormalized(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(IsNormalized(glm::vec3(0.0f, 1.0f, 0.0f)));
    EXPECT_TRUE(IsNormalized(glm::vec3(0.0f, 0.0f, 1.0f)));

    glm::vec3 diagonal = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(IsNormalized(diagonal));
}

TEST(GeometryValidation, IsNormalized_NonUnitVectors)
{
    EXPECT_FALSE(IsNormalized(glm::vec3(2.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(IsNormalized(glm::vec3(0.5f, 0.0f, 0.0f)));
    EXPECT_FALSE(IsNormalized(glm::vec3(0.0f, 0.0f, 0.0f)));
}

TEST(GeometryValidation, IsZero_ZeroVector)
{
    EXPECT_TRUE(IsZero(glm::vec3(0.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(IsZero(glm::vec3(1e-10f, 1e-10f, 1e-10f)));
}

TEST(GeometryValidation, IsZero_NonZeroVector)
{
    EXPECT_FALSE(IsZero(glm::vec3(1.0f, 0.0f, 0.0f)));
    EXPECT_FALSE(IsZero(glm::vec3(0.1f, 0.0f, 0.0f)));
}

// -----------------------------------------------------------------------------
// Sphere Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Sphere_Valid)
{
    Sphere s{{0, 0, 0}, 1.0f};
    EXPECT_TRUE(IsValid(s));

    Sphere s2{{100, -50, 25}, 0.001f};
    EXPECT_TRUE(IsValid(s2));
}

TEST(GeometryValidation, Sphere_Invalid_ZeroRadius)
{
    Sphere s{{0, 0, 0}, 0.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Invalid_NegativeRadius)
{
    Sphere s{{0, 0, 0}, -1.0f};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Invalid_InfiniteRadius)
{
    Sphere s{{0, 0, 0}, std::numeric_limits<float>::infinity()};
    EXPECT_FALSE(IsValid(s));
}

TEST(GeometryValidation, Sphere_Invalid_NaNCenter)
{
    Sphere s{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, 1.0f};
    EXPECT_FALSE(IsValid(s));
}

// -----------------------------------------------------------------------------
// AABB Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, AABB_Valid)
{
    AABB box{{-1, -1, -1}, {1, 1, 1}};
    EXPECT_TRUE(IsValid(box));
}

TEST(GeometryValidation, AABB_Invalid_Inverted)
{
    AABB box{{1, 1, 1}, {-1, -1, -1}};  // Min > Max
    EXPECT_FALSE(IsValid(box));
}

TEST(GeometryValidation, AABB_Valid_Degenerate)
{
    AABB box{{0, 0, 0}, {0, 0, 0}};  // Point box - valid but degenerate
    EXPECT_TRUE(IsValid(box));
    EXPECT_TRUE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_NotDegenerate)
{
    AABB box{{0, 0, 0}, {1, 1, 1}};
    EXPECT_FALSE(IsDegenerate(box));
}

TEST(GeometryValidation, AABB_Degenerate_FlatBox)
{
    AABB box{{0, 0, 0}, {1, 1, 0}};  // Flat in Z
    EXPECT_TRUE(IsDegenerate(box));
}

// -----------------------------------------------------------------------------
// OBB Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, OBB_Valid)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);  // Identity

    EXPECT_TRUE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Invalid_ZeroExtent)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {0, 1, 1};
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_FALSE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Invalid_UnnormalizedRotation)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(2, 0, 0, 0);  // Not normalized

    EXPECT_FALSE(IsValid(obb));
}

TEST(GeometryValidation, OBB_Degenerate)
{
    OBB obb;
    obb.Center = {0, 0, 0};
    obb.Extents = {1e-8f, 1, 1};  // Nearly zero X extent
    obb.Rotation = glm::quat(1, 0, 0, 0);

    EXPECT_TRUE(IsDegenerate(obb));
}

// -----------------------------------------------------------------------------
// Capsule Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Capsule_Valid)
{
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.5f};
    EXPECT_TRUE(IsValid(cap));
}

TEST(GeometryValidation, Capsule_Invalid_ZeroRadius)
{
    Capsule cap{{0, -1, 0}, {0, 1, 0}, 0.0f};
    EXPECT_FALSE(IsValid(cap));
}

TEST(GeometryValidation, Capsule_Degenerate_SameEndpoints)
{
    Capsule cap{{0, 0, 0}, {0, 0, 0}, 1.0f};
    EXPECT_TRUE(IsDegenerate(cap));  // Line has zero length
}

// -----------------------------------------------------------------------------
// Triangle Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Triangle_Valid)
{
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    EXPECT_TRUE(IsValid(tri));
    EXPECT_FALSE(IsDegenerate(tri));
}

TEST(GeometryValidation, Triangle_Degenerate_Collinear)
{
    Triangle tri{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};  // All on X-axis
    EXPECT_TRUE(IsDegenerate(tri));
}

TEST(GeometryValidation, Triangle_Degenerate_Coincident)
{
    Triangle tri{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};  // All at origin
    EXPECT_TRUE(IsDegenerate(tri));
}

// -----------------------------------------------------------------------------
// Ray Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Ray_Valid)
{
    Ray r{{0, 0, 0}, {1, 0, 0}};
    EXPECT_TRUE(IsValid(r));
}

TEST(GeometryValidation, Ray_Invalid_ZeroDirection)
{
    Ray r{{0, 0, 0}, {0, 0, 0}};
    EXPECT_FALSE(IsValid(r));
}

TEST(GeometryValidation, Ray_Invalid_NaNOrigin)
{
    Ray r{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, {1, 0, 0}};
    EXPECT_FALSE(IsValid(r));
}

// -----------------------------------------------------------------------------
// Plane Validation Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Plane_Valid)
{
    Plane p{glm::vec3(0, 1, 0), 5.0f};
    EXPECT_TRUE(IsValid(p));
}

TEST(GeometryValidation, Plane_Invalid_ZeroNormal)
{
    Plane p{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_FALSE(IsValid(p));
}

TEST(GeometryValidation, Plane_Invalid_NaNDistance)
{
    Plane p{glm::vec3(0, 1, 0), std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(IsValid(p));
}

// -----------------------------------------------------------------------------
// Sanitization Tests
// -----------------------------------------------------------------------------

TEST(GeometryValidation, Sanitize_Sphere_Valid)
{
    Sphere s{{1, 2, 3}, 5.0f};
    Sphere sanitized = Sanitize(s);

    EXPECT_EQ(sanitized.Center, s.Center);
    EXPECT_EQ(sanitized.Radius, s.Radius);
}

TEST(GeometryValidation, Sanitize_Sphere_Invalid)
{
    Sphere s{{std::numeric_limits<float>::quiet_NaN(), 0, 0}, -1.0f};
    Sphere sanitized = Sanitize(s);

    EXPECT_TRUE(IsValid(sanitized));
    EXPECT_EQ(sanitized.Center, glm::vec3(0.0f));
    EXPECT_EQ(sanitized.Radius, 1.0f);
}

TEST(GeometryValidation, Sanitize_AABB_Inverted)
{
    AABB box{{10, 10, 10}, {0, 0, 0}};
    AABB sanitized = Sanitize(box);

    EXPECT_TRUE(IsValid(sanitized));
    EXPECT_LE(sanitized.Min.x, sanitized.Max.x);
    EXPECT_LE(sanitized.Min.y, sanitized.Max.y);
    EXPECT_LE(sanitized.Min.z, sanitized.Max.z);
}

TEST(GeometryValidation, Sanitize_Ray_ZeroDirection)
{
    Ray r{{5, 5, 5}, {0, 0, 0}};
    Ray sanitized = Sanitize(r);

    EXPECT_TRUE(IsValid(sanitized));
    EXPECT_EQ(sanitized.Origin, glm::vec3(5, 5, 5));  // Origin preserved
    EXPECT_NE(sanitized.Direction, glm::vec3(0, 0, 0));
    EXPECT_TRUE(IsNormalized(sanitized.Direction));
}

TEST(GeometryValidation, Sanitize_OBB_UnnormalizedRotation)
{
    OBB obb;
    obb.Center = {1, 2, 3};
    obb.Extents = {1, 1, 1};
    obb.Rotation = glm::quat(10, 5, 3, 1);  // Not normalized

    OBB sanitized = Sanitize(obb);

    EXPECT_TRUE(IsValid(sanitized));
    // Check quaternion is normalized: w^2 + x^2 + y^2 + z^2 = 1
    float rotLenSq = sanitized.Rotation.w * sanitized.Rotation.w +
                     sanitized.Rotation.x * sanitized.Rotation.x +
                     sanitized.Rotation.y * sanitized.Rotation.y +
                     sanitized.Rotation.z * sanitized.Rotation.z;
    EXPECT_NEAR(rotLenSq, 1.0f, 1e-4f);
}

