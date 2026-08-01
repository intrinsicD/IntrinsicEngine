// METHOD-017 — deterministic CLOP CPU-reference correctness smoke.

#include "Bench.ContinuousLopReferenceSmoke.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry.PointCloud.Consolidation;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace Consolidation = ::Geometry::PointCloud::Consolidation;

        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 4;

        // Claim-grade confirmation fixtures use distinct dimensions, scale,
        // and noise sequences from the unit-test screening fixtures.
        [[nodiscard]] std::vector<glm::vec3> ConfirmationPlane(
            const int side = 9)
        {
            std::vector<glm::vec3> points{};
            points.reserve(static_cast<std::size_t>(side * side));
            for (int y = 0; y < side; ++y)
            {
                for (int x = 0; x < side; ++x)
                {
                    const float noise =
                        ((x * 23 + y * 13 + 4) % 9 - 4) * 0.014f;
                    points.emplace_back(
                        (static_cast<float>(x) - 0.5f * (side - 1)) * 0.15f,
                        (static_cast<float>(y) - 0.5f * (side - 1)) * 0.15f,
                        noise);
                }
            }
            return points;
        }

        [[nodiscard]] std::vector<glm::vec3> ConfirmationSphere()
        {
            std::vector<glm::vec3> points{};
            constexpr double pi = 3.14159265358979323846;
            for (int latitude = 1; latitude <= 7; ++latitude)
            {
                const double phi = pi * latitude / 8.0;
                for (int longitude = 0; longitude < 16; ++longitude)
                {
                    const double theta = 2.0 * pi * longitude / 16.0;
                    const std::size_t index = points.size();
                    const double noise =
                        (static_cast<int>((index * 29u + 7u) % 11u) - 5) *
                        0.006;
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
            {
                sum += std::abs(
                    static_cast<double>(glm::length(point)) - 1.0);
            }
            return sum / static_cast<double>(points.size());
        }

        [[nodiscard]] double MeanDistance(
            const std::span<const glm::vec3> lhs,
            const std::span<const glm::vec3> rhs)
        {
            if (lhs.size() != rhs.size() || lhs.empty())
                return std::numeric_limits<double>::infinity();
            double sum = 0.0;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
                sum += static_cast<double>(glm::distance(lhs[i], rhs[i]));
            return sum / static_cast<double>(lhs.size());
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
                        static_cast<double>(
                            glm::distance(points[i], points[j])));
                }
            }
            return minimum;
        }

        [[nodiscard]] bool Finite(
            const std::span<const glm::vec3> points)
        {
            return std::ranges::all_of(points, [](const glm::vec3 point)
            {
                return std::isfinite(point.x) && std::isfinite(point.y) &&
                       std::isfinite(point.z);
            });
        }

        [[nodiscard]] Consolidation::Params ClopParams(
            const std::uint32_t components)
        {
            return Consolidation::Params{
                .Method = Consolidation::ClopStrategy{
                    .MixtureComponentCount = components,
                    .MixtureMaxIterations = 120u,
                    .MixtureRelativeTolerance = 1.0e-6,
                    .CovarianceFloor = 1.0e-6,
                },
                .SupportRadius = 0.52,
                .RepulsionWeight = 0.0,
                .MaxIterations = 1u,
                .ConvergenceTolerance = 1.0,
                .TargetPointCount = 49u,
                .Seed = 41u,
            };
        }

        [[nodiscard]] ContinuousLopReferenceSmokeMetrics Tick()
        {
            ContinuousLopReferenceSmokeMetrics metrics{};
            const auto plane = ConfirmationPlane();
            const auto sphere = ConfirmationSphere();

            auto richParams = ClopParams(28u);
            const auto rich = Consolidation::Consolidate(plane, richParams);
            auto compactParams = ClopParams(8u);
            const auto compact =
                Consolidation::Consolidate(plane, compactParams);
            auto wlopParams = richParams;
            wlopParams.Method = Consolidation::WlopStrategy{};
            const auto wlop = Consolidation::Consolidate(plane, wlopParams);

            auto sphereParams = ClopParams(72u);
            sphereParams.SupportRadius = 0.50;
            sphereParams.TargetPointCount = 64u;
            const auto projectedSphere =
                Consolidation::Consolidate(sphere, sphereParams);

            std::vector<glm::vec3> compressed{};
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 6; ++x)
                    compressed.emplace_back(
                        0.026f * x * x, 0.17f * y, 0.0f);
            }
            auto uniformityParams = ClopParams(10u);
            uniformityParams.SupportRadius = 0.68;
            uniformityParams.TargetPointCount = 15u;
            uniformityParams.MaxIterations = 16u;
            uniformityParams.ConvergenceTolerance = 0.0;
            const auto withoutRepulsion =
                Consolidation::Consolidate(compressed, uniformityParams);
            uniformityParams.RepulsionWeight = 0.42;
            const auto withRepulsion =
                Consolidation::Consolidate(compressed, uniformityParams);

            const auto patch = ConfirmationPlane(6);
            auto patchWithOutliers = patch;
            patchWithOutliers.emplace_back(-0.06f, 0.03f, 0.42f);
            patchWithOutliers.emplace_back(0.11f, -0.05f, 0.49f);
            auto outlierParams = ClopParams(12u);
            outlierParams.SupportRadius = 0.55;
            outlierParams.TargetPointCount = 0u;
            const auto patchBaseline =
                Consolidation::Consolidate(patch, outlierParams);
            const auto patchOutliers =
                Consolidation::Consolidate(patchWithOutliers, outlierParams);

            auto invalidParams = ClopParams(1u);
            invalidParams.Method = Consolidation::ClopStrategy{
                .MixtureComponentCount =
                    static_cast<std::uint32_t>(plane.size() + 1u),
            };
            const auto invalid =
                Consolidation::Consolidate(plane, invalidParams);

            if (rich.Positions.empty() || compact.Positions.empty() ||
                wlop.Positions.empty() || projectedSphere.Positions.empty() ||
                withoutRepulsion.Positions.empty() ||
                withRepulsion.Positions.empty() ||
                patchBaseline.Positions.size() < patch.size() ||
                patchOutliers.Positions.size() < patch.size())
            {
                return metrics;
            }

            metrics.RawPlaneError = MeanPlaneError(plane);
            metrics.ClopPlaneError = MeanPlaneError(rich.Positions);
            metrics.CompactPlaneError = MeanPlaneError(compact.Positions);
            metrics.RawSphereError = MeanSphereError(sphere);
            metrics.ClopSphereError =
                MeanSphereError(projectedSphere.Positions);
            metrics.QualityErrorL2 = std::sqrt(
                (metrics.ClopPlaneError * metrics.ClopPlaneError +
                 metrics.ClopSphereError * metrics.ClopSphereError) /
                2.0);
            metrics.WlopParityMeanDistance =
                MeanDistance(rich.Positions, wlop.Positions);
            metrics.UniformityWithoutRepulsion =
                MinimumPairwiseDistance(withoutRepulsion.Positions);
            metrics.UniformityWithRepulsion =
                MinimumPairwiseDistance(withRepulsion.Positions);
            for (std::size_t i = 0u; i < patch.size(); ++i)
            {
                metrics.OutlierPatchMaxDisplacement = std::max(
                    metrics.OutlierPatchMaxDisplacement,
                    static_cast<double>(glm::distance(
                        patchBaseline.Positions[i],
                        patchOutliers.Positions[i])));
            }
            metrics.RichMixtureComponentCount =
                rich.Diagnostics.MixtureComponentCount;
            metrics.CompactMixtureComponentCount =
                compact.Diagnostics.MixtureComponentCount;
            metrics.RichAttractionContributions =
                rich.Diagnostics.AttractionContributionCount;
            metrics.CompactAttractionContributions =
                compact.Diagnostics.AttractionContributionCount;
            metrics.RichMixtureIterations =
                rich.Diagnostics.MixtureIterations;
            metrics.SphereMixtureIterations =
                projectedSphere.Diagnostics.MixtureIterations;
            metrics.PlaneProjectionIterations = rich.Diagnostics.Iterations;
            metrics.SphereProjectionIterations =
                projectedSphere.Diagnostics.Iterations;
            metrics.MixturesConverged = rich.Diagnostics.MixtureConverged &&
                compact.Diagnostics.MixtureConverged &&
                projectedSphere.Diagnostics.MixtureConverged;
            metrics.InvalidRequestFailedClosed =
                invalid.State ==
                    Consolidation::Status::InvalidMixtureComponentCount &&
                invalid.Positions.empty();

            metrics.Succeeded = rich.Succeeded() && compact.Succeeded() &&
                wlop.Succeeded() && projectedSphere.Succeeded() &&
                Finite(rich.Positions) && Finite(compact.Positions) &&
                Finite(projectedSphere.Positions) &&
                metrics.ClopPlaneError < metrics.RawPlaneError &&
                metrics.ClopPlaneError < 0.030 &&
                metrics.ClopSphereError < metrics.RawSphereError &&
                metrics.ClopSphereError < 0.020 &&
                metrics.WlopParityMeanDistance < 0.18 &&
                metrics.CompactPlaneError < 0.050 &&
                metrics.CompactAttractionContributions <
                    metrics.RichAttractionContributions &&
                metrics.UniformityWithRepulsion >
                    metrics.UniformityWithoutRepulsion * 1.02 &&
                metrics.OutlierPatchMaxDisplacement <
                    outlierParams.SupportRadius * 0.30 &&
                metrics.MixturesConverged &&
                metrics.InvalidRequestFailedClosed;
            return metrics;
        }
    }

    ContinuousLopReferenceSmokeMetrics RunContinuousLopReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
            static_cast<void>(Tick());

        ContinuousLopReferenceSmokeMetrics last{};
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
            last = Tick();
        const auto finish = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                finish - start).count();
        last.RuntimeMilliseconds =
            static_cast<double>(elapsed) * 1.0e-6 /
            static_cast<double>(kMeasuredIterations);
        return last;
    }
}
