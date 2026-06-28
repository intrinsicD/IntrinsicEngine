#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

#include <glm/glm.hpp>

import Geometry.PointCloud;
import Geometry.PointCloud.QualityMetrics;

namespace
{
    namespace QM = Geometry::PointCloud::QualityMetrics;

    [[nodiscard]] std::vector<glm::vec3> MakeGrid(std::size_t side)
    {
        std::vector<glm::vec3> points;
        points.reserve(side * side);
        const float step = 1.0f / static_cast<float>(side - 1u);
        for (std::size_t y = 0; y < side; ++y)
        {
            for (std::size_t x = 0; x < side; ++x)
            {
                points.push_back({static_cast<float>(x) * step, static_cast<float>(y) * step, 0.0f});
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> MakeJitteredGrid(std::size_t side)
    {
        std::vector<glm::vec3> points;
        points.reserve(side * side);
        const float cell = 1.0f / static_cast<float>(side);
        for (std::size_t y = 0; y < side; ++y)
        {
            for (std::size_t x = 0; x < side; ++x)
            {
                const float jitterX = ((x * 17u + y * 11u) % 7u) * 0.01f * cell;
                const float jitterY = ((x * 5u + y * 13u) % 7u) * 0.01f * cell;
                points.push_back({(static_cast<float>(x) + 0.5f) * cell + jitterX,
                                  (static_cast<float>(y) + 0.5f) * cell + jitterY,
                                  0.0f});
            }
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> MakeWhiteNoise(std::size_t count)
    {
        std::mt19937 rng(36036u);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        std::vector<glm::vec3> points;
        points.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
        {
            points.push_back({dist(rng), dist(rng), 0.0f});
        }
        return points;
    }

    [[nodiscard]] std::vector<glm::vec3> MakeLatticeCellCenters(std::size_t side)
    {
        std::vector<glm::vec3> points;
        points.reserve(side * side);
        const float cell = 1.0f / static_cast<float>(side);
        for (std::size_t y = 0; y < side; ++y)
        {
            for (std::size_t x = 0; x < side; ++x)
            {
                points.push_back({(static_cast<float>(x) + 0.5f) * cell,
                                  (static_cast<float>(y) + 0.5f) * cell,
                                  0.0f});
            }
        }
        return points;
    }

    [[nodiscard]] QM::AxisAlignedDomain UnitSquare()
    {
        return {.Min = {0.0, 0.0, 0.0}, .Max = {1.0, 1.0, 0.0}, .Dimension = QM::MetricDimension::D2};
    }

    [[nodiscard]] double Sum(const std::vector<double>& values)
    {
        return std::accumulate(values.begin(), values.end(), 0.0);
    }

    [[nodiscard]] double PowerAt(const QM::PeriodogramResult& result, int kx, int ky)
    {
        const auto xIt = std::find(result.Grid.FrequenciesX.begin(), result.Grid.FrequenciesX.end(), kx);
        const auto yIt = std::find(result.Grid.FrequenciesY.begin(), result.Grid.FrequenciesY.end(), ky);
        if (xIt == result.Grid.FrequenciesX.end() || yIt == result.Grid.FrequenciesY.end())
        {
            return 0.0;
        }
        const auto x = static_cast<std::size_t>(std::distance(result.Grid.FrequenciesX.begin(), xIt));
        const auto y = static_cast<std::size_t>(std::distance(result.Grid.FrequenciesY.begin(), yIt));
        return result.Grid.Power[y * result.Grid.Resolution + x];
    }
}

TEST(QualityMetrics, NearestNeighborCvAndPoissonRatioForGrid)
{
    const std::vector<glm::vec3> points = MakeGrid(4);

    const auto nn = QM::ComputeNearestNeighborDistances(points);
    ASSERT_EQ(nn.Status, QM::MetricStatus::Success);
    EXPECT_NEAR(nn.MinDistance, 1.0 / 3.0, 1.0e-6);
    EXPECT_NEAR(nn.MeanDistance, 1.0 / 3.0, 1.0e-6);
    EXPECT_NEAR(nn.CoefficientOfVariation, 0.0, 1.0e-6);

    const auto poisson = QM::ComputePoissonDiskRatio(points, 0.32);
    ASSERT_EQ(poisson.Status, QM::MetricStatus::Success);
    EXPECT_GE(poisson.Ratio, 1.0);

    QM::HistogramParams params;
    params.MaxValue = 0.5;
    params.BinWidth = 0.1;
    const auto histogram = QM::ComputeNearestNeighborHistogram(points, params);
    ASSERT_EQ(histogram.Status, QM::MetricStatus::Success);
    EXPECT_NEAR(Sum(histogram.Distribution.Values), 1.0, 1.0e-9);
}

TEST(QualityMetrics, JitteredGridHasLowNearestNeighborVariation)
{
    const std::vector<glm::vec3> points = MakeJitteredGrid(8);

    const auto nn = QM::ComputeNearestNeighborDistances(points);
    ASSERT_EQ(nn.Status, QM::MetricStatus::Success);
    EXPECT_LT(nn.CoefficientOfVariation, 0.08);
    EXPECT_GT(nn.MinDistance, 0.11);
}

TEST(QualityMetrics, RadialDistributionForWhiteNoiseApproachesOneAwayFromZero)
{
    const std::vector<glm::vec3> points = MakeWhiteNoise(400);

    QM::RadialDistributionParams params;
    params.Bins.MinValue = 0.0;
    params.Bins.MaxValue = 0.35;
    params.Bins.BinWidth = 0.05;
    params.Bins.Normalize = false;
    params.Domain = UnitSquare();
    params.EdgeCorrectionDirections = 96;

    const auto rdf = QM::ComputeRadialDistributionFunction(points, params);
    ASSERT_EQ(rdf.Status, QM::MetricStatus::Success);
    EXPECT_GT(rdf.MeanAwayFromZero, 0.70);
    EXPECT_LT(rdf.MeanAwayFromZero, 1.30);
    ASSERT_EQ(rdf.G.BinCenters.size(), rdf.G.Values.size());
}

TEST(QualityMetrics, PeriodogramFindsRegularLatticeFrequencyPeaks)
{
    const std::vector<glm::vec3> points = MakeLatticeCellCenters(4);

    QM::PeriodogramParams params;
    params.Resolution = 9;
    params.Domain = UnitSquare();

    const auto periodogram = QM::ComputePeriodogram2D(points, params);
    ASSERT_EQ(periodogram.Status, QM::MetricStatus::Success);
    EXPECT_GT(PowerAt(periodogram, 4, 0), 0.99);
    EXPECT_GT(PowerAt(periodogram, 0, 4), 0.99);
    EXPECT_LT(PowerAt(periodogram, 1, 0), 1.0e-6);
    EXPECT_LT(PowerAt(periodogram, 0, 0), 1.0e-12);
}

TEST(QualityMetrics, RadiallyAveragedPowerSpectrumForWhiteNoiseIsBroadlyFlat)
{
    const std::vector<glm::vec3> points = MakeWhiteNoise(512);

    QM::PeriodogramParams periodogramParams;
    periodogramParams.Resolution = 16;
    periodogramParams.Domain = UnitSquare();

    QM::RadialPowerSpectrumParams spectrumParams;
    spectrumParams.FrequencyBinWidth = 2.0;

    const auto raps = QM::ComputeRadiallyAveragedPowerSpectrum2D(points, periodogramParams, spectrumParams);
    ASSERT_EQ(raps.Status, QM::MetricStatus::Success);

    std::vector<double> occupied;
    for (std::size_t i = 1; i + 1u < raps.Spectrum.Values.size(); ++i)
    {
        if (raps.SampleCounts[i] > 0u)
        {
            occupied.push_back(raps.Spectrum.Values[i]);
        }
    }
    ASSERT_GE(occupied.size(), 3u);

    const double mean = Sum(occupied) / static_cast<double>(occupied.size());
    double variance = 0.0;
    for (const double value : occupied)
    {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(occupied.size());
    const double cv = mean > 0.0 ? std::sqrt(variance) / mean : 0.0;
    EXPECT_LT(cv, 1.2);
}

TEST(QualityMetrics, CoverageMeasuresReferenceFractionWithinRadius)
{
    const std::vector<glm::vec3> samples = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f},
    };
    const std::vector<glm::vec3> reference = {
        {0.02f, 0.0f, 0.0f},
        {0.98f, 0.0f, 0.0f},
        {0.50f, 0.0f, 0.0f},
        {0.50f, 0.5f, 0.0f},
    };

    const auto coverage = QM::ComputeCoverage(samples, reference, 0.05);
    ASSERT_EQ(coverage.Status, QM::MetricStatus::Success);
    EXPECT_EQ(coverage.CoveredReferenceCount, 2u);
    EXPECT_NEAR(coverage.CoverageFraction, 0.5, 1.0e-9);
    EXPECT_GT(coverage.MaxNearestDistance, 0.49);
}

TEST(QualityMetrics, CloudAdaptersUseLivePoints)
{
    Geometry::PointCloud::Cloud cloud;
    const auto a = cloud.AddPoint({0.0f, 0.0f, 0.0f});
    const auto b = cloud.AddPoint({1.0f, 0.0f, 0.0f});
    const auto c = cloud.AddPoint({2.0f, 0.0f, 0.0f});
    (void)a;
    (void)c;
    cloud.DeletePoint(b);

    const auto nn = QM::ComputeNearestNeighborDistances(cloud);
    ASSERT_EQ(nn.Status, QM::MetricStatus::Success);
    EXPECT_EQ(nn.Info.InputPointCount, 2u);
    EXPECT_NEAR(nn.MinDistance, 2.0, 1.0e-9);
}

TEST(QualityMetrics, InvalidInputsFailClosed)
{
    const std::vector<glm::vec3> empty;
    EXPECT_EQ(QM::ComputeNearestNeighborDistances(empty).Status, QM::MetricStatus::EmptyInput);

    const std::vector<glm::vec3> one = {{0.0f, 0.0f, 0.0f}};
    EXPECT_EQ(QM::ComputeNearestNeighborDistances(one).Status, QM::MetricStatus::InsufficientPoints);
    EXPECT_EQ(QM::ComputePoissonDiskRatio(one, -1.0).Status, QM::MetricStatus::InvalidRadius);

    const std::vector<glm::vec3> nonFinite = {{0.0f, 0.0f, 0.0f}, {NAN, 0.0f, 0.0f}};
    EXPECT_EQ(QM::ComputeNearestNeighborDistances(nonFinite).Status, QM::MetricStatus::NonFiniteInput);

    QM::HistogramParams badBins;
    badBins.MaxValue = 1.0;
    badBins.BinWidth = 0.0;
    EXPECT_EQ(QM::ComputeNearestNeighborHistogram(MakeGrid(2), badBins).Status, QM::MetricStatus::InvalidBins);

    EXPECT_EQ(QM::ComputeCoverage(MakeGrid(2), empty, 0.1).Status, QM::MetricStatus::EmptyInput);
}

TEST(QualityMetrics, SpectralMetricsReject3DInput)
{
    const std::vector<glm::vec3> points = {
        {0.0f, 0.0f, 0.0f},
        {0.5f, 0.0f, 0.1f},
        {0.0f, 0.5f, 0.2f},
    };

    EXPECT_EQ(QM::ComputePeriodogram2D(points).Status, QM::MetricStatus::Requires2DInput);
    EXPECT_EQ(QM::ComputeRadiallyAveragedPowerSpectrum2D(points).Status, QM::MetricStatus::Requires2DInput);
}
