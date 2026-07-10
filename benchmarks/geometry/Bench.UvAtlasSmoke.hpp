// GEOM-057 — UV atlas fast-staged vs xatlas smoke benchmark declaration.
#pragma once

#include <array>
#include <cstddef>
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

[[nodiscard]] UvAtlasSmokeMetrics RunUvAtlasSmoke();
[[nodiscard]] UvAtlasPromotionMetrics RunUvAtlasPromotionSmoke();
} // namespace Intrinsic::Bench::Geometry
