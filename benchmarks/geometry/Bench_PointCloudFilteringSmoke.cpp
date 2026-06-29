// GEOM-016 — deterministic point-cloud filtering/outlier-removal smoke benchmark.
//
// Exercises the filtering pack (voxel downsample + statistical and radius
// outlier removal) on a fixed two-cluster + isolated-outlier fixture. This is
// quality/smoke evidence only; no performance-win claim is made. The quality
// error is the L2 norm of expected-vs-actual count violations, so it is exactly
// 0 when the operators reject precisely the injected outliers.

#include "Bench.PointCloudFilteringSmoke.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

import Geometry.PointCloud;
import Geometry.PointCloud.Utils;

namespace Intrinsic::Bench::Geometry
{
    namespace
    {
        namespace PC = ::Geometry::PointCloud;
        using Cloud = PC::Cloud;

        constexpr int         kWarmupIterations   = 1;
        constexpr int         kMeasuredIterations = 8;
        constexpr int         kClusterSide        = 6;     // 36 points per cluster.
        constexpr float       kSpacing            = 0.05f;
        constexpr std::size_t kInjectedOutliers   = 4;

        [[nodiscard]] Cloud MakeFixture()
        {
            Cloud cloud;
            const glm::vec3 origins[] = {glm::vec3(0.0f), glm::vec3(3.0f, 0.0f, 0.0f)};
            for (const glm::vec3& origin : origins)
                for (int y = 0; y < kClusterSide; ++y)
                    for (int x = 0; x < kClusterSide; ++x)
                        cloud.AddPoint(origin + glm::vec3(static_cast<float>(x) * kSpacing,
                                                          static_cast<float>(y) * kSpacing, 0.0f));

            const glm::vec3 outliers[] = {
                glm::vec3(15.0f, 15.0f, 15.0f),
                glm::vec3(-12.0f, 9.0f, -5.0f),
                glm::vec3(18.0f, -11.0f, 6.0f),
                glm::vec3(-9.0f, -14.0f, 13.0f),
            };
            for (const glm::vec3& p : outliers)
                cloud.AddPoint(p);
            return cloud;
        }

        struct TickResult
        {
            PointCloudFilteringSmokeMetrics Metrics{};
        };

        [[nodiscard]] TickResult Tick()
        {
            const Cloud cloud = MakeFixture();

            PC::DownsampleParams voxel{};
            voxel.VoxelSize = 0.04f;
            const auto down = PC::VoxelDownsample(cloud, voxel);

            PC::StatisticalOutlierRemovalParams sor{};
            sor.KNeighbors = 8;
            sor.StdDevMultiplier = 1.0f;
            const auto sorResult = PC::RemoveStatisticalOutliers(cloud, sor);

            PC::RadiusOutlierRemovalParams ror{};
            ror.SearchRadius = 0.12f;
            ror.MinNeighbors = 3;
            const auto rorResult = PC::RemoveRadiusOutliers(cloud, ror);

            TickResult tick;
            auto& m = tick.Metrics;
            m.InputCount          = cloud.VerticesSize();
            m.InjectedOutliers    = kInjectedOutliers;
            m.VoxelReducedCount   = down ? down->ReducedCount : 0;
            m.StatisticalKept     = sorResult.KeptCount;
            m.StatisticalRejected = sorResult.RejectedCount;
            m.RadiusKept          = rorResult.KeptCount;
            m.RadiusRejected      = rorResult.RejectedCount;

            const bool statusesPass =
                down.has_value() &&
                sorResult.Status == PC::OutlierRemovalStatus::Success &&
                rorResult.Status == PC::OutlierRemovalStatus::Success;

            // Quality target: both operators reject exactly the injected outliers
            // and downsampling does not grow the cloud.
            const double sorViolation = static_cast<double>(sorResult.RejectedCount) - static_cast<double>(kInjectedOutliers);
            const double rorViolation = static_cast<double>(rorResult.RejectedCount) - static_cast<double>(kInjectedOutliers);
            const double voxelViolation = (down && down->ReducedCount <= cloud.VerticesSize()) ? 0.0 : 1.0;
            m.QualityErrorL2 = std::sqrt(sorViolation * sorViolation
                + rorViolation * rorViolation
                + voxelViolation * voxelViolation);
            m.Succeeded = statusesPass && m.QualityErrorL2 <= 1.0e-6;
            return tick;
        }
    }

    PointCloudFilteringSmokeMetrics RunPointCloudFilteringSmoke()
    {
        for (int i = 0; i < kWarmupIterations; ++i)
            (void)Tick();

        TickResult last{};
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < kMeasuredIterations; ++i)
            last = Tick();
        const auto t1 = std::chrono::steady_clock::now();

        const auto totalNs = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
        last.Metrics.RuntimeMilliseconds =
            (static_cast<double>(totalNs) / static_cast<double>(kMeasuredIterations)) * 1.0e-6;
        return last.Metrics;
    }
}
