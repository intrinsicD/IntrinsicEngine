#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.PointCloud;
import Geometry.PointCloud.Consolidation;

namespace
{
    namespace Consolidation = Geometry::PointCloud::Consolidation;

    [[nodiscard]] Consolidation::Params ReferenceParams()
    {
        return Consolidation::Params{
            .Method = Consolidation::WlopStrategy{},
            .SupportRadius = 0.65,
            .RepulsionWeight = 0.2,
            .MaxIterations = 40u,
            .ConvergenceTolerance = 2.0e-4,
            .TargetPointCount = 0u,
            .Seed = 17u,
        };
    }

    [[nodiscard]] std::vector<glm::vec3> NoisyPlane(
        const int side = 7)
    {
        std::vector<glm::vec3> points{};
        points.reserve(static_cast<std::size_t>(side * side));
        for (int y = 0; y < side; ++y)
        {
            for (int x = 0; x < side; ++x)
            {
                const float noise =
                    ((x * 13 + y * 7) % 5 - 2) * 0.025f;
                points.emplace_back(
                    (static_cast<float>(x) - 0.5f * (side - 1)) * 0.2f,
                    (static_cast<float>(y) - 0.5f * (side - 1)) * 0.2f,
                    noise);
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> NoisySphere()
    {
        std::vector<glm::vec3> points{};
        constexpr double pi = 3.14159265358979323846;
        for (int latitude = 1; latitude <= 5; ++latitude)
        {
            const double phi = pi * latitude / 6.0;
            for (int longitude = 0; longitude < 12; ++longitude)
            {
                const double theta = 2.0 * pi * longitude / 12.0;
                const std::size_t index = points.size();
                const double noise =
                    (static_cast<int>((index * 11u) % 7u) - 3) * 0.012;
                const double radius = 1.0 + noise;
                points.emplace_back(
                    static_cast<float>(radius * std::sin(phi) *
                                       std::cos(theta)),
                    static_cast<float>(radius * std::sin(phi) *
                                       std::sin(theta)),
                    static_cast<float>(radius * std::cos(phi)));
            }
        }
        return points;
    }

    [[nodiscard]] double MeanPlaneError(
        const std::span<const glm::vec3> points)
    {
        double sum = 0.0;
        for (const glm::vec3 point : points)
            sum += std::abs(static_cast<double>(point.z));
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] double MeanSphereError(
        const std::span<const glm::vec3> points)
    {
        double sum = 0.0;
        for (const glm::vec3 point : points)
            sum += std::abs(static_cast<double>(glm::length(point)) - 1.0);
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] double MinimumPairwiseDistance(
        const std::span<const glm::vec3> points)
    {
        double minimum = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0u; i < points.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < points.size(); ++j)
            {
                minimum = std::min(
                    minimum,
                    static_cast<double>(glm::distance(points[i], points[j])));
            }
        }
        return minimum;
    }

    void ExpectFiniteOutput(const Consolidation::Result& result)
    {
        ASSERT_FALSE(result.Positions.empty());
        for (const glm::vec3 point : result.Positions)
        {
            EXPECT_TRUE(std::isfinite(point.x));
            EXPECT_TRUE(std::isfinite(point.y));
            EXPECT_TRUE(std::isfinite(point.z));
        }
    }
}

TEST(PointCloudConsolidation, WlopDenoisesPlaneAndSphere)
{
    const auto plane = NoisyPlane();
    auto planeParams = ReferenceParams();
    planeParams.RepulsionWeight = 0.0;
    planeParams.ConvergenceTolerance = 1.0;
    planeParams.TargetPointCount = 25u;
    planeParams.MaxIterations = 1u;
    const auto projectedPlane =
        Consolidation::Consolidate(plane, planeParams);
    ASSERT_TRUE(projectedPlane.Succeeded())
        << Consolidation::DebugName(projectedPlane.State);
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions),
              MeanPlaneError(plane));
    // Frozen bound: the 0.0301 raw fixture must fall below 0.024 after the
    // deterministic initializer plus one reference iteration.
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions), 0.024);

    const auto sphere = NoisySphere();
    auto sphereParams = ReferenceParams();
    sphereParams.SupportRadius = 0.9;
    sphereParams.RepulsionWeight = 0.0;
    sphereParams.MaxIterations = 50u;
    sphereParams.ConvergenceTolerance = 2.0e-3;
    const auto projectedSphere =
        Consolidation::Consolidate(sphere, sphereParams);
    ASSERT_TRUE(projectedSphere.Succeeded())
        << Consolidation::DebugName(projectedSphere.State);
    EXPECT_LT(MeanSphereError(projectedSphere.Positions),
              MeanSphereError(sphere));
    EXPECT_LT(MeanSphereError(projectedSphere.Positions), 0.025);
}

TEST(PointCloudConsolidation, LopRepulsionImprovesMinimumSpacing)
{
    std::vector<glm::vec3> points{};
    for (int y = 0; y < 5; ++y)
    {
        for (int x = 0; x < 5; ++x)
        {
            const float compressedX = 0.035f * x * x;
            points.emplace_back(compressedX, 0.15f * y, 0.0f);
        }
    }

    auto withoutParams = ReferenceParams();
    withoutParams.Method = Consolidation::LopStrategy{};
    withoutParams.TargetPointCount = 16u;
    withoutParams.RepulsionWeight = 0.0;
    withoutParams.ConvergenceTolerance = 5.0e-4;
    const auto without = Consolidation::Consolidate(points, withoutParams);
    ASSERT_FALSE(without.Positions.empty());

    auto withParams = withoutParams;
    withParams.RepulsionWeight = 0.45;
    const auto with = Consolidation::Consolidate(points, withParams);
    ASSERT_FALSE(with.Positions.empty());
    EXPECT_GT(MinimumPairwiseDistance(with.Positions),
              MinimumPairwiseDistance(without.Positions) * 1.05);
}

TEST(PointCloudConsolidation, WlopCorrectsNonUniformDensityMoreThanLop)
{
    const std::vector<glm::vec3> points{
        {0.00f, 0.0f, 0.0f}, {0.02f, 0.0f, 0.0f},
        {0.04f, 0.0f, 0.0f}, {0.06f, 0.0f, 0.0f},
        {0.08f, 0.0f, 0.0f}, {0.30f, 0.0f, 0.0f},
        {0.55f, 0.0f, 0.0f}, {0.80f, 0.0f, 0.0f},
        {1.00f, 0.0f, 0.0f},
    };
    auto lopParams = ReferenceParams();
    lopParams.Method = Consolidation::LopStrategy{};
    lopParams.SupportRadius = 1.1;
    lopParams.TargetPointCount = 6u;
    lopParams.RepulsionWeight = 0.35;
    const auto lop = Consolidation::Consolidate(points, lopParams);
    ASSERT_FALSE(lop.Positions.empty());

    auto wlopParams = lopParams;
    wlopParams.Method = Consolidation::WlopStrategy{};
    const auto wlop = Consolidation::Consolidate(points, wlopParams);
    ASSERT_FALSE(wlop.Positions.empty());
    EXPECT_GT(MinimumPairwiseDistance(wlop.Positions),
              MinimumPairwiseDistance(lop.Positions));
    EXPECT_TRUE(wlop.Diagnostics.UsedDensityWeighting);
    EXPECT_GT(wlop.Diagnostics.DensityContributionCount, 0u);
}

TEST(PointCloudConsolidation, SeparatedOutlierClusterDoesNotPullPlanePatch)
{
    const std::vector<glm::vec3> plane = NoisyPlane(4);
    std::vector<glm::vec3> withOutliers = plane;
    withOutliers.emplace_back(10.0f, 0.0f, 0.0f);
    withOutliers.emplace_back(10.1f, 0.0f, 0.0f);

    auto params = ReferenceParams();
    params.MaxIterations = 12u;
    params.ConvergenceTolerance = 0.0;
    const auto baseline = Consolidation::Consolidate(plane, params);
    const auto outlierRun = Consolidation::Consolidate(withOutliers, params);
    ASSERT_FALSE(baseline.Positions.empty());
    ASSERT_GE(outlierRun.Positions.size(), plane.size());
    for (std::size_t i = 0u; i < plane.size(); ++i)
        EXPECT_LT(glm::distance(
                      outlierRun.Positions[i], baseline.Positions[i]),
                  1.0e-5f);
}

TEST(PointCloudConsolidation, StrategyAndSeedAreBitwiseDeterministic)
{
    const auto points = NoisyPlane();
    auto params = ReferenceParams();
    params.TargetPointCount = 24u;
    const auto first = Consolidation::Consolidate(points, params);
    const auto second = Consolidation::Consolidate(points, params);
    ASSERT_EQ(first.State, second.State);
    EXPECT_EQ(first.Positions, second.Positions);
    EXPECT_EQ(first.Diagnostics.Strategy,
              Consolidation::StrategyKind::Wlop);
    EXPECT_EQ(first.Diagnostics.Implementation,
              Consolidation::kCpuReferenceImplementation);

    params.Method = Consolidation::LopStrategy{};
    const auto lop = Consolidation::Consolidate(points, params);
    EXPECT_EQ(lop.Diagnostics.Strategy,
              Consolidation::StrategyKind::Lop);
    EXPECT_FALSE(lop.Diagnostics.UsedDensityWeighting);
    ASSERT_EQ(lop.Positions.size(), 24u);
    EXPECT_NEAR(MeanPlaneError(lop.Positions),
                0.032516757084522396, 1.0e-8);
}

TEST(PointCloudConsolidation, InvalidRequestsFailWithoutPublishingPositions)
{
    const std::vector<glm::vec3> points{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    const auto expectFailure = [](
        const Consolidation::Result& result,
        const Consolidation::Status status)
    {
        EXPECT_EQ(result.State, status);
        EXPECT_TRUE(result.Positions.empty());
    };

    expectFailure(Consolidation::Consolidate(
                      std::span<const glm::vec3>{}, ReferenceParams()),
                  Consolidation::Status::EmptyInput);
    expectFailure(Consolidation::Consolidate(
                      std::span<const glm::vec3>{points.data(), 1u},
                      ReferenceParams()),
                  Consolidation::Status::TooFewPoints);

    auto params = ReferenceParams();
    params.SupportRadius = 0.0;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidSupportRadius);
    params = ReferenceParams();
    params.RepulsionWeight = 0.5;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidRepulsionWeight);
    params = ReferenceParams();
    params.MaxIterations = 0u;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidIterationLimit);
    params = ReferenceParams();
    params.ConvergenceTolerance = -1.0;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidConvergenceTolerance);
    params = ReferenceParams();
    params.TargetPointCount = 3u;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::InvalidTargetCount);

    auto nonFinite = points;
    nonFinite[1].z = std::numeric_limits<float>::quiet_NaN();
    expectFailure(Consolidation::Consolidate(nonFinite, ReferenceParams()),
                  Consolidation::Status::NonFiniteInput);

    params = ReferenceParams();
    params.SupportRadius = 0.25;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::EmptyNeighborhood);
}

TEST(PointCloudConsolidation, DegenerateAndNonConvergedStatesStayFinite)
{
    const std::vector<glm::vec3> coincident(6u, glm::vec3(1.0f));
    auto params = ReferenceParams();
    const auto stable = Consolidation::Consolidate(coincident, params);
    ASSERT_TRUE(stable.Succeeded())
        << Consolidation::DebugName(stable.State);
    ExpectFiniteOutput(stable);

    params = ReferenceParams();
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 0.0;
    const auto unfinished =
        Consolidation::Consolidate(NoisyPlane(), params);
    EXPECT_EQ(unfinished.State, Consolidation::Status::NotConverged);
    EXPECT_FALSE(unfinished.Diagnostics.Converged);
    EXPECT_EQ(unfinished.Diagnostics.Iterations, 1u);
    ExpectFiniteOutput(unfinished);
}

TEST(PointCloudConsolidation, CloudOverloadRejectsDeletedSlots)
{
    Geometry::PointCloud::Cloud cloud{};
    const auto first = cloud.AddPoint({0.0f, 0.0f, 0.0f});
    static_cast<void>(cloud.AddPoint({0.1f, 0.0f, 0.0f}));
    cloud.DeletePoint(first);
    const auto result =
        Consolidation::Consolidate(cloud, ReferenceParams());
    EXPECT_EQ(result.State, Consolidation::Status::InvalidCloud);
    EXPECT_TRUE(result.Positions.empty());
}
