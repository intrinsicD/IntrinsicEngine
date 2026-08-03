#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.FixedPoint.Anderson;
import Geometry.GaussianMixture;
import Geometry.KMeans;

namespace
{
    namespace FixedPoint = Geometry::FixedPoint;
    namespace Gmm = Geometry::GaussianMixture;

    [[nodiscard]] std::vector<glm::vec3> TwoClusterFixture()
    {
        std::vector<glm::vec3> points{};
        points.reserve(80u);
        for (std::uint32_t i = 0u; i < 40u; ++i)
        {
            const float dx =
                0.08f * static_cast<float>(
                    static_cast<int>(i % 5u) - 2);
            const float dy =
                0.06f * static_cast<float>(
                    static_cast<int>((i / 5u) % 4u) - 1);
            const float dz =
                0.04f * static_cast<float>(
                    static_cast<int>(i % 3u) - 1);
            points.emplace_back(-2.0f + dx, dy, dz);
            points.emplace_back(2.0f - dx, -dy, dz);
        }
        return points;
    }

    [[nodiscard]] std::vector<std::size_t> SortedByMeanX(
        const Gmm::Model& model)
    {
        std::vector<std::size_t> indices(
            model.Components.size(), 0u);
        for (std::size_t i = 0u; i < indices.size(); ++i)
            indices[i] = i;
        std::sort(
            indices.begin(), indices.end(),
            [&model](const std::size_t lhs, const std::size_t rhs)
            {
                return model.Components[lhs].Mean.x <
                       model.Components[rhs].Mean.x;
            });
        return indices;
    }

    [[nodiscard]] std::vector<glm::vec3> ThreeClusterFixture()
    {
        std::vector<glm::vec3> points{};
        points.reserve(90u);
        constexpr glm::vec3 centers[]{
            {-3.0f, 0.0f, 0.0f},
            {0.0f, 2.0f, 0.0f},
            {3.0f, 0.0f, 0.0f},
        };
        for (const glm::vec3 center : centers)
        {
            for (std::uint32_t i = 0u; i < 30u; ++i)
            {
                points.push_back(center + glm::vec3{
                    0.05f * static_cast<float>(
                        static_cast<int>(i % 5u) - 2),
                    0.04f * static_cast<float>(
                        static_cast<int>((i / 5u) % 3u) - 1),
                    0.03f * static_cast<float>(
                        static_cast<int>(i % 3u) - 1),
                });
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> OverlappingClusterFixture()
    {
        const Gmm::MultivariateGaussian left{
            .Mean = glm::dvec3{-0.75, 0.0, 0.0},
            .Covariance = glm::dmat3{1.0},
        };
        const Gmm::MultivariateGaussian right{
            .Mean = glm::dvec3{0.75, 0.0, 0.0},
            .Covariance = glm::dmat3{1.0},
        };
        std::vector<glm::vec3> points{};
        points.reserve(160u);
        for (std::uint64_t i = 0u; i < 80u; ++i)
        {
            const auto leftSample = Gmm::Sample(left, 100u + i);
            const auto rightSample = Gmm::Sample(right, 1000u + i);
            if (leftSample && rightSample)
            {
                points.emplace_back(*leftSample);
                points.emplace_back(*rightSample);
            }
        }
        return points;
    }
}

TEST(AndersonAcceleration, ContractionUsesFiniteSafeguardedMix)
{
    FixedPoint::AndersonState state{};
    state.Params.Window = 3u;
    state.Params.Damping = 1.0;
    state.Params.SafeguardFactor = 2.0;

    std::vector<double> current{0.0};
    bool accelerated = false;
    for (std::uint32_t iteration = 0u; iteration < 6u; ++iteration)
    {
        const std::vector<double> mapped{
            0.5 * current[0] + 1.0,
        };
        const auto step = FixedPoint::AndersonStep(
            state, current, mapped);
        ASSERT_TRUE(step.Succeeded());
        ASSERT_EQ(step.Value.size(), 1u);
        ASSERT_TRUE(std::isfinite(step.Value[0]));
        accelerated = accelerated || step.UsedAcceleration();
        current = step.Value;
    }
    EXPECT_TRUE(accelerated);
    EXPECT_NEAR(current[0], 2.0, 1.0e-8);
}

TEST(AndersonAcceleration, DimensionChangesResetPrivateHistory)
{
    FixedPoint::AndersonState state{};
    const auto first = FixedPoint::AndersonStep(
        state,
        std::vector<double>{0.0},
        std::vector<double>{1.0});
    ASSERT_EQ(first.Status, FixedPoint::AndersonStepStatus::Plain);

    const auto resized = FixedPoint::AndersonStep(
        state,
        std::vector<double>{0.0, 0.0},
        std::vector<double>{1.0, 1.0});
    EXPECT_EQ(resized.Status, FixedPoint::AndersonStepStatus::Plain);
    EXPECT_EQ(resized.Value, (std::vector<double>{1.0, 1.0}));
}

TEST(AndersonAcceleration, ExtremeFiniteInputsFailClosedWithoutHistoryMutation)
{
    FixedPoint::AndersonState state{};
    const double maximum = std::numeric_limits<double>::max();
    const auto overflow = FixedPoint::AndersonStep(
        state,
        std::vector<double>{-maximum},
        std::vector<double>{maximum});
    EXPECT_EQ(
        overflow.Status,
        FixedPoint::AndersonStepStatus::NumericalFailure);
    EXPECT_TRUE(overflow.Value.empty());
    EXPECT_TRUE(std::isfinite(overflow.PlainResidualNorm));

    const auto next = FixedPoint::AndersonStep(
        state,
        std::vector<double>{0.0},
        std::vector<double>{1.0});
    EXPECT_EQ(next.Status, FixedPoint::AndersonStepStatus::Plain);

    FixedPoint::AndersonState stableState{};
    const auto stableLarge = FixedPoint::AndersonStep(
        stableState,
        std::vector<double>{0.0, 0.0},
        std::vector<double>{maximum / 4.0, maximum / 4.0});
    EXPECT_EQ(
        stableLarge.Status,
        FixedPoint::AndersonStepStatus::Plain);
    EXPECT_TRUE(std::isfinite(stableLarge.PlainResidualNorm));
}

TEST(AndersonAcceleration, SafeguardAndSingularMixFallBackToPlainStep)
{
    FixedPoint::AndersonState safeguardedState{};
    safeguardedState.Params.Regularization = 1.0e12;
    ASSERT_EQ(
        FixedPoint::AndersonStep(
            safeguardedState,
            std::vector<double>{0.0},
            std::vector<double>{100.0}).Status,
        FixedPoint::AndersonStepStatus::Plain);
    const auto rejected = FixedPoint::AndersonStep(
        safeguardedState,
        std::vector<double>{0.0},
        std::vector<double>{1.0});
    EXPECT_EQ(
        rejected.Status,
        FixedPoint::AndersonStepStatus::Safeguarded);
    EXPECT_EQ(rejected.Value, (std::vector<double>{1.0}));
    EXPECT_GT(
        rejected.MixedResidualNorm,
        rejected.PlainResidualNorm);

    FixedPoint::AndersonState singularState{};
    singularState.Params.Regularization = 0.0;
    ASSERT_EQ(
        FixedPoint::AndersonStep(
            singularState,
            std::vector<double>{0.0},
            std::vector<double>{1.0}).Status,
        FixedPoint::AndersonStepStatus::Plain);
    EXPECT_EQ(
        FixedPoint::AndersonStep(
            singularState,
            std::vector<double>{0.0},
            std::vector<double>{1.0}).Status,
        FixedPoint::AndersonStepStatus::Safeguarded);
}

TEST(AndersonAcceleration, ZeroDampingPublishesThePlainMappedValue)
{
    FixedPoint::AndersonState state{};
    state.Params.Damping = 0.0;
    state.Params.SafeguardFactor = 2.0;
    ASSERT_EQ(
        FixedPoint::AndersonStep(
            state,
            std::vector<double>{0.0},
            std::vector<double>{1.0}).Status,
        FixedPoint::AndersonStepStatus::Plain);
    const std::vector<double> mapped{1.5};
    const auto step = FixedPoint::AndersonStep(
        state,
        std::vector<double>{1.0},
        mapped);
    ASSERT_TRUE(step.UsedAcceleration());
    EXPECT_EQ(step.Value, mapped);
}

TEST(GaussianMixture, KMeansPlusPlusInitializationIsSeedDeterministic)
{
    const std::vector<glm::vec3> points = TwoClusterFixture();
    Geometry::KMeans::KMeansParams params{};
    params.ClusterCount = 4u;
    params.Seed = 0x58u;
    params.Init = Geometry::KMeans::Initialization::KMeansPlusPlus;

    const auto first = Geometry::KMeans::BuildInitialCentroids(
        points, {}, params, params.ClusterCount);
    const auto second = Geometry::KMeans::BuildInitialCentroids(
        points, {}, params, params.ClusterCount);
    ASSERT_EQ(first.size(), params.ClusterCount);
    ASSERT_EQ(second.size(), first.size());
    EXPECT_EQ(first, second);
}

TEST(GaussianMixture, IdentityLogPdfAndSeededSampleAreDeterministic)
{
    const Gmm::MultivariateGaussian gaussian{
        .Mean = glm::dvec3{1.0, 2.0, 3.0},
        .Covariance = glm::dmat3{1.0},
    };
    const auto logPdf = Gmm::LogPdf(gaussian, gaussian.Mean);
    ASSERT_TRUE(logPdf.has_value());
    EXPECT_NEAR(
        *logPdf,
        -1.5 * std::log(2.0 * std::acos(-1.0)),
        1.0e-12);

    const auto first = Gmm::Sample(gaussian, 0x58u);
    const auto second = Gmm::Sample(gaussian, 0x58u);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*first, *second);
}

TEST(GaussianMixture, CovarianceSymmetryCheckIsScaleAware)
{
    Gmm::MultivariateGaussian asymmetric{};
    asymmetric.Covariance = glm::dmat3{0.0};
    asymmetric.Covariance[0][0] = 1.0e-12;
    asymmetric.Covariance[1][1] = 1.0e-12;
    asymmetric.Covariance[2][2] = 1.0e-12;
    asymmetric.Covariance[0][1] = 0.0;
    asymmetric.Covariance[1][0] = 9.0e-11;
    EXPECT_FALSE(Gmm::LogPdf(asymmetric, glm::dvec3{0.0}));

    Gmm::MultivariateGaussian tinySpd{};
    tinySpd.Covariance = glm::dmat3{0.0};
    tinySpd.Covariance[0][0] = 1.0e-12;
    tinySpd.Covariance[1][1] = 1.0e-12;
    tinySpd.Covariance[2][2] = 1.0e-12;
    tinySpd.Covariance[0][1] = 1.0e-13;
    tinySpd.Covariance[1][0] = 1.0e-13;
    const auto logPdf = Gmm::LogPdf(tinySpd, glm::dvec3{0.0});
    ASSERT_TRUE(logPdf.has_value());
    EXPECT_TRUE(std::isfinite(*logPdf));

    tinySpd.Covariance[0][1] = 1.0e-12;
    tinySpd.Covariance[1][0] = 1.0e-12;
    EXPECT_FALSE(Gmm::LogPdf(tinySpd, glm::dvec3{0.0}));

    Gmm::MultivariateGaussian hugeAsymmetric{};
    const double maximum = std::numeric_limits<double>::max();
    hugeAsymmetric.Covariance = glm::dmat3{0.0};
    hugeAsymmetric.Covariance[0][0] = maximum;
    hugeAsymmetric.Covariance[1][1] = maximum;
    hugeAsymmetric.Covariance[2][2] = maximum;
    hugeAsymmetric.Covariance[0][1] = 0.0;
    hugeAsymmetric.Covariance[1][0] = maximum / 2.0;
    EXPECT_FALSE(
        Gmm::LogPdf(hugeAsymmetric, glm::dvec3{0.0}));

    Gmm::MultivariateGaussian hugeSymmetric{};
    hugeSymmetric.Covariance = glm::dmat3{0.0};
    hugeSymmetric.Covariance[0][0] = maximum;
    hugeSymmetric.Covariance[1][1] = maximum;
    hugeSymmetric.Covariance[2][2] = maximum;
    hugeSymmetric.Covariance[0][1] = maximum / 4.0;
    hugeSymmetric.Covariance[1][0] = maximum / 4.0;
    const auto hugeLogPdf =
        Gmm::LogPdf(hugeSymmetric, glm::dvec3{0.0});
    ASSERT_TRUE(hugeLogPdf.has_value());
    EXPECT_TRUE(std::isfinite(*hugeLogPdf));

    Gmm::MultivariateGaussian subnormalAsymmetric{};
    const double subnormal =
        std::numeric_limits<double>::denorm_min();
    subnormalAsymmetric.Covariance = glm::dmat3{0.0};
    subnormalAsymmetric.Covariance[0][0] = 4.0 * subnormal;
    subnormalAsymmetric.Covariance[1][1] = 4.0 * subnormal;
    subnormalAsymmetric.Covariance[2][2] = 4.0 * subnormal;
    subnormalAsymmetric.Covariance[0][1] = 0.0;
    subnormalAsymmetric.Covariance[1][0] = subnormal;
    EXPECT_FALSE(
        Gmm::LogPdf(subnormalAsymmetric, glm::dvec3{0.0}));
}

TEST(GaussianMixture, RecoversSeparatedComponentsAndMonotonicLikelihood)
{
    const std::vector<glm::vec3> points = TwoClusterFixture();
    Gmm::FitParams params{};
    params.MaxIterations = 80u;
    params.RelativeTolerance = 1.0e-8;
    params.CovarianceFloor = 1.0e-5;
    params.Seed = 17u;

    const Gmm::FitResult result = Gmm::FitEM(
        points, 2u, params);
    ASSERT_TRUE(result.Succeeded());
    ASSERT_TRUE(result.Diagnostics.Converged);
    ASSERT_EQ(result.Mixture.Components.size(), 2u);
    ASSERT_EQ(result.Mixture.Weights.size(), 2u);
    const auto order = SortedByMeanX(result.Mixture);
    EXPECT_NEAR(
        result.Mixture.Components[order[0]].Mean.x,
        -2.0, 0.12);
    EXPECT_NEAR(
        result.Mixture.Components[order[1]].Mean.x,
        2.0, 0.12);
    EXPECT_NEAR(result.Mixture.Weights[order[0]], 0.5, 0.05);
    EXPECT_NEAR(result.Mixture.Weights[order[1]], 0.5, 0.05);
    EXPECT_GT(
        result.Diagnostics.FinalLogLikelihood,
        result.Diagnostics.InitialLogLikelihood);
    ASSERT_FALSE(
        result.Diagnostics.LogLikelihoodHistory.empty());
    EXPECT_GE(
        result.Diagnostics.LogLikelihoodHistory.front() + 1.0e-10,
        result.Diagnostics.InitialLogLikelihood);
    for (std::size_t i = 1u;
         i < result.Diagnostics.LogLikelihoodHistory.size();
         ++i)
    {
        EXPECT_GE(
            result.Diagnostics.LogLikelihoodHistory[i] + 1.0e-10,
            result.Diagnostics.LogLikelihoodHistory[i - 1u]);
    }
}

TEST(GaussianMixture, RecoversThreeSeparatedComponents)
{
    const std::vector<glm::vec3> points = ThreeClusterFixture();
    Gmm::FitParams params{};
    params.MaxIterations = 80u;
    params.RelativeTolerance = 1.0e-8;
    params.CovarianceFloor = 1.0e-5;
    params.Seed = 31u;

    const Gmm::FitResult result = Gmm::FitEM(points, 3u, params);
    ASSERT_TRUE(result.Succeeded());
    ASSERT_TRUE(result.Diagnostics.Converged);
    const auto order = SortedByMeanX(result.Mixture);
    ASSERT_EQ(order.size(), 3u);
    EXPECT_NEAR(
        result.Mixture.Components[order[0]].Mean.x,
        -3.0, 0.12);
    EXPECT_NEAR(
        result.Mixture.Components[order[1]].Mean.x,
        0.0, 0.12);
    EXPECT_NEAR(
        result.Mixture.Components[order[2]].Mean.x,
        3.0, 0.12);
    EXPECT_NEAR(
        result.Mixture.Components[order[1]].Mean.y,
        2.0, 0.12);
}

TEST(GaussianMixture, AndersonMatchesPlainEmWithoutMoreIterations)
{
    const std::vector<glm::vec3> points = OverlappingClusterFixture();
    Gmm::FitParams plainParams{};
    plainParams.MaxIterations = 80u;
    plainParams.RelativeTolerance = 1.0e-8;
    plainParams.CovarianceFloor = 1.0e-5;
    plainParams.Seed = 29u;
    Gmm::FitParams acceleratedParams = plainParams;
    acceleratedParams.Acceleration =
        Gmm::AccelerationPolicy::Anderson;
    acceleratedParams.Anderson.Window = 4u;
    acceleratedParams.Anderson.SafeguardFactor = 1.0;

    const Gmm::FitResult plain = Gmm::FitEM(
        points, 2u, plainParams);
    const Gmm::FitResult accelerated = Gmm::FitEM(
        points, 2u, acceleratedParams);
    ASSERT_TRUE(plain.Succeeded());
    ASSERT_TRUE(accelerated.Succeeded());
    ASSERT_TRUE(plain.Diagnostics.Converged);
    ASSERT_TRUE(accelerated.Diagnostics.Converged);
    EXPECT_LE(
        accelerated.Diagnostics.Iterations,
        plain.Diagnostics.Iterations);
    EXPECT_NEAR(
        accelerated.Diagnostics.FinalLogLikelihood,
        plain.Diagnostics.FinalLogLikelihood,
        1.0e-4);
    EXPECT_GT(accelerated.Diagnostics.AcceleratedSteps, 0u)
        << "safeguarded="
        << accelerated.Diagnostics.SafeguardedSteps
        << " iterations="
        << accelerated.Diagnostics.Iterations;

    const auto plainOrder = SortedByMeanX(plain.Mixture);
    const auto acceleratedOrder = SortedByMeanX(
        accelerated.Mixture);
    for (std::size_t i = 0u; i < 2u; ++i)
    {
        EXPECT_NEAR(
            accelerated.Mixture.Components[
                acceleratedOrder[i]].Mean.x,
            plain.Mixture.Components[plainOrder[i]].Mean.x,
            1.0e-3);
    }
}

TEST(GaussianMixture, CoincidentPointsUseCovarianceFloorDeterministically)
{
    const std::vector<glm::vec3> points(
        8u, glm::vec3{1.0f, -2.0f, 3.0f});
    Gmm::FitParams params{};
    params.CovarianceFloor = 1.0e-4;
    params.MaxIterations = 16u;
    params.Seed = 7u;

    const Gmm::FitResult first = Gmm::FitEM(
        points, 2u, params);
    const Gmm::FitResult second = Gmm::FitEM(
        points, 2u, params);
    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    ASSERT_EQ(first.Mixture.Weights, second.Mixture.Weights);
    ASSERT_EQ(first.Mixture.Components.size(), 2u);
    for (std::size_t i = 0u; i < 2u; ++i)
    {
        EXPECT_EQ(
            first.Mixture.Components[i].Mean,
            second.Mixture.Components[i].Mean);
        EXPECT_EQ(
            first.Mixture.Components[i].Covariance,
            second.Mixture.Components[i].Covariance);
        EXPECT_GE(
            first.Mixture.Components[i].Covariance[0][0],
            params.CovarianceFloor);
        EXPECT_TRUE(Gmm::LogPdf(
            first.Mixture.Components[i],
            glm::dvec3(points.front())).has_value());
    }
}

TEST(GaussianMixture, InvalidInputsFailClosed)
{
    Gmm::FitParams params{};
    EXPECT_EQ(
        Gmm::FitEM({}, 1u, params).Diagnostics.Status,
        Gmm::FitStatus::EmptyInput);

    const std::vector<glm::vec3> onePoint{
        glm::vec3{0.0f},
    };
    EXPECT_EQ(
        Gmm::FitEM(onePoint, 0u, params).Diagnostics.Status,
        Gmm::FitStatus::InvalidComponentCount);
    EXPECT_EQ(
        Gmm::FitEM(onePoint, 2u, params).Diagnostics.Status,
        Gmm::FitStatus::InvalidComponentCount);

    std::vector<glm::vec3> nonFinite = onePoint;
    nonFinite.front().x =
        std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(
        Gmm::FitEM(nonFinite, 1u, params).Diagnostics.Status,
        Gmm::FitStatus::NonFiniteInput);

    params.CovarianceFloor = 0.0;
    EXPECT_EQ(
        Gmm::FitEM(onePoint, 1u, params).Diagnostics.Status,
        Gmm::FitStatus::InvalidParameters);

    params.CovarianceFloor = 1.0e-6;
    params.Acceleration = Gmm::AccelerationPolicy::Anderson;
    params.Anderson.Window = 0u;
    EXPECT_EQ(
        Gmm::FitEM(onePoint, 1u, params).Diagnostics.Status,
        Gmm::FitStatus::InvalidParameters);

    Gmm::MultivariateGaussian invalid{};
    invalid.Covariance = glm::dmat3{0.0};
    EXPECT_FALSE(Gmm::LogPdf(invalid, glm::dvec3{0.0}));
    EXPECT_FALSE(Gmm::Sample(invalid, 1u));
}
