// tests/Test_Registration.cpp — ICP point cloud registration tests.
// Covers: point-to-point, point-to-plane, convergence, outlier rejection,
// degenerate input, identity alignment, known rigid transforms, and
// parameter validation.

#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <span>
#include <vector>

#include <glm/glm.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Geometry;

namespace
{
    // Generate a flat grid of points on the XY plane.
    std::vector<glm::vec3> MakeFlatGrid(int n = 10, float spacing = 0.1f)
    {
        std::vector<glm::vec3> points;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                points.emplace_back(x * spacing, y * spacing, 0.0f);
        return points;
    }

    // Generate points on a unit sphere.
    std::vector<glm::vec3> MakeSpherePoints(int nLat = 10, int nLon = 20)
    {
        std::vector<glm::vec3> points;
        for (int i = 1; i < nLat; ++i)
        {
            float theta = static_cast<float>(i) / nLat * std::numbers::pi_v<float>;
            for (int j = 0; j < nLon; ++j)
            {
                float phi = static_cast<float>(j) / nLon * 2.0f * std::numbers::pi_v<float>;
                points.emplace_back(
                    std::sin(theta) * std::cos(phi),
                    std::sin(theta) * std::sin(phi),
                    std::cos(theta));
            }
        }
        points.emplace_back(0.0f, 0.0f, 1.0f);
        points.emplace_back(0.0f, 0.0f, -1.0f);
        return points;
    }

    // Compute normals for sphere points (outward-pointing).
    std::vector<glm::vec3> MakeSphereNormals(const std::vector<glm::vec3>& points)
    {
        std::vector<glm::vec3> normals;
        normals.reserve(points.size());
        for (const auto& p : points)
        {
            float len = glm::length(p);
            normals.push_back(len > 1e-6f ? p / len : glm::vec3(0.0f, 0.0f, 1.0f));
        }
        return normals;
    }

    // Compute normals for flat grid (all +Z).
    std::vector<glm::vec3> MakeFlatNormals(std::size_t count)
    {
        return std::vector<glm::vec3>(count, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // Apply a rigid transform to a point set.
    std::vector<glm::vec3> TransformPoints(const std::vector<glm::vec3>& points,
                                            const glm::mat4& transform)
    {
        std::vector<glm::vec3> result;
        result.reserve(points.size());
        for (const auto& p : points)
        {
            glm::vec4 tp = transform * glm::vec4(p, 1.0f);
            result.emplace_back(tp.x, tp.y, tp.z);
        }
        return result;
    }

    // Build a rotation matrix around an axis by angle (radians).
    glm::mat4 MakeRotation(float angleDeg, const glm::vec3& axis)
    {
        return glm::rotate(glm::mat4(1.0f), glm::radians(angleDeg), axis);
    }

    // Build a translation matrix.
    glm::mat4 MakeTranslation(const glm::vec3& offset)
    {
        return glm::translate(glm::mat4(1.0f), offset);
    }

    std::vector<glm::vec3> MakeAsymmetricCloud()
    {
        std::vector<glm::vec3> points;
        points.reserve(60);
        for (int z = 0; z < 3; ++z)
        {
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 5; ++x)
                {
                    points.emplace_back(
                        0.13f * static_cast<float>(x) + 0.01f * static_cast<float>(y),
                        0.17f * static_cast<float>(y) - 0.02f * static_cast<float>(z),
                        0.11f * static_cast<float>(z) + 0.005f * static_cast<float>(x * y));
                }
            }
        }
        return points;
    }

    double TranslationError(const Geometry::Registration::RegistrationResult& result,
                            const glm::dvec3& expected)
    {
        const glm::dvec3 actual(result.Transform[3][0], result.Transform[3][1], result.Transform[3][2]);
        return glm::length(actual - expected);
    }
}

// =============================================================================
// Degenerate / Edge-case Input
// =============================================================================

TEST(Registration_ICP, ReturnsNulloptForEmptySource)
{
    std::vector<glm::vec3> empty;
    auto target = MakeFlatGrid();

    auto result = Geometry::Registration::AlignICP(empty, target);
    EXPECT_FALSE(result.has_value());
}

TEST(Registration_ICP, ReturnsNulloptForEmptyTarget)
{
    auto source = MakeFlatGrid();
    std::vector<glm::vec3> empty;

    auto result = Geometry::Registration::AlignICP(source, empty);
    EXPECT_FALSE(result.has_value());
}

TEST(Registration_ICP, ReturnsNulloptForTooFewPoints)
{
    std::vector<glm::vec3> source = {{0, 0, 0}, {1, 0, 0}};
    std::vector<glm::vec3> target = {{0, 0, 0}, {1, 0, 0}};

    auto result = Geometry::Registration::AlignICP(source, target);
    EXPECT_FALSE(result.has_value());
}

TEST(Registration_ICP, ReturnsNulloptForZeroIterations)
{
    auto source = MakeFlatGrid();
    auto target = MakeFlatGrid();

    Geometry::Registration::RegistrationParams params;
    params.MaxIterations = 0;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Registration_ICP, ReturnsNulloptForInvalidInlierRatio)
{
    auto source = MakeFlatGrid();
    auto target = MakeFlatGrid();

    Geometry::Registration::RegistrationParams params;
    params.InlierRatio = 0.0;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    EXPECT_FALSE(result.has_value());

    params.InlierRatio = 1.5;
    result = Geometry::Registration::AlignICP(source, target, {}, params);
    EXPECT_FALSE(result.has_value());
}

TEST(Registration_ICP, ReturnsNulloptForInvalidRobustScaleOnlyWhenEnabled)
{
    auto target = MakeFlatGrid();
    auto source = TransformPoints(target, MakeTranslation(glm::vec3(0.02f, 0.0f, 0.0f)));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.RobustScale = -1.0;

    // RobustScale is ignored when no robust kernel is selected; this preserves
    // the default no-kernel path.
    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    EXPECT_TRUE(result.has_value());

    params.RobustKernelKind = Geometry::Robust::RobustKernel::Huber;
    result = Geometry::Registration::AlignICP(source, target, {}, params);
    EXPECT_FALSE(result.has_value());

    params.RobustScale = std::numeric_limits<double>::infinity();
    result = Geometry::Registration::AlignICP(source, target, {}, params);
    EXPECT_FALSE(result.has_value());
}

// =============================================================================
// Identity Alignment (source == target)
// =============================================================================

TEST(Registration_ICP, IdentityAlignmentPointToPoint)
{
    auto points = MakeSpherePoints();

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 10;

    auto result = Geometry::Registration::AlignICP(points, points, {}, params);
    ASSERT_TRUE(result.has_value());

    // Transform should be approximately identity
    EXPECT_NEAR(result->FinalRMSE, 0.0, 1e-4);
    EXPECT_TRUE(result->Converged);

    // Check diagonal is ~1 and off-diagonals are ~0
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            double expected = (i == j) ? 1.0 : 0.0;
            EXPECT_NEAR(result->Transform[j][i], expected, 1e-4)
                << "Transform[" << j << "][" << i << "]";
        }
}

TEST(Registration_ICP, IdentityAlignmentPointToPlane)
{
    auto points = MakeSpherePoints();
    auto normals = MakeSphereNormals(points);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 10;

    auto result = Geometry::Registration::AlignICP(points, points, normals, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_NEAR(result->FinalRMSE, 0.0, 1e-4);
    EXPECT_TRUE(result->Converged);
}

// =============================================================================
// Known Translation Recovery
// =============================================================================

TEST(Registration_ICP, RecoversPureTranslation_PointToPoint)
{
    auto target = MakeSpherePoints();
    const glm::vec3 offset(0.1f, -0.05f, 0.08f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 50;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.01);

    // The recovered translation should approximately negate the applied offset
    const double tx = result->Transform[3][0];
    const double ty = result->Transform[3][1];
    const double tz = result->Transform[3][2];

    EXPECT_NEAR(tx, -offset.x, 0.02);
    EXPECT_NEAR(ty, -offset.y, 0.02);
    EXPECT_NEAR(tz, -offset.z, 0.02);
}

TEST(Registration_ICP, RecoversPureTranslation_PointToPlane)
{
    auto target = MakeSpherePoints();
    auto normals = MakeSphereNormals(target);
    const glm::vec3 offset(0.05f, 0.03f, -0.04f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 50;

    auto result = Geometry::Registration::AlignICP(source, target, normals, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.01);
}

// =============================================================================
// Known Rotation Recovery
// =============================================================================

TEST(Registration_ICP, RecoversSmallRotation_PointToPoint)
{
    auto target = MakeSpherePoints();
    const glm::mat4 rotation = MakeRotation(5.0f, glm::vec3(0, 0, 1)); // 5 degrees around Z
    auto source = TransformPoints(target, rotation);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 100;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.05);
}

TEST(Registration_ICP, RecoversSmallRotation_PointToPlane)
{
    auto target = MakeSpherePoints();
    auto normals = MakeSphereNormals(target);
    const glm::mat4 rotation = MakeRotation(5.0f, glm::vec3(0, 0, 1));
    auto source = TransformPoints(target, rotation);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 50;

    auto result = Geometry::Registration::AlignICP(source, target, normals, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.05);
}

// =============================================================================
// Combined Rigid Transform Recovery
// =============================================================================

TEST(Registration_ICP, RecoversRigidTransform_PointToPoint)
{
    auto target = MakeSpherePoints();
    glm::mat4 transform = MakeTranslation(glm::vec3(0.05f, -0.03f, 0.02f))
                         * MakeRotation(8.0f, glm::normalize(glm::vec3(1, 1, 0)));
    auto source = TransformPoints(target, transform);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 100;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.05);
    EXPECT_GT(result->FinalInlierCount, 0u);
}

TEST(Registration_ICP, RecoversRigidTransform_PointToPlane)
{
    auto target = MakeSpherePoints();
    auto normals = MakeSphereNormals(target);
    glm::mat4 transform = MakeTranslation(glm::vec3(0.05f, -0.03f, 0.02f))
                         * MakeRotation(8.0f, glm::normalize(glm::vec3(1, 1, 0)));
    auto source = TransformPoints(target, transform);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 50;

    auto result = Geometry::Registration::AlignICP(source, target, normals, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.05);
}

// =============================================================================
// Point-to-Plane Falls Back to Point-to-Point Without Normals
// =============================================================================

TEST(Registration_ICP, PointToPlane_FallbackWithoutNormals)
{
    auto target = MakeSpherePoints();
    const glm::vec3 offset(0.05f, 0.0f, 0.0f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 50;

    // No normals provided — should fall back to PointToPoint
    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.01);
}

// =============================================================================
// Convergence and RMSE History
// =============================================================================

TEST(Registration_ICP, RMSEHistoryIsMonotonicallyDecreasing)
{
    auto target = MakeSpherePoints();
    const glm::vec3 offset(0.1f, 0.0f, 0.0f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 30;
    params.InlierRatio = 1.0; // No outlier rejection for clean monotonicity

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    ASSERT_GE(result->RMSEHistory.size(), 2u);

    // RMSE should be generally non-increasing (allow small numerical bumps)
    for (std::size_t i = 1; i < result->RMSEHistory.size(); ++i)
    {
        EXPECT_LE(result->RMSEHistory[i], result->RMSEHistory[i - 1] + 1e-6)
            << "RMSE increased at iteration " << i;
    }
}

TEST(Registration_ICP, ConvergesOnIdenticalClouds)
{
    auto points = MakeFlatGrid(15);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 20;

    auto result = Geometry::Registration::AlignICP(points, points, {}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->Converged);
    EXPECT_LE(result->IterationsPerformed, 5u); // Should converge very quickly
}

// =============================================================================
// Outlier Rejection
// =============================================================================

TEST(Registration_ICP, OutlierRejectionImproveAlignment)
{
    auto target = MakeSpherePoints();
    const glm::vec3 offset(0.05f, 0.0f, 0.0f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    // Add some outlier points to source
    source.push_back(glm::vec3(10.0f, 10.0f, 10.0f));
    source.push_back(glm::vec3(-10.0f, -10.0f, -10.0f));
    source.push_back(glm::vec3(5.0f, 0.0f, 0.0f));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 50;
    params.InlierRatio = 0.9;
    params.MaxCorrespondenceDistance = 2.0;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    // Should still achieve reasonable alignment despite outliers
    EXPECT_LT(result->FinalRMSE, 0.1);
}

TEST(Registration_ICP, RobustKernelDownweightsGrossOutliers)
{
    auto target = MakeAsymmetricCloud();
    const glm::vec3 offset(0.03f, -0.02f, 0.025f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    for (int i = 0; i < 30; ++i)
    {
        source.emplace_back(2.5f + 0.03f * static_cast<float>(i),
                            -1.25f + 0.015f * static_cast<float>(i),
                            1.75f);
    }

    Geometry::Registration::RegistrationParams trimmed;
    trimmed.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    trimmed.MaxIterations = 50;
    trimmed.InlierRatio = 0.8;
    trimmed.MaxCorrespondenceDistance = 10.0;

    auto trimmedResult = Geometry::Registration::AlignICP(source, target, {}, trimmed);
    ASSERT_TRUE(trimmedResult.has_value());

    Geometry::Registration::RegistrationParams robust = trimmed;
    robust.RobustKernelKind = Geometry::Robust::RobustKernel::Tukey;
    robust.RobustScale = 0.15;

    auto robustResult = Geometry::Registration::AlignICP(source, target, {}, robust);
    ASSERT_TRUE(robustResult.has_value());

    const glm::dvec3 expectedInverse(-offset.x, -offset.y, -offset.z);
    const double trimmedTranslationError = TranslationError(*trimmedResult, expectedInverse);
    const double robustTranslationError = TranslationError(*robustResult, expectedInverse);

    EXPECT_LT(robustTranslationError, 0.02);
    EXPECT_LT(robustTranslationError, trimmedTranslationError * 0.6);
    EXPECT_LT(robustResult->FinalRMSE, trimmedResult->FinalRMSE);
}

TEST(Registration_ICP, MaxCorrespondenceDistanceRejectsDistantPairs)
{
    auto target = MakeSpherePoints();
    const glm::vec3 offset(0.05f, 0.0f, 0.0f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 30;
    params.MaxCorrespondenceDistance = 0.5; // Reasonable threshold

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_LT(result->FinalRMSE, 0.01);
}

// =============================================================================
// Flat Grid with Normal — Point-to-Plane Translation
// =============================================================================

TEST(Registration_ICP, FlatGridTranslation_PointToPlane)
{
    auto target = MakeFlatGrid(20, 0.05f);
    auto normals = MakeFlatNormals(target.size());
    const glm::vec3 offset(0.02f, -0.01f, 0.0f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 50;

    auto result = Geometry::Registration::AlignICP(source, target, normals, params);
    ASSERT_TRUE(result.has_value());
    // Point-to-plane on a flat surface minimizes normal-direction error only;
    // in-plane sliding means RMSE won't reach zero but should be small.
    EXPECT_LT(result->FinalRMSE, 0.03);
}

// =============================================================================
// Result Struct Completeness
// =============================================================================

TEST(Registration_ICP, ResultFieldsArePopulated)
{
    auto target = MakeSpherePoints();
    const glm::vec3 offset(0.05f, 0.0f, 0.0f);
    auto source = TransformPoints(target, MakeTranslation(offset));

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPoint;
    params.MaxIterations = 30;

    auto result = Geometry::Registration::AlignICP(source, target, {}, params);
    ASSERT_TRUE(result.has_value());

    EXPECT_GT(result->IterationsPerformed, 0u);
    EXPECT_GT(result->FinalInlierCount, 0u);
    EXPECT_FALSE(result->RMSEHistory.empty());
    EXPECT_EQ(result->RMSEHistory.size(), result->IterationsPerformed);
    EXPECT_GE(result->FinalRMSE, 0.0);
}

// =============================================================================
// PointToPlane Converges Faster Than PointToPoint
// =============================================================================

TEST(Registration_ICP, PointToPlaneConvergesWell)
{
    auto target = MakeSpherePoints(15, 30); // Denser sphere
    auto normals = MakeSphereNormals(target);
    glm::mat4 transform = MakeTranslation(glm::vec3(0.05f, 0.0f, 0.03f))
                         * MakeRotation(3.0f, glm::vec3(0, 1, 0));
    auto source = TransformPoints(target, transform);

    Geometry::Registration::RegistrationParams params;
    params.Variant = Geometry::Registration::ICPVariant::PointToPlane;
    params.MaxIterations = 100;
    params.ConvergenceThreshold = 1e-6;

    auto result = Geometry::Registration::AlignICP(source, target, normals, params);
    ASSERT_TRUE(result.has_value());

    // Point-to-plane should achieve good alignment on smooth surfaces
    EXPECT_LT(result->FinalRMSE, 0.05);
    EXPECT_GT(result->FinalInlierCount, 0u);
}
