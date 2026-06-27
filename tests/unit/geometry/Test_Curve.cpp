#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

import Geometry.Curve;

namespace
{
    constexpr float kTol = 1e-5f;

    void ExpectVecNear(const glm::vec3& a, const glm::vec3& b, float tol = kTol)
    {
        EXPECT_NEAR(a.x, b.x, tol);
        EXPECT_NEAR(a.y, b.y, tol);
        EXPECT_NEAR(a.z, b.z, tol);
    }
}

TEST(GeometryCurve, EndpointsInterpolateControlPoints)
{
    const std::vector<glm::vec3> ctrl{
        {0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, {3.0f, 2.0f, 0.0f}, {4.0f, 0.0f, 0.0f}};
    auto p0 = Geometry::EvaluateBezier(ctrl, 0.0f);
    auto p1 = Geometry::EvaluateBezier(ctrl, 1.0f);
    ASSERT_TRUE(p0.has_value());
    ASSERT_TRUE(p1.has_value());
    ExpectVecNear(*p0, ctrl.front());
    ExpectVecNear(*p1, ctrl.back());
}

TEST(GeometryCurve, DegreeOneIsLinearLerp)
{
    const std::array<glm::vec3, 2> ctrl{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{10.0f, -4.0f, 2.0f}};
    auto mid = Geometry::EvaluateBezier(ctrl, 0.5f);
    ASSERT_TRUE(mid.has_value());
    ExpectVecNear(*mid, glm::vec3{5.0f, -2.0f, 1.0f});
}

TEST(GeometryCurve, DeCasteljauMatchesBernstein)
{
    const std::vector<glm::vec3> ctrl{
        {0.0f, 0.0f, 1.0f}, {1.0f, 3.0f, -2.0f}, {4.0f, 1.0f, 5.0f}, {6.0f, -2.0f, 0.0f}, {2.0f, 2.0f, 3.0f}};
    for (float t = 0.0f; t <= 1.0f; t += 0.1f)
    {
        auto a = Geometry::EvaluateBezier(ctrl, t);
        auto b = Geometry::EvaluateBezierBernstein(ctrl, t);
        ASSERT_TRUE(a.has_value());
        ASSERT_TRUE(b.has_value());
        ExpectVecNear(*a, *b, 1e-4f);
    }
}

TEST(GeometryCurve, DerivativeAtEndpoints)
{
    // Cubic: derivative at t=0 is n*(P1-P0), at t=1 is n*(Pn - P_{n-1}).
    const std::vector<glm::vec3> ctrl{
        {0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, 0.0f}, {3.0f, 2.0f, 0.0f}, {4.0f, 0.0f, 0.0f}};
    const float n = 3.0f;
    auto d0 = Geometry::EvaluateBezierDerivative(ctrl, 0.0f);
    auto d1 = Geometry::EvaluateBezierDerivative(ctrl, 1.0f);
    ASSERT_TRUE(d0.has_value());
    ASSERT_TRUE(d1.has_value());
    ExpectVecNear(*d0, n * (ctrl[1] - ctrl[0]));
    ExpectVecNear(*d1, n * (ctrl[3] - ctrl[2]));
}

TEST(GeometryCurve, SingleControlPointIsConstant)
{
    const std::array<glm::vec3, 1> ctrl{glm::vec3{7.0f, -1.0f, 3.0f}};
    auto p = Geometry::EvaluateBezier(ctrl, 0.3f);
    ASSERT_TRUE(p.has_value());
    ExpectVecNear(*p, ctrl[0]);
    auto d = Geometry::EvaluateBezierDerivative(ctrl, 0.3f);
    ASSERT_TRUE(d.has_value());
    ExpectVecNear(*d, glm::vec3{0.0f});
}

TEST(GeometryCurve, FailClosedOnBadInput)
{
    const std::vector<glm::vec3> ctrl{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const std::vector<glm::vec3> empty{};
    EXPECT_FALSE(Geometry::EvaluateBezier(empty, 0.5f).has_value());
    EXPECT_FALSE(Geometry::EvaluateBezier(ctrl, -0.01f).has_value());
    EXPECT_FALSE(Geometry::EvaluateBezier(ctrl, 1.01f).has_value());
    EXPECT_FALSE(Geometry::EvaluateBezier(ctrl, std::numeric_limits<float>::quiet_NaN()).has_value());
    EXPECT_FALSE(Geometry::EvaluateBezierBernstein(empty, 0.5f).has_value());
    EXPECT_FALSE(Geometry::EvaluateBezierDerivative(empty, 0.5f).has_value());
}
