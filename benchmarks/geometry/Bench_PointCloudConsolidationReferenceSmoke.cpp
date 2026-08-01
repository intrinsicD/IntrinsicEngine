// METHOD-016 — deterministic LOP/WLOP CPU-reference correctness smoke.

#include "Bench.PointCloudConsolidationReferenceSmoke.hpp"

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
        constexpr int kMeasuredIterations = 8;

        // Confirmation fixtures deliberately differ from the unit-test
        // screening grid, noise sequence, scale, and sample count.
        [[nodiscard]] std::vector<glm::vec3> ConfirmationPlane(
            const int side = 8)
        {
            std::vector<glm::vec3> points{};
            points.reserve(static_cast<std::size_t>(side * side));
            for (int y = 0; y < side; ++y)
            {
                for (int x = 0; x < side; ++x)
                {
                    const float noise =
                        ((x * 17 + y * 11 + 3) % 7 - 3) * 0.018f;
                    points.emplace_back(
                        (static_cast<float>(x) - 0.5f * (side - 1)) * 0.17f,
                        (static_cast<float>(y) - 0.5f * (side - 1)) * 0.17f,
                        noise);
                }
            }
            return points;
        }

        [[nodiscard]] std::vector<glm::vec3> ConfirmationSphere()
        {
            std::vector<glm::vec3> points{};
            constexpr double pi = 3.14159265358979323846;
            for (int latitude = 1; latitude <= 6; ++latitude)
            {
                const double phi = pi * latitude / 7.0;
                for (int longitude = 0; longitude < 14; ++longitude)
                {
                    const double theta = 2.0 * pi * longitude / 14.0;
                    const std::size_t index = points.size();
                    const double noise =
                        (static_cast<int>((index * 19u + 5u) % 9u) - 4) *
                        0.008;
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

        [[nodiscard]] PointCloudConsolidationReferenceSmokeMetrics Tick()
        {
            PointCloudConsolidationReferenceSmokeMetrics metrics{};
            const auto plane = ConfirmationPlane();
            const auto sphere = ConfirmationSphere();

            Consolidation::Params planeParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.58,
                .RepulsionWeight = 0.0,
                .MaxIterations = 1u,
                .ConvergenceTolerance = 1.0,
                .TargetPointCount = 36u,
                .Seed = 29u,
            };
            const auto wlopPlane =
                Consolidation::Consolidate(plane, planeParams);
            auto lopParams = planeParams;
            lopParams.Method = Consolidation::LopStrategy{};
            const auto lopPlane = Consolidation::Consolidate(plane, lopParams);

            Consolidation::Params sphereParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.82,
                .RepulsionWeight = 0.0,
                .MaxIterations = 50u,
                .ConvergenceTolerance = 2.0e-3,
                .TargetPointCount = 0u,
                .Seed = 29u,
            };
            const auto wlopSphere =
                Consolidation::Consolidate(sphere, sphereParams);

            std::vector<glm::vec3> compressed{};
            for (int y = 0; y < 4; ++y)
            {
                for (int x = 0; x < 6; ++x)
                    compressed.emplace_back(
                        0.028f * x * x, 0.18f * y, 0.0f);
            }
            Consolidation::Params uniformityParams{
                .Method = Consolidation::LopStrategy{},
                .SupportRadius = 0.70,
                .RepulsionWeight = 0.0,
                .MaxIterations = 40u,
                .ConvergenceTolerance = 5.0e-4,
                .TargetPointCount = 15u,
                .Seed = 29u,
            };
            const auto withoutRepulsion =
                Consolidation::Consolidate(compressed, uniformityParams);
            uniformityParams.RepulsionWeight = 0.45;
            const auto withRepulsion =
                Consolidation::Consolidate(compressed, uniformityParams);

            const auto patch = ConfirmationPlane(5);
            auto patchWithOutliers = patch;
            patchWithOutliers.emplace_back(-0.05f, 0.05f, 0.40f);
            patchWithOutliers.emplace_back(0.12f, -0.04f, 0.48f);
            Consolidation::Params outlierParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.58,
                .RepulsionWeight = 0.2,
                .MaxIterations = 12u,
                .ConvergenceTolerance = 0.0,
                .TargetPointCount = 0u,
                .Seed = 29u,
            };
            const auto patchBaseline =
                Consolidation::Consolidate(patch, outlierParams);
            const auto patchOutliers =
                Consolidation::Consolidate(patchWithOutliers, outlierParams);

            metrics.RawPlaneError = MeanPlaneError(plane);
            metrics.WlopPlaneError = MeanPlaneError(wlopPlane.Positions);
            metrics.RawSphereError = MeanSphereError(sphere);
            metrics.WlopSphereError = MeanSphereError(wlopSphere.Positions);
            metrics.QualityErrorL2 = std::sqrt(
                (metrics.WlopPlaneError * metrics.WlopPlaneError +
                 metrics.WlopSphereError * metrics.WlopSphereError) /
                2.0);
            metrics.UniformityWithoutRepulsion =
                MinimumPairwiseDistance(withoutRepulsion.Positions);
            metrics.UniformityWithRepulsion =
                MinimumPairwiseDistance(withRepulsion.Positions);
            for (std::size_t i = 0u; i < patch.size(); ++i)
            {
                metrics.OutlierPatchMaxDisplacement = std::max(
                    metrics.OutlierPatchMaxDisplacement,
                    static_cast<double>(glm::distance(
                        patchBaseline.Positions[i], patchOutliers.Positions[i])));
            }
            metrics.LopIterations = lopPlane.Diagnostics.Iterations;
            metrics.WlopPlaneIterations = wlopPlane.Diagnostics.Iterations;
            metrics.WlopSphereIterations = wlopSphere.Diagnostics.Iterations;

            metrics.Succeeded = wlopPlane.Succeeded() && lopPlane.Succeeded() &&
                wlopSphere.Succeeded() && Finite(wlopPlane.Positions) &&
                Finite(lopPlane.Positions) && Finite(wlopSphere.Positions) &&
                metrics.WlopPlaneError < metrics.RawPlaneError &&
                metrics.WlopPlaneError < 0.030 &&
                metrics.WlopSphereError < metrics.RawSphereError &&
                metrics.WlopSphereError < 0.025 &&
                metrics.UniformityWithRepulsion >
                    metrics.UniformityWithoutRepulsion * 1.05 &&
                metrics.OutlierPatchMaxDisplacement <
                    outlierParams.SupportRadius * 0.30;
            return metrics;
        }
    }

    PointCloudConsolidationReferenceSmokeMetrics
    RunPointCloudConsolidationReferenceSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
            static_cast<void>(Tick());

        PointCloudConsolidationReferenceSmokeMetrics last{};
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
