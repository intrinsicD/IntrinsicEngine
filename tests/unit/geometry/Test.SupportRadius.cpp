#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.SupportRadius;

namespace
{
    namespace Radius = Geometry::SupportRadius;

    [[nodiscard]] std::vector<glm::vec3> Line(
        const std::size_t count,
        const float spacing = 1.0F)
    {
        std::vector<glm::vec3> points{};
        points.reserve(count);
        for (std::size_t index = 0u; index < count; ++index)
        {
            points.emplace_back(
                static_cast<float>(index) * spacing, 0.0F, 0.0F);
        }
        return points;
    }
}

TEST(SupportRadius, RecommendedProfileIsDeterministicAndBounded)
{
    const std::vector<glm::vec3> points = Line(3'000u, 0.01F);
    const Radius::RecommendationPolicy policy{
        .NeighborRank = 16u,
        .Quantile = Radius::CoverageQuantile::P75,
        .RadiusMultiplier = 1.0,
    };

    const Radius::Analysis first = Radius::Analyze(
        points, std::nullopt, policy);
    const Radius::Analysis second = Radius::Analyze(
        points, std::nullopt, policy);

    ASSERT_TRUE(first.Succeeded()) << Radius::DebugName(first.State);
    ASSERT_TRUE(second.Succeeded()) << Radius::DebugName(second.State);
    EXPECT_EQ(first.ProfileSampleCount, Radius::kMaximumProfileSamples);
    EXPECT_EQ(first.ProfileSampleCount, second.ProfileSampleCount);
    EXPECT_EQ(first.Ranks.size(), 32u);
    EXPECT_DOUBLE_EQ(first.Radius, second.Radius);
    EXPECT_DOUBLE_EQ(
        first.SupportNeighborsP95, second.SupportNeighborsP95);
    EXPECT_GT(first.Radius, 0.0);
    EXPECT_GT(first.SelectedNeighborDistance, 0.0);
    EXPECT_GT(first.BoundingBoxDiagonal, first.Radius);
}

TEST(SupportRadius, PolicySelectsRankQuantileAndMultiplier)
{
    const std::vector<glm::vec3> points = Line(65u);
    const Radius::RecommendationPolicy policy{
        .NeighborRank = 4u,
        .Quantile = Radius::CoverageQuantile::P90,
        .RadiusMultiplier = 1.5,
    };
    const Radius::ProfileParams profile{
        .MaxSamples = points.size(),
        .MaxNeighborRank = 8u,
    };

    const Radius::Analysis result = Radius::Analyze(
        points, std::nullopt, policy, {}, profile);

    ASSERT_TRUE(result.Succeeded()) << Radius::DebugName(result.State);
    EXPECT_EQ(result.SelectedNeighborRank, 4u);
    EXPECT_EQ(result.SelectedQuantile, Radius::CoverageQuantile::P90);
    EXPECT_DOUBLE_EQ(
        result.Radius, result.SelectedNeighborDistance * 1.5);
    EXPECT_GE(result.SupportNeighborsMax, 3u);
}

TEST(SupportRadius, RecommendationTracksTheSelectedPropertyScale)
{
    const std::vector<glm::vec3> unitScale = Line(65u, 0.1F);
    const std::vector<glm::vec3> largeScale = Line(65u, 10.0F);

    const Radius::Analysis unit = Radius::Analyze(unitScale, std::nullopt);
    const Radius::Analysis large = Radius::Analyze(largeScale, std::nullopt);

    ASSERT_TRUE(unit.Succeeded()) << Radius::DebugName(unit.State);
    ASSERT_TRUE(large.Succeeded()) << Radius::DebugName(large.State);
    EXPECT_GT(large.Radius, unit.Radius * 99.0);
    EXPECT_LT(large.Radius, unit.Radius * 101.0);
}

TEST(SupportRadius, RecommendationClampsRankForSmallInputs)
{
    const std::vector<glm::vec3> points = Line(4u);
    const Radius::Analysis result = Radius::Analyze(
        points,
        std::nullopt,
        Radius::RecommendationPolicy{
            .NeighborRank = 16u,
            .Quantile = Radius::CoverageQuantile::P75,
            .RadiusMultiplier = 1.25,
        });

    ASSERT_TRUE(result.Succeeded()) << Radius::DebugName(result.State);
    EXPECT_EQ(result.SelectedNeighborRank, 3u);
    EXPECT_GT(result.Radius, result.SelectedNeighborDistance);
}

TEST(SupportRadius, NonUniformAndDuplicateSamplesRemainDiagnosable)
{
    std::vector<glm::vec3> points{};
    points.reserve(54u);
    for (std::size_t index = 0u; index < 25u; ++index)
    {
        points.emplace_back(
            static_cast<float>(index) * 0.01F, 0.0F, 0.0F);
    }
    for (std::size_t index = 1u; index <= 25u; ++index)
    {
        points.emplace_back(
            0.24F + static_cast<float>(index), 0.0F, 0.0F);
    }
    points.insert(points.end(), 4u, points.front());

    const Radius::Analysis result = Radius::Analyze(
        points,
        std::nullopt,
        Radius::RecommendationPolicy{
            .NeighborRank = 8u,
            .Quantile = Radius::CoverageQuantile::P75,
            .RadiusMultiplier = 1.25,
        });

    ASSERT_TRUE(result.Succeeded()) << Radius::DebugName(result.State);
    ASSERT_GE(result.Ranks.size(), 8u);
    EXPECT_GT(result.CoincidentNeighborCount, 0u);
    EXPECT_GT(result.Ranks[7u].Distance.P95,
              result.Ranks[7u].Distance.P10);
    EXPECT_GT(result.SupportNeighborsMax, result.SupportNeighborsP50);
}

TEST(SupportRadius, ManualRadiusIsPreservedAndStillProfilesWork)
{
    const std::vector<glm::vec3> points = Line(32u);
    const Radius::Analysis result = Radius::Analyze(points, 1.01);

    ASSERT_TRUE(result.Succeeded()) << Radius::DebugName(result.State);
    EXPECT_EQ(result.Source, Radius::RadiusSource::Manual);
    EXPECT_DOUBLE_EQ(result.Radius, 1.01);
    EXPECT_TRUE(result.Ranks.empty());
    EXPECT_EQ(result.SelectedNeighborRank, 0u);
    EXPECT_DOUBLE_EQ(result.SupportNeighborsP50, 3.0);
    EXPECT_EQ(result.SupportNeighborsMax, 3u);
}

TEST(SupportRadius, ExactCutoffExcludesPointsAtTheRadius)
{
    const std::vector<glm::vec3> points = Line(9u);
    const Radius::Analysis result = Radius::Analyze(points, 1.0);

    ASSERT_TRUE(result.Succeeded()) << Radius::DebugName(result.State);
    EXPECT_DOUBLE_EQ(result.SupportNeighborsP50, 1.0);
    EXPECT_EQ(result.SupportNeighborsMax, 1u);
}

TEST(SupportRadius, WorkloadLimitsRetainDiagnosticsAndFailClosed)
{
    const std::vector<glm::vec3> points = Line(100u);
    const Radius::WorkloadBudget budget{
        .PredictedQueryCount = 1'000u,
        .FixedContributionCount = 7u,
        .MaxNeighborsPerSample = 4u,
        .MaxPredictedContributions = 100u,
    };
    const Radius::Analysis result = Radius::Analyze(
        points, 10.0, {}, budget);

    EXPECT_EQ(result.State, Radius::Status::WorkloadLimitExceeded);
    EXPECT_TRUE(result.NeighborLimitExceeded);
    EXPECT_TRUE(result.ContributionLimitExceeded);
    EXPECT_GT(result.SupportNeighborsMax, 4u);
    EXPECT_GT(result.PredictedContributionCount, 100u);
}

TEST(SupportRadius, DegenerateAndInvalidInputsHaveExplicitStatuses)
{
    EXPECT_EQ(
        Radius::Analyze({}, std::nullopt).State,
        Radius::Status::EmptyInput);
    EXPECT_EQ(
        Radius::Analyze(Line(1u), std::nullopt).State,
        Radius::Status::InsufficientPoints);

    std::vector<glm::vec3> nonFinite = Line(4u);
    nonFinite[2].x = std::numeric_limits<float>::infinity();
    EXPECT_EQ(
        Radius::Analyze(nonFinite, std::nullopt).State,
        Radius::Status::NonFiniteInput);

    const std::vector<glm::vec3> coincident(
        8u, glm::vec3{1.0F, 2.0F, 3.0F});
    EXPECT_EQ(
        Radius::Analyze(coincident, std::nullopt).State,
        Radius::Status::DegenerateNeighborhood);

    const Radius::ProfileParams tooManySamples{
        .MaxSamples = Radius::kMaximumProfileSamples + 1u,
        .MaxNeighborRank = 16u,
    };
    EXPECT_EQ(
        Radius::Analyze(
            Line(8u), std::nullopt, {}, {}, tooManySamples).State,
        Radius::Status::InvalidParameters);
    const Radius::RecommendationPolicy invalidQuantile{
        .NeighborRank = 4u,
        .Quantile = static_cast<Radius::CoverageQuantile>(255u),
        .RadiusMultiplier = 1.0,
    };
    EXPECT_EQ(
        Radius::Analyze(
            Line(8u), std::nullopt, invalidQuantile).State,
        Radius::Status::InvalidParameters);
    EXPECT_EQ(
        Radius::Analyze(Line(8u), 0.0).State,
        Radius::Status::InvalidRadius);
}
