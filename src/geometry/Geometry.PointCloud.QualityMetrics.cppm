module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.PointCloud.QualityMetrics;

export import Geometry.PointCloud;

export namespace Geometry::PointCloud::QualityMetrics
{
    enum class MetricStatus : std::uint8_t
    {
        Success,
        EmptyInput,
        InsufficientPoints,
        NonFiniteInput,
        InvalidRadius,
        InvalidBins,
        InvalidDomain,
        PointOutsideDomain,
        Requires2DInput,
    };

    enum class MetricDimension : std::uint8_t
    {
        Auto,
        D2,
        D3,
    };

    struct AxisAlignedDomain
    {
        glm::dvec3 Min{0.0};
        glm::dvec3 Max{1.0};
        MetricDimension Dimension{MetricDimension::Auto};
    };

    struct HistogramParams
    {
        double MinValue{0.0};
        double MaxValue{1.0};
        double BinWidth{0.05};
        bool Normalize{true};
    };

    struct Histogram
    {
        std::vector<double> BinCenters{};
        std::vector<double> Values{};
        double MinValue{0.0};
        double MaxValue{0.0};
        double BinWidth{0.0};
        bool Normalized{false};
    };

    struct MetricDiagnostics
    {
        std::size_t InputPointCount{0};
        std::size_t ReferencePointCount{0};
        std::size_t NonFinitePointCount{0};
        std::size_t PointsOutsideDomain{0};
        std::size_t PairCount{0};
        MetricDimension Dimension{MetricDimension::Auto};
        double DomainMeasure{0.0};
    };

    struct NearestNeighborResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        std::vector<double> Distances{};
        double MeanDistance{0.0};
        double StdDevDistance{0.0};
        double CoefficientOfVariation{0.0};
        double MinDistance{0.0};
        double MaxDistance{0.0};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    struct HistogramResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        Histogram Distribution{};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    struct PoissonDiskResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        double TargetRadius{0.0};
        double MinDistance{0.0};
        double Ratio{0.0};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    struct CoverageResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        double Radius{0.0};
        double CoverageFraction{0.0};
        double MeanNearestDistance{0.0};
        double MaxNearestDistance{0.0};
        std::size_t CoveredReferenceCount{0};
        std::vector<double> ReferenceNearestDistances{};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    struct RadialDistributionParams
    {
        HistogramParams Bins{};
        std::optional<AxisAlignedDomain> Domain{};
        MetricDimension Dimension{MetricDimension::Auto};
        bool UseEdgeCorrection{true};
        std::uint32_t EdgeCorrectionDirections{64};
        double PlanarTolerance{1.0e-6};
    };

    struct RadialDistributionResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        Histogram G{};
        double MeanAwayFromZero{0.0};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    struct PeriodogramParams
    {
        std::uint32_t Resolution{16};
        std::optional<AxisAlignedDomain> Domain{};
        double PlanarTolerance{1.0e-6};
        bool RemoveDC{true};
    };

    struct PeriodogramGrid
    {
        std::uint32_t Resolution{0};
        std::vector<int> FrequenciesX{};
        std::vector<int> FrequenciesY{};
        std::vector<double> Power{};
    };

    struct PeriodogramResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        PeriodogramGrid Grid{};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    struct RadialPowerSpectrumParams
    {
        double FrequencyBinWidth{1.0};
        double MaxFrequency{0.0};
    };

    struct RadialPowerSpectrumResult
    {
        MetricStatus Status{MetricStatus::EmptyInput};
        Histogram Spectrum{};
        std::vector<std::size_t> SampleCounts{};
        MetricDiagnostics Info{};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == MetricStatus::Success; }
    };

    [[nodiscard]] std::string_view ToString(MetricStatus status) noexcept;

    [[nodiscard]] NearestNeighborResult ComputeNearestNeighborDistances(std::span<const glm::vec3> points);
    [[nodiscard]] NearestNeighborResult ComputeNearestNeighborDistances(const Cloud& cloud);

    [[nodiscard]] HistogramResult ComputeNearestNeighborHistogram(std::span<const glm::vec3> points,
                                                                  const HistogramParams& params = {});
    [[nodiscard]] HistogramResult ComputeNearestNeighborHistogram(const Cloud& cloud,
                                                                  const HistogramParams& params = {});

    [[nodiscard]] PoissonDiskResult ComputePoissonDiskRatio(std::span<const glm::vec3> points,
                                                            double targetRadius);
    [[nodiscard]] PoissonDiskResult ComputePoissonDiskRatio(const Cloud& cloud,
                                                            double targetRadius);

    [[nodiscard]] CoverageResult ComputeCoverage(std::span<const glm::vec3> samples,
                                                 std::span<const glm::vec3> reference,
                                                 double radius);
    [[nodiscard]] CoverageResult ComputeCoverage(const Cloud& samples,
                                                 const Cloud& reference,
                                                 double radius);

    [[nodiscard]] RadialDistributionResult ComputeRadialDistributionFunction(
        std::span<const glm::vec3> points,
        const RadialDistributionParams& params = {});
    [[nodiscard]] RadialDistributionResult ComputeRadialDistributionFunction(
        const Cloud& cloud,
        const RadialDistributionParams& params = {});

    [[nodiscard]] PeriodogramResult ComputePeriodogram2D(std::span<const glm::vec3> points,
                                                         const PeriodogramParams& params = {});
    [[nodiscard]] PeriodogramResult ComputePeriodogram2D(const Cloud& cloud,
                                                         const PeriodogramParams& params = {});

    [[nodiscard]] RadialPowerSpectrumResult ComputeRadiallyAveragedPowerSpectrum2D(
        std::span<const glm::vec3> points,
        const PeriodogramParams& periodogramParams = {},
        const RadialPowerSpectrumParams& spectrumParams = {});
    [[nodiscard]] RadialPowerSpectrumResult ComputeRadiallyAveragedPowerSpectrum2D(
        const Cloud& cloud,
        const PeriodogramParams& periodogramParams = {},
        const RadialPowerSpectrumParams& spectrumParams = {});
}
