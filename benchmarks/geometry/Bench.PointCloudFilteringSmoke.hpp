// GEOM-016 — point-cloud filtering/outlier-removal smoke benchmark declaration.
#pragma once

#include <cstddef>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char* kPointCloudFilteringSmokeBenchmarkId = "geometry.pointcloud_filtering.smoke";
    inline constexpr const char* kPointCloudFilteringSmokeMethod      = "geometry.pointcloud.filtering";
    inline constexpr const char* kPointCloudFilteringSmokeDataset     = "builtin.two_cluster_grid_with_outliers";

    struct PointCloudFilteringSmokeMetrics
    {
        double      RuntimeMilliseconds{0.0};
        double      QualityErrorL2{0.0};      // 0 when every expected-count check holds.
        std::size_t InputCount{0};
        std::size_t InjectedOutliers{0};
        std::size_t VoxelReducedCount{0};
        std::size_t StatisticalKept{0};
        std::size_t StatisticalRejected{0};
        std::size_t RadiusKept{0};
        std::size_t RadiusRejected{0};
        bool        Succeeded{false};
    };

    [[nodiscard]] PointCloudFilteringSmokeMetrics RunPointCloudFilteringSmoke();
}
