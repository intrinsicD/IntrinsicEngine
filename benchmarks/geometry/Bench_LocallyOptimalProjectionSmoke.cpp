// METHOD-016 — WLOP consolidation CPU reference smoke benchmark workload.
//
// The smoke workload mirrors the noisy-plane fixture from the correctness
// tests and reports both runtime and residual distance-to-plane L2 error.
// No performance win is claimed; this is a PR-fast contract check for the
// reference backend.

#include "Bench.LocallyOptimalProjectionSmoke.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <random>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        constexpr int kWarmupIterations = 1;
        constexpr int kMeasuredIterations = 8;
        constexpr int kPlaneSide = 24;
        constexpr float kNoiseSigma = 0.02f;
        constexpr double kQualityErrorMax = 0.015;

        [[nodiscard]] std::vector<glm::vec3> MakeNoisyPlane()
        {
            std::mt19937 rng(7u);
            std::normal_distribution<float> noise(0.0f, kNoiseSigma);
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

        [[nodiscard]] double RootMeanSquarePlaneError(const std::vector<glm::vec3>& points)
        {
            double sumSq = 0.0;
            for (const glm::vec3& p : points)
            {
                const double z = static_cast<double>(p.z);
                sumSq += z * z;
            }
            return points.empty() ? 0.0 : std::sqrt(sumSq / static_cast<double>(points.size()));
        }

        [[nodiscard]] LocallyOptimalProjectionSmokeMetrics Tick()
        {
            const std::vector<glm::vec3> points = MakeNoisyPlane();

            ::Geometry::PointCloud::Consolidation::WlopParams params{};
            params.SupportRadius = 0.35f;
            params.RepulsionWeight = 0.3f;
            params.Iterations = 12;
            params.TargetCount = 200;
            params.Seed = 42;

            const ::Geometry::PointCloud::Consolidation::ConsolidateResult result =
                ::Geometry::PointCloud::Consolidation::Consolidate(
                    std::span<const glm::vec3>(points), params);

            LocallyOptimalProjectionSmokeMetrics metrics{};
            metrics.RawQualityErrorL2 = RootMeanSquarePlaneError(points);
            metrics.QualityErrorL2 = RootMeanSquarePlaneError(result.Positions);
            metrics.IterationsRun = result.Report.IterationsRun;
            metrics.ProjectedPointCount =
                static_cast<std::uint32_t>(result.Report.ProjectedPointCount);
            metrics.EmptyAttractionNeighborhoods =
                static_cast<std::uint32_t>(result.Report.EmptyAttractionNeighborhoods);
            if (!result.Report.Movement.empty())
                metrics.LastMeanMovement = result.Report.Movement.back().MeanMovement;

            const auto nn =
                ::Geometry::PointCloud::QualityMetrics::ComputeNearestNeighborDistances(
                    std::span<const glm::vec3>(result.Positions));
            if (nn.Succeeded())
                metrics.MinPairwiseDistance = nn.MinDistance;

            metrics.Succeeded = result.Succeeded()
                && result.Positions.size() == 200u
                && metrics.QualityErrorL2 < metrics.RawQualityErrorL2
                && metrics.QualityErrorL2 < kQualityErrorMax;
            return metrics;
        }
    } // namespace

    LocallyOptimalProjectionSmokeMetrics RunLocallyOptimalProjectionSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
        {
            (void)Tick();
        }

        LocallyOptimalProjectionSmokeMetrics last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
        {
            last = Tick();
        }
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.RuntimeMilliseconds =
            (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last;
    }
} // namespace Intrinsic::Bench::Geometry
