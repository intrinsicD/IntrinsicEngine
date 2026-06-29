// GEOM-036 — deterministic quality-metrics smoke benchmark.

#include "Bench.QualityMetricsSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <random>
#include <vector>

#include <glm/glm.hpp>

import Geometry.PointCloud.QualityMetrics;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace QM = ::Geometry::PointCloud::QualityMetrics;

        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr std::size_t kGridSide = 16;
        constexpr std::size_t kWhiteNoiseCount = 384;

        [[nodiscard]] QM::AxisAlignedDomain UnitSquare()
        {
            return {.Min = {0.0, 0.0, 0.0}, .Max = {1.0, 1.0, 0.0}, .Dimension = QM::MetricDimension::D2};
        }

        [[nodiscard]] std::vector<glm::vec3> MakeJitteredGrid()
        {
            std::vector<glm::vec3> points;
            points.reserve(kGridSide * kGridSide);
            const float cell = 1.0f / static_cast<float>(kGridSide);
            for (std::size_t y = 0; y < kGridSide; ++y)
            {
                for (std::size_t x = 0; x < kGridSide; ++x)
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

        [[nodiscard]] std::vector<glm::vec3> MakeWhiteNoise()
        {
            std::mt19937 rng(36036u);
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            std::vector<glm::vec3> points;
            points.reserve(kWhiteNoiseCount);
            for (std::size_t i = 0; i < kWhiteNoiseCount; ++i)
            {
                points.push_back({dist(rng), dist(rng), 0.0f});
            }
            return points;
        }

        [[nodiscard]] double RapsCv(const QM::RadialPowerSpectrumResult& raps)
        {
            std::vector<double> occupied;
            for (std::size_t i = 1; i + 1u < raps.Spectrum.Values.size(); ++i)
            {
                if (raps.SampleCounts[i] > 0u)
                {
                    occupied.push_back(raps.Spectrum.Values[i]);
                }
            }
            if (occupied.empty())
            {
                return 0.0;
            }

            const double mean = std::accumulate(occupied.begin(), occupied.end(), 0.0)
                / static_cast<double>(occupied.size());
            double variance = 0.0;
            for (const double value : occupied)
            {
                const double delta = value - mean;
                variance += delta * delta;
            }
            variance /= static_cast<double>(occupied.size());
            return mean > 0.0 ? std::sqrt(variance) / mean : 0.0;
        }

        struct TickResult
        {
            QualityMetricsSmokeMetrics Metrics{};
        };

        [[nodiscard]] TickResult Tick()
        {
            const std::vector<glm::vec3> jittered = MakeJitteredGrid();
            const std::vector<glm::vec3> white = MakeWhiteNoise();

            const auto nn = QM::ComputeNearestNeighborDistances(jittered);
            const auto poisson = QM::ComputePoissonDiskRatio(jittered, 0.055);
            const auto coverage = QM::ComputeCoverage(jittered, jittered, 0.001);

            QM::RadialDistributionParams rdfParams;
            rdfParams.Bins.MinValue = 0.0;
            rdfParams.Bins.MaxValue = 0.35;
            rdfParams.Bins.BinWidth = 0.05;
            rdfParams.Bins.Normalize = false;
            rdfParams.Domain = UnitSquare();
            rdfParams.EdgeCorrectionDirections = 96;
            const auto rdf = QM::ComputeRadialDistributionFunction(white, rdfParams);

            QM::PeriodogramParams periodogramParams;
            periodogramParams.Resolution = 16;
            periodogramParams.Domain = UnitSquare();
            QM::RadialPowerSpectrumParams spectrumParams;
            spectrumParams.FrequencyBinWidth = 2.0;
            const auto raps = QM::ComputeRadiallyAveragedPowerSpectrum2D(white, periodogramParams, spectrumParams);

            TickResult tick;
            tick.Metrics.PointCount = jittered.size();
            tick.Metrics.NearestNeighborCv = nn.CoefficientOfVariation;
            tick.Metrics.PoissonRatio = poisson.Ratio;
            tick.Metrics.CoverageFraction = coverage.CoverageFraction;
            tick.Metrics.RdfMeanAwayFromZero = rdf.MeanAwayFromZero;
            tick.Metrics.RapsCv = RapsCv(raps);

            const bool statusesPass = nn.Succeeded()
                && poisson.Succeeded()
                && coverage.Succeeded()
                && rdf.Succeeded()
                && raps.Succeeded();
            const double cvViolation = std::max(0.0, tick.Metrics.NearestNeighborCv - 0.10);
            const double poissonViolation = std::max(0.0, 1.0 - tick.Metrics.PoissonRatio);
            const double coverageViolation = std::max(0.0, 1.0 - tick.Metrics.CoverageFraction);
            const double rdfViolation = std::max(0.0, std::abs(tick.Metrics.RdfMeanAwayFromZero - 1.0) - 0.35);
            const double rapsViolation = std::max(0.0, tick.Metrics.RapsCv - 1.20);
            tick.Metrics.QualityErrorL2 = std::sqrt(cvViolation * cvViolation
                + poissonViolation * poissonViolation
                + coverageViolation * coverageViolation
                + rdfViolation * rdfViolation
                + rapsViolation * rapsViolation);
            tick.Metrics.Succeeded = statusesPass && tick.Metrics.QualityErrorL2 <= 1.0e-6;
            return tick;
        }
    }

    QualityMetricsSmokeMetrics RunQualityMetricsSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)Tick();
        }

        TickResult last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = Tick();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.Metrics.RuntimeMilliseconds =
            (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last.Metrics;
    }
}
