module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

module Geometry.PointCloud.QualityMetrics;

import Geometry.PointCloud;

namespace Geometry::PointCloud::QualityMetrics
{
    namespace
    {
        constexpr double kEpsilon = 1.0e-12;

        [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
        {
            return std::isfinite(static_cast<double>(p.x))
                && std::isfinite(static_cast<double>(p.y))
                && std::isfinite(static_cast<double>(p.z));
        }

        [[nodiscard]] double Distance(const glm::vec3& a, const glm::vec3& b) noexcept
        {
            const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
            const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
            const double dz = static_cast<double>(a.z) - static_cast<double>(b.z);
            return std::sqrt(dx * dx + dy * dy + dz * dz);
        }

        [[nodiscard]] std::vector<glm::vec3> CollectLivePositions(const Cloud& cloud)
        {
            std::vector<glm::vec3> points;
            points.reserve(cloud.VertexCount());
            for (const auto point : cloud.LivePoints())
            {
                points.push_back(cloud.Position(point));
            }
            return points;
        }

        [[nodiscard]] MetricDiagnostics BaseDiagnostics(std::span<const glm::vec3> points)
        {
            MetricDiagnostics info;
            info.InputPointCount = points.size();
            for (const glm::vec3& point : points)
            {
                if (!IsFinite(point))
                {
                    ++info.NonFinitePointCount;
                }
            }
            return info;
        }

        [[nodiscard]] bool HasNonFinite(std::span<const glm::vec3> points, MetricDiagnostics& info)
        {
            info = BaseDiagnostics(points);
            return info.NonFinitePointCount > 0u;
        }

        [[nodiscard]] Histogram MakeHistogram(const HistogramParams& params)
        {
            Histogram histogram;
            histogram.MinValue = params.MinValue;
            histogram.MaxValue = params.MaxValue;
            histogram.BinWidth = params.BinWidth;
            histogram.Normalized = params.Normalize;

            const double span = params.MaxValue - params.MinValue;
            const auto binCount = static_cast<std::size_t>(std::ceil(span / params.BinWidth));
            histogram.BinCenters.reserve(binCount);
            histogram.Values.assign(binCount, 0.0);
            for (std::size_t bin = 0; bin < binCount; ++bin)
            {
                histogram.BinCenters.push_back(params.MinValue + (static_cast<double>(bin) + 0.5) * params.BinWidth);
            }
            return histogram;
        }

        [[nodiscard]] bool ValidateHistogramParams(const HistogramParams& params) noexcept
        {
            return std::isfinite(params.MinValue)
                && std::isfinite(params.MaxValue)
                && std::isfinite(params.BinWidth)
                && params.BinWidth > 0.0
                && params.MaxValue > params.MinValue;
        }

        void AccumulateHistogram(Histogram& histogram, double value)
        {
            if (value < histogram.MinValue || value >= histogram.MaxValue)
            {
                return;
            }
            const auto index = static_cast<std::size_t>((value - histogram.MinValue) / histogram.BinWidth);
            if (index < histogram.Values.size())
            {
                histogram.Values[index] += 1.0;
            }
        }

        void NormalizeHistogram(Histogram& histogram, double denominator)
        {
            if (!histogram.Normalized || denominator <= 0.0)
            {
                return;
            }
            for (double& value : histogram.Values)
            {
                value /= denominator;
            }
        }

        [[nodiscard]] AxisAlignedDomain ComputeBounds(std::span<const glm::vec3> points,
                                                      MetricDimension dimension)
        {
            AxisAlignedDomain domain;
            domain.Dimension = dimension;
            domain.Min = glm::dvec3{std::numeric_limits<double>::max()};
            domain.Max = glm::dvec3{std::numeric_limits<double>::lowest()};
            for (const glm::vec3& point : points)
            {
                domain.Min.x = std::min(domain.Min.x, static_cast<double>(point.x));
                domain.Min.y = std::min(domain.Min.y, static_cast<double>(point.y));
                domain.Min.z = std::min(domain.Min.z, static_cast<double>(point.z));
                domain.Max.x = std::max(domain.Max.x, static_cast<double>(point.x));
                domain.Max.y = std::max(domain.Max.y, static_cast<double>(point.y));
                domain.Max.z = std::max(domain.Max.z, static_cast<double>(point.z));
            }
            return domain;
        }

        [[nodiscard]] MetricDimension ResolveDimension(std::span<const glm::vec3> points,
                                                       MetricDimension requested,
                                                       double planarTolerance,
                                                       MetricStatus& status)
        {
            status = MetricStatus::Success;
            if (requested == MetricDimension::D2 || requested == MetricDimension::D3)
            {
                if (requested == MetricDimension::D2 && !points.empty())
                {
                    const double z0 = static_cast<double>(points.front().z);
                    for (const glm::vec3& point : points)
                    {
                        if (std::abs(static_cast<double>(point.z) - z0) > planarTolerance)
                        {
                            status = MetricStatus::Requires2DInput;
                            return MetricDimension::Auto;
                        }
                    }
                }
                return requested;
            }

            if (points.empty())
            {
                return MetricDimension::Auto;
            }

            double minZ = static_cast<double>(points.front().z);
            double maxZ = minZ;
            for (const glm::vec3& point : points)
            {
                minZ = std::min(minZ, static_cast<double>(point.z));
                maxZ = std::max(maxZ, static_cast<double>(point.z));
            }
            return (maxZ - minZ) <= planarTolerance ? MetricDimension::D2 : MetricDimension::D3;
        }

        [[nodiscard]] double DomainMeasure(const AxisAlignedDomain& domain, MetricDimension dimension)
        {
            const double sx = domain.Max.x - domain.Min.x;
            const double sy = domain.Max.y - domain.Min.y;
            const double sz = domain.Max.z - domain.Min.z;
            if (dimension == MetricDimension::D2)
            {
                return sx * sy;
            }
            if (dimension == MetricDimension::D3)
            {
                return sx * sy * sz;
            }
            return 0.0;
        }

        [[nodiscard]] bool ValidateDomain(const AxisAlignedDomain& domain,
                                          MetricDimension dimension) noexcept
        {
            if (!std::isfinite(domain.Min.x) || !std::isfinite(domain.Min.y) || !std::isfinite(domain.Min.z)
                || !std::isfinite(domain.Max.x) || !std::isfinite(domain.Max.y) || !std::isfinite(domain.Max.z))
            {
                return false;
            }

            if (dimension == MetricDimension::D2)
            {
                return domain.Max.x > domain.Min.x && domain.Max.y > domain.Min.y;
            }
            if (dimension == MetricDimension::D3)
            {
                return domain.Max.x > domain.Min.x && domain.Max.y > domain.Min.y && domain.Max.z > domain.Min.z;
            }
            return false;
        }

        [[nodiscard]] bool Contains(const AxisAlignedDomain& domain,
                                    MetricDimension dimension,
                                    const glm::dvec3& p,
                                    double tolerance = 0.0) noexcept
        {
            const bool xy = p.x >= domain.Min.x - tolerance && p.x <= domain.Max.x + tolerance
                         && p.y >= domain.Min.y - tolerance && p.y <= domain.Max.y + tolerance;
            if (dimension == MetricDimension::D2)
            {
                return xy;
            }
            return xy && p.z >= domain.Min.z - tolerance && p.z <= domain.Max.z + tolerance;
        }

        [[nodiscard]] std::size_t CountOutsideDomain(std::span<const glm::vec3> points,
                                                     const AxisAlignedDomain& domain,
                                                     MetricDimension dimension)
        {
            std::size_t outside = 0;
            for (const glm::vec3& point : points)
            {
                if (!Contains(domain, dimension, glm::dvec3{point}, 1.0e-9))
                {
                    ++outside;
                }
            }
            return outside;
        }

        [[nodiscard]] double ShellMeasure(double r0, double r1, MetricDimension dimension)
        {
            if (dimension == MetricDimension::D2)
            {
                return std::numbers::pi * (r1 * r1 - r0 * r0);
            }
            return (4.0 / 3.0) * std::numbers::pi * (r1 * r1 * r1 - r0 * r0 * r0);
        }

        [[nodiscard]] glm::dvec3 EdgeDirection2D(std::uint32_t index, std::uint32_t count)
        {
            const double theta = (2.0 * std::numbers::pi * static_cast<double>(index)) / static_cast<double>(count);
            return {std::cos(theta), std::sin(theta), 0.0};
        }

        [[nodiscard]] glm::dvec3 EdgeDirection3D(std::uint32_t index, std::uint32_t count)
        {
            const double z = 1.0 - 2.0 * (static_cast<double>(index) + 0.5) / static_cast<double>(count);
            const double radius = std::sqrt(std::max(0.0, 1.0 - z * z));
            const double theta = static_cast<double>(index) * std::numbers::pi * (3.0 - std::sqrt(5.0));
            return {radius * std::cos(theta), radius * std::sin(theta), z};
        }

        [[nodiscard]] double EdgeFraction(const glm::vec3& point,
                                          double radius,
                                          const AxisAlignedDomain& domain,
                                          MetricDimension dimension,
                                          std::uint32_t directionCount)
        {
            if (directionCount == 0u)
            {
                return 1.0;
            }

            const glm::dvec3 center{point};
            std::uint32_t inside = 0;
            for (std::uint32_t i = 0; i < directionCount; ++i)
            {
                const glm::dvec3 direction = dimension == MetricDimension::D2
                    ? EdgeDirection2D(i, directionCount)
                    : EdgeDirection3D(i, directionCount);
                if (Contains(domain, dimension, center + radius * direction))
                {
                    ++inside;
                }
            }
            return static_cast<double>(inside) / static_cast<double>(directionCount);
        }

        [[nodiscard]] std::vector<int> BuildFrequencyAxis(std::uint32_t resolution)
        {
            std::vector<int> frequencies;
            frequencies.reserve(resolution);
            const int half = static_cast<int>(resolution / 2u);
            for (std::uint32_t i = 0; i < resolution; ++i)
            {
                frequencies.push_back(static_cast<int>(i) - half);
            }
            return frequencies;
        }

        [[nodiscard]] double Mean(const std::vector<double>& values)
        {
            if (values.empty())
            {
                return 0.0;
            }
            return std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
        }
    }

    std::string_view ToString(MetricStatus status) noexcept
    {
        switch (status)
        {
            case MetricStatus::Success: return "Success";
            case MetricStatus::EmptyInput: return "EmptyInput";
            case MetricStatus::InsufficientPoints: return "InsufficientPoints";
            case MetricStatus::NonFiniteInput: return "NonFiniteInput";
            case MetricStatus::InvalidRadius: return "InvalidRadius";
            case MetricStatus::InvalidBins: return "InvalidBins";
            case MetricStatus::InvalidDomain: return "InvalidDomain";
            case MetricStatus::PointOutsideDomain: return "PointOutsideDomain";
            case MetricStatus::Requires2DInput: return "Requires2DInput";
        }
        return "Unknown";
    }

    NearestNeighborResult ComputeNearestNeighborDistances(std::span<const glm::vec3> points)
    {
        NearestNeighborResult result;
        if (points.empty())
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::EmptyInput;
            return result;
        }
        if (points.size() < 2u)
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::InsufficientPoints;
            return result;
        }
        if (HasNonFinite(points, result.Info))
        {
            result.Status = MetricStatus::NonFiniteInput;
            return result;
        }

        result.Distances.assign(points.size(), std::numeric_limits<double>::max());
        result.MinDistance = std::numeric_limits<double>::max();
        result.MaxDistance = 0.0;

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < points.size(); ++j)
            {
                const double d = Distance(points[i], points[j]);
                result.Distances[i] = std::min(result.Distances[i], d);
                result.Distances[j] = std::min(result.Distances[j], d);
                result.MinDistance = std::min(result.MinDistance, d);
                result.MaxDistance = std::max(result.MaxDistance, d);
                ++result.Info.PairCount;
            }
        }

        result.MeanDistance = Mean(result.Distances);
        double variance = 0.0;
        for (const double distance : result.Distances)
        {
            const double delta = distance - result.MeanDistance;
            variance += delta * delta;
        }
        variance /= static_cast<double>(result.Distances.size());
        result.StdDevDistance = std::sqrt(variance);
        result.CoefficientOfVariation =
            result.MeanDistance > 0.0 ? result.StdDevDistance / result.MeanDistance : 0.0;
        result.Status = MetricStatus::Success;
        return result;
    }

    NearestNeighborResult ComputeNearestNeighborDistances(const Cloud& cloud)
    {
        const std::vector<glm::vec3> points = CollectLivePositions(cloud);
        return ComputeNearestNeighborDistances(points);
    }

    HistogramResult ComputeNearestNeighborHistogram(std::span<const glm::vec3> points,
                                                    const HistogramParams& params)
    {
        HistogramResult result;
        if (!ValidateHistogramParams(params))
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::InvalidBins;
            return result;
        }

        const NearestNeighborResult distances = ComputeNearestNeighborDistances(points);
        result.Info = distances.Info;
        if (!distances.Succeeded())
        {
            result.Status = distances.Status;
            return result;
        }

        result.Distribution = MakeHistogram(params);
        for (const double distance : distances.Distances)
        {
            AccumulateHistogram(result.Distribution, distance);
        }
        NormalizeHistogram(result.Distribution, static_cast<double>(distances.Distances.size()));
        result.Status = MetricStatus::Success;
        return result;
    }

    HistogramResult ComputeNearestNeighborHistogram(const Cloud& cloud,
                                                    const HistogramParams& params)
    {
        const std::vector<glm::vec3> points = CollectLivePositions(cloud);
        return ComputeNearestNeighborHistogram(points, params);
    }

    PoissonDiskResult ComputePoissonDiskRatio(std::span<const glm::vec3> points,
                                              double targetRadius)
    {
        PoissonDiskResult result;
        result.TargetRadius = targetRadius;
        if (!std::isfinite(targetRadius) || targetRadius <= 0.0)
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::InvalidRadius;
            return result;
        }

        const NearestNeighborResult distances = ComputeNearestNeighborDistances(points);
        result.Info = distances.Info;
        if (!distances.Succeeded())
        {
            result.Status = distances.Status;
            return result;
        }

        result.MinDistance = distances.MinDistance;
        result.Ratio = distances.MinDistance / targetRadius;
        result.Status = MetricStatus::Success;
        return result;
    }

    PoissonDiskResult ComputePoissonDiskRatio(const Cloud& cloud,
                                              double targetRadius)
    {
        const std::vector<glm::vec3> points = CollectLivePositions(cloud);
        return ComputePoissonDiskRatio(points, targetRadius);
    }

    CoverageResult ComputeCoverage(std::span<const glm::vec3> samples,
                                   std::span<const glm::vec3> reference,
                                   double radius)
    {
        CoverageResult result;
        result.Radius = radius;
        result.Info = BaseDiagnostics(samples);
        result.Info.ReferencePointCount = reference.size();
        for (const glm::vec3& point : reference)
        {
            if (!IsFinite(point))
            {
                ++result.Info.NonFinitePointCount;
            }
        }

        if (!std::isfinite(radius) || radius <= 0.0)
        {
            result.Status = MetricStatus::InvalidRadius;
            return result;
        }
        if (reference.empty())
        {
            result.Status = MetricStatus::EmptyInput;
            return result;
        }
        if (samples.empty())
        {
            result.Status = MetricStatus::InsufficientPoints;
            return result;
        }
        if (result.Info.NonFinitePointCount > 0u)
        {
            result.Status = MetricStatus::NonFiniteInput;
            return result;
        }

        result.ReferenceNearestDistances.reserve(reference.size());
        double sumNearest = 0.0;
        for (const glm::vec3& ref : reference)
        {
            double nearest = std::numeric_limits<double>::max();
            for (const glm::vec3& sample : samples)
            {
                nearest = std::min(nearest, Distance(ref, sample));
            }
            result.ReferenceNearestDistances.push_back(nearest);
            sumNearest += nearest;
            result.MaxNearestDistance = std::max(result.MaxNearestDistance, nearest);
            if (nearest <= radius)
            {
                ++result.CoveredReferenceCount;
            }
        }

        result.MeanNearestDistance = sumNearest / static_cast<double>(reference.size());
        result.CoverageFraction =
            static_cast<double>(result.CoveredReferenceCount) / static_cast<double>(reference.size());
        result.Status = MetricStatus::Success;
        return result;
    }

    CoverageResult ComputeCoverage(const Cloud& samples,
                                   const Cloud& reference,
                                   double radius)
    {
        const std::vector<glm::vec3> samplePoints = CollectLivePositions(samples);
        const std::vector<glm::vec3> referencePoints = CollectLivePositions(reference);
        return ComputeCoverage(samplePoints, referencePoints, radius);
    }

    RadialDistributionResult ComputeRadialDistributionFunction(
        std::span<const glm::vec3> points,
        const RadialDistributionParams& params)
    {
        RadialDistributionResult result;
        if (!ValidateHistogramParams(params.Bins))
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::InvalidBins;
            return result;
        }
        if (points.empty())
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::EmptyInput;
            return result;
        }
        if (points.size() < 2u)
        {
            result.Info = BaseDiagnostics(points);
            result.Status = MetricStatus::InsufficientPoints;
            return result;
        }
        if (HasNonFinite(points, result.Info))
        {
            result.Status = MetricStatus::NonFiniteInput;
            return result;
        }

        MetricStatus dimensionStatus = MetricStatus::Success;
        MetricDimension dimension = ResolveDimension(points, params.Dimension, params.PlanarTolerance, dimensionStatus);
        if (dimensionStatus != MetricStatus::Success)
        {
            result.Status = dimensionStatus;
            return result;
        }
        if (params.Domain.has_value() && params.Domain->Dimension != MetricDimension::Auto)
        {
            dimension = params.Domain->Dimension;
        }
        result.Info.Dimension = dimension;

        AxisAlignedDomain domain = params.Domain.value_or(ComputeBounds(points, dimension));
        domain.Dimension = dimension;
        if (!ValidateDomain(domain, dimension))
        {
            result.Status = MetricStatus::InvalidDomain;
            return result;
        }
        result.Info.DomainMeasure = DomainMeasure(domain, dimension);
        if (result.Info.DomainMeasure <= 0.0)
        {
            result.Status = MetricStatus::InvalidDomain;
            return result;
        }
        result.Info.PointsOutsideDomain = CountOutsideDomain(points, domain, dimension);
        if (result.Info.PointsOutsideDomain > 0u)
        {
            result.Status = MetricStatus::PointOutsideDomain;
            return result;
        }

        result.G = MakeHistogram(params.Bins);
        result.G.Normalized = false;
        std::vector<double> expected(result.G.Values.size(), 0.0);

        for (std::size_t i = 0; i < points.size(); ++i)
        {
            for (std::size_t j = i + 1u; j < points.size(); ++j)
            {
                const double d = Distance(points[i], points[j]);
                if (d < result.G.MinValue || d >= result.G.MaxValue)
                {
                    continue;
                }
                const auto bin = static_cast<std::size_t>((d - result.G.MinValue) / result.G.BinWidth);
                if (bin < result.G.Values.size())
                {
                    result.G.Values[bin] += 2.0;
                }
                ++result.Info.PairCount;
            }
        }

        const double density = static_cast<double>(points.size()) / result.Info.DomainMeasure;
        const std::uint32_t directions = std::max<std::uint32_t>(params.EdgeCorrectionDirections, 8u);
        for (std::size_t bin = 0; bin < result.G.Values.size(); ++bin)
        {
            const double r0 = result.G.MinValue + static_cast<double>(bin) * result.G.BinWidth;
            const double r1 = std::min(result.G.MaxValue, r0 + result.G.BinWidth);
            const double center = result.G.BinCenters[bin];
            const double shell = ShellMeasure(r0, r1, dimension);
            double edgeSum = 0.0;
            for (const glm::vec3& point : points)
            {
                edgeSum += params.UseEdgeCorrection
                    ? EdgeFraction(point, center, domain, dimension, directions)
                    : 1.0;
            }
            expected[bin] = density * shell * edgeSum;
        }

        double sumAway = 0.0;
        std::size_t countAway = 0;
        for (std::size_t bin = 0; bin < result.G.Values.size(); ++bin)
        {
            result.G.Values[bin] = expected[bin] > kEpsilon ? result.G.Values[bin] / expected[bin] : 0.0;
            if (result.G.BinCenters[bin] > result.G.MinValue + 1.5 * result.G.BinWidth)
            {
                sumAway += result.G.Values[bin];
                ++countAway;
            }
        }
        result.MeanAwayFromZero = countAway > 0u ? sumAway / static_cast<double>(countAway) : 0.0;
        result.Status = MetricStatus::Success;
        return result;
    }

    RadialDistributionResult ComputeRadialDistributionFunction(
        const Cloud& cloud,
        const RadialDistributionParams& params)
    {
        const std::vector<glm::vec3> points = CollectLivePositions(cloud);
        return ComputeRadialDistributionFunction(points, params);
    }

    PeriodogramResult ComputePeriodogram2D(std::span<const glm::vec3> points,
                                           const PeriodogramParams& params)
    {
        PeriodogramResult result;
        result.Info = BaseDiagnostics(points);
        if (points.empty())
        {
            result.Status = MetricStatus::EmptyInput;
            return result;
        }
        if (params.Resolution == 0u)
        {
            result.Status = MetricStatus::InvalidBins;
            return result;
        }
        if (result.Info.NonFinitePointCount > 0u)
        {
            result.Status = MetricStatus::NonFiniteInput;
            return result;
        }

        MetricStatus dimensionStatus = MetricStatus::Success;
        const MetricDimension dimension =
            ResolveDimension(points, MetricDimension::D2, params.PlanarTolerance, dimensionStatus);
        if (dimensionStatus != MetricStatus::Success || dimension != MetricDimension::D2)
        {
            result.Status = MetricStatus::Requires2DInput;
            return result;
        }
        result.Info.Dimension = MetricDimension::D2;

        AxisAlignedDomain domain = params.Domain.value_or(ComputeBounds(points, MetricDimension::D2));
        domain.Dimension = MetricDimension::D2;
        if (!ValidateDomain(domain, MetricDimension::D2))
        {
            result.Status = MetricStatus::InvalidDomain;
            return result;
        }
        result.Info.DomainMeasure = DomainMeasure(domain, MetricDimension::D2);
        result.Info.PointsOutsideDomain = CountOutsideDomain(points, domain, MetricDimension::D2);
        if (result.Info.PointsOutsideDomain > 0u)
        {
            result.Status = MetricStatus::PointOutsideDomain;
            return result;
        }

        result.Grid.Resolution = params.Resolution;
        result.Grid.FrequenciesX = BuildFrequencyAxis(params.Resolution);
        result.Grid.FrequenciesY = result.Grid.FrequenciesX;
        const std::size_t cellCount = static_cast<std::size_t>(params.Resolution) * params.Resolution;
        result.Grid.Power.assign(cellCount, 0.0);

        const double invWidth = 1.0 / (domain.Max.x - domain.Min.x);
        const double invHeight = 1.0 / (domain.Max.y - domain.Min.y);
        const double invN2 = 1.0 / (static_cast<double>(points.size()) * static_cast<double>(points.size()));

        for (std::uint32_t y = 0; y < params.Resolution; ++y)
        {
            const int ky = result.Grid.FrequenciesY[y];
            for (std::uint32_t x = 0; x < params.Resolution; ++x)
            {
                const int kx = result.Grid.FrequenciesX[x];
                if (params.RemoveDC && kx == 0 && ky == 0)
                {
                    continue;
                }

                double real = 0.0;
                double imag = 0.0;
                for (const glm::vec3& point : points)
                {
                    const double u = (static_cast<double>(point.x) - domain.Min.x) * invWidth;
                    const double v = (static_cast<double>(point.y) - domain.Min.y) * invHeight;
                    const double angle = -2.0 * std::numbers::pi
                        * (static_cast<double>(kx) * u + static_cast<double>(ky) * v);
                    real += std::cos(angle);
                    imag += std::sin(angle);
                }
                const std::size_t index = static_cast<std::size_t>(y) * params.Resolution + x;
                result.Grid.Power[index] = (real * real + imag * imag) * invN2;
            }
        }

        result.Status = MetricStatus::Success;
        return result;
    }

    PeriodogramResult ComputePeriodogram2D(const Cloud& cloud,
                                           const PeriodogramParams& params)
    {
        const std::vector<glm::vec3> points = CollectLivePositions(cloud);
        return ComputePeriodogram2D(points, params);
    }

    RadialPowerSpectrumResult ComputeRadiallyAveragedPowerSpectrum2D(
        std::span<const glm::vec3> points,
        const PeriodogramParams& periodogramParams,
        const RadialPowerSpectrumParams& spectrumParams)
    {
        RadialPowerSpectrumResult result;
        const PeriodogramResult periodogram = ComputePeriodogram2D(points, periodogramParams);
        result.Info = periodogram.Info;
        if (!periodogram.Succeeded())
        {
            result.Status = periodogram.Status;
            return result;
        }
        if (!std::isfinite(spectrumParams.FrequencyBinWidth) || spectrumParams.FrequencyBinWidth <= 0.0)
        {
            result.Status = MetricStatus::InvalidBins;
            return result;
        }

        const int maxAbsFrequency = periodogram.Grid.FrequenciesX.empty()
            ? 0
            : std::max(std::abs(periodogram.Grid.FrequenciesX.front()),
                       std::abs(periodogram.Grid.FrequenciesX.back()));
        const double maxFrequency = spectrumParams.MaxFrequency > 0.0
            ? spectrumParams.MaxFrequency
            : std::sqrt(2.0) * static_cast<double>(maxAbsFrequency) + spectrumParams.FrequencyBinWidth;

        HistogramParams histogramParams;
        histogramParams.MinValue = 0.0;
        histogramParams.MaxValue = maxFrequency;
        histogramParams.BinWidth = spectrumParams.FrequencyBinWidth;
        histogramParams.Normalize = false;
        if (!ValidateHistogramParams(histogramParams))
        {
            result.Status = MetricStatus::InvalidBins;
            return result;
        }
        result.Spectrum = MakeHistogram(histogramParams);
        result.SampleCounts.assign(result.Spectrum.Values.size(), 0u);

        for (std::uint32_t y = 0; y < periodogram.Grid.Resolution; ++y)
        {
            const int ky = periodogram.Grid.FrequenciesY[y];
            for (std::uint32_t x = 0; x < periodogram.Grid.Resolution; ++x)
            {
                const int kx = periodogram.Grid.FrequenciesX[x];
                if (kx == 0 && ky == 0)
                {
                    continue;
                }
                const double radius = std::sqrt(static_cast<double>(kx * kx + ky * ky));
                if (radius < result.Spectrum.MinValue || radius >= result.Spectrum.MaxValue)
                {
                    continue;
                }
                const auto bin = static_cast<std::size_t>((radius - result.Spectrum.MinValue)
                                                          / result.Spectrum.BinWidth);
                if (bin >= result.Spectrum.Values.size())
                {
                    continue;
                }
                const std::size_t index = static_cast<std::size_t>(y) * periodogram.Grid.Resolution + x;
                result.Spectrum.Values[bin] += periodogram.Grid.Power[index];
                ++result.SampleCounts[bin];
            }
        }

        for (std::size_t bin = 0; bin < result.Spectrum.Values.size(); ++bin)
        {
            if (result.SampleCounts[bin] > 0u)
            {
                result.Spectrum.Values[bin] /= static_cast<double>(result.SampleCounts[bin]);
            }
        }

        result.Status = MetricStatus::Success;
        return result;
    }

    RadialPowerSpectrumResult ComputeRadiallyAveragedPowerSpectrum2D(
        const Cloud& cloud,
        const PeriodogramParams& periodogramParams,
        const RadialPowerSpectrumParams& spectrumParams)
    {
        const std::vector<glm::vec3> points = CollectLivePositions(cloud);
        return ComputeRadiallyAveragedPowerSpectrum2D(points, periodogramParams, spectrumParams);
    }
}
