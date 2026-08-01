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

        [[nodiscard]] std::vector<glm::vec3> NoisyPlane(const int side = 7)
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
            const auto plane = NoisyPlane();
            const auto sphere = NoisySphere();

            Consolidation::Params planeParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.65,
                .RepulsionWeight = 0.0,
                .MaxIterations = 1u,
                .ConvergenceTolerance = 1.0,
                .TargetPointCount = 25u,
                .Seed = 17u,
            };
            const auto wlopPlane =
                Consolidation::Consolidate(plane, planeParams);
            auto lopParams = planeParams;
            lopParams.Method = Consolidation::LopStrategy{};
            const auto lopPlane = Consolidation::Consolidate(plane, lopParams);

            Consolidation::Params sphereParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.9,
                .RepulsionWeight = 0.0,
                .MaxIterations = 50u,
                .ConvergenceTolerance = 2.0e-3,
                .TargetPointCount = 0u,
                .Seed = 17u,
            };
            const auto wlopSphere =
                Consolidation::Consolidate(sphere, sphereParams);

            std::vector<glm::vec3> compressed{};
            for (int y = 0; y < 5; ++y)
            {
                for (int x = 0; x < 5; ++x)
                    compressed.emplace_back(0.035f * x * x, 0.15f * y, 0.0f);
            }
            Consolidation::Params uniformityParams{
                .Method = Consolidation::LopStrategy{},
                .SupportRadius = 0.65,
                .RepulsionWeight = 0.0,
                .MaxIterations = 40u,
                .ConvergenceTolerance = 5.0e-4,
                .TargetPointCount = 16u,
                .Seed = 17u,
            };
            const auto withoutRepulsion =
                Consolidation::Consolidate(compressed, uniformityParams);
            uniformityParams.RepulsionWeight = 0.45;
            const auto withRepulsion =
                Consolidation::Consolidate(compressed, uniformityParams);

            const auto patch = NoisyPlane(4);
            auto patchWithOutliers = patch;
            patchWithOutliers.emplace_back(10.0f, 0.0f, 0.0f);
            patchWithOutliers.emplace_back(10.1f, 0.0f, 0.0f);
            Consolidation::Params outlierParams{
                .Method = Consolidation::WlopStrategy{},
                .SupportRadius = 0.65,
                .RepulsionWeight = 0.2,
                .MaxIterations = 12u,
                .ConvergenceTolerance = 0.0,
                .TargetPointCount = 0u,
                .Seed = 17u,
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
                metrics.WlopPlaneError < 0.024 &&
                metrics.WlopSphereError < metrics.RawSphereError &&
                metrics.WlopSphereError < 0.025 &&
                metrics.UniformityWithRepulsion >
                    metrics.UniformityWithoutRepulsion * 1.05 &&
                metrics.OutlierPatchMaxDisplacement < 1.0e-5;
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
