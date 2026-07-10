// GEOM-057 - deterministic UV atlas smoke and promotion benchmarks.

#include "Bench.UvAtlasSmoke.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/glm.hpp>

import Geometry.MeshSoup;
import Geometry.Parameterization.Diagnostics;
import Geometry.UvAtlas;

namespace Intrinsic::Bench::Geometry {
namespace {
constexpr int kSmokeWarmupIterations = 1;
constexpr int kSmokeMeasuredIterations = 4;

constexpr double kRuntimeRatioMeanMax = 1.0;
constexpr double kRuntimeRatioPerFixtureMax = 1.25;
constexpr double kConformalRegressionTolerance = 0.25;
constexpr double kStretchRegressionTolerance = 0.05;
constexpr float kUvBoundsTolerance = 1.0e-5f;

using IndexedMesh = ::Geometry::MeshSoup::IndexedMesh;
using UvAtlasDiagnostics = ::Geometry::UvAtlas::UvAtlasDiagnostics;
using UvAtlasMethod = ::Geometry::UvAtlas::UvAtlasMethod;
using UvAtlasOptions = ::Geometry::UvAtlas::UvAtlasOptions;
using UvAtlasResult = ::Geometry::UvAtlas::UvAtlasResult;

struct Fixture {
  std::string Name{};
  IndexedMesh Mesh{};
};

struct TickResult {
  UvAtlasDiagnostics Diagnostics{};
  UvAtlasResult Result{};
  bool Succeeded{false};
};

struct TimedRun {
  TickResult Last{};
  double MeanRuntimeMilliseconds{0.0};
};

struct TimedTick {
  TickResult Result{};
  double RuntimeMilliseconds{0.0};
};

struct PairedTimedRun {
  TickResult FastLast{};
  TickResult XAtlasLast{};
  std::array<double, kUvAtlasPromotionMeasuredPairs>
      FastRuntimeSamplesMilliseconds{};
  std::array<double, kUvAtlasPromotionMeasuredPairs>
      XAtlasRuntimeSamplesMilliseconds{};
  std::array<double, kUvAtlasPromotionMeasuredPairs> RuntimeRatios{};
  double FastMedianRuntimeMilliseconds{0.0};
  double XAtlasMedianRuntimeMilliseconds{0.0};
  double MedianRuntimeRatio{0.0};
};

[[nodiscard]] IndexedMesh MakeSquareGridFixture(const std::uint32_t columns,
                                                const std::uint32_t rows,
                                                const float width,
                                                const float height) {
  IndexedMesh mesh;
  std::vector<std::uint32_t> vertices;
  vertices.reserve(static_cast<std::size_t>(columns + 1u) *
                   static_cast<std::size_t>(rows + 1u));
  for (std::uint32_t y = 0; y <= rows; ++y) {
    for (std::uint32_t x = 0; x <= columns; ++x) {
      const float px =
          (static_cast<float>(x) / static_cast<float>(columns) - 0.5f) * width;
      const float py =
          (static_cast<float>(y) / static_cast<float>(rows) - 0.5f) * height;
      vertices.push_back(mesh.AddVertex({px, py, 0.0f}));
    }
  }

  auto vertex = [&](const std::uint32_t x, const std::uint32_t y) {
    return vertices[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(columns + 1u) +
                    static_cast<std::size_t>(x)];
  };

  for (std::uint32_t y = 0; y < rows; ++y) {
    for (std::uint32_t x = 0; x < columns; ++x) {
      const auto v00 = vertex(x, y);
      const auto v10 = vertex(x + 1u, y);
      const auto v01 = vertex(x, y + 1u);
      const auto v11 = vertex(x + 1u, y + 1u);
      (void)mesh.AddTriangle(v00, v10, v11);
      (void)mesh.AddTriangle(v00, v11, v01);
    }
  }
  return mesh;
}

[[nodiscard]] IndexedMesh MakeCubeSurfaceFixture() {
  IndexedMesh mesh;
  const auto v0 = mesh.AddVertex({-1.0f, -1.0f, -1.0f});
  const auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
  const auto v2 = mesh.AddVertex({1.0f, 1.0f, -1.0f});
  const auto v3 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
  const auto v4 = mesh.AddVertex({-1.0f, -1.0f, 1.0f});
  const auto v5 = mesh.AddVertex({1.0f, -1.0f, 1.0f});
  const auto v6 = mesh.AddVertex({1.0f, 1.0f, 1.0f});
  const auto v7 = mesh.AddVertex({-1.0f, 1.0f, 1.0f});

  (void)mesh.AddTriangle(v0, v2, v1);
  (void)mesh.AddTriangle(v0, v3, v2);
  (void)mesh.AddTriangle(v4, v5, v6);
  (void)mesh.AddTriangle(v4, v6, v7);
  (void)mesh.AddTriangle(v0, v4, v7);
  (void)mesh.AddTriangle(v0, v7, v3);
  (void)mesh.AddTriangle(v1, v2, v6);
  (void)mesh.AddTriangle(v1, v6, v5);
  (void)mesh.AddTriangle(v0, v1, v5);
  (void)mesh.AddTriangle(v0, v5, v4);
  (void)mesh.AddTriangle(v2, v3, v7);
  (void)mesh.AddTriangle(v2, v7, v6);
  return mesh;
}

[[nodiscard]] IndexedMesh MakeOpenCylinderFixture(const std::uint32_t slices) {
  IndexedMesh mesh;
  std::vector<std::uint32_t> bottom;
  std::vector<std::uint32_t> top;
  bottom.reserve(slices);
  top.reserve(slices);
  for (std::uint32_t i = 0; i < slices; ++i) {
    const float angle = (static_cast<float>(i) / static_cast<float>(slices)) *
                        2.0f * static_cast<float>(std::numbers::pi);
    const float x = std::cos(angle);
    const float z = std::sin(angle);
    bottom.push_back(mesh.AddVertex({x, -1.0f, z}));
    top.push_back(mesh.AddVertex({x, 1.0f, z}));
  }

  for (std::uint32_t i = 0; i < slices; ++i) {
    const std::uint32_t next = (i + 1u) % slices;
    (void)mesh.AddTriangle(bottom[i], bottom[next], top[next]);
    (void)mesh.AddTriangle(bottom[i], top[next], top[i]);
  }
  return mesh;
}

[[nodiscard]] IndexedMesh MakeOctahedronFixture() {
  IndexedMesh mesh;
  const auto top = mesh.AddVertex({0.0f, 1.0f, 0.0f});
  const auto bottom = mesh.AddVertex({0.0f, -1.0f, 0.0f});
  const auto xp = mesh.AddVertex({1.0f, 0.0f, 0.0f});
  const auto zp = mesh.AddVertex({0.0f, 0.0f, 1.0f});
  const auto xn = mesh.AddVertex({-1.0f, 0.0f, 0.0f});
  const auto zn = mesh.AddVertex({0.0f, 0.0f, -1.0f});

  (void)mesh.AddTriangle(top, xp, zp);
  (void)mesh.AddTriangle(top, zp, xn);
  (void)mesh.AddTriangle(top, xn, zn);
  (void)mesh.AddTriangle(top, zn, xp);
  (void)mesh.AddTriangle(bottom, zp, xp);
  (void)mesh.AddTriangle(bottom, xn, zp);
  (void)mesh.AddTriangle(bottom, zn, xn);
  (void)mesh.AddTriangle(bottom, xp, zn);
  return mesh;
}

[[nodiscard]] IndexedMesh
MakeHighValenceFanFixture(const std::uint32_t slices) {
  IndexedMesh mesh;
  const auto center = mesh.AddVertex({0.0f, 0.0f, 0.0f});
  std::vector<std::uint32_t> ring;
  ring.reserve(slices);
  for (std::uint32_t i = 0; i < slices; ++i) {
    const float angle = (static_cast<float>(i) / static_cast<float>(slices)) *
                        2.0f * static_cast<float>(std::numbers::pi);
    ring.push_back(mesh.AddVertex({std::cos(angle), std::sin(angle), 0.0f}));
  }
  for (std::uint32_t i = 0; i < slices; ++i) {
    (void)mesh.AddTriangle(center, ring[i], ring[(i + 1u) % slices]);
  }
  return mesh;
}

[[nodiscard]] IndexedMesh MakeDisconnectedQuadsFixture() {
  IndexedMesh mesh;
  const auto addQuad = [&](const float xOffset) {
    const auto v0 = mesh.AddVertex({xOffset + 0.0f, 0.0f, 0.0f});
    const auto v1 = mesh.AddVertex({xOffset + 1.0f, 0.0f, 0.0f});
    const auto v2 = mesh.AddVertex({xOffset + 1.0f, 1.0f, 0.0f});
    const auto v3 = mesh.AddVertex({xOffset + 0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
  };
  addQuad(-2.0f);
  addQuad(1.0f);
  return mesh;
}

[[nodiscard]] std::vector<Fixture> MakePromotionFixtures() {
  std::vector<Fixture> fixtures;
  fixtures.push_back(
      Fixture{.Name = "planar_grid_4x4",
              .Mesh = MakeSquareGridFixture(4u, 4u, 2.0f, 2.0f)});
  fixtures.push_back(
      Fixture{.Name = "thin_strip_12x1",
              .Mesh = MakeSquareGridFixture(12u, 1u, 4.0f, 0.4f)});
  fixtures.push_back(
      Fixture{.Name = "cube_surface", .Mesh = MakeCubeSurfaceFixture()});
  fixtures.push_back(Fixture{.Name = "open_cylinder_12",
                             .Mesh = MakeOpenCylinderFixture(12u)});
  fixtures.push_back(Fixture{.Name = "octahedron_sphere_proxy",
                             .Mesh = MakeOctahedronFixture()});
  fixtures.push_back(Fixture{.Name = "high_valence_fan_16",
                             .Mesh = MakeHighValenceFanFixture(16u)});
  fixtures.push_back(Fixture{.Name = "disconnected_quads",
                             .Mesh = MakeDisconnectedQuadsFixture()});
  return fixtures;
}

[[nodiscard]] UvAtlasOptions MakeOptions(const UvAtlasMethod method) {
  UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.ForceRegenerate = true;
  options.Method = method;
  options.AllowXAtlasFallback = false;
  options.Resolution = 256u;
  options.Padding = 2u;
  return options;
}

[[nodiscard]] bool HasSuccessfulQuality(const UvAtlasDiagnostics &diagnostics) {
  return diagnostics.Quality.Status ==
             ::Geometry::Parameterization::ParameterizationDiagnosticsStatus::
                 Success &&
         diagnostics.Quality.HasUsableFaces() &&
         !diagnostics.Quality.HasInvalidInput();
}

[[nodiscard]] TickResult Tick(const IndexedMesh &mesh,
                              const UvAtlasMethod method) {
  const UvAtlasOptions options = MakeOptions(method);
  UvAtlasResult result = ::Geometry::UvAtlas::ResolveUvAtlas(
      ::Geometry::UvAtlas::BorrowInput(mesh), options);

  TickResult tick{};
  tick.Diagnostics = result.Diagnostics;
  tick.Succeeded =
      result.Status == ::Geometry::UvAtlas::UvAtlasStatus::Success &&
      HasSuccessfulQuality(result.Diagnostics) &&
      result.Diagnostics.ChartCount > 0u &&
      result.OutputMesh.VertexCount() > 0u &&
      result.OutputMesh.FaceCount() == mesh.FaceCount();
  tick.Result = std::move(result);
  return tick;
}

[[nodiscard]] TimedRun MeanRuntimeMilliseconds(const IndexedMesh &mesh,
                                               const UvAtlasMethod method,
                                               const int measuredIterations) {
  TimedRun run{};
  const auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < measuredIterations; ++i) {
    run.Last = Tick(mesh, method);
  }
  const auto t1 = std::chrono::steady_clock::now();
  const auto totalNs =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  run.MeanRuntimeMilliseconds =
      (static_cast<double>(totalNs) / static_cast<double>(measuredIterations)) *
      1.0e-6;
  return run;
}

[[nodiscard]] TimedTick TimeTick(const IndexedMesh &mesh,
                                 const UvAtlasMethod method) {
  const auto t0 = std::chrono::steady_clock::now();
  TickResult result = Tick(mesh, method);
  const auto t1 = std::chrono::steady_clock::now();
  const auto elapsed =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  return TimedTick{
      .Result = std::move(result),
      .RuntimeMilliseconds = static_cast<double>(elapsed) * 1.0e-6,
  };
}

[[nodiscard]] constexpr double MedianOfFive(
    std::array<double, kUvAtlasPromotionMeasuredPairs> samples) {
  static_assert(kUvAtlasPromotionMeasuredPairs == 5u);
  for (std::size_t i = 0; i < samples.size(); ++i) {
    for (std::size_t j = i + 1u; j < samples.size(); ++j) {
      if (samples[j] < samples[i]) {
        const double tmp = samples[i];
        samples[i] = samples[j];
        samples[j] = tmp;
      }
    }
  }
  return samples[samples.size() / 2u];
}

// BUG-080: the strict threshold must ignore a minority of isolated scheduler
// stalls but still reject a sustained majority slowdown.
static_assert(MedianOfFive({0.50, 0.55, 0.60, 20.0, 30.0}) == 0.60);
static_assert(MedianOfFive({0.50, 0.55, 2.00, 2.50, 3.00}) >
              kRuntimeRatioPerFixtureMax);

[[nodiscard]] PairedTimedRun
MeasurePromotionRuntime(const IndexedMesh &mesh) {
  PairedTimedRun run{};
  for (std::size_t pairIndex = 0;
       pairIndex < kUvAtlasPromotionMeasuredPairs;
       ++pairIndex) {
    TimedTick fast{};
    TimedTick xatlas{};
    if ((pairIndex % 2u) == 0u) {
      fast = TimeTick(mesh, UvAtlasMethod::FastStaged);
      xatlas = TimeTick(mesh, UvAtlasMethod::XAtlas);
    } else {
      xatlas = TimeTick(mesh, UvAtlasMethod::XAtlas);
      fast = TimeTick(mesh, UvAtlasMethod::FastStaged);
    }

    run.FastRuntimeSamplesMilliseconds[pairIndex] = fast.RuntimeMilliseconds;
    run.XAtlasRuntimeSamplesMilliseconds[pairIndex] =
        xatlas.RuntimeMilliseconds;
    run.RuntimeRatios[pairIndex] =
        xatlas.RuntimeMilliseconds > 0.0
            ? fast.RuntimeMilliseconds / xatlas.RuntimeMilliseconds
            : std::numeric_limits<double>::infinity();
    run.FastLast = std::move(fast.Result);
    run.XAtlasLast = std::move(xatlas.Result);
  }

  run.FastMedianRuntimeMilliseconds =
      MedianOfFive(run.FastRuntimeSamplesMilliseconds);
  run.XAtlasMedianRuntimeMilliseconds =
      MedianOfFive(run.XAtlasRuntimeSamplesMilliseconds);
  run.MedianRuntimeRatio = MedianOfFive(run.RuntimeRatios);
  return run;
}

[[nodiscard]] bool IsFiniteNormalizedUv(const glm::vec2 uv) noexcept {
  return std::isfinite(uv.x) && std::isfinite(uv.y) &&
         uv.x >= -kUvBoundsTolerance && uv.y >= -kUvBoundsTolerance &&
         uv.x <= 1.0f + kUvBoundsTolerance && uv.y <= 1.0f + kUvBoundsTolerance;
}

[[nodiscard]] bool AllOutputUvsFiniteNormalized(const UvAtlasResult &result) {
  const auto outputUvs =
      result.OutputMesh.GetVertexProperty<glm::vec2>("v:texcoord");
  if (!outputUvs.IsValid() ||
      outputUvs.Vector().size() != result.OutputMesh.VertexCount()) {
    return false;
  }
  return std::all_of(outputUvs.Vector().begin(), outputUvs.Vector().end(),
                     IsFiniteNormalizedUv);
}

[[nodiscard]] bool ChartBoundsOverlapInterior(
    const ::Geometry::UvAtlas::UvAtlasChartRecord &a,
    const ::Geometry::UvAtlas::UvAtlasChartRecord &b) noexcept {
  constexpr float epsilon = 1.0e-6f;
  return a.UvMin.x < b.UvMax.x - epsilon && a.UvMax.x > b.UvMin.x + epsilon &&
         a.UvMin.y < b.UvMax.y - epsilon && a.UvMax.y > b.UvMin.y + epsilon;
}

[[nodiscard]] std::size_t CountChartOverlaps(const UvAtlasResult &result) {
  std::size_t overlaps = 0u;
  for (std::size_t i = 0u; i < result.Charts.size(); ++i) {
    for (std::size_t j = i + 1u; j < result.Charts.size(); ++j) {
      if (ChartBoundsOverlapInterior(result.Charts[i], result.Charts[j])) {
        ++overlaps;
      }
    }
  }
  return overlaps;
}

[[nodiscard]] double PackingUtilization(const UvAtlasResult &result) {
  double utilization = 0.0;
  for (const ::Geometry::UvAtlas::UvAtlasChartRecord &chart : result.Charts) {
    const glm::vec2 extent =
        glm::max(chart.UvMax - chart.UvMin, glm::vec2{0.0f});
    utilization +=
        static_cast<double>(extent.x) * static_cast<double>(extent.y);
  }
  return std::clamp(utilization, 0.0, 1.0);
}

[[nodiscard]] UvAtlasSmokeMetrics RunCubeSmokeComparison() {
  const IndexedMesh mesh = MakeCubeSurfaceFixture();
  for (int i = 0; i < kSmokeWarmupIterations; ++i) {
    (void)Tick(mesh, UvAtlasMethod::FastStaged);
    (void)Tick(mesh, UvAtlasMethod::XAtlas);
  }

  const TimedRun fast = MeanRuntimeMilliseconds(mesh, UvAtlasMethod::FastStaged,
                                                kSmokeMeasuredIterations);
  const TimedRun xatlas = MeanRuntimeMilliseconds(mesh, UvAtlasMethod::XAtlas,
                                                  kSmokeMeasuredIterations);

  const double conformalDelta =
      fast.Last.Diagnostics.Quality.MeanConformalDistortion -
      xatlas.Last.Diagnostics.Quality.MeanConformalDistortion;
  const double stretchDelta = fast.Last.Diagnostics.Quality.MaxStretch -
                              xatlas.Last.Diagnostics.Quality.MaxStretch;
  const double flippedDelta =
      static_cast<double>(fast.Last.Diagnostics.Quality.FlippedElementCount) -
      static_cast<double>(xatlas.Last.Diagnostics.Quality.FlippedElementCount);

  UvAtlasSmokeMetrics metrics{};
  metrics.FastRuntimeMilliseconds = fast.MeanRuntimeMilliseconds;
  metrics.XAtlasRuntimeMilliseconds = xatlas.MeanRuntimeMilliseconds;
  metrics.RuntimeMilliseconds = fast.MeanRuntimeMilliseconds;
  metrics.FastToXAtlasRuntimeRatio =
      xatlas.MeanRuntimeMilliseconds > 0.0
          ? fast.MeanRuntimeMilliseconds / xatlas.MeanRuntimeMilliseconds
          : 0.0;
  metrics.QualityErrorL2 =
      std::sqrt(conformalDelta * conformalDelta + stretchDelta * stretchDelta +
                flippedDelta * flippedDelta);
  metrics.FastMeanConformalDistortion =
      fast.Last.Diagnostics.Quality.MeanConformalDistortion;
  metrics.XAtlasMeanConformalDistortion =
      xatlas.Last.Diagnostics.Quality.MeanConformalDistortion;
  metrics.FastMaxStretch = fast.Last.Diagnostics.Quality.MaxStretch;
  metrics.XAtlasMaxStretch = xatlas.Last.Diagnostics.Quality.MaxStretch;
  metrics.FastChartCount = fast.Last.Diagnostics.ChartCount;
  metrics.XAtlasChartCount = xatlas.Last.Diagnostics.ChartCount;
  metrics.FastFlippedElementCount =
      fast.Last.Diagnostics.Quality.FlippedElementCount;
  metrics.XAtlasFlippedElementCount =
      xatlas.Last.Diagnostics.Quality.FlippedElementCount;
  metrics.Succeeded = fast.Last.Succeeded && xatlas.Last.Succeeded;
  return metrics;
}

[[nodiscard]] UvAtlasPromotionFixtureMetrics
RunPromotionFixture(const Fixture &fixture) {
  for (std::size_t i = 0; i < kUvAtlasPromotionWarmupPairs; ++i) {
    (void)Tick(fixture.Mesh, UvAtlasMethod::FastStaged);
    (void)Tick(fixture.Mesh, UvAtlasMethod::XAtlas);
  }

  const PairedTimedRun timing = MeasurePromotionRuntime(fixture.Mesh);

  UvAtlasPromotionFixtureMetrics metrics{};
  metrics.Name = fixture.Name;
  metrics.InputVertexCount = fixture.Mesh.VertexCount();
  metrics.InputFaceCount = fixture.Mesh.FaceCount();
  metrics.FastRuntimeMilliseconds = timing.FastMedianRuntimeMilliseconds;
  metrics.XAtlasRuntimeMilliseconds = timing.XAtlasMedianRuntimeMilliseconds;
  metrics.FastToXAtlasRuntimeRatio = timing.MedianRuntimeRatio;
  metrics.FastRuntimeSamplesMilliseconds =
      timing.FastRuntimeSamplesMilliseconds;
  metrics.XAtlasRuntimeSamplesMilliseconds =
      timing.XAtlasRuntimeSamplesMilliseconds;
  metrics.PairedRuntimeRatios = timing.RuntimeRatios;
  metrics.FastOutputVertexCount =
      timing.FastLast.Result.OutputMesh.VertexCount();
  metrics.XAtlasOutputVertexCount =
      timing.XAtlasLast.Result.OutputMesh.VertexCount();
  metrics.FastOutputFaceCount =
      timing.FastLast.Result.OutputMesh.FaceCount();
  metrics.XAtlasOutputFaceCount =
      timing.XAtlasLast.Result.OutputMesh.FaceCount();
  metrics.FastChartCount = timing.FastLast.Diagnostics.ChartCount;
  metrics.XAtlasChartCount = timing.XAtlasLast.Diagnostics.ChartCount;
  metrics.FastFlippedElementCount =
      timing.FastLast.Diagnostics.Quality.FlippedElementCount;
  metrics.XAtlasFlippedElementCount =
      timing.XAtlasLast.Diagnostics.Quality.FlippedElementCount;
  metrics.FastChartOverlapCount = CountChartOverlaps(timing.FastLast.Result);
  metrics.FastMeanConformalDistortion =
      timing.FastLast.Diagnostics.Quality.MeanConformalDistortion;
  metrics.XAtlasMeanConformalDistortion =
      timing.XAtlasLast.Diagnostics.Quality.MeanConformalDistortion;
  metrics.FastMaxStretch = timing.FastLast.Diagnostics.Quality.MaxStretch;
  metrics.XAtlasMaxStretch = timing.XAtlasLast.Diagnostics.Quality.MaxStretch;
  metrics.FastPackingUtilization = PackingUtilization(timing.FastLast.Result);
  metrics.FastSucceeded =
      timing.FastLast.Succeeded &&
      timing.FastLast.Diagnostics.ActualMethod == UvAtlasMethod::FastStaged;
  metrics.XAtlasSucceeded =
      timing.XAtlasLast.Succeeded &&
      timing.XAtlasLast.Diagnostics.ActualMethod == UvAtlasMethod::XAtlas;
  metrics.FastFiniteNormalized =
      AllOutputUvsFiniteNormalized(timing.FastLast.Result) &&
      IsFiniteNormalizedUv(timing.FastLast.Diagnostics.NormalizedUvMin) &&
      IsFiniteNormalizedUv(timing.FastLast.Diagnostics.NormalizedUvMax);
  metrics.FastUsedFallback = timing.FastLast.Diagnostics.UsedFallback;

  metrics.ConformalRegression =
      std::max(0.0, metrics.FastMeanConformalDistortion -
                        metrics.XAtlasMeanConformalDistortion -
                        kConformalRegressionTolerance);
  metrics.StretchRegression =
      std::max(0.0, metrics.FastMaxStretch - metrics.XAtlasMaxStretch -
                        kStretchRegressionTolerance);

  metrics.Passed =
      metrics.FastSucceeded && metrics.XAtlasSucceeded &&
      metrics.FastFiniteNormalized && !metrics.FastUsedFallback &&
      metrics.FastOutputFaceCount == metrics.InputFaceCount &&
      metrics.FastFlippedElementCount == 0u &&
      metrics.FastChartOverlapCount == 0u &&
      metrics.FastToXAtlasRuntimeRatio <= kRuntimeRatioPerFixtureMax &&
      metrics.ConformalRegression == 0.0 && metrics.StretchRegression == 0.0;
  return metrics;
}
} // namespace

UvAtlasSmokeMetrics RunUvAtlasSmoke() { return RunCubeSmokeComparison(); }

UvAtlasPromotionMetrics RunUvAtlasPromotionSmoke() {
  UvAtlasPromotionMetrics metrics{};
  metrics.Fixtures.reserve(8u);
  for (const Fixture &fixture : MakePromotionFixtures()) {
    UvAtlasPromotionFixtureMetrics fixtureMetrics =
        RunPromotionFixture(fixture);

    metrics.MeanFastRuntimeMilliseconds +=
        fixtureMetrics.FastRuntimeMilliseconds;
    metrics.MeanXAtlasRuntimeMilliseconds +=
        fixtureMetrics.XAtlasRuntimeMilliseconds;
    metrics.MeanFastToXAtlasRuntimeRatio +=
        fixtureMetrics.FastToXAtlasRuntimeRatio;
    metrics.MaxFastToXAtlasRuntimeRatio =
        std::max(metrics.MaxFastToXAtlasRuntimeRatio,
                 fixtureMetrics.FastToXAtlasRuntimeRatio);
    metrics.FastFlippedElementCountTotal +=
        fixtureMetrics.FastFlippedElementCount;
    metrics.FastChartOverlapCountTotal += fixtureMetrics.FastChartOverlapCount;

    const double fixtureQualityError = std::sqrt(
        fixtureMetrics.ConformalRegression *
            fixtureMetrics.ConformalRegression +
        fixtureMetrics.StretchRegression * fixtureMetrics.StretchRegression +
        static_cast<double>(fixtureMetrics.FastFlippedElementCount *
                            fixtureMetrics.FastFlippedElementCount) +
        static_cast<double>(fixtureMetrics.FastChartOverlapCount *
                            fixtureMetrics.FastChartOverlapCount));
    metrics.QualityErrorL2 += fixtureQualityError * fixtureQualityError;
    metrics.QualityErrorLinf =
        std::max(metrics.QualityErrorLinf, fixtureQualityError);

    if (fixtureMetrics.Passed) {
      ++metrics.PassedFixtureCount;
    }
    metrics.Fixtures.push_back(std::move(fixtureMetrics));
  }

  metrics.FixtureCount = metrics.Fixtures.size();
  if (metrics.FixtureCount > 0u) {
    const double fixtureCount = static_cast<double>(metrics.FixtureCount);
    metrics.MeanFastRuntimeMilliseconds /= fixtureCount;
    metrics.MeanXAtlasRuntimeMilliseconds /= fixtureCount;
    metrics.MeanFastToXAtlasRuntimeRatio /= fixtureCount;
    metrics.QualityErrorL2 = std::sqrt(metrics.QualityErrorL2);
  }
  metrics.FailedFixtureCount =
      metrics.FixtureCount - metrics.PassedFixtureCount;
  metrics.RuntimeMilliseconds = metrics.MeanFastRuntimeMilliseconds;
  metrics.PromotionPass =
      metrics.FixtureCount > 0u &&
      metrics.PassedFixtureCount == metrics.FixtureCount &&
      metrics.MeanFastToXAtlasRuntimeRatio <= kRuntimeRatioMeanMax &&
      metrics.MaxFastToXAtlasRuntimeRatio <= kRuntimeRatioPerFixtureMax &&
      metrics.FastFlippedElementCountTotal == 0u &&
      metrics.FastChartOverlapCountTotal == 0u &&
      metrics.QualityErrorLinf == 0.0;
  return metrics;
}
} // namespace Intrinsic::Bench::Geometry
