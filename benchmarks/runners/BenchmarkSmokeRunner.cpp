// GEOM-009 — geometry smoke benchmark runner.
//
// Invokes the registered geometry smoke workloads and emits result JSON
// matching docs/benchmarking/result-json-schema.md (validated by
// tools/benchmark/validate_benchmark_results.py).
//
// Usage:
//   IntrinsicBenchmarkSmoke <output_path>
//
// `<output_path>` may be a directory (one JSON per benchmark is written
// inside it) or a file path (the first benchmark is written to that exact
// path; any additional benchmarks are written as siblings keyed by
// benchmark_id). The single-file form preserves backwards compatibility
// with the previous scaffold's CMake/CI wiring.

#include "../geometry/Bench.GeometrySmoke.hpp"
#include "../geometry/Bench.PointCloudFilteringSmoke.hpp"
#include "../geometry/Bench.ProgressivePoissonReferenceSmoke.hpp"
#include "../geometry/Bench.QualityMetricsSmoke.hpp"
#include "../geometry/Bench.SignedHeatReferenceSmoke.hpp"
#include "../geometry/Bench.SurfaceSamplingSmoke.hpp"
#include "../geometry/Bench.UvAtlasSmoke.hpp"
#include "../physics/Bench.ParticleSpringReferenceSmoke.hpp"
#include "../physics/Bench.RigidBodyReferenceSmoke.hpp"
#include "../physics/Bench.SphFluidReferenceSmoke.hpp"
#include "../physics/Bench.XpbdClothReferenceSmoke.hpp"
#include "../rendering/Bench.FramegraphBarrierEmissionSmoke.hpp"
#include "../rendering/Bench.FramegraphCompilerIndexingSmoke.hpp"
#include "../rendering/Bench.FramegraphScratchReuseSmoke.hpp"
#include "../rendering/Bench.FrameRecipeCompileCacheSmoke.hpp"
#include "../rendering/Bench.RenderGraphParallelRecordingSmoke.hpp"
#include "../rendering/Bench.VertexFetchLayoutSmoke.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
auto EscapeJson(std::string_view input) -> std::string {
  std::string out;
  out.reserve(input.size());
  for (const char ch : input) {
    switch (ch) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += ch;
      break;
    }
  }
  return out;
}

template <std::size_t N>
void EmitDoubleSamples(std::ostringstream &out,
                       const std::array<double, N> &samples) {
  out << "[";
  for (std::size_t i = 0; i < samples.size(); ++i) {
    if (i > 0u) {
      out << ", ";
    }
    out << samples[i];
  }
  out << "]";
}

auto ResolveCommit() -> std::string {
  const char *env = std::getenv("GIT_COMMIT");
  if (env != nullptr && env[0] != '\0') {
    return env;
  }
  return "local-dev";
}

struct EmittedBenchmark {
  std::string Id;
  std::string Payload;
  bool Passed{false};
};

auto EmitHalfedgeSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunHalfedgeSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \"" << EscapeJson(kHalfedgeSmokeBenchmarkId)
      << "\",\n"
      << "  \"method\": \"" << EscapeJson(kHalfedgeSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kHalfedgeSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": 0.0\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"total_area\": " << metrics.TotalArea << ",\n"
      << "    \"vertex_count\": " << metrics.VertexCount << ",\n"
      << "    \"face_count\": " << metrics.FaceCount << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kHalfedgeSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitParameterizationDiagnosticsSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunParameterizationDiagnosticsSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kParameterizationDiagnosticsSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kParameterizationDiagnosticsSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kParameterizationDiagnosticsSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": 0.0\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"mean_conformal_distortion\": "
      << metrics.MeanConformalDistortion << ",\n"
      << "    \"max_area_distortion\": " << metrics.MaxAreaDistortion << ",\n"
      << "    \"mean_stretch\": " << metrics.MeanStretch << ",\n"
      << "    \"evaluated_face_count\": " << metrics.EvaluatedFaceCount << ",\n"
      << "    \"flipped_element_count\": " << metrics.FlippedElementCount
      << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kParameterizationDiagnosticsSmokeBenchmarkId,
                          out.str(), metrics.Succeeded};
}

auto EmitUvAtlasSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunUvAtlasSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \"" << EscapeJson(kUvAtlasSmokeBenchmarkId)
      << "\",\n"
      << "  \"method\": \"" << EscapeJson(kUvAtlasSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kUvAtlasSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 4,\n"
      << "    \"baseline_method\": \"xatlas\",\n"
      << "    \"probe_method\": \"fast_staged\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"fast_runtime_ms\": " << metrics.FastRuntimeMilliseconds << ",\n"
      << "    \"xatlas_runtime_ms\": " << metrics.XAtlasRuntimeMilliseconds
      << ",\n"
      << "    \"fast_to_xatlas_runtime_ratio\": "
      << metrics.FastToXAtlasRuntimeRatio << ",\n"
      << "    \"fast_chart_count\": " << metrics.FastChartCount << ",\n"
      << "    \"xatlas_chart_count\": " << metrics.XAtlasChartCount << ",\n"
      << "    \"fast_mean_conformal_distortion\": "
      << metrics.FastMeanConformalDistortion << ",\n"
      << "    \"xatlas_mean_conformal_distortion\": "
      << metrics.XAtlasMeanConformalDistortion << ",\n"
      << "    \"fast_max_stretch\": " << metrics.FastMaxStretch << ",\n"
      << "    \"xatlas_max_stretch\": " << metrics.XAtlasMaxStretch << ",\n"
      << "    \"fast_flipped_element_count\": "
      << metrics.FastFlippedElementCount << ",\n"
      << "    \"xatlas_flipped_element_count\": "
      << metrics.XAtlasFlippedElementCount << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kUvAtlasSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitUvAtlasPromotionSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunUvAtlasPromotionSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \"" << EscapeJson(kUvAtlasPromotionBenchmarkId)
      << "\",\n"
      << "  \"method\": \"" << EscapeJson(kUvAtlasPromotionMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kUvAtlasPromotionDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << ",\n"
      << "    \"quality_error_linf\": " << metrics.QualityErrorLinf << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"promotion_smoke\",\n"
      << "    \"warmup_pairs\": " << kUvAtlasPromotionWarmupPairs
      << ",\n"
      << "    \"measured_pairs\": " << kUvAtlasPromotionMeasuredPairs
      << ",\n"
      << "    \"timing_statistic\": \""
      << EscapeJson(kUvAtlasPromotionTimingStatistic) << "\",\n"
      << "    \"backend_runtime_statistic\": \""
      << EscapeJson(kUvAtlasPromotionBackendRuntimeStatistic) << "\",\n"
      << "    \"measurement_order\": \""
      << EscapeJson(kUvAtlasPromotionMeasurementOrder) << "\",\n"
      << "    \"baseline_method\": \"xatlas\",\n"
      << "    \"probe_method\": \"fast_staged\",\n"
      << "    \"adoption_claim\": "
      << (metrics.PromotionPass ? "true" : "false") << ",\n"
      << "    \"promotion_pass\": "
      << (metrics.PromotionPass ? "true" : "false") << ",\n"
      << "    \"runtime_ratio_mean_max\": 1.0,\n"
      << "    \"runtime_ratio_per_fixture_max\": 1.25,\n"
      << "    \"conformal_regression_tolerance\": 0.25,\n"
      << "    \"stretch_regression_tolerance\": 0.05,\n"
      << "    \"fixture_count\": " << metrics.FixtureCount << ",\n"
      << "    \"passed_fixture_count\": " << metrics.PassedFixtureCount << ",\n"
      << "    \"failed_fixture_count\": " << metrics.FailedFixtureCount << ",\n"
      << "    \"mean_fast_runtime_ms\": " << metrics.MeanFastRuntimeMilliseconds
      << ",\n"
      << "    \"mean_xatlas_runtime_ms\": "
      << metrics.MeanXAtlasRuntimeMilliseconds << ",\n"
      << "    \"mean_fast_to_xatlas_runtime_ratio\": "
      << metrics.MeanFastToXAtlasRuntimeRatio << ",\n"
      << "    \"max_fast_to_xatlas_runtime_ratio\": "
      << metrics.MaxFastToXAtlasRuntimeRatio << ",\n"
      << "    \"fast_flipped_element_count_total\": "
      << metrics.FastFlippedElementCountTotal << ",\n"
      << "    \"fast_chart_overlap_count_total\": "
      << metrics.FastChartOverlapCountTotal << ",\n"
      << "    \"fixtures\": [\n";

  for (std::size_t i = 0; i < metrics.Fixtures.size(); ++i) {
    const auto &fixture = metrics.Fixtures[i];
    out << "      {\n"
        << "        \"name\": \"" << EscapeJson(fixture.Name) << "\",\n"
        << "        \"passed\": " << (fixture.Passed ? "true" : "false")
        << ",\n"
        << "        \"input_vertex_count\": " << fixture.InputVertexCount
        << ",\n"
        << "        \"input_face_count\": " << fixture.InputFaceCount << ",\n"
        << "        \"fast_succeeded\": "
        << (fixture.FastSucceeded ? "true" : "false") << ",\n"
        << "        \"xatlas_succeeded\": "
        << (fixture.XAtlasSucceeded ? "true" : "false") << ",\n"
        << "        \"fast_used_fallback\": "
        << (fixture.FastUsedFallback ? "true" : "false") << ",\n"
        << "        \"fast_finite_normalized\": "
        << (fixture.FastFiniteNormalized ? "true" : "false") << ",\n"
        << "        \"fast_runtime_ms\": " << fixture.FastRuntimeMilliseconds
        << ",\n"
        << "        \"xatlas_runtime_ms\": "
        << fixture.XAtlasRuntimeMilliseconds << ",\n"
        << "        \"fast_to_xatlas_runtime_ratio\": "
        << fixture.FastToXAtlasRuntimeRatio << ",\n"
        << "        \"fast_runtime_samples_ms\": ";
    EmitDoubleSamples(out, fixture.FastRuntimeSamplesMilliseconds);
    out << ",\n"
        << "        \"xatlas_runtime_samples_ms\": ";
    EmitDoubleSamples(out, fixture.XAtlasRuntimeSamplesMilliseconds);
    out << ",\n"
        << "        \"paired_runtime_ratio_samples\": ";
    EmitDoubleSamples(out, fixture.PairedRuntimeRatios);
    out << ",\n"
        << "        \"conformal_regression\": " << fixture.ConformalRegression
        << ",\n"
        << "        \"stretch_regression\": " << fixture.StretchRegression
        << ",\n"
        << "        \"fast_output_vertex_count\": "
        << fixture.FastOutputVertexCount << ",\n"
        << "        \"xatlas_output_vertex_count\": "
        << fixture.XAtlasOutputVertexCount << ",\n"
        << "        \"fast_output_face_count\": " << fixture.FastOutputFaceCount
        << ",\n"
        << "        \"xatlas_output_face_count\": "
        << fixture.XAtlasOutputFaceCount << ",\n"
        << "        \"fast_chart_count\": " << fixture.FastChartCount << ",\n"
        << "        \"xatlas_chart_count\": " << fixture.XAtlasChartCount
        << ",\n"
        << "        \"fast_flipped_element_count\": "
        << fixture.FastFlippedElementCount << ",\n"
        << "        \"xatlas_flipped_element_count\": "
        << fixture.XAtlasFlippedElementCount << ",\n"
        << "        \"fast_chart_overlap_count\": "
        << fixture.FastChartOverlapCount << ",\n"
        << "        \"fast_mean_conformal_distortion\": "
        << fixture.FastMeanConformalDistortion << ",\n"
        << "        \"xatlas_mean_conformal_distortion\": "
        << fixture.XAtlasMeanConformalDistortion << ",\n"
        << "        \"fast_max_stretch\": " << fixture.FastMaxStretch << ",\n"
        << "        \"xatlas_max_stretch\": " << fixture.XAtlasMaxStretch
        << ",\n"
        << "        \"fast_packing_utilization\": "
        << fixture.FastPackingUtilization << "\n"
        << "      }" << (i + 1u == metrics.Fixtures.size() ? "\n" : ",\n");
  }

  out << "    ]\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.PromotionPass ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kUvAtlasPromotionBenchmarkId, out.str(),
                          metrics.PromotionPass};
}

auto EmitProgressivePoissonReferenceSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunProgressivePoissonReferenceSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kProgressivePoissonReferenceSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kProgressivePoissonReferenceSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kProgressivePoissonReferenceSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"poisson_ratio_min\": " << metrics.PoissonRatioMin << ",\n"
      << "    \"coverage_fraction\": " << metrics.CoverageFraction << ",\n"
      << "    \"accepted_count\": " << metrics.AcceptedCount << ",\n"
      << "    \"level_count\": " << metrics.LevelCount << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kProgressivePoissonReferenceSmokeBenchmarkId,
                          out.str(), metrics.Succeeded};
}

auto EmitSignedHeatReferenceSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunSignedHeatReferenceSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kSignedHeatReferenceSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kSignedHeatReferenceSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kSignedHeatReferenceSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"source_vertex_count\": " << metrics.SourceVertexCount << ",\n"
      << "    \"degenerate_boundary_vertex_count\": "
      << metrics.DegenerateBoundaryVertexCount << ",\n"
      << "    \"max_abs_distance\": " << metrics.MaxAbsDistance << ",\n"
      << "    \"mean_boundary_offset\": " << metrics.MeanBoundaryOffset << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kSignedHeatReferenceSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitSurfaceSamplingSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunSurfaceSamplingSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kSurfaceSamplingSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kSurfaceSamplingSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kSurfaceSamplingSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"sample_count\": " << metrics.WrittenSampleCount << ",\n"
      << "    \"accepted_triangle_count\": " << metrics.AcceptedTriangleCount
      << ",\n"
      << "    \"small_triangle_fraction\": " << metrics.SmallTriangleFraction
      << ",\n"
      << "    \"expected_small_triangle_fraction\": "
      << metrics.ExpectedSmallTriangleFraction << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kSurfaceSamplingSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitQualityMetricsSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunQualityMetricsSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \"" << EscapeJson(kQualityMetricsSmokeBenchmarkId)
      << "\",\n"
      << "  \"method\": \"" << EscapeJson(kQualityMetricsSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kQualityMetricsSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"point_count\": " << metrics.PointCount << ",\n"
      << "    \"nearest_neighbor_cv\": " << metrics.NearestNeighborCv << ",\n"
      << "    \"poisson_ratio\": " << metrics.PoissonRatio << ",\n"
      << "    \"rdf_mean_away_from_zero\": " << metrics.RdfMeanAwayFromZero
      << ",\n"
      << "    \"raps_cv\": " << metrics.RapsCv << ",\n"
      << "    \"coverage_fraction\": " << metrics.CoverageFraction << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kQualityMetricsSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitPointCloudFilteringSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Geometry;

  const auto metrics = RunPointCloudFilteringSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kPointCloudFilteringSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kPointCloudFilteringSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kPointCloudFilteringSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"input_count\": " << metrics.InputCount << ",\n"
      << "    \"injected_outliers\": " << metrics.InjectedOutliers << ",\n"
      << "    \"voxel_reduced_count\": " << metrics.VoxelReducedCount << ",\n"
      << "    \"statistical_kept\": " << metrics.StatisticalKept << ",\n"
      << "    \"statistical_rejected\": " << metrics.StatisticalRejected
      << ",\n"
      << "    \"radius_kept\": " << metrics.RadiusKept << ",\n"
      << "    \"radius_rejected\": " << metrics.RadiusRejected << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kPointCloudFilteringSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitRigidBodyReferenceSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Physics;

  const auto metrics = RunRigidBodyReferenceSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kRigidBodyReferenceSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kRigidBodyReferenceSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kRigidBodyReferenceSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"contact_count\": " << metrics.ContactCount << ",\n"
      << "    \"unsupported_pair_count\": " << metrics.UnsupportedPairCount
      << ",\n"
      << "    \"final_velocity_a\": " << metrics.FinalVelocityA << ",\n"
      << "    \"final_velocity_b\": " << metrics.FinalVelocityB << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kRigidBodyReferenceSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitParticleSpringReferenceSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Physics;

  const auto metrics = RunParticleSpringReferenceSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kParticleSpringReferenceSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kParticleSpringReferenceSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kParticleSpringReferenceSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"degenerate_spring_count\": " << metrics.DegenerateSpringCount
      << ",\n"
      << "    \"max_spring_residual\": " << metrics.MaxSpringResidual << ",\n"
      << "    \"energy_drift\": " << metrics.EnergyDrift << ",\n"
      << "    \"max_stiffness_dt_ratio\": " << metrics.MaxStiffnessDtRatio
      << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kParticleSpringReferenceSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitXpbdClothReferenceSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Physics;

  const auto metrics = RunXpbdClothReferenceSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kXpbdClothReferenceSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kXpbdClothReferenceSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kXpbdClothReferenceSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 8,\n"
      << "    \"max_bend_residual\": " << metrics.MaxBendResidual << ",\n"
      << "    \"degenerate_triangle_count\": "
      << metrics.DegenerateTriangleCount << ",\n"
      << "    \"degenerate_constraint_count\": "
      << metrics.DegenerateConstraintCount << ",\n"
      << "    \"converged\": " << (metrics.Converged ? "true" : "false") << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kXpbdClothReferenceSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitSphFluidReferenceSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Physics;

  const auto metrics = RunSphFluidReferenceSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kSphFluidReferenceSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kSphFluidReferenceSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kSphFluidReferenceSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 4,\n"
      << "    \"column_max_compression\": " << metrics.ColumnMaxCompression
      << ",\n"
      << "    \"column_avg_density_error\": "
      << metrics.ColumnAverageDensityError << ",\n"
      << "    \"column_max_neighbor_count\": " << metrics.ColumnMaxNeighborCount
      << ",\n"
      << "    \"column_stable\": " << (metrics.ColumnStable ? "true" : "false")
      << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kSphFluidReferenceSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitVertexFetchLayoutSmoke(const std::string &commit) -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Rendering;

  const auto metrics = RunVertexFetchLayoutSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kVertexFetchLayoutSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \"" << EscapeJson(kVertexFetchLayoutSmokeMethod)
      << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \"" << EscapeJson(kVertexFetchLayoutSmokeDataset)
      << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"throughput_items_per_sec\": "
      << metrics.ThroughputItemsPerSecond << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": 1,\n"
      << "    \"measured_iterations\": 6,\n"
      << "    \"baseline_layout\": \"uniform_soa\",\n"
      << "    \"probe_layout\": \"interleaved_aos\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"soa_runtime_ms\": " << metrics.SoaRuntimeMilliseconds << ",\n"
      << "    \"interleaved_runtime_ms\": "
      << metrics.InterleavedRuntimeMilliseconds << ",\n"
      << "    \"interleaved_to_soa_runtime_ratio\": "
      << metrics.InterleavedToSoaRuntimeRatio << ",\n"
      << "    \"vertex_count\": " << metrics.VertexCount << ",\n"
      << "    \"index_count\": " << metrics.IndexCount << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kVertexFetchLayoutSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitFramegraphBarrierEmissionSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Rendering;

  const auto metrics = RunFramegraphBarrierEmissionSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kFramegraphBarrierEmissionSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kFramegraphBarrierEmissionSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kFramegraphBarrierEmissionSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": " << metrics.WarmupIterations << ",\n"
      << "    \"measured_iterations\": " << metrics.MeasuredIterations << ",\n"
      << "    \"baseline_mode\": \"legacy_full_scan\",\n"
      << "    \"probe_mode\": \"indexed_range_lookup\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"legacy_full_scan_ms\": " << metrics.LegacyFullScanMilliseconds << ",\n"
      << "    \"indexed_range_ms\": " << metrics.IndexedRangeMilliseconds << ",\n"
      << "    \"pass_count\": " << metrics.PassCount << ",\n"
      << "    \"barrier_packet_count\": " << metrics.BarrierPacketCount << ",\n"
      << "    \"legacy_packet_comparisons\": "
      << metrics.LegacyPacketComparisons << ",\n"
      << "    \"indexed_range_packet_visits\": "
      << metrics.IndexedRangePacketVisits << ",\n"
      << "    \"texture_barrier_visits\": "
      << metrics.TextureBarrierVisits << ",\n"
      << "    \"buffer_barrier_visits\": "
      << metrics.BufferBarrierVisits << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kFramegraphBarrierEmissionSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitFramegraphCompilerIndexingSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Rendering;

  const auto metrics = RunFramegraphCompilerIndexingSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kFramegraphCompilerIndexingSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kFramegraphCompilerIndexingSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kFramegraphCompilerIndexingSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": " << metrics.WarmupIterations << ",\n"
      << "    \"measured_iterations\": " << metrics.MeasuredIterations << ",\n"
      << "    \"baseline_mode\": \"legacy_nested_scan_and_linear_packet_insert\",\n"
      << "    \"probe_mode\": \"sorted_pass_ids_and_indexed_packet_insert\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"legacy_scan_ms\": " << metrics.LegacyScanMilliseconds << ",\n"
      << "    \"indexed_ms\": " << metrics.IndexedMilliseconds << ",\n"
      << "    \"pass_count\": " << metrics.PassCount << ",\n"
      << "    \"request_count\": " << metrics.RequestCount << ",\n"
      << "    \"barrier_packet_count\": " << metrics.BarrierPacketCount << ",\n"
      << "    \"legacy_pass_id_comparisons\": "
      << metrics.LegacyPassIdComparisons << ",\n"
      << "    \"indexed_pass_id_comparisons\": "
      << metrics.IndexedPassIdComparisons << ",\n"
      << "    \"legacy_packet_comparisons\": "
      << metrics.LegacyPacketComparisons << ",\n"
      << "    \"indexed_packet_lookups\": " << metrics.IndexedPacketLookups
      << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kFramegraphCompilerIndexingSmokeBenchmarkId,
                          out.str(), metrics.Succeeded};
}

auto EmitFramegraphScratchReuseSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Rendering;

  const auto metrics = RunFramegraphScratchReuseSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kFramegraphScratchReuseSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kFramegraphScratchReuseSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kFramegraphScratchReuseSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": " << metrics.WarmupIterations << ",\n"
      << "    \"measured_iterations\": " << metrics.MeasuredIterations << ",\n"
      << "    \"baseline_mode\": \"fresh_graph_rebuild\",\n"
      << "    \"probe_mode\": \"reset_redeclare_reuse\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"fresh_declare_compile_ms\": "
      << metrics.FreshDeclareCompileMilliseconds << ",\n"
      << "    \"reused_declare_compile_ms\": "
      << metrics.ReusedDeclareCompileMilliseconds << ",\n"
      << "    \"pass_count\": " << metrics.PassCount << ",\n"
      << "    \"resource_count\": " << metrics.ResourceCount << ",\n"
      << "    \"barrier_packet_count\": " << metrics.BarrierPacketCount
      << ",\n"
      << "    \"fresh_declare_allocations\": "
      << metrics.FreshDeclareAllocations << ",\n"
      << "    \"reused_declare_allocations\": "
      << metrics.ReusedDeclareAllocations << ",\n"
      << "    \"fresh_declare_bytes\": " << metrics.FreshDeclareBytes
      << ",\n"
      << "    \"reused_declare_bytes\": " << metrics.ReusedDeclareBytes
      << ",\n"
      << "    \"fresh_declare_compile_allocations\": "
      << metrics.FreshDeclareCompileAllocations << ",\n"
      << "    \"reused_declare_compile_allocations\": "
      << metrics.ReusedDeclareCompileAllocations << ",\n"
      << "    \"fresh_declare_compile_bytes\": "
      << metrics.FreshDeclareCompileBytes << ",\n"
      << "    \"reused_declare_compile_bytes\": "
      << metrics.ReusedDeclareCompileBytes << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kFramegraphScratchReuseSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitFrameRecipeCompileCacheSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Rendering;

  const auto metrics = RunFrameRecipeCompileCacheSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kFrameRecipeCompileCacheSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kFrameRecipeCompileCacheSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kFrameRecipeCompileCacheSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": " << metrics.WarmupIterations << ",\n"
      << "    \"measured_iterations\": " << metrics.MeasuredIterations << ",\n"
      << "    \"baseline_mode\": \"rebuild_each_frame\",\n"
      << "    \"probe_mode\": \"cached_steady_state\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"baseline_rebuild_declare_compile_ms\": "
      << metrics.BaselineRebuildDeclareCompileMilliseconds << ",\n"
      << "    \"cached_steady_state_declare_compile_ms\": "
      << metrics.CachedSteadyStateDeclareCompileMilliseconds << ",\n"
      << "    \"avoided_declare_compile_ms\": "
      << metrics.AvoidedDeclareCompileMilliseconds << ",\n"
      << "    \"baseline_compile_attempts_per_frame\": "
      << metrics.BaselineCompileAttemptsPerFrame << ",\n"
      << "    \"cached_compile_attempts_per_frame\": "
      << metrics.CachedCompileAttemptsPerFrame << ",\n"
      << "    \"pass_count\": " << metrics.PassCount << ",\n"
      << "    \"resource_count\": " << metrics.ResourceCount << ",\n"
      << "    \"barrier_count\": " << metrics.BarrierCount << ",\n"
      << "    \"validation_error_count\": "
      << metrics.ValidationErrorCount << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kFrameRecipeCompileCacheSmokeBenchmarkId, out.str(),
                          metrics.Succeeded};
}

auto EmitRenderGraphParallelRecordingSmoke(const std::string &commit)
    -> EmittedBenchmark {
  using namespace Intrinsic::Bench::Rendering;

  const auto metrics = RunRenderGraphParallelRecordingSmoke();

  std::ostringstream out;
  out.setf(std::ios::fixed);
  out.precision(6);
  out << "{\n"
      << "  \"benchmark_id\": \""
      << EscapeJson(kRenderGraphParallelRecordingSmokeBenchmarkId) << "\",\n"
      << "  \"method\": \""
      << EscapeJson(kRenderGraphParallelRecordingSmokeMethod) << "\",\n"
      << "  \"backend\": \"cpu_reference\",\n"
      << "  \"dataset\": \""
      << EscapeJson(kRenderGraphParallelRecordingSmokeDataset) << "\",\n"
      << "  \"commit\": \"" << EscapeJson(commit) << "\",\n"
      << "  \"metrics\": {\n"
      << "    \"runtime_ms\": " << metrics.RuntimeMilliseconds << ",\n"
      << "    \"quality_error_l2\": " << metrics.QualityErrorL2 << "\n"
      << "  },\n"
      << "  \"diagnostics\": {\n"
      << "    \"runner\": \"IntrinsicBenchmarkSmoke\",\n"
      << "    \"mode\": \"smoke\",\n"
      << "    \"warmup_iterations\": " << metrics.WarmupIterations << ",\n"
      << "    \"measured_iterations\": " << metrics.MeasuredIterations << ",\n"
      << "    \"baseline_mode\": \"serial_execute_recording\",\n"
      << "    \"probe_mode\": \"scheduler_parallel_record_join\",\n"
      << "    \"adoption_claim\": false,\n"
      << "    \"serial_record_ms\": " << metrics.SerialRecordMilliseconds
      << ",\n"
      << "    \"parallel_record_ms\": "
      << metrics.ParallelRecordMilliseconds << ",\n"
      << "    \"parallel_to_serial_runtime_ratio\": "
      << metrics.ParallelToSerialRuntimeRatio << ",\n"
      << "    \"pass_count\": " << metrics.PassCount << ",\n"
      << "    \"scheduler_worker_count\": " << metrics.SchedulerWorkerCount
      << ",\n"
      << "    \"record_ops_per_pass\": " << metrics.RecordOpsPerPass
      << ",\n"
      << "    \"parallel_layer_count\": " << metrics.ParallelLayerCount
      << ",\n"
      << "    \"parallel_max_layer_width\": "
      << metrics.ParallelMaxLayerWidth << ",\n"
      << "    \"parallel_worker_task_count\": "
      << metrics.ParallelWorkerTaskCount << ",\n"
      << "    \"parallel_caller_record_count\": "
      << metrics.ParallelCallerRecordCount << ",\n"
      << "    \"serial_checksum\": " << metrics.SerialChecksum << ",\n"
      << "    \"parallel_checksum\": " << metrics.ParallelChecksum << "\n"
      << "  },\n"
      << "  \"status\": \"" << (metrics.Succeeded ? "passed" : "failed")
      << "\"\n"
      << "}\n";

  return EmittedBenchmark{kRenderGraphParallelRecordingSmokeBenchmarkId,
                          out.str(), metrics.Succeeded};
}

auto WriteFile(const std::filesystem::path &path, std::string_view payload)
    -> bool {
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    std::cerr << "Failed to open output file: " << path << '\n';
    return false;
  }
  file << payload;
  return file.good();
}
} // namespace

auto main(int argc, char **argv) -> int {
  const std::filesystem::path outArg =
      argc > 1 ? std::filesystem::path(argv[1])
               : std::filesystem::path("benchmark_smoke_result.json");

  const std::string commit = ResolveCommit();

  std::vector<EmittedBenchmark> emitted;
  emitted.push_back(EmitHalfedgeSmoke(commit));
  emitted.push_back(EmitParameterizationDiagnosticsSmoke(commit));
  emitted.push_back(EmitUvAtlasSmoke(commit));
  emitted.push_back(EmitUvAtlasPromotionSmoke(commit));
  emitted.push_back(EmitProgressivePoissonReferenceSmoke(commit));
  emitted.push_back(EmitSignedHeatReferenceSmoke(commit));
  emitted.push_back(EmitSurfaceSamplingSmoke(commit));
  emitted.push_back(EmitQualityMetricsSmoke(commit));
  emitted.push_back(EmitPointCloudFilteringSmoke(commit));
  emitted.push_back(EmitRigidBodyReferenceSmoke(commit));
  emitted.push_back(EmitParticleSpringReferenceSmoke(commit));
  emitted.push_back(EmitXpbdClothReferenceSmoke(commit));
  emitted.push_back(EmitSphFluidReferenceSmoke(commit));
  emitted.push_back(EmitFramegraphBarrierEmissionSmoke(commit));
  emitted.push_back(EmitFramegraphCompilerIndexingSmoke(commit));
  emitted.push_back(EmitFramegraphScratchReuseSmoke(commit));
  emitted.push_back(EmitFrameRecipeCompileCacheSmoke(commit));
  emitted.push_back(EmitRenderGraphParallelRecordingSmoke(commit));
  emitted.push_back(EmitVertexFetchLayoutSmoke(commit));

  // Output target: an existing directory or a path with no extension (or no
  // filename component) is treated as a directory and gets one JSON per
  // benchmark inside; create it eagerly so a fresh build tree behaves the
  // same as a populated one. A path with an extension is treated as an
  // explicit file (preserving the single-artifact CI path); extras land as
  // siblings keyed by id.
  std::error_code dirEc;
  const bool isExistingDir = std::filesystem::is_directory(outArg, dirEc);
  const bool looksLikeDir = !outArg.has_filename() || !outArg.has_extension();
  const bool isDirectoryArg = isExistingDir || looksLikeDir;
  if (looksLikeDir && !isExistingDir) {
    std::filesystem::create_directories(outArg, dirEc);
  }

  int exitCode = 0;
  bool anyNotPassed = false;
  for (std::size_t i = 0; i < emitted.size(); ++i) {
    std::filesystem::path target;
    if (isDirectoryArg) {
      target = outArg / (emitted[i].Id + ".json");
    } else if (i == 0) {
      target = outArg;
    } else {
      target = outArg.parent_path() / (emitted[i].Id + ".json");
    }

    if (!WriteFile(target, emitted[i].Payload)) {
      exitCode = 1;
      continue;
    }
    std::cout << "Wrote benchmark smoke result: " << target << '\n';

    if (!emitted[i].Passed) {
      anyNotPassed = true;
      std::cerr << "Benchmark reported non-passed status: " << emitted[i].Id
                << '\n';
    }
  }

  // The result JSON itself remains schema-valid for downstream consumers
  // even when the workload failed (status="failed" is a valid value), but
  // the runner exit code must reflect failure so CTest/CI gates do not pass
  // silently on a broken smoke benchmark.
  if (anyNotPassed && exitCode == 0) {
    exitCode = 2;
  }
  return exitCode;
}
