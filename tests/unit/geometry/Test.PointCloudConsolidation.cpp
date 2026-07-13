#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace
{
    namespace Consolidation = Geometry::PointCloud::Consolidation;

    // Deterministic noisy plane: a regular grid on z = 0 in [0, 1]^2 with
    // Gaussian z-offsets. sigma = 0.02, spacing ~ 0.043 (24 x 24).
    constexpr int kPlaneSide = 24;
    constexpr float kPlaneNoiseSigma = 0.02f;
    constexpr float kPlaneSupportRadius = 0.15f;

    [[nodiscard]] std::vector<glm::vec3> MakeNoisyPlane(const std::uint32_t seed = 7u)
    {
        std::mt19937 rng(seed);
        std::normal_distribution<float> noise(0.0f, kPlaneNoiseSigma);
        std::vector<glm::vec3> points;
        points.reserve(static_cast<std::size_t>(kPlaneSide) * kPlaneSide);
        for (int row = 0; row < kPlaneSide; ++row)
        {
            for (int column = 0; column < kPlaneSide; ++column)
            {
                const float x = static_cast<float>(column) / (kPlaneSide - 1);
                const float y = static_cast<float>(row) / (kPlaneSide - 1);
                points.emplace_back(x, y, noise(rng));
            }
        }
        return points;
    }

    // Deterministic noisy sphere: Fibonacci-spiral samples of the unit sphere
    // with Gaussian radial offsets. sigma = 0.02, spacing ~ 0.079 (2000 pts).
    constexpr std::size_t kSphereCount = 2000;
    constexpr float kSphereNoiseSigma = 0.02f;
    constexpr float kSphereSupportRadius = 0.16f;

    [[nodiscard]] std::vector<glm::vec3> MakeNoisySphere(const std::uint32_t seed = 11u)
    {
        std::mt19937 rng(seed);
        std::normal_distribution<float> noise(0.0f, kSphereNoiseSigma);
        std::vector<glm::vec3> points;
        points.reserve(kSphereCount);
        const float golden = 3.14159265358979f * (3.0f - std::sqrt(5.0f));
        for (std::size_t i = 0; i < kSphereCount; ++i)
        {
            const float z = 1.0f - 2.0f * (static_cast<float>(i) + 0.5f) / kSphereCount;
            const float ring = std::sqrt(std::max(0.0f, 1.0f - z * z));
            const float angle = golden * static_cast<float>(i);
            const glm::vec3 unit(ring * std::cos(angle), ring * std::sin(angle), z);
            points.push_back(unit * (1.0f + noise(rng)));
        }
        return points;
    }

    [[nodiscard]] double MeanAbsPlaneDistance(const std::vector<glm::vec3>& points)
    {
        double sum = 0.0;
        for (const glm::vec3& p : points)
            sum += std::abs(static_cast<double>(p.z));
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] double MeanAbsSphereDistance(const std::vector<glm::vec3>& points)
    {
        double sum = 0.0;
        for (const glm::vec3& p : points)
            sum += std::abs(static_cast<double>(glm::length(p)) - 1.0);
        return sum / static_cast<double>(points.size());
    }

    [[nodiscard]] Consolidation::WlopParams MakePlaneParams()
    {
        Consolidation::WlopParams params{};
        params.SupportRadius = kPlaneSupportRadius;
        params.RepulsionWeight = 0.45f;
        params.Iterations = 12;
        params.TargetCount = 200;
        params.Seed = 42;
        return params;
    }
}

TEST(PointCloudConsolidation, WlopDenoisesNoisyPlane)
{
    const std::vector<glm::vec3> points = MakeNoisyPlane();
    const double rawMean = MeanAbsPlaneDistance(points);

    const Consolidation::ConsolidateResult result =
        Consolidation::Consolidate(points, MakePlaneParams());

    ASSERT_TRUE(result.Succeeded())
        << Consolidation::DebugName(result.Status);
    ASSERT_EQ(result.Positions.size(), 200u);
    const double projectedMean = MeanAbsPlaneDistance(result.Positions);
    EXPECT_LT(projectedMean, rawMean);
    // Documented bound (method README): averaging ~40 support-radius
    // neighbors shrinks sigma = 0.02 noise well below 0.008.
    EXPECT_LT(projectedMean, 0.008);
}

TEST(PointCloudConsolidation, WlopDenoisesNoisySphere)
{
    const std::vector<glm::vec3> points = MakeNoisySphere();
    const double rawMean = MeanAbsSphereDistance(points);

    Consolidation::WlopParams params{};
    params.SupportRadius = kSphereSupportRadius;
    params.RepulsionWeight = 0.45f;
    params.Iterations = 12;
    params.TargetCount = 500;
    params.Seed = 42;

    const Consolidation::ConsolidateResult result =
        Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded());
    ASSERT_EQ(result.Positions.size(), 500u);
    const double projectedMean = MeanAbsSphereDistance(result.Positions);
    EXPECT_LT(projectedMean, rawMean);
    // Documented bound: residual = attenuated noise plus the inward
    // curvature bias of the localized L1 median (~h^2 / 8).
    EXPECT_LT(projectedMean, 0.012);
}

TEST(PointCloudConsolidation, RepulsionImprovesMinPairwiseDistance)
{
    const std::vector<glm::vec3> points = MakeNoisyPlane();

    Consolidation::WlopParams repulsed = MakePlaneParams();
    Consolidation::WlopParams unrepulsed = MakePlaneParams();
    unrepulsed.RepulsionWeight = 0.0f;

    const Consolidation::ConsolidateResult withRepulsion =
        Consolidation::Consolidate(points, repulsed);
    const Consolidation::ConsolidateResult withoutRepulsion =
        Consolidation::Consolidate(points, unrepulsed);

    ASSERT_TRUE(withRepulsion.Succeeded());
    ASSERT_TRUE(withoutRepulsion.Succeeded());

    const auto repulsedMetrics =
        Geometry::PointCloud::QualityMetrics::ComputeNearestNeighborDistances(
            std::span<const glm::vec3>(withRepulsion.Positions));
    const auto unrepulsedMetrics =
        Geometry::PointCloud::QualityMetrics::ComputeNearestNeighborDistances(
            std::span<const glm::vec3>(withoutRepulsion.Positions));
    ASSERT_TRUE(repulsedMetrics.Succeeded());
    ASSERT_TRUE(unrepulsedMetrics.Succeeded());

    EXPECT_GT(repulsedMetrics.MinDistance, unrepulsedMetrics.MinDistance);
}

TEST(PointCloudConsolidation, OutliersBeyondSupportDoNotAttract)
{
    std::vector<glm::vec3> points = MakeNoisyPlane();
    const std::size_t planeCount = points.size();
    // Sparse far outliers, well outside the support radius of every plane
    // point (z = 0.5 versus h = 0.15).
    for (int i = 0; i < 6; ++i)
    {
        const float t = static_cast<float>(i) / 5.0f;
        points.emplace_back(t, 1.0f - t, 0.5f);
    }

    Consolidation::WlopParams params = MakePlaneParams();
    params.TargetCount = 0;
    params.InitialIndices.resize(200);
    // Explicit seeds drawn from the plane subset only; a seed placed on an
    // isolated outlier would legitimately stay there (documented limitation).
    for (std::size_t i = 0; i < params.InitialIndices.size(); ++i)
        params.InitialIndices[i] = (i * planeCount) / params.InitialIndices.size();

    const Consolidation::ConsolidateResult result =
        Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded());
    double maxAbsZ = 0.0;
    for (const glm::vec3& p : result.Positions)
        maxAbsZ = std::max(maxAbsZ, std::abs(static_cast<double>(p.z)));
    EXPECT_LT(maxAbsZ, 3.0 * kPlaneNoiseSigma);
}

TEST(PointCloudConsolidation, LopVariantDenoisesPlane)
{
    const std::vector<glm::vec3> points = MakeNoisyPlane();
    const double rawMean = MeanAbsPlaneDistance(points);

    Consolidation::WlopParams params = MakePlaneParams();
    params.Method = Consolidation::Variant::Lop;

    const Consolidation::ConsolidateResult result =
        Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded());
    const double projectedMean = MeanAbsPlaneDistance(result.Positions);
    EXPECT_LT(projectedMean, rawMean);
    EXPECT_LT(projectedMean, 0.008);
}

TEST(PointCloudConsolidation, ConvergenceReportTracksIterations)
{
    const std::vector<glm::vec3> points = MakeNoisyPlane();
    const Consolidation::WlopParams params = MakePlaneParams();

    const Consolidation::ConsolidateResult result =
        Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Report.IterationsRun, params.Iterations);
    ASSERT_EQ(result.Report.Movement.size(), params.Iterations);
    EXPECT_EQ(result.Report.InputPointCount, points.size());
    EXPECT_EQ(result.Report.ProjectedPointCount, 200u);
    // The fixed-point iteration settles: late displacement is well below the
    // initial projection step.
    EXPECT_LT(result.Report.Movement.back().MeanMovement,
              result.Report.Movement.front().MeanMovement);
    EXPECT_EQ(result.InitialIndices.size(), 200u);
}

TEST(PointCloudConsolidation, DeterministicAcrossRunsAndOverloads)
{
    const std::vector<glm::vec3> points = MakeNoisyPlane();
    const Consolidation::WlopParams params = MakePlaneParams();

    const Consolidation::ConsolidateResult first =
        Consolidation::Consolidate(points, params);
    const Consolidation::ConsolidateResult second =
        Consolidation::Consolidate(points, params);

    ASSERT_TRUE(first.Succeeded());
    ASSERT_TRUE(second.Succeeded());
    ASSERT_EQ(first.Positions.size(), second.Positions.size());
    for (std::size_t i = 0; i < first.Positions.size(); ++i)
    {
        EXPECT_EQ(first.Positions[i].x, second.Positions[i].x);
        EXPECT_EQ(first.Positions[i].y, second.Positions[i].y);
        EXPECT_EQ(first.Positions[i].z, second.Positions[i].z);
    }
    EXPECT_EQ(first.InitialIndices, second.InitialIndices);

    Geometry::PointCloud::Cloud cloud;
    cloud.Reserve(points.size());
    for (const glm::vec3& p : points)
        cloud.AddPoint(p);
    const Consolidation::ConsolidateResult viaCloud =
        Consolidation::Consolidate(cloud, params);
    ASSERT_TRUE(viaCloud.Succeeded());
    ASSERT_EQ(viaCloud.Positions.size(), first.Positions.size());
    for (std::size_t i = 0; i < first.Positions.size(); ++i)
        EXPECT_EQ(viaCloud.Positions[i], first.Positions[i]);
}

TEST(PointCloudConsolidation, ExplicitInitialIndicesAreRespected)
{
    const std::vector<glm::vec3> points = MakeNoisyPlane();

    Consolidation::WlopParams params = MakePlaneParams();
    params.TargetCount = 0;
    params.InitialIndices = {5, 40, 99, 200, 333, 471};

    const Consolidation::ConsolidateResult result =
        Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.Positions.size(), 6u);
    EXPECT_EQ(result.InitialIndices,
              (std::vector<std::size_t>{5, 40, 99, 200, 333, 471}));
}

TEST(PointCloudConsolidation, FailsClosedOnInvalidInput)
{
    using Status = Consolidation::ConsolidateStatus;
    const std::vector<glm::vec3> points = MakeNoisyPlane();
    const Consolidation::WlopParams good = MakePlaneParams();

    {
        const auto result =
            Consolidation::Consolidate(std::span<const glm::vec3>{}, good);
        EXPECT_EQ(result.Status, Status::EmptyInput);
    }
    {
        const std::vector<glm::vec3> two{{0, 0, 0}, {1, 0, 0}};
        EXPECT_EQ(Consolidation::Consolidate(two, good).Status,
                  Status::InsufficientPoints);
    }
    {
        std::vector<glm::vec3> bad = points;
        bad[3].y = std::numeric_limits<float>::quiet_NaN();
        EXPECT_EQ(Consolidation::Consolidate(bad, good).Status,
                  Status::NonFinitePositions);
    }
    {
        Consolidation::WlopParams params = good;
        params.SupportRadius = 0.0f;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidSupportRadius);
        params.SupportRadius = -1.0f;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidSupportRadius);
        params.SupportRadius = std::numeric_limits<float>::infinity();
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidSupportRadius);
    }
    {
        Consolidation::WlopParams params = good;
        params.RepulsionWeight = 0.5f;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidRepulsionWeight);
        params.RepulsionWeight = -0.01f;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidRepulsionWeight);
    }
    {
        Consolidation::WlopParams params = good;
        params.Iterations = 0;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidIterationCount);
    }
    {
        Consolidation::WlopParams params = good;
        params.TargetCount = 0;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidTargetCount);
        params.TargetCount = points.size() + 1;
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidTargetCount);
    }
    {
        Consolidation::WlopParams params = good;
        params.InitialIndices = {0, points.size()};
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidInitialIndices);
        params.InitialIndices = {7, 7};
        EXPECT_EQ(Consolidation::Consolidate(points, params).Status,
                  Status::InvalidInitialIndices);
    }
}
