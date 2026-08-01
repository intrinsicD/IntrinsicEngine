#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <limits>
#include <span>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

import Geometry.PointCloud;
import Geometry.PointCloud.Consolidation;
import Geometry.GaussianMixture;

namespace
{
    namespace Consolidation = Geometry::PointCloud::Consolidation;
    namespace GMM = Geometry::GaussianMixture;

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

    [[nodiscard]] Consolidation::Params ClopParams(
        const std::uint32_t componentCount)
    {
        auto params = ReferenceParams();
        params.Method = Consolidation::ClopStrategy{
            .MixtureComponentCount = componentCount,
            .MixtureMaxIterations = 100u,
            .MixtureRelativeTolerance = 1.0e-6,
            .CovarianceFloor = 1.0e-5,
        };
        return params;
    }

    struct AnalyticTerm
    {
        double Weight;
        double Sigma;
    };

    [[nodiscard]] glm::vec3 AnalyticClopAttraction(
        const GMM::Model& model,
        const glm::vec3 query,
        const double supportRadius,
        const std::span<const AnalyticTerm> terms)
    {
        glm::dvec3 numerator{0.0};
        double denominator = 0.0;
        for (std::size_t component = 0u;
             component < model.Components.size(); ++component)
        {
            for (const AnalyticTerm term : terms)
            {
                const double scaledSigma = term.Sigma * supportRadius;
                const double variance = scaledSigma * scaledSigma;
                const glm::dmat3 lambda =
                    model.Components[component].Covariance +
                    glm::dmat3{variance};
                const glm::dmat3 inverse = glm::inverse(lambda);
                const glm::dvec3 delta =
                    model.Components[component].Mean - glm::dvec3(query);
                const double weight = model.Weights[component] *
                    term.Weight * scaledSigma * scaledSigma * scaledSigma /
                    std::sqrt(glm::determinant(lambda)) *
                    std::exp(-0.5 * glm::dot(delta, inverse * delta));
                numerator += weight *
                    (glm::dvec3(query) + variance * inverse * delta);
                denominator += weight;
            }
        }
        return glm::vec3(numerator / denominator);
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

TEST(PointCloudConsolidation, LopOneStepMatchesPaperEquationOracle)
{
    const std::vector<glm::vec3> points{
        {-0.25f, 0.0f, 0.0f},
        {0.45f, 0.0f, 0.0f},
    };
    constexpr double h = 1.0;
    constexpr double distanceFloor = 0.01 * h;
    const auto theta = [](const double distance)
    {
        return std::exp(-16.0 * distance * distance / (h * h));
    };
    const auto expectedOneStep = [&](const std::size_t seedIndex)
    {
        double l2Weight = 0.0;
        double l2Numerator = 0.0;
        for (const glm::vec3 point : points)
        {
            const double distance = std::abs(
                static_cast<double>(points[seedIndex].x) - point.x);
            const double weight = theta(distance);
            l2Weight += weight;
            l2Numerator += static_cast<double>(point.x) * weight;
        }
        // The implementation deliberately stores each finite L2 iterate in
        // its public float coordinate type before evaluating the L1 step.
        const float initialized = static_cast<float>(l2Numerator / l2Weight);

        double l1Weight = 0.0;
        double l1Numerator = 0.0;
        for (const glm::vec3 point : points)
        {
            const double distance = std::abs(
                static_cast<double>(initialized) - point.x);
            const double weight = theta(distance) /
                std::max(distance, distanceFloor);
            l1Weight += weight;
            l1Numerator += static_cast<double>(point.x) * weight;
        }
        return l1Numerator / l1Weight;
    };

    auto params = ReferenceParams();
    params.Method = Consolidation::LopStrategy{};
    params.SupportRadius = h;
    params.RepulsionWeight = 0.0;
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    params.TargetPointCount = points.size();
    const auto result = Consolidation::Consolidate(points, params);

    ASSERT_TRUE(result.Succeeded())
        << Consolidation::DebugName(result.State);
    ASSERT_EQ(result.Positions.size(), points.size());
    EXPECT_NEAR(result.Positions[0].x, expectedOneStep(0u), 1.0e-6);
    EXPECT_NEAR(result.Positions[1].x, expectedOneStep(1u), 1.0e-6);
    EXPECT_FLOAT_EQ(result.Positions[0].y, 0.0f);
    EXPECT_FLOAT_EQ(result.Positions[1].z, 0.0f);
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

TEST(PointCloudConsolidation, InSupportOutliersHaveBoundedInfluenceOnPlanePatch)
{
    const std::vector<glm::vec3> plane = NoisyPlane(4);
    std::vector<glm::vec3> withOutliers = plane;
    withOutliers.emplace_back(0.0f, 0.0f, 0.45f);
    withOutliers.emplace_back(0.1f, 0.0f, 0.50f);

    auto params = ReferenceParams();
    params.MaxIterations = 12u;
    params.ConvergenceTolerance = 0.0;
    const auto baseline = Consolidation::Consolidate(plane, params);
    const auto outlierRun = Consolidation::Consolidate(withOutliers, params);
    ASSERT_FALSE(baseline.Positions.empty());
    ASSERT_GE(outlierRun.Positions.size(), plane.size());
    double maxDisplacement = 0.0;
    for (std::size_t i = 0u; i < plane.size(); ++i)
    {
        maxDisplacement = std::max(
            maxDisplacement,
            static_cast<double>(glm::distance(
                outlierRun.Positions[i], baseline.Positions[i])));
    }
    // Both outliers are inside h of the patch center. Density weighting need
    // not erase their influence, but must keep the original patch stable to
    // less than one quarter of the 0.65 world-unit support radius.
    EXPECT_LT(maxDisplacement, params.SupportRadius * 0.25);
}

TEST(PointCloudConsolidation, ClopOneStepMatchesGaussianProductEquation)
{
    const auto points = NoisyPlane(3);
    auto params = ClopParams(3u);
    params.SupportRadius = 0.75;
    params.RepulsionWeight = 0.0;
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    params.TargetPointCount = points.size();

    const auto* strategy =
        std::get_if<Consolidation::ClopStrategy>(&params.Method);
    ASSERT_NE(strategy, nullptr);
    GMM::FitParams fitParams{};
    fitParams.MaxIterations = strategy->MixtureMaxIterations;
    fitParams.RelativeTolerance = strategy->MixtureRelativeTolerance;
    fitParams.CovarianceFloor = strategy->CovarianceFloor;
    fitParams.Seed = params.Seed;
    const auto fit = GMM::FitEM(
        points, strategy->MixtureComponentCount, fitParams);
    ASSERT_TRUE(fit.Succeeded());
    ASSERT_TRUE(fit.Diagnostics.Converged);

    constexpr std::array<AnalyticTerm, 1u> l2Terms{{
        {1.0, 0.1767766952966369},
    }};
    constexpr std::array<AnalyticTerm, 3u> l1Terms{{
        {11.453, 0.11772},
        {29.886, 0.03287},
        {97.761, 0.01010},
    }};
    std::vector<glm::vec3> expected{};
    expected.reserve(points.size());
    for (const glm::vec3 point : points)
    {
        const glm::vec3 initialized = AnalyticClopAttraction(
            fit.Mixture, point, params.SupportRadius, l2Terms);
        expected.push_back(AnalyticClopAttraction(
            fit.Mixture, initialized, params.SupportRadius, l1Terms));
    }

    const auto result = Consolidation::Consolidate(points, params);
    ASSERT_TRUE(result.Succeeded())
        << Consolidation::DebugName(result.State);
    ASSERT_EQ(result.Positions.size(), expected.size());
    for (std::size_t i = 0u; i < expected.size(); ++i)
    {
        EXPECT_NEAR(result.Positions[i].x, expected[i].x, 2.0e-6);
        EXPECT_NEAR(result.Positions[i].y, expected[i].y, 2.0e-6);
        EXPECT_NEAR(result.Positions[i].z, expected[i].z, 2.0e-6);
    }
    EXPECT_EQ(result.Diagnostics.Strategy,
              Consolidation::StrategyKind::Clop);
    EXPECT_TRUE(result.Diagnostics.UsedContinuousAttraction);
    EXPECT_EQ(result.Diagnostics.MixtureComponentCount, 3u);
    EXPECT_TRUE(result.Diagnostics.MixtureConverged);
}

TEST(PointCloudConsolidation, ClopDenoisesPlaneAndSphere)
{
    const auto plane = NoisyPlane();
    auto planeParams = ClopParams(10u);
    planeParams.RepulsionWeight = 0.0;
    planeParams.MaxIterations = 1u;
    planeParams.ConvergenceTolerance = 1.0;
    planeParams.TargetPointCount = 25u;
    const auto projectedPlane =
        Consolidation::Consolidate(plane, planeParams);
    ASSERT_TRUE(projectedPlane.Succeeded())
        << Consolidation::DebugName(projectedPlane.State);
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions),
              MeanPlaneError(plane));
    EXPECT_LT(MeanPlaneError(projectedPlane.Positions), 0.04);

    const auto sphere = NoisySphere();
    auto sphereParams = ClopParams(40u);
    std::get<Consolidation::ClopStrategy>(sphereParams.Method)
        .CovarianceFloor = 1.0e-6;
    sphereParams.SupportRadius = 0.6;
    sphereParams.RepulsionWeight = 0.0;
    sphereParams.MaxIterations = 1u;
    sphereParams.ConvergenceTolerance = 1.0;
    sphereParams.TargetPointCount = 36u;
    const auto projectedSphere =
        Consolidation::Consolidate(sphere, sphereParams);
    ASSERT_TRUE(projectedSphere.Succeeded())
        << Consolidation::DebugName(projectedSphere.State);
    EXPECT_LT(MeanSphereError(projectedSphere.Positions),
              MeanSphereError(sphere));
    // Curved fixtures require a richer mixture than the planar case to avoid
    // covariance-induced shrinkage. This 40-component reference remains
    // below the raw fixture's 0.02081 mean radial error after one step.
    EXPECT_LT(MeanSphereError(projectedSphere.Positions), 0.020);
}

TEST(PointCloudConsolidation, ClopTracksMixtureWorkAndAgreesWithWlop)
{
    const auto points = NoisyPlane(6);
    auto wlopParams = ReferenceParams();
    wlopParams.RepulsionWeight = 0.0;
    wlopParams.MaxIterations = 1u;
    wlopParams.ConvergenceTolerance = 1.0;
    wlopParams.TargetPointCount = 20u;
    const auto wlop = Consolidation::Consolidate(points, wlopParams);
    ASSERT_TRUE(wlop.Succeeded());

    auto richParams = ClopParams(18u);
    richParams.RepulsionWeight = 0.0;
    richParams.MaxIterations = 1u;
    richParams.ConvergenceTolerance = 1.0;
    richParams.TargetPointCount = 20u;
    const auto rich = Consolidation::Consolidate(points, richParams);
    ASSERT_TRUE(rich.Succeeded())
        << Consolidation::DebugName(rich.State);
    ASSERT_EQ(rich.Positions.size(), wlop.Positions.size());
    double meanParityDistance = 0.0;
    for (std::size_t i = 0u; i < rich.Positions.size(); ++i)
    {
        meanParityDistance +=
            static_cast<double>(glm::distance(
                rich.Positions[i], wlop.Positions[i]));
    }
    meanParityDistance /= static_cast<double>(rich.Positions.size());
    EXPECT_LT(meanParityDistance, 0.18);

    auto compactParams = richParams;
    compactParams.Method = Consolidation::ClopStrategy{
        .MixtureComponentCount = 6u,
        .MixtureMaxIterations = 100u,
        .MixtureRelativeTolerance = 1.0e-6,
        .CovarianceFloor = 1.0e-5,
    };
    const auto compact = Consolidation::Consolidate(points, compactParams);
    ASSERT_TRUE(compact.Succeeded())
        << Consolidation::DebugName(compact.State);
    EXPECT_LT(compact.Diagnostics.AttractionContributionCount,
              rich.Diagnostics.AttractionContributionCount);
    EXPECT_EQ(compact.Diagnostics.MixtureComponentCount, 6u);
    EXPECT_EQ(rich.Diagnostics.MixtureComponentCount, 18u);
    EXPECT_LT(MeanPlaneError(compact.Positions), 0.05);
}

TEST(PointCloudConsolidation, ClopKeepsSparseOutlierInfluenceBounded)
{
    const std::vector<glm::vec3> plane = NoisyPlane(4);
    std::vector<glm::vec3> withOutliers = plane;
    withOutliers.emplace_back(0.0f, 0.0f, 0.45f);
    withOutliers.emplace_back(0.1f, 0.0f, 0.50f);

    auto params = ClopParams(8u);
    params.MaxIterations = 1u;
    params.ConvergenceTolerance = 1.0;
    params.RepulsionWeight = 0.0;
    const auto baseline = Consolidation::Consolidate(plane, params);
    const auto outlierRun = Consolidation::Consolidate(withOutliers, params);
    ASSERT_FALSE(baseline.Positions.empty())
        << Consolidation::DebugName(baseline.State);
    ASSERT_GE(outlierRun.Positions.size(), plane.size())
        << Consolidation::DebugName(outlierRun.State);
    double maxDisplacement = 0.0;
    for (std::size_t i = 0u; i < plane.size(); ++i)
    {
        maxDisplacement = std::max(
            maxDisplacement,
            static_cast<double>(glm::distance(
                outlierRun.Positions[i], baseline.Positions[i])));
    }
    EXPECT_LT(maxDisplacement, params.SupportRadius * 0.25);
}

TEST(PointCloudConsolidation, ClopIsDeterministicAndFailsClosed)
{
    const auto points = NoisyPlane(5);
    auto params = ClopParams(8u);
    params.TargetPointCount = 16u;
    params.MaxIterations = 3u;
    params.ConvergenceTolerance = 0.0;
    const auto first = Consolidation::Consolidate(points, params);
    const auto second = Consolidation::Consolidate(points, params);
    ASSERT_EQ(first.State, second.State);
    EXPECT_EQ(first.Positions, second.Positions);
    EXPECT_EQ(first.Diagnostics.MixtureIterations,
              second.Diagnostics.MixtureIterations);

    const auto expectFailure = [&](
        const Consolidation::ClopStrategy strategy,
        const Consolidation::Status status)
    {
        auto invalid = params;
        invalid.Method = strategy;
        const auto result = Consolidation::Consolidate(points, invalid);
        EXPECT_EQ(result.State, status);
        EXPECT_TRUE(result.Positions.empty());
    };
    expectFailure(
        Consolidation::ClopStrategy{.MixtureComponentCount = 0u},
        Consolidation::Status::InvalidMixtureComponentCount);
    expectFailure(
        Consolidation::ClopStrategy{
            .MixtureComponentCount =
                static_cast<std::uint32_t>(points.size() + 1u)},
        Consolidation::Status::InvalidMixtureComponentCount);
    expectFailure(
        Consolidation::ClopStrategy{
            .MixtureComponentCount = 4u,
            .MixtureMaxIterations = 0u},
        Consolidation::Status::InvalidMixtureParameters);
    expectFailure(
        Consolidation::ClopStrategy{
            .MixtureComponentCount = 4u,
            .CovarianceFloor = 0.0},
        Consolidation::Status::InvalidMixtureParameters);
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

TEST(PointCloudConsolidation, ConcurrentCallersAreBitwiseDeterministic)
{
    const auto points = NoisyPlane(6);
    auto params = ReferenceParams();
    params.TargetPointCount = 20u;
    params.MaxIterations = 16u;
    params.ConvergenceTolerance = 0.0;

    auto firstFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    auto secondFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    const auto serial = Consolidation::Consolidate(points, params);
    const auto first = firstFuture.get();
    const auto second = secondFuture.get();

    ASSERT_EQ(first.State, serial.State);
    ASSERT_EQ(second.State, serial.State);
    EXPECT_EQ(first.Positions, serial.Positions);
    EXPECT_EQ(second.Positions, serial.Positions);
    EXPECT_EQ(first.Diagnostics.Iterations, serial.Diagnostics.Iterations);
    EXPECT_EQ(second.Diagnostics.Iterations, serial.Diagnostics.Iterations);
}

TEST(PointCloudConsolidation, ClopConcurrentCallersAreBitwiseDeterministic)
{
    const auto points = NoisyPlane(6);
    auto params = ClopParams(10u);
    params.TargetPointCount = 20u;
    params.MaxIterations = 4u;
    params.ConvergenceTolerance = 0.0;

    auto firstFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    auto secondFuture = std::async(std::launch::async, [&]
    {
        return Consolidation::Consolidate(points, params);
    });
    const auto serial = Consolidation::Consolidate(points, params);
    const auto first = firstFuture.get();
    const auto second = secondFuture.get();

    ASSERT_EQ(first.State, serial.State);
    ASSERT_EQ(second.State, serial.State);
    EXPECT_EQ(first.Positions, serial.Positions);
    EXPECT_EQ(second.Positions, serial.Positions);
    EXPECT_EQ(first.Diagnostics.MixtureIterations,
              serial.Diagnostics.MixtureIterations);
    EXPECT_EQ(second.Diagnostics.MixtureIterations,
              serial.Diagnostics.MixtureIterations);
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
    params = ReferenceParams();
    params.MaxInputPointCount = 1u;
    expectFailure(Consolidation::Consolidate(points, params),
                  Consolidation::Status::ResourceLimit);

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
