// METHOD-037 — signed-curvature segmentation CPU-reference smoke declaration.
#pragma once

#include <cstdint>

namespace Intrinsic::Bench::Geometry
{
    inline constexpr const char*
        kCurvatureSegmentationReferenceSmokeBenchmarkId =
            "geometry.curvature_segmentation.reference.smoke";
    inline constexpr const char*
        kCurvatureSegmentationReferenceSmokeMethod =
            "geometry.curvature_segmentation";
    inline constexpr const char*
        kCurvatureSegmentationReferenceSmokeDataset =
            "builtin.curvature_regimes.folded_strip.v1";

    struct CurvatureSegmentationReferenceSmokeMetrics
    {
        double RuntimeMilliseconds{0.0};
        double QualityErrorL2{0.0};
        double FoldBoundaryRecall{0.0};
        std::uint32_t SelectedComponentCount{0u};
        std::uint32_t ConnectedRegionCount{0u};
        std::uint32_t BoundaryEdgeCount{0u};
        std::uint32_t GmmIterations{0u};
        std::uint32_t SpatialIterations{0u};
        bool Succeeded{false};
    };

    [[nodiscard]] CurvatureSegmentationReferenceSmokeMetrics
    RunCurvatureSegmentationReferenceSmoke();
}
