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
        // Kept in the benchmark's historical quality_error_l2 schema slot;
        // the unit is a label-permutation-invariant face fraction, not an L2
        // norm.
        double RegimeMisclassificationFraction{0.0};
        double FoldBoundaryRecall{0.0};
        double FaceAggregationMilliseconds{0.0};
        double GmmFittingMilliseconds{0.0};
        double UnaryConstructionMilliseconds{0.0};
        double DualGraphConstructionMilliseconds{0.0};
        double SpatialOptimizationMilliseconds{0.0};
        double ConnectivityPublicationMilliseconds{0.0};
        double SegmentationTotalMilliseconds{0.0};
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
