#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

import Geometry.Curve;

namespace
{
    constexpr float kTolerance = 1.0e-5f;

    void ExpectVecNear(const glm::vec3& actual, const glm::vec3& expected, float tolerance = kTolerance)
    {
        EXPECT_NEAR(actual.x, expected.x, tolerance);
        EXPECT_NEAR(actual.y, expected.y, tolerance);
        EXPECT_NEAR(actual.z, expected.z, tolerance);
    }
}

TEST(BezierCurve, ReportsDegreeAndInterpolatesEndpoints)
{
    const Geometry::BezierCurve curve{{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 2.0f, 0.0f},
        {3.0f, 2.0f, 0.0f},
        {4.0f, 0.0f, 0.0f},
    }};

    ASSERT_TRUE(curve.GetDegree().has_value());
    EXPECT_EQ(*curve.GetDegree(), 3u);

    const auto first = Geometry::EvaluateDeCasteljau(curve, 0.0f);
    const auto last = Geometry::EvaluateBernstein(curve, 1.0f);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(last.has_value());
    ExpectVecNear(*first, curve.ControlPoints.front());
    ExpectVecNear(*last, curve.ControlPoints.back());
}

TEST(BezierCurve, DegreeOneEqualsLinearLerp)
{
    const Geometry::BezierCurve curve{{glm::vec3{-2.0f, 1.0f, 3.0f}, glm::vec3{6.0f, -3.0f, 1.0f}}};

    for (const float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
    {
        const auto point = Geometry::EvaluateDeCasteljau(curve, t);
        ASSERT_TRUE(point.has_value());
        ExpectVecNear(*point, curve.ControlPoints[0] * (1.0f - t) + curve.ControlPoints[1] * t);
    }
}

TEST(BezierCurve, BernsteinMatchesDeCasteljauForQuadraticAndCubic)
{
    const std::array<Geometry::BezierCurve, 2> curves{{
        Geometry::BezierCurve{{{0.0f, 0.0f, 0.0f}, {2.0f, 4.0f, 1.0f}, {4.0f, 0.0f, -1.0f}}},
        Geometry::BezierCurve{{{1.0f, -1.0f, 0.5f}, {2.0f, 3.0f, 2.0f}, {5.0f, 1.0f, -2.0f}, {7.0f, -2.0f, 1.0f}}},
    }};

    for (const Geometry::BezierCurve& curve : curves)
    {
        for (const float t : {0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.0f})
        {
            const auto bernstein = Geometry::EvaluateBernstein(curve, t);
            const auto casteljau = Geometry::EvaluateDeCasteljau(curve, t);
            ASSERT_TRUE(bernstein.has_value());
            ASSERT_TRUE(casteljau.has_value());
            ExpectVecNear(*bernstein, *casteljau, 1.0e-4f);
        }
    }
}

TEST(BezierCurve, EmptyAndNonFiniteInputsFailClosed)
{
    const Geometry::BezierCurve empty;
    EXPECT_FALSE(empty.GetDegree().has_value());
    EXPECT_FALSE(Geometry::EvaluateBernstein(empty, 0.5f).has_value());
    EXPECT_FALSE(Geometry::EvaluateDeCasteljau(empty, 0.5f).has_value());

    const Geometry::BezierCurve curve{{glm::vec3{0.0f}, glm::vec3{1.0f}}};
    EXPECT_FALSE(Geometry::EvaluateBernstein(curve, std::numeric_limits<float>::quiet_NaN()).has_value());
    EXPECT_FALSE(Geometry::EvaluateDeCasteljau(curve, std::numeric_limits<float>::infinity()).has_value());
    EXPECT_FALSE(Geometry::EvaluateDeCasteljau(curve, -0.01f).has_value());
    EXPECT_FALSE(Geometry::EvaluateBernstein(curve, 1.01f).has_value());
}
