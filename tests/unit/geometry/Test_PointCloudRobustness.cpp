// Test_PointCloudRobustness — Tests for C15 Point Cloud Robustness Operators:
//   Bilateral Filter, Outlier Estimation, Kernel Density Estimation.

#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

import Geometry.PointCloud;
import Geometry.PointCloudUtils;

namespace
{
    using Cloud = Geometry::PointCloud::Cloud;

    // =========================================================================
    // Helpers
    // =========================================================================

    // Create a planar point cloud on the XY plane with uniform grid spacing.
    Cloud MakeFlatGrid(int sideN, float spacing = 0.1f)
    {
        Cloud cloud;
        cloud.EnableNormals();
        for (int y = 0; y < sideN; ++y)
        {
            for (int x = 0; x < sideN; ++x)
            {
                auto h = cloud.AddPoint(glm::vec3(
                    static_cast<float>(x) * spacing,
                    static_cast<float>(y) * spacing,
                    0.0f));
                cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }
        return cloud;
    }

    // Create a noisy planar cloud: flat grid with Gaussian noise on Z.
    Cloud MakeNoisyFlatGrid(int sideN, float spacing, float noiseStd, uint32_t seed = 42)
    {
        Cloud cloud;
        cloud.EnableNormals();
        std::mt19937 rng(seed);
        std::normal_distribution<float> noise(0.0f, noiseStd);

        for (int y = 0; y < sideN; ++y)
        {
            for (int x = 0; x < sideN; ++x)
            {
                auto h = cloud.AddPoint(glm::vec3(
                    static_cast<float>(x) * spacing,
                    static_cast<float>(y) * spacing,
                    noise(rng)));
                cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
            }
        }
        return cloud;
    }

    // Create a sphere point cloud.
    Cloud MakeSphere(int nPoints, float radius = 1.0f)
    {
        Cloud cloud;
        cloud.EnableNormals();

        // Fibonacci sphere sampling.
        const float goldenAngle = static_cast<float>(std::numbers::pi) * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < nPoints; ++i)
        {
            float y = 1.0f - (2.0f * static_cast<float>(i) / static_cast<float>(nPoints - 1));
            float r = std::sqrt(1.0f - y * y);
            float theta = goldenAngle * static_cast<float>(i);
            glm::vec3 p(r * std::cos(theta), y, r * std::sin(theta));
            auto h = cloud.AddPoint(p * radius);
            cloud.Normal(h) = glm::normalize(p);
        }
        return cloud;
    }

    // Create a cloud with outliers: uniform sphere + random outliers far away.
    Cloud MakeCloudWithOutliers(int goodPoints, int outlierPoints, float radius = 1.0f, uint32_t seed = 42)
    {
        auto cloud = MakeSphere(goodPoints, radius);

        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

        for (int i = 0; i < outlierPoints; ++i)
        {
            glm::vec3 p(dist(rng), dist(rng), dist(rng));
            p = glm::normalize(p) * radius * 5.0f; // Far away
            auto h = cloud.AddPoint(p);
            if (cloud.HasNormals())
                cloud.Normal(h) = glm::normalize(p);
        }
        return cloud;
    }

    // =========================================================================
    // Bilateral Filter Tests
    // =========================================================================

    TEST(BilateralFilter, NulloptOnEmptyCloud)
    {
        Cloud cloud;
        auto result = Geometry::PointCloud::BilateralFilter(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(BilateralFilter, NulloptOnSinglePoint)
    {
        Cloud cloud;
        cloud.EnableNormals();
        auto h = cloud.AddPoint(glm::vec3(0.0f));
        cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
        auto result = Geometry::PointCloud::BilateralFilter(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(BilateralFilter, NulloptWithoutNormals)
    {
        Cloud cloud;
        cloud.AddPoint(glm::vec3(0.0f));
        cloud.AddPoint(glm::vec3(1.0f, 0.0f, 0.0f));
        auto result = Geometry::PointCloud::BilateralFilter(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(BilateralFilter, ZeroIterationsReturnsEmptyResult)
    {
        auto cloud = MakeFlatGrid(5);
        Geometry::PointCloud::BilateralFilterParams params;
        params.Iterations = 0;
        auto result = Geometry::PointCloud::BilateralFilter(cloud, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->PointsFiltered, 0u);
    }

    TEST(BilateralFilter, FlatGridUnchanged)
    {
        auto cloud = MakeFlatGrid(10, 0.1f);
        auto positionsBefore = std::vector<glm::vec3>(cloud.Positions().begin(), cloud.Positions().end());

        Geometry::PointCloud::BilateralFilterParams params;
        params.KNeighbors = 8;
        params.Iterations = 1;
        auto result = Geometry::PointCloud::BilateralFilter(cloud, params);
        ASSERT_TRUE(result.has_value());

        // A perfectly flat grid should have near-zero displacement.
        EXPECT_LT(result->MaxDisplacement, 1e-6f);
    }

    TEST(BilateralFilter, ReducesNoise)
    {
        auto cloud = MakeNoisyFlatGrid(15, 0.1f, 0.01f);

        // Measure initial Z variance.
        auto positions = cloud.Positions();
        float varianceBefore = 0.0f;
        for (const auto& p : positions)
            varianceBefore += p.z * p.z;
        varianceBefore /= static_cast<float>(positions.size());

        Geometry::PointCloud::BilateralFilterParams params;
        params.KNeighbors = 10;
        params.NormalSigma = 0.5f;
        params.Iterations = 3;
        auto result = Geometry::PointCloud::BilateralFilter(cloud, params);
        ASSERT_TRUE(result.has_value());

        positions = cloud.Positions();
        float varianceAfter = 0.0f;
        for (const auto& p : positions)
            varianceAfter += p.z * p.z;
        varianceAfter /= static_cast<float>(positions.size());

        // Noise should be reduced.
        EXPECT_LT(varianceAfter, varianceBefore);
    }

    TEST(BilateralFilter, MultipleIterationsConverge)
    {
        auto cloud = MakeNoisyFlatGrid(10, 0.1f, 0.02f);

        Geometry::PointCloud::BilateralFilterParams params1;
        params1.KNeighbors = 8;
        params1.Iterations = 1;

        Geometry::PointCloud::BilateralFilterParams params3;
        params3.KNeighbors = 8;
        params3.Iterations = 3;

        auto cloud1 = cloud; // copy
        auto result1 = Geometry::PointCloud::BilateralFilter(cloud1, params1);
        auto result3 = Geometry::PointCloud::BilateralFilter(cloud, params3);

        ASSERT_TRUE(result1.has_value());
        ASSERT_TRUE(result3.has_value());

        // More iterations should yield smaller max displacement (convergence).
        EXPECT_LE(result3->MaxDisplacement, result1->MaxDisplacement + 1e-6f);
    }

    TEST(BilateralFilter, DegenerateNormalsSkipped)
    {
        Cloud cloud;
        cloud.EnableNormals();

        // Add some points with valid normals.
        for (int i = 0; i < 20; ++i)
        {
            auto h = cloud.AddPoint(glm::vec3(static_cast<float>(i) * 0.1f, 0.0f, 0.0f));
            cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        // Add some points with zero normals.
        for (int i = 0; i < 5; ++i)
        {
            auto h = cloud.AddPoint(glm::vec3(static_cast<float>(i) * 0.1f, 0.1f, 0.0f));
            cloud.Normal(h) = glm::vec3(0.0f, 0.0f, 0.0f);
        }

        auto result = Geometry::PointCloud::BilateralFilter(cloud);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->DegenerateNormals, 5u);
        EXPECT_EQ(result->PointsFiltered, 20u);
    }

    TEST(BilateralFilter, ResultDiagnosticsComplete)
    {
        auto cloud = MakeNoisyFlatGrid(8, 0.1f, 0.01f);
        auto result = Geometry::PointCloud::BilateralFilter(cloud);
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result->PointsFiltered, 0u);
        EXPECT_GE(result->AverageDisplacement, 0.0f);
        EXPECT_GE(result->MaxDisplacement, 0.0f);
        EXPECT_GE(result->MaxDisplacement, result->AverageDisplacement);
    }

    // =========================================================================
    // Outlier Estimation Tests
    // =========================================================================

    TEST(OutlierEstimation, NulloptOnEmptyCloud)
    {
        Cloud cloud;
        auto result = Geometry::PointCloud::EstimateOutlierProbability(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(OutlierEstimation, NulloptOnSinglePoint)
    {
        Cloud cloud;
        cloud.AddPoint(glm::vec3(0.0f));
        auto result = Geometry::PointCloud::EstimateOutlierProbability(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(OutlierEstimation, UniformCloudLowScores)
    {
        auto cloud = MakeSphere(200, 1.0f);

        Geometry::PointCloud::OutlierEstimationParams params;
        params.KNeighbors = 10;
        params.ScoreThreshold = 2.0f;

        auto result = Geometry::PointCloud::EstimateOutlierProbability(cloud, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->Scores.size(), cloud.VerticesSize());

        // Uniform sphere should have scores near 1.0, very few outliers.
        EXPECT_LT(result->MeanScore, 1.5f);
        EXPECT_EQ(result->OutlierCount, 0u);
    }

    TEST(OutlierEstimation, DetectsOutliers)
    {
        auto cloud = MakeCloudWithOutliers(200, 10, 1.0f);

        Geometry::PointCloud::OutlierEstimationParams params;
        params.KNeighbors = 15;
        params.ScoreThreshold = 2.0f;

        auto result = Geometry::PointCloud::EstimateOutlierProbability(cloud, params);
        ASSERT_TRUE(result.has_value());

        // Should detect at least some of the 10 far-away outliers.
        EXPECT_GT(result->OutlierCount, 0u);
        EXPECT_GT(result->MaxScore, 2.0f);
    }

    TEST(OutlierEstimation, PublishesProperty)
    {
        auto cloud = MakeSphere(50, 1.0f);
        auto result = Geometry::PointCloud::EstimateOutlierProbability(cloud);
        ASSERT_TRUE(result.has_value());

        auto prop = cloud.GetVertexProperty<float>("p:outlier_score");
        ASSERT_TRUE(prop.IsValid());

        // Verify property values match result scores.
        for (std::size_t i = 0; i < cloud.VerticesSize(); ++i)
            EXPECT_FLOAT_EQ(prop[i], result->Scores[i]);
    }

    TEST(OutlierEstimation, ScoreThresholdAffectsCount)
    {
        auto cloud = MakeCloudWithOutliers(200, 10, 1.0f);

        Geometry::PointCloud::OutlierEstimationParams paramsLoose;
        paramsLoose.KNeighbors = 15;
        paramsLoose.ScoreThreshold = 5.0f;

        Geometry::PointCloud::OutlierEstimationParams paramsStrict;
        paramsStrict.KNeighbors = 15;
        paramsStrict.ScoreThreshold = 1.5f;

        // Need separate clouds since the function modifies them.
        auto cloud1 = cloud;
        auto cloud2 = cloud;
        auto resultLoose = Geometry::PointCloud::EstimateOutlierProbability(cloud1, paramsLoose);
        auto resultStrict = Geometry::PointCloud::EstimateOutlierProbability(cloud2, paramsStrict);

        ASSERT_TRUE(resultLoose.has_value());
        ASSERT_TRUE(resultStrict.has_value());

        // Stricter threshold should find more outliers.
        EXPECT_GE(resultStrict->OutlierCount, resultLoose->OutlierCount);
    }

    TEST(OutlierEstimation, ResultDiagnosticsComplete)
    {
        auto cloud = MakeSphere(100, 1.0f);
        auto result = Geometry::PointCloud::EstimateOutlierProbability(cloud);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->Scores.size(), cloud.VerticesSize());
        EXPECT_GE(result->MeanScore, 0.0f);
        EXPECT_GE(result->MaxScore, result->MeanScore);
    }

    // =========================================================================
    // Kernel Density Estimation Tests
    // =========================================================================

    TEST(KDE, NulloptOnEmptyCloud)
    {
        Cloud cloud;
        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(KDE, NulloptOnSinglePoint)
    {
        Cloud cloud;
        cloud.AddPoint(glm::vec3(0.0f));
        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud);
        ASSERT_FALSE(result.has_value());
    }

    TEST(KDE, UniformCloudSimilarDensities)
    {
        auto cloud = MakeSphere(200, 1.0f);

        Geometry::PointCloud::KDEParams params;
        params.KNeighbors = 10;

        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->Densities.size(), cloud.VerticesSize());

        // Uniform sphere: ratio of max/min density should be bounded.
        ASSERT_GT(result->MinDensity, 0.0f);
        float ratio = result->MaxDensity / result->MinDensity;
        // Allow variation from Fibonacci sampling non-uniformity at poles.
        EXPECT_LT(ratio, 10.0f);
    }

    TEST(KDE, HigherDensityInDenseRegion)
    {
        // Create two clusters: one dense (100 points in 1 unit cube),
        // one sparse (20 points in 10 unit cube).
        Cloud cloud;
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> denseD(-0.5f, 0.5f);
        std::uniform_real_distribution<float> sparseD(-5.0f, 5.0f);

        for (int i = 0; i < 100; ++i)
            cloud.AddPoint(glm::vec3(denseD(rng), denseD(rng), denseD(rng)));
        for (int i = 0; i < 20; ++i)
            cloud.AddPoint(glm::vec3(sparseD(rng) + 20.0f, sparseD(rng), sparseD(rng)));

        Geometry::PointCloud::KDEParams params;
        params.KNeighbors = 10;

        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud, params);
        ASSERT_TRUE(result.has_value());

        // Average density of first 100 (dense) should be higher than last 20 (sparse).
        float denseAvg = 0.0f;
        for (std::size_t i = 0; i < 100; ++i)
            denseAvg += result->Densities[i];
        denseAvg /= 100.0f;

        float sparseAvg = 0.0f;
        for (std::size_t i = 100; i < 120; ++i)
            sparseAvg += result->Densities[i];
        sparseAvg /= 20.0f;

        EXPECT_GT(denseAvg, sparseAvg);
    }

    TEST(KDE, PublishesProperty)
    {
        auto cloud = MakeSphere(50, 1.0f);
        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud);
        ASSERT_TRUE(result.has_value());

        auto prop = cloud.GetVertexProperty<float>("p:density");
        ASSERT_TRUE(prop.IsValid());

        for (std::size_t i = 0; i < cloud.VerticesSize(); ++i)
            EXPECT_FLOAT_EQ(prop[i], result->Densities[i]);
    }

    TEST(KDE, CustomBandwidth)
    {
        auto cloud = MakeSphere(100, 1.0f);

        // Use bandwidths relative to point spacing (~0.35 for 100 pts on unit sphere).
        Geometry::PointCloud::KDEParams paramsNarrow;
        paramsNarrow.KNeighbors = 10;
        paramsNarrow.Bandwidth = 0.2f;

        Geometry::PointCloud::KDEParams paramsWide;
        paramsWide.KNeighbors = 10;
        paramsWide.Bandwidth = 2.0f;

        auto cloud1 = cloud;
        auto cloud2 = cloud;
        auto resultNarrow = Geometry::PointCloud::EstimateKernelDensity(cloud1, paramsNarrow);
        auto resultWide = Geometry::PointCloud::EstimateKernelDensity(cloud2, paramsWide);

        ASSERT_TRUE(resultNarrow.has_value());
        ASSERT_TRUE(resultWide.has_value());

        EXPECT_FLOAT_EQ(resultNarrow->UsedBandwidth, 0.2f);
        EXPECT_FLOAT_EQ(resultWide->UsedBandwidth, 2.0f);

        // Both should produce positive densities.
        EXPECT_GT(resultNarrow->MeanDensity, 0.0f);
        EXPECT_GT(resultWide->MeanDensity, 0.0f);

        // Wider bandwidth should produce smoother (less variable) densities.
        float narrowCV = (resultNarrow->MeanDensity > 0.0f)
            ? (resultNarrow->MaxDensity - resultNarrow->MinDensity) / resultNarrow->MeanDensity : 0.0f;
        float wideCV = (resultWide->MeanDensity > 0.0f)
            ? (resultWide->MaxDensity - resultWide->MinDensity) / resultWide->MeanDensity : 0.0f;
        EXPECT_LT(wideCV, narrowCV + 0.01f);
    }

    TEST(KDE, SilvermanBandwidthAutomatic)
    {
        auto cloud = MakeSphere(100, 1.0f);

        Geometry::PointCloud::KDEParams params;
        params.Bandwidth = 0.0f; // auto

        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result->UsedBandwidth, 0.0f);
    }

    TEST(KDE, ResultDiagnosticsComplete)
    {
        auto cloud = MakeSphere(100, 1.0f);
        auto result = Geometry::PointCloud::EstimateKernelDensity(cloud);
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->Densities.size(), cloud.VerticesSize());
        EXPECT_GT(result->MeanDensity, 0.0f);
        EXPECT_GE(result->MaxDensity, result->MeanDensity);
        EXPECT_LE(result->MinDensity, result->MeanDensity);
        EXPECT_GT(result->UsedBandwidth, 0.0f);
    }

} // anonymous namespace
