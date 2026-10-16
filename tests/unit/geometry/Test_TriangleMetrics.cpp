#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include <glm/glm.hpp>

import Geometry.Triangle;

namespace
{
    void ExpectVecNear(const glm::vec3& actual, const glm::vec3& expected, float tolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }
}

TEST(TriangleMetrics, ThreeFourFiveTriangleMatchesAnalyticValues)
{
    const Geometry::Triangle triangle{
        {0.0f, 0.0f, 0.0f},
        {4.0f, 0.0f, 0.0f},
        {0.0f, 3.0f, 0.0f},
    };

    ExpectVecNear(triangle.EdgeLengths(), {5.0f, 3.0f, 4.0f}, 1.0e-5f);
    EXPECT_NEAR(triangle.Perimeter(), 12.0f, 1.0e-5f);
    EXPECT_NEAR(triangle.StableArea(), 6.0f, 1.0e-5f);

    const glm::vec3 angles = triangle.Angles();
    EXPECT_NEAR(angles.x, std::numbers::pi_v<float> * 0.5f, 1.0e-5f);
    EXPECT_NEAR(angles.y, std::acos(0.8f), 1.0e-5f);
    EXPECT_NEAR(angles.z, std::acos(0.6f), 1.0e-5f);
    EXPECT_NEAR(angles.x + angles.y + angles.z, std::numbers::pi_v<float>, 1.0e-5f);
}

TEST(TriangleMetrics, SafeAcosClampsOutsideDomain)
{
    EXPECT_TRUE(std::isfinite(Geometry::SafeAcos(1.0f + 1.0e-5f)));
    EXPECT_TRUE(std::isfinite(Geometry::SafeAcos(-1.0f - 1.0e-5f)));
    EXPECT_NEAR(Geometry::SafeAcos(1.0f + 1.0e-5f), 0.0f, 1.0e-6f);
    EXPECT_NEAR(Geometry::SafeAcos(-1.0f - 1.0e-5f), std::numbers::pi_v<float>, 1.0e-6f);
    EXPECT_TRUE(std::isfinite(Geometry::SafeAcos(std::numeric_limits<float>::quiet_NaN())));
}

TEST(TriangleMetrics, DegenerateTriangleReturnsFiniteZeroedMetrics)
{
    const Geometry::Triangle collinear{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
        {2.0f, 0.0f, 0.0f},
    };

    ExpectVecNear(collinear.EdgeLengths(), {0.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_EQ(collinear.Perimeter(), 0.0f);
    ExpectVecNear(collinear.Angles(), {0.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_EQ(collinear.StableArea(), 0.0f);
}

TEST(TriangleMetrics, NonFiniteTriangleReturnsFiniteZeroedMetrics)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Geometry::Triangle bad{{0.0f, 0.0f, 0.0f}, {nan, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

    ExpectVecNear(bad.EdgeLengths(), {0.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_EQ(bad.Perimeter(), 0.0f);
    ExpectVecNear(bad.Angles(), {0.0f, 0.0f, 0.0f}, 0.0f);
    EXPECT_EQ(bad.StableArea(), 0.0f);
}


TEST(TriangleMetrics, UniformScalePreservesNormalAndInteriorProjection)
{
    for (const float scale : {1.0e-8f, 5.0e-4f, 1.0f, 1.0e8f})
    {
        SCOPED_TRACE(scale);
        const Geometry::Triangle triangle{{0, 0, 0}, {scale, 0, 0}, {0, scale, 0}};
        ExpectVecNear(triangle.GetNormal(), {0, 0, 1}, 1.0e-6f);
        const glm::vec3 projected = Geometry::ClosestPoint(triangle, {scale * .2f, scale * .2f, scale * .3f});
        ExpectVecNear(projected / scale, {.2f, .2f, 0}, 1.0e-6f);
        ExpectVecNear(triangle.EdgeLengths() / scale, {std::sqrt(2.f), 1, 1}, 1.0e-6f);
        EXPECT_NEAR(triangle.Perimeter() / scale, 2.f + std::sqrt(2.f), 1.0e-6f);
        ExpectVecNear(triangle.Angles(), {std::numbers::pi_v<float> * .5f,
                      std::numbers::pi_v<float> * .25f, std::numbers::pi_v<float> * .25f}, 1.0e-6f);
    }
    const Geometry::Triangle collinear{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
    EXPECT_EQ(collinear.GetNormal(), glm::vec3(0));
    ExpectVecNear(Geometry::ClosestPoint(collinear, {.5f, 1, 0}), {.5f, 0, 0}, 1.0e-6f);
}
