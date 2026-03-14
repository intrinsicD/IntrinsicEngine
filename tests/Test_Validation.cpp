#include <gtest/gtest.h>
#include <cmath>
#include <limits>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import Geometry;

using namespace Geometry::Validation;

// =============================================================================
// Scalar IsFinite Tests
// =============================================================================

TEST(Validation_IsFinite, Float_FiniteValues)
{
    EXPECT_TRUE(IsFinite(0.0f));
    EXPECT_TRUE(IsFinite(1.0f));
    EXPECT_TRUE(IsFinite(-1.0f));
    EXPECT_TRUE(IsFinite(1e30f));
    EXPECT_TRUE(IsFinite(-1e-30f));
}

TEST(Validation_IsFinite, Float_NonFiniteValues)
{
    EXPECT_FALSE(IsFinite(std::numeric_limits<float>::infinity()));
    EXPECT_FALSE(IsFinite(-std::numeric_limits<float>::infinity()));
    EXPECT_FALSE(IsFinite(std::numeric_limits<float>::quiet_NaN()));
}

TEST(Validation_IsFinite, Double_FiniteValues)
{
    EXPECT_TRUE(IsFinite(0.0));
    EXPECT_TRUE(IsFinite(1e300));
    EXPECT_TRUE(IsFinite(-1e-300));
}

TEST(Validation_IsFinite, Double_NonFiniteValues)
{
    EXPECT_FALSE(IsFinite(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(IsFinite(-std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(IsFinite(std::numeric_limits<double>::quiet_NaN()));
}

// =============================================================================
// Vector IsFinite Tests
// =============================================================================

TEST(Validation_IsFinite, Vec2_AllFinite)
{
    EXPECT_TRUE(IsFinite(glm::vec2(1.0f, 2.0f)));
    EXPECT_TRUE(IsFinite(glm::vec2(0.0f, 0.0f)));
}

TEST(Validation_IsFinite, Vec2_AnyNonFinite)
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsFinite(glm::vec2(inf, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec2(0.0f, nan)));
    EXPECT_FALSE(IsFinite(glm::vec2(inf, nan)));
}

TEST(Validation_IsFinite, Vec3_AllFinite)
{
    EXPECT_TRUE(IsFinite(glm::vec3(1.0f, -2.0f, 3.0f)));
    EXPECT_TRUE(IsFinite(glm::vec3(0.0f)));
}

TEST(Validation_IsFinite, Vec3_AnyNonFinite)
{
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsFinite(glm::vec3(inf, 0.0f, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, nan, 0.0f)));
    EXPECT_FALSE(IsFinite(glm::vec3(0.0f, 0.0f, -inf)));
}

TEST(Validation_IsFinite, DVec3_AllFinite)
{
    EXPECT_TRUE(IsFinite(glm::dvec3(1.0, 2.0, 3.0)));
    EXPECT_TRUE(IsFinite(glm::dvec3(1e300, -1e-300, 0.0)));
}

TEST(Validation_IsFinite, DVec3_AnyNonFinite)
{
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(IsFinite(glm::dvec3(inf, 0.0, 0.0)));
    EXPECT_FALSE(IsFinite(glm::dvec3(0.0, 0.0, -inf)));
}

// =============================================================================
// IsNormalized / IsZero Tests
// =============================================================================

TEST(Validation_Vector, IsNormalized_UnitVectors)
{
    EXPECT_TRUE(IsNormalized(glm::vec3(1, 0, 0)));
    EXPECT_TRUE(IsNormalized(glm::vec3(0, 1, 0)));
    EXPECT_TRUE(IsNormalized(glm::vec3(0, 0, 1)));
    EXPECT_TRUE(IsNormalized(glm::normalize(glm::vec3(1, 1, 1))));
}

TEST(Validation_Vector, IsNormalized_NonUnitVectors)
{
    EXPECT_FALSE(IsNormalized(glm::vec3(2, 0, 0)));
    EXPECT_FALSE(IsNormalized(glm::vec3(0, 0, 0)));
    EXPECT_FALSE(IsNormalized(glm::vec3(0.5f, 0.5f, 0.5f)));
}

TEST(Validation_Vector, IsZero_ZeroVector)
{
    EXPECT_TRUE(IsZero(glm::vec3(0, 0, 0)));
    EXPECT_TRUE(IsZero(glm::vec3(1e-8f, 1e-8f, 1e-8f)));
}

TEST(Validation_Vector, IsZero_NonZeroVector)
{
    EXPECT_FALSE(IsZero(glm::vec3(1, 0, 0)));
    EXPECT_FALSE(IsZero(glm::vec3(0.01f, 0, 0)));
}

// =============================================================================
// Primitive Validation Tests
// =============================================================================

TEST(Validation_Primitives, Sphere_ValidAndInvalid)
{
    using namespace Geometry;
    Sphere valid{glm::vec3(0), 1.0f};
    EXPECT_TRUE(IsValid(valid));

    Sphere zeroRadius{glm::vec3(0), 0.0f};
    EXPECT_FALSE(IsValid(zeroRadius));

    Sphere negRadius{glm::vec3(0), -1.0f};
    EXPECT_FALSE(IsValid(negRadius));

    Sphere nanCenter{glm::vec3(std::numeric_limits<float>::quiet_NaN()), 1.0f};
    EXPECT_FALSE(IsValid(nanCenter));
}

TEST(Validation_Primitives, AABB_ValidAndInvalid)
{
    using namespace Geometry;
    AABB valid{glm::vec3(-1), glm::vec3(1)};
    EXPECT_TRUE(IsValid(valid));
    EXPECT_FALSE(IsDegenerate(valid));

    // Min > Max
    AABB inverted{glm::vec3(1), glm::vec3(-1)};
    EXPECT_FALSE(IsValid(inverted));

    // Degenerate (zero thickness)
    AABB flat{glm::vec3(0, 0, 0), glm::vec3(1, 1, 0)};
    EXPECT_TRUE(IsDegenerate(flat));
}

TEST(Validation_Primitives, Triangle_ValidAndDegenerate)
{
    using namespace Geometry;
    Triangle valid{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(0, 1, 0)};
    EXPECT_TRUE(IsValid(valid));
    EXPECT_FALSE(IsDegenerate(valid));

    // Collinear points
    Triangle degen{glm::vec3(0, 0, 0), glm::vec3(1, 0, 0), glm::vec3(2, 0, 0)};
    EXPECT_TRUE(IsDegenerate(degen));

    // Coincident points
    Triangle coinc{glm::vec3(1, 1, 1), glm::vec3(1, 1, 1), glm::vec3(1, 1, 1)};
    EXPECT_TRUE(IsDegenerate(coinc));
}

TEST(Validation_Primitives, OBB_ValidAndDegenerate)
{
    using namespace Geometry;
    OBB valid{glm::vec3(0), glm::vec3(1), glm::quat(1, 0, 0, 0)};
    EXPECT_TRUE(IsValid(valid));
    EXPECT_FALSE(IsDegenerate(valid));

    OBB zeroExtent{glm::vec3(0), glm::vec3(0, 1, 1), glm::quat(1, 0, 0, 0)};
    EXPECT_FALSE(IsValid(zeroExtent));
}

TEST(Validation_Primitives, Capsule_ValidAndDegenerate)
{
    using namespace Geometry;
    Capsule valid{glm::vec3(0, 0, 0), glm::vec3(0, 1, 0), 0.5f};
    EXPECT_TRUE(IsValid(valid));
    EXPECT_FALSE(IsDegenerate(valid));

    Capsule degen{glm::vec3(1, 1, 1), glm::vec3(1, 1, 1), 0.5f};
    EXPECT_TRUE(IsDegenerate(degen));
}

TEST(Validation_Primitives, Ray_ValidAndInvalid)
{
    using namespace Geometry;
    Ray valid{glm::vec3(0), glm::vec3(0, 0, 1)};
    EXPECT_TRUE(IsValid(valid));

    Ray zeroDir{glm::vec3(0), glm::vec3(0, 0, 0)};
    EXPECT_FALSE(IsValid(zeroDir));
}

TEST(Validation_Primitives, Plane_ValidAndInvalid)
{
    using namespace Geometry;
    Plane valid{glm::vec3(0, 1, 0), 1.0f};
    EXPECT_TRUE(IsValid(valid));

    Plane zeroNormal{glm::vec3(0, 0, 0), 1.0f};
    EXPECT_FALSE(IsValid(zeroNormal));

    Plane nanDist{glm::vec3(0, 1, 0), std::numeric_limits<float>::quiet_NaN()};
    EXPECT_FALSE(IsValid(nanDist));
}

TEST(Validation_Primitives, Segment_ValidAndDegenerate)
{
    using namespace Geometry;
    Segment valid{glm::vec3(0), glm::vec3(1, 0, 0)};
    EXPECT_TRUE(IsValid(valid));
    EXPECT_FALSE(IsDegenerate(valid));

    Segment degen{glm::vec3(5, 5, 5), glm::vec3(5, 5, 5)};
    EXPECT_TRUE(IsDegenerate(degen));
}

// =============================================================================
// Sanitize Tests
// =============================================================================

TEST(Validation_Sanitize, Sphere_FixesNonFiniteCenter)
{
    using namespace Geometry;
    Sphere bad{glm::vec3(std::numeric_limits<float>::quiet_NaN()), -1.0f};
    Sphere fixed = Sanitize(bad);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_EQ(fixed.Center, glm::vec3(0.0f));
    EXPECT_GT(fixed.Radius, 0.0f);
}

TEST(Validation_Sanitize, AABB_FixesInvertedMinMax)
{
    using namespace Geometry;
    AABB bad{glm::vec3(5, 5, 5), glm::vec3(-5, -5, -5)};
    AABB fixed = Sanitize(bad);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_LE(fixed.Min.x, fixed.Max.x);
    EXPECT_LE(fixed.Min.y, fixed.Max.y);
    EXPECT_LE(fixed.Min.z, fixed.Max.z);
}

TEST(Validation_Sanitize, Ray_FixesZeroDirection)
{
    using namespace Geometry;
    Ray bad{glm::vec3(0), glm::vec3(0, 0, 0)};
    Ray fixed = Sanitize(bad);
    EXPECT_TRUE(IsValid(fixed));
    EXPECT_TRUE(IsNormalized(fixed.Direction));
}

TEST(Validation_Sanitize, OBB_FixesZeroQuaternion)
{
    using namespace Geometry;
    OBB bad{glm::vec3(0), glm::vec3(0), glm::quat(0, 0, 0, 0)};
    OBB fixed = Sanitize(bad);
    EXPECT_TRUE(IsValid(fixed));
}
