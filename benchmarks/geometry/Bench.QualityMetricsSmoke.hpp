// GEOM-036 — quality-metrics smoke benchmark declaration.
#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kQualityMetricsSmokeBenchmarkId = "geometry.pointcloud_quality_metrics.smoke";
    inline constexpr const char* kQualityMetricsSmokeMethod      = "geometry.pointcloud.quality_metrics";
    inline constexpr const char* kQualityMetricsSmokeDataset     = "builtin.jittered_grid_white_noise_2d";

    struct QualityMetricsSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double NearestNeighborCv{0.0};
        double PoissonRatio{0.0};
        double RdfMeanAwayFromZero{0.0};
        double RapsCv{0.0};
        double CoverageFraction{0.0};
        std::size_t PointCount{0};
        bool Succeeded{false};
    };

    [[nodiscard]] QualityMetricsSmokeMetrics RunQualityMetricsSmoke();
}
