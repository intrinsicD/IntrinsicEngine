// GEOM-057 — UV atlas fast-staged vs xatlas smoke benchmark declaration.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Intrinsic::Bench::Geometry {
inline constexpr const char *kUvAtlasSmokeBenchmarkId =
    "geometry.uv_atlas.fast_staged_vs_xatlas.smoke";
inline constexpr const char *kUvAtlasSmokeMethod =
    "geometry.uv_atlas.fast_staged_vs_xatlas";
inline constexpr const char *kUvAtlasSmokeDataset = "builtin.cube_surface";
inline constexpr const char *kUvAtlasPromotionBenchmarkId =
    "geometry.uv_atlas.fast_staged_promotion.smoke";
inline constexpr const char *kUvAtlasPromotionMethod =
    "geometry.uv_atlas.fast_staged_promotion";
inline constexpr const char *kUvAtlasPromotionDataset =
    "builtin.uv_atlas_promotion_suite";
inline constexpr std::size_t kUvAtlasPromotionWarmupPairs = 1u;
inline constexpr std::size_t kUvAtlasPromotionMeasuredPairs = 5u;
inline constexpr const char *kUvAtlasPromotionTimingStatistic =
    "median_paired_runtime_ratio";
inline constexpr const char *kUvAtlasPromotionBackendRuntimeStatistic =
    "median_individual_runtime_ms";
inline constexpr const char *kUvAtlasPromotionMeasurementOrder =
    "alternating_fast_xatlas_xatlas_fast";
inline constexpr const char *kUvAtlasEdgeGroupingScalingBenchmarkId =
    "geometry.uv_atlas.fast_staged_edge_grouping.scaling";
inline constexpr const char *kUvAtlasEdgeGroupingScalingMethod =
    "geometry.uv_atlas.fast_staged_edge_grouping";
inline constexpr const char *kUvAtlasEdgeGroupingScalingDataset =
    "builtin.uv_atlas_edge_grouping_grid_pair_v1";
inline constexpr std::uint32_t kUvAtlasEdgeGroupingSmallGridSide = 16u;
inline constexpr std::uint32_t kUvAtlasEdgeGroupingLargeGridSide = 32u;
inline constexpr std::size_t kUvAtlasEdgeGroupingWarmupPairs = 1u;
inline constexpr std::size_t kUvAtlasEdgeGroupingMeasuredPairs = 5u;
inline constexpr const char *kUvAtlasEdgeGroupingTimingStatistic =
    "median_alternating_pair_runtime";
inline constexpr const char *kUvAtlasEdgeGroupingBaselineCommit = "8ca52438";
inline constexpr const char *kUvAtlasEdgeGroupingBaselineSnapshot =
    "benchmarks/baselines/"
    "geometry_uv_atlas_fast_staged_edge_grouping_scaling_8ca52438.json";
inline constexpr double kUvAtlasEdgeGroupingBaselineLargeRuntimeMilliseconds =
    1031.89589499999988220;
inline constexpr double kUvAtlasEdgeGroupingBaselineNormalizedScalingFactor =
    1.72673931985979334;
inline constexpr std::size_t kUvAtlasEdgeGroupingQualityVectorSize = 12u;
inline constexpr std::size_t kUvAtlasEdgeGroupingBaselineOutputVertexCount =
    1089u;
inline constexpr std::size_t kUvAtlasEdgeGroupingBaselineOutputFaceCount =
    2048u;
inline constexpr std::size_t kUvAtlasEdgeGroupingBaselineChartCount = 1u;
inline constexpr std::size_t kUvAtlasEdgeGroupingBaselineSeamCount = 0u;
inline constexpr std::size_t kUvAtlasEdgeGroupingBaselineBoundarySeamCount =
    128u;
inline constexpr double kUvAtlasEdgeGroupingBaselineUvMinX = 0.0078125;
inline constexpr double kUvAtlasEdgeGroupingBaselineUvMinY = 0.0078125;
inline constexpr double kUvAtlasEdgeGroupingBaselineUvMaxX =
    0.99218755960464478;
inline constexpr double kUvAtlasEdgeGroupingBaselineUvMaxY =
    0.91768068075180054;
inline constexpr double kUvAtlasEdgeGroupingBaselineMeanConformalDistortion =
    1.18278551100503537;
inline constexpr double kUvAtlasEdgeGroupingBaselineMaxStretch =
    0.70545702589212356;
inline constexpr std::size_t kUvAtlasEdgeGroupingBaselineFlippedElementCount =
    0u;
inline constexpr std::uint64_t kUvAtlasEdgeGroupingBaselineOutputSignature =
    5684639256857304174ull;

struct UvAtlasSmokeMetrics {
  double RuntimeMilliseconds{0.0};
  double FastRuntimeMilliseconds{0.0};
  double XAtlasRuntimeMilliseconds{0.0};
  double FastToXAtlasRuntimeRatio{0.0};
  double QualityErrorL2{0.0};
  double FastMeanConformalDistortion{0.0};
  double XAtlasMeanConformalDistortion{0.0};
  double FastMaxStretch{0.0};
  double XAtlasMaxStretch{0.0};
  std::size_t FastChartCount{0};
  std::size_t XAtlasChartCount{0};
  std::size_t FastFlippedElementCount{0};
  std::size_t XAtlasFlippedElementCount{0};
  bool Succeeded{false};
};

struct UvAtlasPromotionFixtureMetrics {
  std::string Name{};
  std::size_t InputVertexCount{0};
  std::size_t InputFaceCount{0};
  double FastRuntimeMilliseconds{0.0};
  double XAtlasRuntimeMilliseconds{0.0};
  double FastToXAtlasRuntimeRatio{0.0};
  std::array<double, kUvAtlasPromotionMeasuredPairs>
      FastRuntimeSamplesMilliseconds{};
  std::array<double, kUvAtlasPromotionMeasuredPairs>
      XAtlasRuntimeSamplesMilliseconds{};
  std::array<double, kUvAtlasPromotionMeasuredPairs> PairedRuntimeRatios{};
  double ConformalRegression{0.0};
  double StretchRegression{0.0};
  std::size_t FastOutputVertexCount{0};
  std::size_t XAtlasOutputVertexCount{0};
  std::size_t FastOutputFaceCount{0};
  std::size_t XAtlasOutputFaceCount{0};
  std::size_t FastChartCount{0};
  std::size_t XAtlasChartCount{0};
  std::size_t FastFlippedElementCount{0};
  std::size_t XAtlasFlippedElementCount{0};
  std::size_t FastChartOverlapCount{0};
  double FastMeanConformalDistortion{0.0};
  double XAtlasMeanConformalDistortion{0.0};
  double FastMaxStretch{0.0};
  double XAtlasMaxStretch{0.0};
  double FastPackingUtilization{0.0};
  bool FastSucceeded{false};
  bool XAtlasSucceeded{false};
  bool FastFiniteNormalized{false};
  bool FastUsedFallback{false};
  bool Passed{false};
};

struct UvAtlasPromotionMetrics {
  double RuntimeMilliseconds{0.0};
  double QualityErrorL2{0.0};
  double QualityErrorLinf{0.0};
  double MeanFastRuntimeMilliseconds{0.0};
  double MeanXAtlasRuntimeMilliseconds{0.0};
  double MeanFastToXAtlasRuntimeRatio{0.0};
  double MaxFastToXAtlasRuntimeRatio{0.0};
  std::size_t FixtureCount{0};
  std::size_t PassedFixtureCount{0};
  std::size_t FailedFixtureCount{0};
  std::size_t FastFlippedElementCountTotal{0};
  std::size_t FastChartOverlapCountTotal{0};
  bool PromotionPass{false};
  std::vector<UvAtlasPromotionFixtureMetrics> Fixtures{};
};

struct UvAtlasEdgeGroupingScalingMetrics {
  double RuntimeMilliseconds{0.0};
  double QualityErrorL2{0.0};
  double ThroughputFacesPerSecond{0.0};
  double SmallMedianRuntimeMilliseconds{0.0};
  double LargeMedianRuntimeMilliseconds{0.0};
  double FaceCountRatio{0.0};
  double RuntimeScalingRatio{0.0};
  double NormalizedRuntimeScalingFactor{0.0};
  std::array<double, kUvAtlasEdgeGroupingMeasuredPairs>
      SmallRuntimeSamplesMilliseconds{};
  std::array<double, kUvAtlasEdgeGroupingMeasuredPairs>
      LargeRuntimeSamplesMilliseconds{};
  std::array<double, kUvAtlasEdgeGroupingQualityVectorSize>
      QualityVectorDelta{};
  std::size_t SmallVertexCount{0};
  std::size_t SmallFaceCount{0};
  std::size_t LargeVertexCount{0};
  std::size_t LargeFaceCount{0};
  std::size_t LargeOutputVertexCount{0};
  std::size_t LargeOutputFaceCount{0};
  std::size_t LargeChartCount{0};
  std::size_t LargeSeamCount{0};
  std::size_t LargeBoundarySeamCount{0};
  double LargeUvMinX{0.0};
  double LargeUvMinY{0.0};
  double LargeUvMaxX{0.0};
  double LargeUvMaxY{0.0};
  double LargeMeanConformalDistortion{0.0};
  double LargeMaxStretch{0.0};
  std::size_t LargeFlippedElementCount{0};
  std::uint64_t LargeOutputSignature{0u};
  bool LargeSucceeded{false};
  bool LargeFiniteNormalized{false};
  bool LargeUsedFallback{false};
  bool DeterministicTopology{false};
  bool MatchesBaselineOutputSignature{false};
  bool Passed{false};
};

[[nodiscard]] UvAtlasSmokeMetrics RunUvAtlasSmoke();
[[nodiscard]] UvAtlasPromotionMetrics RunUvAtlasPromotionSmoke();
[[nodiscard]] UvAtlasEdgeGroupingScalingMetrics
RunUvAtlasEdgeGroupingScaling();
} // namespace Intrinsic::Bench::Geometry
