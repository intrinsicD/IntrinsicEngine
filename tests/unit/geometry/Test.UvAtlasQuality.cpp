#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <set>
#include <string>
#include <vector>

#include <glm/glm.hpp>

import Geometry.UvAtlas;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.MeshSoup;
import Geometry.HalfedgeMesh.Boundary;
import Geometry.KMeans;
import Geometry.UvAtlas.ChartSolve;

namespace {
namespace Atlas = Geometry::UvAtlas;
using Geometry::MeshSoup::IndexedMesh;

// Latitude/longitude sphere with pole vertices: closed, genus 0.
[[nodiscard]] IndexedMesh MakeSphere(const std::uint32_t stacks,
                                     const std::uint32_t slices,
                                     const float radius = 1.0f) {
  IndexedMesh mesh;
  const auto north = mesh.AddVertex({0.0f, 0.0f, radius});
  std::vector<std::uint32_t> ring;
  for (std::uint32_t i = 1u; i < stacks; ++i) {
    const double theta = std::numbers::pi * static_cast<double>(i) / stacks;
    for (std::uint32_t j = 0u; j < slices; ++j) {
      const double phi = 2.0 * std::numbers::pi * static_cast<double>(j) / slices;
      ring.push_back(mesh.AddVertex(
          {static_cast<float>(radius * std::sin(theta) * std::cos(phi)),
           static_cast<float>(radius * std::sin(theta) * std::sin(phi)),
           static_cast<float>(radius * std::cos(theta))}));
    }
  }
  const auto south = mesh.AddVertex({0.0f, 0.0f, -radius});
  const auto at = [&](const std::uint32_t i, const std::uint32_t j) {
    return ring[(i - 1u) * slices + (j % slices)];
  };
  for (std::uint32_t j = 0u; j < slices; ++j) {
    (void)mesh.AddTriangle(north, at(1u, j), at(1u, j + 1u));
  }
  for (std::uint32_t i = 1u; i + 1u < stacks; ++i) {
    for (std::uint32_t j = 0u; j < slices; ++j) {
      (void)mesh.AddTriangle(at(i, j), at(i + 1u, j), at(i + 1u, j + 1u));
      (void)mesh.AddTriangle(at(i, j), at(i + 1u, j + 1u), at(i, j + 1u));
    }
  }
  for (std::uint32_t j = 0u; j < slices; ++j) {
    (void)mesh.AddTriangle(at(stacks - 1u, j), south, at(stacks - 1u, j + 1u));
  }
  return mesh;
}

// Torus of genus one.
[[nodiscard]] IndexedMesh MakeTorus(const std::uint32_t major,
                                    const std::uint32_t minor) {
  IndexedMesh mesh;
  constexpr double R = 2.0;
  constexpr double r = 0.6;
  for (std::uint32_t i = 0u; i < major; ++i) {
    const double u = 2.0 * std::numbers::pi * i / major;
    for (std::uint32_t j = 0u; j < minor; ++j) {
      const double v = 2.0 * std::numbers::pi * j / minor;
      (void)mesh.AddVertex({static_cast<float>((R + r * std::cos(v)) * std::cos(u)),
                            static_cast<float>((R + r * std::cos(v)) * std::sin(u)),
                            static_cast<float>(r * std::sin(v))});
    }
  }
  const auto at = [&](const std::uint32_t i, const std::uint32_t j) {
    return (i % major) * minor + (j % minor);
  };
  for (std::uint32_t i = 0u; i < major; ++i) {
    for (std::uint32_t j = 0u; j < minor; ++j) {
      (void)mesh.AddTriangle(at(i, j), at(i + 1u, j), at(i + 1u, j + 1u));
      (void)mesh.AddTriangle(at(i, j), at(i + 1u, j + 1u), at(i, j + 1u));
    }
  }
  return mesh;
}

// Planar annulus: one component, two boundary loops.
[[nodiscard]] IndexedMesh MakeAnnulus(const std::uint32_t slices) {
  IndexedMesh mesh;
  for (std::uint32_t j = 0u; j < slices; ++j) {
    const double phi = 2.0 * std::numbers::pi * j / slices;
    (void)mesh.AddVertex({static_cast<float>(std::cos(phi)),
                          static_cast<float>(std::sin(phi)), 0.0f});
    (void)mesh.AddVertex({static_cast<float>(2.0 * std::cos(phi)),
                          static_cast<float>(2.0 * std::sin(phi)), 0.0f});
  }
  for (std::uint32_t j = 0u; j < slices; ++j) {
    const std::uint32_t inner0 = 2u * j;
    const std::uint32_t outer0 = 2u * j + 1u;
    const std::uint32_t inner1 = 2u * ((j + 1u) % slices);
    const std::uint32_t outer1 = inner1 + 1u;
    (void)mesh.AddTriangle(inner0, outer0, outer1);
    (void)mesh.AddTriangle(inner0, outer1, inner1);
  }
  return mesh;
}

// Planar grid, optionally offset so several grids form disconnected pieces.
void AppendGrid(IndexedMesh &mesh, const std::uint32_t side, const float scale,
                const glm::vec3 offset) {
  const auto base = static_cast<std::uint32_t>(mesh.VertexCount());
  for (std::uint32_t y = 0u; y <= side; ++y) {
    for (std::uint32_t x = 0u; x <= side; ++x) {
      (void)mesh.AddVertex(offset + scale * glm::vec3{static_cast<float>(x),
                                                      static_cast<float>(y),
                                                      0.0f});
    }
  }
  const auto at = [&](const std::uint32_t x, const std::uint32_t y) {
    return base + y * (side + 1u) + x;
  };
  for (std::uint32_t y = 0u; y < side; ++y) {
    for (std::uint32_t x = 0u; x < side; ++x) {
      (void)mesh.AddTriangle(at(x, y), at(x + 1u, y), at(x + 1u, y + 1u));
      (void)mesh.AddTriangle(at(x, y), at(x + 1u, y + 1u), at(x, y + 1u));
    }
  }
}

// Spherical cap of the given polar half-angle: one curved disk.
[[nodiscard]] IndexedMesh MakeCap(const double halfAngle,
                                  const std::uint32_t stacks,
                                  const std::uint32_t slices) {
  IndexedMesh mesh;
  const auto pole = mesh.AddVertex({0.0f, 0.0f, 1.0f});
  for (std::uint32_t i = 1u; i <= stacks; ++i) {
    const double theta = halfAngle * static_cast<double>(i) / stacks;
    for (std::uint32_t j = 0u; j < slices; ++j) {
      const double phi = 2.0 * std::numbers::pi * j / slices;
      (void)mesh.AddVertex({static_cast<float>(std::sin(theta) * std::cos(phi)),
                            static_cast<float>(std::sin(theta) * std::sin(phi)),
                            static_cast<float>(std::cos(theta))});
    }
  }
  const auto at = [&](const std::uint32_t i, const std::uint32_t j) {
    return 1u + (i - 1u) * slices + (j % slices);
  };
  for (std::uint32_t j = 0u; j < slices; ++j) {
    (void)mesh.AddTriangle(pole, at(1u, j), at(1u, j + 1u));
  }
  for (std::uint32_t i = 1u; i < stacks; ++i) {
    for (std::uint32_t j = 0u; j < slices; ++j) {
      (void)mesh.AddTriangle(at(i, j), at(i + 1u, j), at(i + 1u, j + 1u));
      (void)mesh.AddTriangle(at(i, j), at(i + 1u, j + 1u), at(i, j + 1u));
    }
  }
  return mesh;
}

[[nodiscard]] IndexedMesh ScaledCopy(const IndexedMesh &source,
                                     const float scale) {
  IndexedMesh mesh;
  for (const glm::vec3 p : source.Positions()) {
    (void)mesh.AddVertex(p * scale);
  }
  for (const auto &face : source.Faces()) {
    (void)mesh.AddTriangle(face.Indices[0], face.Indices[1], face.Indices[2]);
  }
  return mesh;
}

[[nodiscard]] Atlas::UvAtlasOptions
GenerateOptions(const Atlas::UvAtlasDistortion distortion) {
  Atlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.ForceRegenerate = true;
  options.AllowXAtlasFallback = false;
  options.Distortion = distortion;
  options.Resolution = 512u;
  options.Padding = 2u;
  return options;
}

[[nodiscard]] Atlas::UvAtlasInput
InputFor(const IndexedMesh &mesh, const std::vector<std::uint32_t> &regions = {}) {
  Atlas::UvAtlasInput input = Atlas::BorrowInput(mesh);
  input.FaceRegions = regions;
  return input;
}

// Checks every accepted-atlas contract that does not depend on the objective.
void ExpectAcceptedAtlas(const IndexedMesh &mesh,
                         const Atlas::UvAtlasResult &result,
                         const Atlas::UvAtlasOptions &options) {
  ASSERT_EQ(result.Status, Atlas::UvAtlasStatus::Success)
      << Atlas::ToString(result.Status) << ": "
      << result.Diagnostics.BackendDetail;
  const auto &report = result.Diagnostics.Validation;
  EXPECT_TRUE(report.Passed()) << report.Detail;
  EXPECT_EQ(result.Diagnostics.ActualDistortion, options.Distortion);
  EXPECT_EQ(result.Diagnostics.RequestedDistortion, options.Distortion);
  ASSERT_EQ(result.SourceCornerUvs.size(), 3u * mesh.FaceCount());
  ASSERT_EQ(result.SourceFaceChart.size(), mesh.FaceCount());
  ASSERT_EQ(result.SourceFaceRegionComponent.size(), mesh.FaceCount());
  EXPECT_EQ(result.OutputMesh.FaceCount(), mesh.FaceCount());
  for (const glm::vec2 uv : result.SourceCornerUvs) {
    ASSERT_TRUE(std::isfinite(uv.x) && std::isfinite(uv.y));
    EXPECT_GE(uv.x, 0.0f);
    EXPECT_GE(uv.y, 0.0f);
    EXPECT_LE(uv.x, 1.0f);
    EXPECT_LE(uv.y, 1.0f);
  }
  for (std::size_t f = 0u; f < mesh.FaceCount(); ++f) {
    EXPECT_LT(result.SourceFaceChart[f], result.Charts.size());
    EXPECT_GT(Atlas::ExactUvOrientation(result.SourceCornerUvs[3u * f],
                                        result.SourceCornerUvs[3u * f + 1u],
                                        result.SourceCornerUvs[3u * f + 2u]),
              0);
  }
  EXPECT_EQ(report.NonPositiveOrientationCount, 0u);
  EXPECT_EQ(report.Overlaps.OverlapPairCount, 0u);
  EXPECT_EQ(report.RegionCrossingChartCount, 0u);
  EXPECT_LE(report.MaxConformalDistortion,
            options.MaxConformalDistortion * (1.0 + 1.0e-6));
  EXPECT_LE(report.MaxAreaDistortion,
            options.MaxAreaDistortion * (1.0 + 1.0e-6));
  EXPECT_EQ(report.UnderResolvedChartCount, 0u);
  EXPECT_EQ(report.ChartsWithoutTexelCenterCount, 0u);
  // Source topology and positions are untouched by construction: the input
  // is borrowed read-only; the correspondence maps each face exactly once.
  std::vector<std::uint8_t> seen(mesh.FaceCount(), 0u);
  for (const std::uint32_t source : result.SourceFaceForOutputFace) {
    ASSERT_LT(source, mesh.FaceCount());
    EXPECT_EQ(seen[source], 0u);
    seen[source] = 1u;
  }
}

[[nodiscard]] double MaxAreaDistortionOf(const Atlas::UvChartSolveResult &solve,
                                         const IndexedMesh &mesh) {
  double worst = 1.0;
  for (const auto &face : mesh.Faces()) {
    const auto m = Atlas::MeasureUvFaceDistortion(
        glm::dvec3{mesh.Position(face.Indices[0])},
        glm::dvec3{mesh.Position(face.Indices[1])},
        glm::dvec3{mesh.Position(face.Indices[2])}, solve.Uvs[face.Indices[0]],
        solve.Uvs[face.Indices[1]], solve.Uvs[face.Indices[2]]);
    EXPECT_TRUE(m.Valid);
    const double a = m.SigmaMax * m.SigmaMin;
    worst = std::max(worst, std::max(a, 1.0 / a));
  }
  return worst;
}

[[nodiscard]] double MaxConformalOf(const Atlas::UvChartSolveResult &solve,
                                    const IndexedMesh &mesh) {
  double worst = 1.0;
  for (const auto &face : mesh.Faces()) {
    const auto m = Atlas::MeasureUvFaceDistortion(
        glm::dvec3{mesh.Position(face.Indices[0])},
        glm::dvec3{mesh.Position(face.Indices[1])},
        glm::dvec3{mesh.Position(face.Indices[2])}, solve.Uvs[face.Indices[0]],
        solve.Uvs[face.Indices[1]], solve.Uvs[face.Indices[2]]);
    worst = std::max(worst, m.SigmaMax / m.SigmaMin);
  }
  return worst;
}

[[nodiscard]] std::vector<std::uint32_t> Triangles(const IndexedMesh &mesh) {
  std::vector<std::uint32_t> indices;
  for (const auto &face : mesh.Faces()) {
    indices.insert(indices.end(), face.Indices.begin(), face.Indices.end());
  }
  return indices;
}

constexpr Atlas::UvAtlasDistortion kAllObjectives[]{
    Atlas::UvAtlasDistortion::None, Atlas::UvAtlasDistortion::Angle,
    Atlas::UvAtlasDistortion::Area, Atlas::UvAtlasDistortion::Both};
} // namespace

TEST(UvAtlasQuality, EveryObjectiveAcceptsClosedGenusAndMultiBoundaryMeshes) {
  IndexedMesh disconnected;
  AppendGrid(disconnected, 4u, 1.0f, glm::vec3{0.0f});
  AppendGrid(disconnected, 3u, 0.5f, glm::vec3{10.0f, 0.0f, 0.0f});
  const std::vector<std::pair<std::string, IndexedMesh>> meshes{
      {"sphere", MakeSphere(10u, 16u)},
      {"torus", MakeTorus(24u, 10u)},
      {"annulus", MakeAnnulus(24u)},
      {"disconnected", disconnected},
  };
  for (const auto &[name, mesh] : meshes) {
    for (const Atlas::UvAtlasDistortion objective : kAllObjectives) {
      SCOPED_TRACE(name + " / " + Atlas::ToString(objective));
      const Atlas::UvAtlasOptions options = GenerateOptions(objective);
      const auto result = Atlas::ResolveUvAtlas(InputFor(mesh), options);
      ExpectAcceptedAtlas(mesh, result, options);
      EXPECT_EQ(result.Diagnostics.ActualMethod,
                Atlas::UvAtlasMethod::FastStaged);
      EXPECT_FALSE(result.Diagnostics.UsedFallback);
      // Closed and non-disk components must be cut into several charts.
      if (name != "disconnected") {
        EXPECT_GT(result.Charts.size(), 1u);
      }
    }
  }
}

TEST(UvAtlasQuality, SelectedObjectiveActuallyRunsOnACurvedChart) {
  // A 70-degree cap is one disk whose LSCM map has measurable area
  // distortion, so each objective must change the result.
  const IndexedMesh cap = MakeCap(70.0 * std::numbers::pi / 180.0, 8u, 24u);
  const std::vector<std::uint32_t> tris = Triangles(cap);

  Atlas::UvChartSolveParams params{};
  params.Objective = Atlas::UvAtlasDistortion::None;
  const auto none = Atlas::SolveUvChart(cap.Positions(), tris, params);
  ASSERT_TRUE(none.Succeeded()) << none.Detail;
  EXPECT_EQ(none.Backend, "tutte");
  EXPECT_EQ(none.Iterations, 0u);

  params.Objective = Atlas::UvAtlasDistortion::Angle;
  const auto angle = Atlas::SolveUvChart(cap.Positions(), tris, params);
  ASSERT_TRUE(angle.Succeeded()) << angle.Detail;
  EXPECT_EQ(angle.Backend, "lscm");
  EXPECT_EQ(angle.Iterations, 0u);

  params.Objective = Atlas::UvAtlasDistortion::Area;
  const auto area = Atlas::SolveUvChart(cap.Positions(), tris, params);
  ASSERT_TRUE(area.Succeeded()) << area.Detail;
  EXPECT_EQ(area.Backend, "lscm+area_priority");
  EXPECT_EQ(area.ActualObjective, Atlas::UvAtlasDistortion::Area);
  EXPECT_GT(area.Iterations, 0u);
  EXPECT_LT(area.FinalEnergy, area.InitialEnergy);

  params.Objective = Atlas::UvAtlasDistortion::Both;
  const auto both = Atlas::SolveUvChart(cap.Positions(), tris, params);
  ASSERT_TRUE(both.Succeeded()) << both.Detail;
  EXPECT_EQ(both.Backend, "lscm+slim");
  EXPECT_EQ(both.ActualObjective, Atlas::UvAtlasDistortion::Both);
  EXPECT_GT(both.Iterations, 0u);
  EXPECT_LT(both.FinalEnergy, both.InitialEnergy);

  // Area priority trades angle for area relative to LSCM.
  const double angleArea = MaxAreaDistortionOf(angle, cap);
  const double areaArea = MaxAreaDistortionOf(area, cap);
  EXPECT_LT(areaArea, angleArea);
  EXPECT_LT(areaArea, 1.2);
  EXPECT_LE(MaxConformalOf(angle, cap), MaxConformalOf(area, cap));
  // Every solved chart is locally injective at unit density.
  for (const auto *solve : {&none, &angle, &area, &both}) {
    const auto measured = MaxAreaDistortionOf(*solve, cap);
    EXPECT_TRUE(std::isfinite(measured));
  }
}

TEST(UvAtlasQuality, AtlasReportsObjectiveIterationsAndBackends) {
  const IndexedMesh sphere = MakeSphere(12u, 20u);
  for (const Atlas::UvAtlasDistortion objective : kAllObjectives) {
    SCOPED_TRACE(Atlas::ToString(objective));
    const auto options = GenerateOptions(objective);
    const auto result = Atlas::ResolveUvAtlas(InputFor(sphere), options);
    ExpectAcceptedAtlas(sphere, result, options);
    const bool optimizes = objective == Atlas::UvAtlasDistortion::Area ||
                           objective == Atlas::UvAtlasDistortion::Both;
    if (optimizes) {
      EXPECT_GT(result.Diagnostics.OptimizationIterationCount, 0u);
    } else {
      EXPECT_EQ(result.Diagnostics.OptimizationIterationCount, 0u);
    }
    for (const auto &chart : result.Charts) {
      EXPECT_EQ(chart.Objective, objective);
      if (chart.SourceFaceCount == 1u) {
        EXPECT_EQ(chart.ParameterizationBackend, "single_triangle_exact");
        continue;
      }
      switch (objective) {
      case Atlas::UvAtlasDistortion::None:
        EXPECT_EQ(chart.ParameterizationBackend, "tutte");
        break;
      case Atlas::UvAtlasDistortion::Angle:
        EXPECT_EQ(chart.ParameterizationBackend, "lscm");
        break;
      case Atlas::UvAtlasDistortion::Area:
        EXPECT_NE(chart.ParameterizationBackend.find("+area_priority"),
                  std::string::npos);
        break;
      case Atlas::UvAtlasDistortion::Both:
        EXPECT_NE(chart.ParameterizationBackend.find("+slim"),
                  std::string::npos);
        break;
      }
    }
  }
}

TEST(UvAtlasQuality, UnitScalingLeavesTheNormalizedAtlasUnchanged) {
  const IndexedMesh sphere = MakeSphere(10u, 16u);
  const auto options = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  const auto reference = Atlas::ResolveUvAtlas(InputFor(sphere), options);
  ExpectAcceptedAtlas(sphere, reference, options);
  for (const float scale : {1.0e-4f, 1.0e4f}) {
    SCOPED_TRACE(scale);
    const IndexedMesh scaled = ScaledCopy(sphere, scale);
    const auto result = Atlas::ResolveUvAtlas(InputFor(scaled), options);
    ExpectAcceptedAtlas(scaled, result, options);
    EXPECT_EQ(result.SourceFaceChart, reference.SourceFaceChart);
    ASSERT_EQ(result.SourceCornerUvs.size(), reference.SourceCornerUvs.size());
    double maxDifference = 0.0;
    for (std::size_t i = 0u; i < result.SourceCornerUvs.size(); ++i) {
      const glm::vec2 d = result.SourceCornerUvs[i] - reference.SourceCornerUvs[i];
      maxDifference = std::max<double>(
          maxDifference, std::max(std::abs(d.x), std::abs(d.y)));
    }
    EXPECT_LT(maxDifference, 1.0e-3);
    EXPECT_NEAR(result.Diagnostics.Validation.MaxAreaDistortion,
                reference.Diagnostics.Validation.MaxAreaDistortion, 1.0e-2);
    EXPECT_NEAR(result.Diagnostics.TexelsPerUnit * scale,
                reference.Diagnostics.TexelsPerUnit,
                1.0e-3 * reference.Diagnostics.TexelsPerUnit);
  }
}

TEST(UvAtlasQuality, RegionLabelsAndTheirComponentsArePreserved) {
  // Column bands 1 | 2 | 1: label 1 has two separate components.
  IndexedMesh grid;
  constexpr std::uint32_t side = 9u;
  AppendGrid(grid, side, 1.0f, glm::vec3{0.0f});
  std::vector<std::uint32_t> regions(grid.FaceCount());
  for (std::size_t f = 0u; f < grid.FaceCount(); ++f) {
    const std::uint32_t column = static_cast<std::uint32_t>((f / 2u) % side);
    regions[f] = column < 3u || column >= 6u ? 1u : 2u;
  }
  for (const auto method :
       {Atlas::UvAtlasMethod::FastStaged, Atlas::UvAtlasMethod::XAtlas}) {
    SCOPED_TRACE(Atlas::ToString(method));
    auto options = GenerateOptions(method == Atlas::UvAtlasMethod::XAtlas
                                       ? Atlas::UvAtlasDistortion::Angle
                                       : Atlas::UvAtlasDistortion::Both);
    options.Method = method;
    const auto result = Atlas::ResolveUvAtlas(InputFor(grid, regions), options);
    ExpectAcceptedAtlas(grid, result, options);
    EXPECT_EQ(result.Diagnostics.RegionLabelCount, 2u);
    EXPECT_EQ(result.Diagnostics.RegionComponentCount, 3u);
    EXPECT_EQ(result.Diagnostics.Validation.RegionComponentCount, 3u);
    // Each chart holds exactly one label and one region component.
    std::vector<std::set<std::uint32_t>> chartComponents(result.Charts.size());
    std::vector<std::set<std::uint32_t>> chartLabels(result.Charts.size());
    for (std::size_t f = 0u; f < grid.FaceCount(); ++f) {
      const std::uint32_t chart = result.SourceFaceChart[f];
      chartComponents[chart].insert(result.SourceFaceRegionComponent[f]);
      chartLabels[chart].insert(regions[f]);
    }
    for (std::size_t chart = 0u; chart < result.Charts.size(); ++chart) {
      EXPECT_EQ(chartComponents[chart].size(), 1u) << "chart " << chart;
      EXPECT_EQ(chartLabels[chart].size(), 1u) << "chart " << chart;
    }
    EXPECT_GE(result.Charts.size(), 3u);
    if (method == Atlas::UvAtlasMethod::FastStaged) {
      // A flat grid needs no other cut: exactly one chart per component.
      EXPECT_EQ(result.Charts.size(), 3u);
      std::size_t regionSeams = 0u;
      for (const auto &seam : result.SeamCuts) {
        if (!seam.Boundary) {
          EXPECT_EQ(seam.Reason, Atlas::UvAtlasSeamReason::RegionBoundary);
          ++regionSeams;
        }
      }
      EXPECT_EQ(regionSeams, 2u * side);
    }
  }
}

TEST(UvAtlasQuality, SmallConeFragmentsMergeIntoTheirNeighbour) {
  // A steep tent around one raised grid vertex deviates more than the growth
  // cone from the flat chart; its fragments must join that chart instead of
  // becoming separate slivers. Equal-size planar charts (cube faces, see
  // Test.UvAtlas) are never merged.
  IndexedMesh grid;
  AppendGrid(grid, 8u, 1.0f, glm::vec3{0.0f});
  grid.Position(4u * 9u + 4u).z = 2.5f;
  const auto options = GenerateOptions(Atlas::UvAtlasDistortion::Angle);
  const auto result = Atlas::ResolveUvAtlas(InputFor(grid), options);
  ExpectAcceptedAtlas(grid, result, options);
  EXPECT_EQ(result.Diagnostics.InitialChartCount, 1u);
  EXPECT_EQ(result.Diagnostics.SingleTriangleChartCount, 0u);
}

TEST(UvAtlasQuality, InvalidRegionLabelCountIsRejected) {
  IndexedMesh grid;
  AppendGrid(grid, 2u, 1.0f, glm::vec3{0.0f});
  const std::vector<std::uint32_t> regions{0u, 1u};
  const auto result = Atlas::ResolveUvAtlas(
      InputFor(grid, regions), GenerateOptions(Atlas::UvAtlasDistortion::Both));
  EXPECT_EQ(result.Status, Atlas::UvAtlasStatus::InvalidRegionLabels);
}

TEST(UvAtlasQuality, DegenerateFaceAmongValidFacesIsIdentified) {
  IndexedMesh mesh;
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{0.0f});
  const auto a = mesh.AddVertex({3.0f, 0.0f, 0.0f});
  const auto b = mesh.AddVertex({4.0f, 0.0f, 0.0f});
  const auto c = mesh.AddVertex({5.0f, 0.0f, 0.0f});
  (void)mesh.AddTriangle(a, b, c);
  auto options = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  options.AllowXAtlasFallback = true;
  const auto result = Atlas::ResolveUvAtlas(InputFor(mesh), options);
  EXPECT_EQ(result.Status, Atlas::UvAtlasStatus::DegenerateInput);
  EXPECT_FALSE(result.Diagnostics.UsedFallback);
  ASSERT_EQ(result.Diagnostics.InvalidFaces.size(), 1u);
  EXPECT_EQ(result.Diagnostics.InvalidFaces.front(), 2u);
}

TEST(UvAtlasQuality, NonManifoldBowtieAndMisorientedInputsAreCutNotMutated) {
  // Bowtie: two fans share only vertex 0.
  IndexedMesh bowtie;
  const auto o = bowtie.AddVertex({0.0f, 0.0f, 0.0f});
  const auto b1 = bowtie.AddVertex({1.0f, 0.0f, 0.0f});
  const auto b2 = bowtie.AddVertex({1.0f, 1.0f, 0.0f});
  const auto b3 = bowtie.AddVertex({-1.0f, 0.0f, 0.0f});
  const auto b4 = bowtie.AddVertex({-1.0f, -1.0f, 0.0f});
  (void)bowtie.AddTriangle(o, b1, b2);
  (void)bowtie.AddTriangle(o, b3, b4);

  // Two coplanar triangles sharing edge (0,1) with the same winding.
  IndexedMesh misoriented;
  const auto m0 = misoriented.AddVertex({0.0f, 0.0f, 0.0f});
  const auto m1 = misoriented.AddVertex({1.0f, 0.0f, 0.0f});
  const auto m2 = misoriented.AddVertex({0.5f, 1.0f, 0.0f});
  const auto m3 = misoriented.AddVertex({0.5f, -1.0f, 0.0f});
  (void)misoriented.AddTriangle(m0, m1, m2);
  (void)misoriented.AddTriangle(m0, m1, m3);

  // Three sheets on one edge plus a quad strip on the first sheet.
  IndexedMesh fin;
  const auto f0 = fin.AddVertex({0.0f, 0.0f, 0.0f});
  const auto f1 = fin.AddVertex({1.0f, 0.0f, 0.0f});
  const auto f2 = fin.AddVertex({0.0f, 1.0f, 0.0f});
  const auto f3 = fin.AddVertex({0.0f, 0.0f, 1.0f});
  const auto f4 = fin.AddVertex({0.0f, -1.0f, 0.0f});
  const auto f5 = fin.AddVertex({1.0f, 1.0f, 0.0f});
  (void)fin.AddTriangle(f0, f1, f2);
  (void)fin.AddTriangle(f1, f0, f3);
  (void)fin.AddTriangle(f0, f1, f4);
  (void)fin.AddTriangle(f1, f5, f2);

  for (const auto *mesh : {&bowtie, &misoriented, &fin}) {
    for (const Atlas::UvAtlasDistortion objective : kAllObjectives) {
      SCOPED_TRACE(Atlas::ToString(objective));
      const auto options = GenerateOptions(objective);
      const auto result = Atlas::ResolveUvAtlas(InputFor(*mesh), options);
      ExpectAcceptedAtlas(*mesh, result, options);
    }
  }

  const auto options = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  const auto bowtieResult = Atlas::ResolveUvAtlas(InputFor(bowtie), options);
  // The shared vertex is split per fan; both fans stay separate charts.
  EXPECT_EQ(bowtieResult.OutputMesh.VertexCount(), 6u);
  const auto misorientedResult =
      Atlas::ResolveUvAtlas(InputFor(misoriented), options);
  EXPECT_EQ(misorientedResult.Diagnostics.InconsistentOrientationEdgeCount, 1u);
  ASSERT_EQ(misorientedResult.Charts.size(), 2u);
  bool sawCut = false;
  for (const auto &seam : misorientedResult.SeamCuts) {
    sawCut = sawCut || seam.Reason == Atlas::UvAtlasSeamReason::TopologyCut;
  }
  EXPECT_TRUE(sawCut);
  const auto finResult = Atlas::ResolveUvAtlas(InputFor(fin), options);
  EXPECT_EQ(finResult.Diagnostics.NonManifoldEdgeCount, 1u);
  // Faces 0 and 3 are manifold neighbours; 1 and 2 are cut away.
  EXPECT_EQ(finResult.SourceFaceChart[0], finResult.SourceFaceChart[3]);
  EXPECT_NE(finResult.SourceFaceChart[0], finResult.SourceFaceChart[1]);
  EXPECT_NE(finResult.SourceFaceChart[0], finResult.SourceFaceChart[2]);

  // Equal labels connect through the non-manifold edge even when the first
  // encountered sheet has a different label.
  const std::array<std::uint32_t, 4u> labels{0u, 1u, 1u, 0u};
  auto labeledInput = InputFor(fin);
  labeledInput.FaceRegions = labels;
  const auto labeled = Atlas::ResolveUvAtlas(labeledInput, options);
  ExpectAcceptedAtlas(fin, labeled, options);
  EXPECT_EQ(labeled.Diagnostics.RegionComponentCount, 2u);
  EXPECT_EQ(labeled.Diagnostics.Validation.RegionComponentCount, 2u);
}

TEST(UvAtlasQuality, TightLimitsRefineChartsUntilHonored) {
  const IndexedMesh sphere = MakeSphere(8u, 12u);
  auto options = GenerateOptions(Atlas::UvAtlasDistortion::Angle);
  options.MaxConformalDistortion = 1.05;
  options.MaxAreaDistortion = 1.05;
  const auto result = Atlas::ResolveUvAtlas(InputFor(sphere), options);
  ExpectAcceptedAtlas(sphere, result, options);
  EXPECT_GT(result.Diagnostics.RefinementSplitCount, 0u);
  EXPECT_GT(result.Diagnostics.RejectedChartCount, 0u);
  bool sawSplitSeam = false;
  for (const auto &seam : result.SeamCuts) {
    sawSplitSeam =
        sawSplitSeam || seam.Reason == Atlas::UvAtlasSeamReason::RefinementSplit;
  }
  EXPECT_TRUE(sawSplitSeam);

  // The same bounds cannot be met within two charts.
  options.MaxCharts = 2u;
  const auto limited = Atlas::ResolveUvAtlas(InputFor(sphere), options);
  EXPECT_EQ(limited.Status, Atlas::UvAtlasStatus::ResourceLimitExceeded);
  EXPECT_TRUE(limited.SourceCornerUvs.empty());
}

TEST(UvAtlasQuality, ResolutionAndDensityLimitsFailExplicitly) {
  IndexedMesh soup;
  for (std::uint32_t i = 0u; i < 400u; ++i) {
    const float x = static_cast<float>(i) * 3.0f;
    const auto a = soup.AddVertex({x, 0.0f, 0.0f});
    const auto b = soup.AddVertex({x + 1.0f, 0.0f, 0.0f});
    const auto c = soup.AddVertex({x, 1.0f, 0.0f});
    (void)soup.AddTriangle(a, b, c);
  }
  auto options = GenerateOptions(Atlas::UvAtlasDistortion::Angle);
  options.Resolution = 16u;
  options.Padding = 0u;
  const auto noSpace = Atlas::ResolveUvAtlas(InputFor(soup), options);
  EXPECT_EQ(noSpace.Status, Atlas::UvAtlasStatus::UnderResolved);
  EXPECT_NE(noSpace.Diagnostics.BackendDetail.find("texel anchors"), std::string::npos);
  // The boxes can fit at 32, but the common density still gives each island
  // less than one texel of area. Sample alignment cannot excuse that failure.
  options.Resolution = 32u;
  const auto underResolved = Atlas::ResolveUvAtlas(InputFor(soup), options);
  EXPECT_EQ(underResolved.Status, Atlas::UvAtlasStatus::UnderResolved)
      << Atlas::ToString(underResolved.Status);
  EXPECT_GT(underResolved.Diagnostics.Validation.UnderResolvedChartCount, 0u);

  options.Resolution = 512u;
  options.TexelsPerUnit = 1.0e5f;
  const auto tooDense = Atlas::ResolveUvAtlas(InputFor(soup), options);
  EXPECT_EQ(tooDense.Status, Atlas::UvAtlasStatus::ResourceLimitExceeded);
}

TEST(UvAtlasQuality, InvalidOptionsAndCancellationAreExplicit) {
  IndexedMesh grid;
  AppendGrid(grid, 2u, 1.0f, glm::vec3{0.0f});
  const auto base = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  const auto statusFor = [&](const Atlas::UvAtlasOptions &options) {
    return Atlas::ResolveUvAtlas(InputFor(grid), options).Status;
  };
  auto options = base;
  options.MaxConformalDistortion = 0.5;
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::InvalidOptions);
  options = base;
  options.MaxAreaDistortion = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::InvalidOptions);
  options = base;
  options.MaxCharts = 0u;
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::InvalidOptions);
  options = base;
  options.MaxIterations = 0u;
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::InvalidOptions);
  options.Distortion = Atlas::UvAtlasDistortion::Angle;
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::Success);
  options = base;
  options.Resolution = 20000u;
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::InvalidOptions);
  EXPECT_FALSE(Atlas::ValidateUvAtlasOptions(options).Detail.empty());

  const std::atomic<bool> cancel{true};
  options = base;
  options.CancelFlag = &cancel;
  EXPECT_EQ(statusFor(options), Atlas::UvAtlasStatus::Cancelled);
}

TEST(UvAtlasQuality, XAtlasHonorsOnlyTheAngleObjective) {
  const IndexedMesh sphere = MakeSphere(8u, 12u);
  auto options = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  options.Method = Atlas::UvAtlasMethod::XAtlas;
  const auto rejected = Atlas::ResolveUvAtlas(InputFor(sphere), options);
  EXPECT_EQ(rejected.Status, Atlas::UvAtlasStatus::ObjectiveUnsupported);

  options.Distortion = Atlas::UvAtlasDistortion::Angle;
  const auto accepted = Atlas::ResolveUvAtlas(InputFor(sphere), options);
  ExpectAcceptedAtlas(sphere, accepted, options);
  EXPECT_EQ(accepted.Diagnostics.ActualMethod, Atlas::UvAtlasMethod::XAtlas);
}

TEST(UvAtlasQuality, TokensRoundTrip) {
  for (const Atlas::UvAtlasDistortion objective : kAllObjectives) {
    EXPECT_EQ(Atlas::ParseUvAtlasDistortion(Atlas::ToString(objective)),
              objective);
  }
  for (const auto method :
       {Atlas::UvAtlasMethod::None, Atlas::UvAtlasMethod::Authored,
        Atlas::UvAtlasMethod::XAtlas, Atlas::UvAtlasMethod::FastStaged}) {
    EXPECT_EQ(Atlas::ParseUvAtlasMethod(Atlas::ToString(method)), method);
  }
  EXPECT_FALSE(Atlas::ParseUvAtlasDistortion("conformal").has_value());
  EXPECT_FALSE(Atlas::ParseUvAtlasMethod("").has_value());
}

TEST(UvAtlasQuality, GenerationIsDeterministic) {
  const IndexedMesh torus = MakeTorus(20u, 8u);
  const auto options = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  const auto first = Atlas::ResolveUvAtlas(InputFor(torus), options);
  const auto second = Atlas::ResolveUvAtlas(InputFor(torus), options);
  ExpectAcceptedAtlas(torus, first, options);
  EXPECT_EQ(first.SourceCornerUvs, second.SourceCornerUvs);
  EXPECT_EQ(first.SourceFaceChart, second.SourceFaceChart);
  EXPECT_EQ(first.SourceVertexForOutputVertex,
            second.SourceVertexForOutputVertex);
}

// ---------------------------------------------------------------------------
// Independent validator.
// ---------------------------------------------------------------------------

TEST(UvAtlasValidation, ExactOrientationResolvesNearlyCollinearFloats) {
  const glm::vec2 a{0.0f, 0.0f};
  const glm::vec2 b{1.0f, 1.0f};
  const glm::vec2 on{0.5f, 0.5f};
  const glm::vec2 above{0.5f, std::nextafter(0.5f, 1.0f)};
  const glm::vec2 below{0.5f, std::nextafter(0.5f, 0.0f)};
  EXPECT_EQ(Atlas::ExactUvOrientation(a, b, on), 0);
  EXPECT_EQ(Atlas::ExactUvOrientation(a, b, above), 1);
  EXPECT_EQ(Atlas::ExactUvOrientation(a, b, below), -1);
  // Large and tiny magnitudes mixed.
  const glm::vec2 far{1.0e20f, 1.0e20f};
  const glm::vec2 tiny{1.0e-20f, 0.0f};
  EXPECT_EQ(Atlas::ExactUvOrientation(glm::vec2{0.0f}, far, tiny), -1);
  EXPECT_EQ(Atlas::ExactUvOrientation(glm::vec2{0.0f}, tiny, far), 1);
}

TEST(UvAtlasValidation, SharedEdgesAreNotOverlapsButFoldsAre) {
  IndexedMesh grid;
  constexpr std::uint32_t side = 60u;
  AppendGrid(grid, side, 1.0f / side, glm::vec3{0.0f});
  std::vector<glm::vec2> corners;
  for (const auto &face : grid.Faces()) {
    for (const auto index : face.Indices) {
      corners.push_back(glm::vec2{grid.Position(index)});
    }
  }
  const auto clean = Atlas::FindUvTriangleOverlaps(corners);
  EXPECT_EQ(clean.OverlapPairCount, 0u);
  // Broad phase stays proportional to local neighbourhoods, not all pairs.
  EXPECT_LT(clean.CandidatePairCount, 20u * clean.TriangleCount);

  // Move one interior vertex copy across its neighbours: a local fold.
  std::vector<glm::vec2> folded = corners;
  folded[3u * 100u + 2u] += glm::vec2{3.0f / side, 0.0f};
  const auto overlap = Atlas::FindUvTriangleOverlaps(folded);
  EXPECT_GT(overlap.OverlapPairCount, 0u);

  // Two separate triangles that only touch at a vertex do not overlap.
  const std::vector<glm::vec2> touching{
      {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
      {1.0f, 0.0f}, {2.0f, 0.0f}, {1.0f, 1.0f}};
  EXPECT_EQ(Atlas::FindUvTriangleOverlaps(touching).OverlapPairCount, 0u);
  // Identical triangles overlap.
  const std::vector<glm::vec2> stacked{
      {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f},
      {0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}};
  EXPECT_EQ(Atlas::FindUvTriangleOverlaps(stacked).OverlapPairCount, 1u);
}

TEST(UvAtlasValidation, DensityNormalizationUsesOneGlobalScale) {
  // Two unit squares; the second is drawn at twice the linear UV scale, so
  // the density-normalized area ratios are 2/5 and 8/5.
  IndexedMesh mesh;
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{0.0f});
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{5.0f, 0.0f, 0.0f});
  std::vector<glm::vec2> corners;
  for (std::size_t f = 0u; f < mesh.FaceCount(); ++f) {
    const auto &face = mesh.Faces()[f];
    for (const auto index : face.Indices) {
      const glm::vec3 p = mesh.Position(index);
      corners.push_back(f < 2u ? glm::vec2{0.05f + 0.2f * p.x, 0.05f + 0.2f * p.y}
                               : glm::vec2{0.3f + 0.4f * (p.x - 5.0f),
                                           0.3f + 0.4f * p.y});
    }
  }
  const std::vector<std::uint32_t> charts{0u, 0u, 1u, 1u};
  Atlas::UvAtlasValidationInput input{
      .Positions = mesh.Positions(),
      .Faces = mesh.Faces(),
      .CornerUvs = corners,
      .FaceCharts = charts,
  };
  Atlas::UvAtlasValidationOptions options{};
  options.AtlasWidth = 256u;
  options.AtlasHeight = 256u;
  options.MaxAreaDistortion = 3.0;
  const auto passed = Atlas::ValidateUvAtlasCorners(input, options);
  EXPECT_TRUE(passed.Passed()) << passed.Detail;
  EXPECT_NEAR(passed.MaxAreaDistortion, 2.5, 1.0e-5);
  EXPECT_NEAR(passed.MaxConformalDistortion, 1.0, 1.0e-5);
  EXPECT_NEAR(passed.TexelsPerUnit, 256.0 * std::sqrt(0.1), 1.0e-3);

  options.MaxAreaDistortion = 2.0;
  const auto failed = Atlas::ValidateUvAtlasCorners(input, options);
  EXPECT_EQ(failed.Status, Atlas::UvAtlasStatus::QualityLimitNotMet);
  EXPECT_EQ(failed.AreaLimitViolationCount, 2u);
}

TEST(UvAtlasValidation, DetectsFlipsBoundsRegionsAndCrossChartOverlap) {
  IndexedMesh mesh;
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{0.0f});
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{5.0f, 0.0f, 0.0f});
  std::vector<glm::vec2> corners;
  for (std::size_t f = 0u; f < mesh.FaceCount(); ++f) {
    for (const auto index : mesh.Faces()[f].Indices) {
      const glm::vec3 p = mesh.Position(index);
      const float x = f < 2u ? p.x : p.x - 5.0f;
      corners.push_back(glm::vec2{0.1f + 0.3f * x, 0.1f + 0.3f * p.y});
    }
  }
  const std::vector<std::uint32_t> charts{0u, 0u, 1u, 1u};
  Atlas::UvAtlasValidationInput input{
      .Positions = mesh.Positions(),
      .Faces = mesh.Faces(),
      .CornerUvs = corners,
      .FaceCharts = charts,
  };
  // Both squares are drawn on the same UV square: distinct charts overlap.
  const auto stacked = Atlas::ValidateUvAtlasCorners(input);
  EXPECT_EQ(stacked.Status, Atlas::UvAtlasStatus::ValidationFailed);
  EXPECT_GT(stacked.Overlaps.OverlapPairCount, 0u);

  for (std::size_t c = 6u; c < corners.size(); ++c) {
    corners[c].x += 0.5f;
  }
  input.CornerUvs = corners;
  EXPECT_TRUE(Atlas::ValidateUvAtlasCorners(input).Passed());

  std::vector<glm::vec2> flipped = corners;
  std::swap(flipped[1], flipped[2]);
  input.CornerUvs = flipped;
  const auto flip = Atlas::ValidateUvAtlasCorners(input);
  EXPECT_EQ(flip.Status, Atlas::UvAtlasStatus::ValidationFailed);
  EXPECT_EQ(flip.NonPositiveOrientationCount, 1u);
  EXPECT_EQ(flip.FirstNonPositiveFace, 0u);

  std::vector<glm::vec2> outside = corners;
  outside[0].x = -1.0e-7f;
  input.CornerUvs = outside;
  EXPECT_EQ(Atlas::ValidateUvAtlasCorners(input).OutOfBoundsUvCount, 1u);

  input.CornerUvs = corners;
  const std::vector<std::uint32_t> regions{3u, 4u, 3u, 3u};
  input.FaceRegions = regions;
  const auto crossing = Atlas::ValidateUvAtlasCorners(input);
  EXPECT_EQ(crossing.Status, Atlas::UvAtlasStatus::ValidationFailed);
  EXPECT_EQ(crossing.RegionCrossingChartCount, 1u);
  EXPECT_EQ(crossing.RegionComponentCount, 3u);
}

TEST(UvAtlasValidation, ResolveRejectsInvalidCallerBackendOutput) {
  IndexedMesh mesh;
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{0.0f});
  const Atlas::UvAtlasBackend overlapping{
      .Name = "overlapping",
      .Generate = [](const Atlas::UvAtlasInput &input,
                     const Atlas::UvAtlasOptions &options) {
        Atlas::UvAtlasResult result{};
        result.Status = Atlas::UvAtlasStatus::Success;
        result.Provenance = Atlas::UvAtlasProvenance::Generated;
        result.Diagnostics.ActualDistortion = options.Distortion;
        for (std::size_t i = 0u; i < input.Positions.size(); ++i) {
          (void)result.OutputMesh.AddVertex(input.Positions[i]);
          result.SourceVertexForOutputVertex.push_back(
              static_cast<std::uint32_t>(i));
        }
        auto uvs = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
            Geometry::MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
        for (std::size_t i = 0u; i < input.Positions.size(); ++i) {
          // Every vertex on one diagonal: all triangles collapse.
          uvs.Vector()[i] = glm::vec2{0.25f + 0.5f * input.Positions[i].x,
                                      0.25f + 0.5f * input.Positions[i].x};
        }
        for (std::size_t f = 0u; f < input.Faces.size(); ++f) {
          const auto &face = input.Faces[f];
          (void)result.OutputMesh.AddTriangle(face.Indices[0], face.Indices[1],
                                              face.Indices[2]);
          result.SourceFaceForOutputFace.push_back(static_cast<std::uint32_t>(f));
          result.OutputFaceChart.push_back(0u);
        }
        return result;
      }};
  const auto result = Atlas::ResolveUvAtlas(
      InputFor(mesh), GenerateOptions(Atlas::UvAtlasDistortion::Both),
      &overlapping);
  EXPECT_EQ(result.Status, Atlas::UvAtlasStatus::ValidationFailed);
  EXPECT_GT(result.Diagnostics.Validation.NonPositiveOrientationCount, 0u);
  EXPECT_NE(result.Diagnostics.BackendDetail.find("independent validation"),
            std::string::npos);
}

TEST(UvAtlasValidation, ChartsWithoutATexelCenterAreUnderResolved) {
  // An isometric sliver along the pixel diagonal: 8 texels of area, but
  // x - y stays within [0.3, 0.7] while every texel center has integral
  // x - y, so rasterization would sample nothing.
  constexpr float kExtent = 64.0f;
  const auto validate = [&](const float shift) {
    IndexedMesh mesh;
    const std::vector<glm::vec2> pixels{{10.3f + shift, 10.0f},
                                        {50.7f + shift, 50.0f},
                                        {50.3f + shift, 50.0f}};
    for (const glm::vec2 p : pixels) {
      (void)mesh.AddVertex(glm::vec3{p, 0.0f});
    }
    (void)mesh.AddTriangle(0u, 1u, 2u);
    std::vector<glm::vec2> corners;
    for (const glm::vec2 p : pixels) {
      corners.push_back(p / kExtent);
    }
    const std::vector<std::uint32_t> charts{0u};
    Atlas::UvAtlasValidationOptions options{};
    options.AtlasWidth = 64u;
    options.AtlasHeight = 64u;
    return Atlas::ValidateUvAtlasCorners(
        Atlas::UvAtlasValidationInput{.Positions = mesh.Positions(),
                                      .Faces = mesh.Faces(),
                                      .CornerUvs = corners,
                                      .FaceCharts = charts},
        options);
  };
  const auto missed = validate(0.0f);
  EXPECT_EQ(missed.Status, Atlas::UvAtlasStatus::UnderResolved)
      << missed.Detail;
  EXPECT_EQ(missed.UnderResolvedChartCount, 0u);
  EXPECT_GT(missed.MinChartTexelArea, 1.0);
  EXPECT_EQ(missed.ChartsWithoutTexelCenterCount, 1u);

  // Shifted by half a texel the widening end covers the x - y = 1 centers.
  const auto covered = validate(0.5f);
  EXPECT_TRUE(covered.Passed()) << covered.Detail;
  EXPECT_EQ(covered.ChartsWithoutTexelCenterCount, 0u);
}

TEST(UvAtlasQuality, PreservedAuthoredUvsPublishCompleteChartAndRegionMaps) {
  // Two separate 2x2 grids with valid authored per-vertex UVs.
  IndexedMesh mesh;
  AppendGrid(mesh, 2u, 1.0f, glm::vec3{0.0f});
  AppendGrid(mesh, 2u, 1.0f, glm::vec3{5.0f, 0.0f, 0.0f});
  std::vector<glm::vec2> authored;
  for (const glm::vec3 p : mesh.Positions()) {
    authored.push_back(p.x < 4.0f
                           ? glm::vec2{0.05f + 0.1f * p.x, 0.05f + 0.1f * p.y}
                           : glm::vec2{0.55f + 0.1f * (p.x - 5.0f),
                                       0.05f + 0.1f * p.y});
  }
  const std::size_t faceCount = mesh.FaceCount();
  const std::size_t half = faceCount / 2u;
  const auto resolve = [&](const std::vector<std::uint32_t> &regions) {
    Atlas::UvAtlasInput input = Atlas::BorrowInput(mesh, authored);
    input.FaceRegions = regions;
    return Atlas::ResolveUvAtlas(input);
  };
  const auto expectPerGrid = [&](const std::vector<std::uint32_t> &values) {
    ASSERT_EQ(values.size(), faceCount);
    for (std::size_t f = 0u; f < faceCount; ++f) {
      EXPECT_EQ(values[f], f < half ? 0u : 1u) << "face " << f;
    }
  };

  for (const std::vector<std::uint32_t> &regions :
       {std::vector<std::uint32_t>{}, [&] {
          std::vector<std::uint32_t> labels(faceCount, 7u);
          std::fill(labels.begin() + static_cast<std::ptrdiff_t>(half),
                    labels.end(), 9u);
          return labels;
        }()}) {
    SCOPED_TRACE(regions.empty() ? "no regions" : "one label per grid");
    const auto result = resolve(regions);
    ASSERT_EQ(result.Status, Atlas::UvAtlasStatus::Success)
        << result.Diagnostics.BackendDetail;
    EXPECT_EQ(result.Provenance, Atlas::UvAtlasProvenance::AuthoredPreserved);
    EXPECT_EQ(result.Diagnostics.ChartCount, 2u);
    EXPECT_EQ(result.Diagnostics.ConnectedComponentCount, 2u);
    EXPECT_EQ(result.Diagnostics.RegionComponentCount, 2u);
    EXPECT_EQ(result.Diagnostics.RegionLabelCount, regions.empty() ? 1u : 2u);
    expectPerGrid(result.SourceFaceChart);
    expectPerGrid(result.SourceFaceRegionComponent);
    ASSERT_EQ(result.OutputFaceChart.size(), faceCount);
    for (std::size_t f = 0u; f < faceCount; ++f) {
      EXPECT_EQ(result.OutputFaceChart[f], result.SourceFaceChart[f]);
    }
    EXPECT_EQ(result.SourceCornerUvs.size(), 3u * faceCount);
  }

  // Labels splitting one UV-continuous authored chart cannot be honored by
  // preservation: the atlas is regenerated and says why.
  std::vector<std::uint32_t> split(faceCount, 1u);
  std::fill(split.begin(), split.begin() + 4, 2u);
  const auto regenerated = resolve(split);
  ASSERT_EQ(regenerated.Status, Atlas::UvAtlasStatus::Success)
      << regenerated.Diagnostics.BackendDetail;
  EXPECT_EQ(regenerated.Provenance, Atlas::UvAtlasProvenance::Generated);
  EXPECT_EQ(regenerated.Diagnostics.ActualMethod,
            Atlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(regenerated.Diagnostics.PreservedAuthoredUvCount, 0u);
  EXPECT_NE(regenerated.Diagnostics.BackendDetail.find(
                "valid authored UVs not preserved"),
            std::string::npos)
      << regenerated.Diagnostics.BackendDetail;
  EXPECT_TRUE(regenerated.Diagnostics.Validation.Passed())
      << regenerated.Diagnostics.Validation.Detail;
  EXPECT_EQ(regenerated.Diagnostics.Validation.RegionCrossingChartCount, 0u);
  EXPECT_EQ(regenerated.Diagnostics.RegionComponentCount, 3u);
  ASSERT_EQ(regenerated.SourceFaceRegionComponent.size(), faceCount);
  ASSERT_EQ(regenerated.SourceFaceChart.size(), faceCount);

  const std::vector<std::uint32_t> wrongCount(faceCount - 1u, 0u);
  EXPECT_EQ(resolve(wrongCount).Status,
            Atlas::UvAtlasStatus::InvalidRegionLabels);
}

TEST(UvAtlasValidation, AcceptedCallerBackendOutputGetsCompleteSourceMaps) {
  IndexedMesh mesh;
  AppendGrid(mesh, 1u, 1.0f, glm::vec3{0.0f});
  // A valid single-chart atlas that fills only the output-mesh fields.
  const Atlas::UvAtlasBackend minimal{
      .Name = "minimal",
      .Generate = [](const Atlas::UvAtlasInput &input,
                     const Atlas::UvAtlasOptions &options) {
        Atlas::UvAtlasResult result{};
        result.Status = Atlas::UvAtlasStatus::Success;
        result.Provenance = Atlas::UvAtlasProvenance::Generated;
        result.Diagnostics.ActualDistortion = options.Distortion;
        for (std::size_t i = 0u; i < input.Positions.size(); ++i) {
          (void)result.OutputMesh.AddVertex(input.Positions[i]);
          result.SourceVertexForOutputVertex.push_back(
              static_cast<std::uint32_t>(i));
        }
        auto uvs = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
            Geometry::MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
        for (std::size_t i = 0u; i < input.Positions.size(); ++i) {
          uvs.Vector()[i] = glm::vec2{0.25f + 0.5f * input.Positions[i].x,
                                      0.25f + 0.5f * input.Positions[i].y};
        }
        for (std::size_t f = 0u; f < input.Faces.size(); ++f) {
          const auto &face = input.Faces[f];
          (void)result.OutputMesh.AddTriangle(face.Indices[0], face.Indices[1],
                                              face.Indices[2]);
          result.SourceFaceForOutputFace.push_back(static_cast<std::uint32_t>(f));
          result.OutputFaceChart.push_back(0u);
        }
        return result;
      }};
  const auto result = Atlas::ResolveUvAtlas(
      InputFor(mesh), GenerateOptions(Atlas::UvAtlasDistortion::Both),
      &minimal);
  ASSERT_EQ(result.Status, Atlas::UvAtlasStatus::Success)
      << result.Diagnostics.BackendDetail;
  EXPECT_EQ(result.SourceFaceChart, (std::vector<std::uint32_t>{0u, 0u}));
  EXPECT_EQ(result.SourceFaceRegionComponent,
            (std::vector<std::uint32_t>{0u, 0u}));
  EXPECT_EQ(result.SourceCornerUvs.size(), 6u);
}

TEST(UvAtlasQuality, PackingAlignsThinChartsToTexelCentersWithoutChangingDensity) {
  IndexedMesh mesh;
  const auto a = mesh.AddVertex({0.0f, 0.0f, 0.0f});
  const auto b = mesh.AddVertex({10.0f, 0.0f, 0.0f});
  const auto c = mesh.AddVertex({0.0f, 10.0f, 0.0f});
  (void)mesh.AddTriangle(a, b, c);
  const auto d = mesh.AddVertex({20.0f, 0.0f, 0.0f});
  const auto e = mesh.AddVertex({40.0f, 0.0f, 0.0f});
  const auto f = mesh.AddVertex({20.0f, 0.02f, 0.0f});
  (void)mesh.AddTriangle(d, e, f);
  auto options = GenerateOptions(Atlas::UvAtlasDistortion::Both);
  options.Resolution = 128u;
  options.Padding = 2u;
  const auto result = Atlas::ResolveUvAtlas(InputFor(mesh), options);
  ExpectAcceptedAtlas(mesh, result, options);
  EXPECT_EQ(result.Diagnostics.ChartCount, 2u);
  EXPECT_LT(result.Diagnostics.Validation.MaxConformalDistortion, 1.01);
  EXPECT_LT(result.Diagnostics.Validation.MaxAreaDistortion, 1.01);
}
