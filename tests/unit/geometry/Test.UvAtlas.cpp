#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

namespace {
struct FakeBackendState {
  int Called{0};
  std::size_t PositionCount{0};
  std::size_t FaceCount{0};
  std::uint32_t Resolution{0};
};

FakeBackendState g_FakeBackendState{};

[[nodiscard]] Geometry::MeshSoup::IndexedMesh MakeSquareMesh() {
  Geometry::MeshSoup::IndexedMesh mesh;
  const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
  const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
  const auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
  const auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
  (void)mesh.AddTriangle(v0, v1, v2);
  (void)mesh.AddTriangle(v0, v2, v3);
  return mesh;
}

[[nodiscard]] Geometry::MeshSoup::IndexedMesh
MakeGridMesh(const std::uint32_t side) {
  Geometry::MeshSoup::IndexedMesh mesh;
  std::vector<std::uint32_t> vertices;
  vertices.reserve(static_cast<std::size_t>(side + 1u) *
                   static_cast<std::size_t>(side + 1u));
  for (std::uint32_t y = 0u; y <= side; ++y) {
    for (std::uint32_t x = 0u; x <= side; ++x) {
      vertices.push_back(mesh.AddVertex(
          {static_cast<float>(x), static_cast<float>(y), 0.0f}));
    }
  }

  const auto vertex = [&vertices, side](const std::uint32_t x,
                                        const std::uint32_t y) {
    return vertices[static_cast<std::size_t>(y) *
                        static_cast<std::size_t>(side + 1u) +
                    static_cast<std::size_t>(x)];
  };
  for (std::uint32_t y = 0u; y < side; ++y) {
    for (std::uint32_t x = 0u; x < side; ++x) {
      const std::uint32_t v00 = vertex(x, y);
      const std::uint32_t v10 = vertex(x + 1u, y);
      const std::uint32_t v01 = vertex(x, y + 1u);
      const std::uint32_t v11 = vertex(x + 1u, y + 1u);
      (void)mesh.AddTriangle(v00, v10, v11);
      (void)mesh.AddTriangle(v00, v11, v01);
    }
  }
  return mesh;
}

[[nodiscard]] Geometry::MeshSoup::IndexedMesh MakeNonmanifoldEdgeMesh() {
  Geometry::MeshSoup::IndexedMesh mesh;
  const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
  const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
  const auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
  const auto v3 = mesh.AddVertex({0.0f, 0.0f, 1.0f});
  const auto v4 = mesh.AddVertex({0.0f, -1.0f, 0.0f});
  (void)mesh.AddTriangle(v0, v1, v2);
  (void)mesh.AddTriangle(v1, v0, v3);
  (void)mesh.AddTriangle(v0, v1, v4);
  return mesh;
}

[[nodiscard]] Geometry::MeshSoup::IndexedMesh MakeDegenerateMesh() {
  Geometry::MeshSoup::IndexedMesh mesh;
  const auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
  const auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
  const auto v2 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
  (void)mesh.AddTriangle(v0, v1, v2);
  return mesh;
}

[[nodiscard]] Geometry::MeshSoup::IndexedMesh MakeCubeMesh() {
  Geometry::MeshSoup::IndexedMesh mesh;
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

[[nodiscard]] std::vector<glm::vec2> SquareUvs() {
  return {
      glm::vec2{0.0f, 0.0f},
      glm::vec2{1.0f, 0.0f},
      glm::vec2{1.0f, 1.0f},
      glm::vec2{0.0f, 1.0f},
  };
}

[[nodiscard]] Geometry::UvAtlas::UvAtlasResult
FakeBackend(const Geometry::UvAtlas::UvAtlasInput &input,
            const Geometry::UvAtlas::UvAtlasOptions &options) {
  ++g_FakeBackendState.Called;
  g_FakeBackendState.PositionCount = input.Positions.size();
  g_FakeBackendState.FaceCount = input.Faces.size();
  g_FakeBackendState.Resolution = options.Resolution;

  Geometry::UvAtlas::UvAtlasResult result{};
  result.Status = Geometry::UvAtlas::UvAtlasStatus::Success;
  result.Provenance = Geometry::UvAtlas::UvAtlasProvenance::Generated;
  result.Diagnostics.Status = result.Status;
  result.Diagnostics.Provenance = result.Provenance;
  result.Diagnostics.BackendName = "fake";

  result.SourceVertexForOutputVertex.reserve(input.Positions.size());
  for (std::size_t i = 0; i < input.Positions.size(); ++i) {
    (void)result.OutputMesh.AddVertex(input.Positions[i]);
    result.SourceVertexForOutputVertex.push_back(static_cast<std::uint32_t>(i));
  }

  auto uvs = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
  for (std::size_t i = 0; i < input.Positions.size(); ++i) {
    uvs.Vector()[i] = glm::vec2{input.Positions[i].x, input.Positions[i].y};
  }

  result.SourceFaceForOutputFace.reserve(input.Faces.size());
  result.OutputFaceChart.reserve(input.Faces.size());
  for (std::size_t faceIndex = 0; faceIndex < input.Faces.size(); ++faceIndex) {
    const auto &face = input.Faces[faceIndex];
    (void)result.OutputMesh.AddTriangle(face.Indices[0], face.Indices[1],
                                        face.Indices[2]);
    result.SourceFaceForOutputFace.push_back(
        static_cast<std::uint32_t>(faceIndex));
    result.OutputFaceChart.push_back(0u);
  }
  return result;
}

[[nodiscard]] Geometry::UvAtlas::UvAtlasResult
FailingFastBackend(const Geometry::UvAtlas::UvAtlasInput &input,
                   const Geometry::UvAtlas::UvAtlasOptions &) {
  Geometry::UvAtlas::UvAtlasResult result{};
  result.Status = Geometry::UvAtlas::UvAtlasStatus::BackendFailed;
  result.Provenance = Geometry::UvAtlas::UvAtlasProvenance::None;
  result.Diagnostics.InputVertexCount = input.Positions.size();
  result.Diagnostics.InputFaceCount = input.Faces.size();
  result.Diagnostics.Status = result.Status;
  result.Diagnostics.Provenance = result.Provenance;
  result.Diagnostics.BackendName = "failing-fast";
  result.Diagnostics.BackendDetail = "test backend failure";
  return result;
}

void ExpectFiniteNormalizedUv(const glm::vec2 uv) {
  EXPECT_TRUE(std::isfinite(uv.x));
  EXPECT_TRUE(std::isfinite(uv.y));
  EXPECT_GE(uv.x, -1.0e-5f);
  EXPECT_GE(uv.y, -1.0e-5f);
  EXPECT_LE(uv.x, 1.0f + 1.0e-5f);
  EXPECT_LE(uv.y, 1.0f + 1.0e-5f);
}

[[nodiscard]] bool
OverlapInterior(const Geometry::UvAtlas::UvAtlasChartRecord &a,
                const Geometry::UvAtlas::UvAtlasChartRecord &b) {
  constexpr float epsilon = 1.0e-6f;
  return a.UvMin.x < b.UvMax.x - epsilon && a.UvMax.x > b.UvMin.x + epsilon &&
         a.UvMin.y < b.UvMax.y - epsilon && a.UvMax.y > b.UvMin.y + epsilon;
}

void ExpectSuccessfulChartQuality(
    const Geometry::UvAtlas::UvAtlasChartRecord &chart) {
  EXPECT_FALSE(chart.ParameterizationBackend.empty());
  EXPECT_NE(chart.ParameterizationBackend, "projection");
  EXPECT_EQ(
      chart.Quality.Status,
      Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
  EXPECT_GT(chart.Quality.EvaluatedFaceCount, 0u);
  EXPECT_EQ(chart.Quality.FlippedElementCount, 0u);
}

void ExpectSameAtlasRecords(
    const Geometry::UvAtlas::UvAtlasResult &expected,
    const Geometry::UvAtlas::UvAtlasResult &actual) {
  EXPECT_EQ(actual.Status, expected.Status);
  EXPECT_EQ(actual.Provenance, expected.Provenance);
  EXPECT_EQ(actual.SourceVertexForOutputVertex,
            expected.SourceVertexForOutputVertex);
  EXPECT_EQ(actual.SourceFaceForOutputFace, expected.SourceFaceForOutputFace);
  EXPECT_EQ(actual.OutputFaceChart, expected.OutputFaceChart);
  ASSERT_EQ(actual.Charts.size(), expected.Charts.size());
  for (std::size_t i = 0u; i < expected.Charts.size(); ++i) {
    const auto &a = actual.Charts[i];
    const auto &b = expected.Charts[i];
    EXPECT_EQ(a.ChartId, b.ChartId);
    EXPECT_EQ(a.SourceFaceStart, b.SourceFaceStart);
    EXPECT_EQ(a.SourceFaceCount, b.SourceFaceCount);
    EXPECT_EQ(a.OutputFaceStart, b.OutputFaceStart);
    EXPECT_EQ(a.OutputFaceCount, b.OutputFaceCount);
    EXPECT_EQ(a.OutputVertexStart, b.OutputVertexStart);
    EXPECT_EQ(a.OutputVertexCount, b.OutputVertexCount);
    EXPECT_EQ(a.UvMin, b.UvMin);
    EXPECT_EQ(a.UvMax, b.UvMax);
    EXPECT_EQ(a.ParameterizationBackend, b.ParameterizationBackend);
  }

  ASSERT_EQ(actual.SeamCuts.size(), expected.SeamCuts.size());
  for (std::size_t i = 0u; i < expected.SeamCuts.size(); ++i) {
    const auto &a = actual.SeamCuts[i];
    const auto &b = expected.SeamCuts[i];
    EXPECT_EQ(a.SourceVertexA, b.SourceVertexA);
    EXPECT_EQ(a.SourceVertexB, b.SourceVertexB);
    EXPECT_EQ(a.SourceFaceA, b.SourceFaceA);
    EXPECT_EQ(a.SourceFaceB, b.SourceFaceB);
    EXPECT_EQ(a.ChartA, b.ChartA);
    EXPECT_EQ(a.ChartB, b.ChartB);
    EXPECT_EQ(a.Boundary, b.Boundary);
  }

  const auto actualUvs = actual.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  const auto expectedUvs = expected.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  ASSERT_TRUE(actualUvs.IsValid());
  ASSERT_TRUE(expectedUvs.IsValid());
  EXPECT_EQ(actualUvs.Vector(), expectedUvs.Vector());
}
} // namespace

TEST(UvAtlas, FakeBackendCanReplaceDefault) {
  g_FakeBackendState = {};
  auto mesh = MakeSquareMesh();
  const Geometry::UvAtlas::UvAtlasBackend fakeBackend{.Name = "fake",
                                                      .Generate = &FakeBackend};

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Resolution = 256u;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options, &fakeBackend);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success);
  EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
  EXPECT_EQ(g_FakeBackendState.Called, 1);
  EXPECT_EQ(g_FakeBackendState.PositionCount, mesh.VertexCount());
  EXPECT_EQ(g_FakeBackendState.FaceCount, mesh.FaceCount());
  EXPECT_EQ(g_FakeBackendState.Resolution, 256u);
  EXPECT_EQ(result.Diagnostics.BackendName, "fake");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_FALSE(result.Diagnostics.UsedFallback);
}

TEST(UvAtlas, FastStagedRequestUsesBuiltInBackendWithoutXAtlasFallback) {
  auto mesh = MakeSquareMesh();

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = false;
  options.Resolution = 64u;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(result.Status);
  EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
  EXPECT_EQ(result.Diagnostics.BackendName, "fast-staged");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_FALSE(result.Diagnostics.UsedFallback);
  EXPECT_TRUE(result.Diagnostics.FallbackReason.empty());
  EXPECT_EQ(result.Diagnostics.ChartCount, 1u);
  EXPECT_EQ(result.Diagnostics.SeamCutCount, 0u);
  EXPECT_EQ(result.Diagnostics.BoundarySeamCount, 4u);
  EXPECT_EQ(result.Charts.size(), 1u);
  EXPECT_EQ(result.SeamCuts.size(), 4u);
  EXPECT_EQ(result.OutputMesh.FaceCount(), mesh.FaceCount());
  EXPECT_EQ(result.OutputMesh.VertexCount(), mesh.VertexCount());
  EXPECT_EQ(
      result.Diagnostics.Quality.Status,
      Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
  ASSERT_EQ(result.Charts.size(), 1u);
  ExpectSuccessfulChartQuality(result.Charts.front());
  EXPECT_NE(result.Diagnostics.BackendDetail.find("shelf packing"),
            std::string::npos);

  const auto outputUvs = result.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  ASSERT_TRUE(outputUvs.IsValid());
  for (const glm::vec2 uv : outputUvs.Vector()) {
    ExpectFiniteNormalizedUv(uv);
  }
  for (std::size_t i = 0; i < result.Charts.size(); ++i) {
    for (std::size_t j = i + 1u; j < result.Charts.size(); ++j) {
      EXPECT_FALSE(OverlapInterior(result.Charts[i], result.Charts[j]))
          << "chart " << i << " overlaps chart " << j;
    }
  }
}

TEST(UvAtlas, FastStagedCubeCutsFaceChartsAndCopiesProperties) {
  auto mesh = MakeCubeMesh();
  auto colors =
      mesh.GetOrAddVertexProperty<glm::vec4>("v:color", glm::vec4{0.0f});
  for (std::size_t i = 0; i < mesh.VertexCount(); ++i) {
    colors.Vector()[i] =
        glm::vec4{static_cast<float>(i + 1u), static_cast<float>(i + 2u),
                  static_cast<float>(i + 3u), 1.0f};
  }

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = false;
  options.Resolution = 128u;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(result.Status);
  EXPECT_EQ(result.Diagnostics.BackendName, "fast-staged");
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ChartCount, 6u);
  EXPECT_EQ(result.Charts.size(), 6u);
  EXPECT_EQ(result.OutputMesh.VertexCount(), 24u);
  EXPECT_EQ(result.SourceVertexForOutputVertex.size(),
            result.OutputMesh.VertexCount());
  EXPECT_EQ(result.Diagnostics.SeamCutCount, 12u);
  EXPECT_EQ(result.Diagnostics.BoundarySeamCount, 0u);
  EXPECT_EQ(
      result.Diagnostics.Quality.Status,
      Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
  EXPECT_EQ(result.Diagnostics.Quality.FlippedElementCount, 0u);
  EXPECT_NE(result.Diagnostics.BackendDetail.find("shelf packing"),
            std::string::npos);
  for (const Geometry::UvAtlas::UvAtlasChartRecord &chart : result.Charts) {
    ExpectSuccessfulChartQuality(chart);
  }
  for (std::size_t i = 0; i < result.Charts.size(); ++i) {
    for (std::size_t j = i + 1u; j < result.Charts.size(); ++j) {
      EXPECT_FALSE(OverlapInterior(result.Charts[i], result.Charts[j]))
          << "chart " << i << " overlaps chart " << j;
    }
  }

  const auto outputColors =
      result.OutputMesh.GetVertexProperty<glm::vec4>("v:color");
  ASSERT_TRUE(outputColors.IsValid());
  for (std::size_t i = 0; i < result.SourceVertexForOutputVertex.size(); ++i) {
    const std::uint32_t source = result.SourceVertexForOutputVertex[i];
    ASSERT_LT(source, mesh.VertexCount());
    EXPECT_EQ(outputColors[i], colors.Vector()[source]);
  }
}

TEST(UvAtlas, FastStagedHighCardinalityGridIsFiniteAndDeterministic) {
  constexpr std::uint32_t side = 20u;
  auto mesh = MakeGridMesh(side);

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = false;
  options.Resolution = 256u;

  const auto first = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);
  const auto second = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(first.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(first.Status);
  ASSERT_EQ(second.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(second.Status);
  EXPECT_EQ(first.OutputMesh.FaceCount(), mesh.FaceCount());
  EXPECT_EQ(first.SourceFaceForOutputFace.size(), mesh.FaceCount());
  EXPECT_EQ(first.Diagnostics.ChartCount, 1u);
  EXPECT_EQ(first.Diagnostics.SeamCutCount, 0u);
  EXPECT_EQ(first.Diagnostics.BoundarySeamCount, side * 4u);
  const auto outputUvs = first.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  ASSERT_TRUE(outputUvs.IsValid());
  for (const glm::vec2 uv : outputUvs.Vector()) {
    ExpectFiniteNormalizedUv(uv);
  }
  ExpectSameAtlasRecords(first, second);
}

TEST(UvAtlas, FastStagedKeepsFirstSeenBoundaryAndNonmanifoldSeamOrder) {
  auto mesh = MakeNonmanifoldEdgeMesh();

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = false;
  options.Resolution = 128u;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(result.Status);
  EXPECT_EQ(result.Diagnostics.ChartCount, 3u);
  EXPECT_EQ(result.Diagnostics.SeamCutCount, 3u);
  EXPECT_EQ(result.Diagnostics.BoundarySeamCount, 6u);
  ASSERT_EQ(result.SeamCuts.size(), 9u);

  for (std::size_t i = 0u; i < 3u; ++i) {
    EXPECT_EQ(result.SeamCuts[i].SourceVertexA, 0u);
    EXPECT_EQ(result.SeamCuts[i].SourceVertexB, 1u);
    EXPECT_FALSE(result.SeamCuts[i].Boundary);
  }
  EXPECT_EQ(result.SeamCuts[0].SourceFaceA, 0u);
  EXPECT_EQ(result.SeamCuts[0].SourceFaceB, 1u);
  EXPECT_EQ(result.SeamCuts[1].SourceFaceA, 0u);
  EXPECT_EQ(result.SeamCuts[1].SourceFaceB, 2u);
  EXPECT_EQ(result.SeamCuts[2].SourceFaceA, 1u);
  EXPECT_EQ(result.SeamCuts[2].SourceFaceB, 2u);

  const std::vector<std::pair<std::uint32_t, std::uint32_t>>
      expectedBoundaryEdges{
          {1u, 2u}, {0u, 2u}, {0u, 3u},
          {1u, 3u}, {1u, 4u}, {0u, 4u},
      };
  for (std::size_t i = 0u; i < expectedBoundaryEdges.size(); ++i) {
    const auto &seam = result.SeamCuts[i + 3u];
    EXPECT_TRUE(seam.Boundary);
    EXPECT_EQ(seam.SourceVertexA, expectedBoundaryEdges[i].first);
    EXPECT_EQ(seam.SourceVertexB, expectedBoundaryEdges[i].second);
  }
}

TEST(UvAtlas, FastStagedFailingBackendFallsBackToXAtlasWhenAllowed) {
  auto mesh = MakeSquareMesh();
  const Geometry::UvAtlas::UvAtlasBackend failingFast{
      .Name = "failing-fast", .Generate = &FailingFastBackend};

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = true;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options, &failingFast);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(result.Status);
  EXPECT_EQ(result.Diagnostics.BackendName, "xatlas");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::XAtlas);
  EXPECT_TRUE(result.Diagnostics.UsedFallback);
  EXPECT_NE(result.Diagnostics.FallbackReason.find("failing-fast"),
            std::string::npos);
}

TEST(UvAtlas, FastStagedFailingBackendFailsClosedWhenXAtlasFallbackDisabled) {
  auto mesh = MakeSquareMesh();
  const Geometry::UvAtlas::UvAtlasBackend failingFast{
      .Name = "failing-fast", .Generate = &FailingFastBackend};

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = false;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options, &failingFast);

  EXPECT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::BackendFailed);
  EXPECT_EQ(result.Diagnostics.BackendName, "failing-fast");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_FALSE(result.Diagnostics.UsedFallback);
}

TEST(UvAtlas, FastStagedRequestCanUseCallerSuppliedBackend) {
  g_FakeBackendState = {};
  auto mesh = MakeSquareMesh();
  const Geometry::UvAtlas::UvAtlasBackend fastBackend{
      .Name = "fast-staged-fake", .Generate = &FakeBackend};

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = false;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::FastStaged;
  options.AllowXAtlasFallback = false;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options, &fastBackend);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success);
  EXPECT_EQ(g_FakeBackendState.Called, 1);
  EXPECT_EQ(result.Diagnostics.BackendName, "fake");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_FALSE(result.Diagnostics.UsedFallback);
}

TEST(UvAtlas, ValidAuthoredUvsArePreservedWithoutCallingBackend) {
  g_FakeBackendState = {};
  auto mesh = MakeSquareMesh();
  const auto authoredUvs = SquareUvs();
  const Geometry::UvAtlas::UvAtlasBackend fakeBackend{.Name = "fake",
                                                      .Generate = &FakeBackend};

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh, authoredUvs), {}, &fakeBackend);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success);
  EXPECT_EQ(result.Provenance,
            Geometry::UvAtlas::UvAtlasProvenance::AuthoredPreserved);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::Authored);
  EXPECT_EQ(g_FakeBackendState.Called, 0);
  ASSERT_EQ(result.OutputMesh.VertexCount(), authoredUvs.size());
  const auto outputUvs = result.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  ASSERT_TRUE(outputUvs.IsValid());
  for (std::size_t i = 0; i < authoredUvs.size(); ++i) {
    EXPECT_EQ(outputUvs[i], authoredUvs[i]);
    EXPECT_EQ(result.SourceVertexForOutputVertex[i], i);
  }
}

TEST(UvAtlas, MissingUvsGenerateFiniteNormalizedFastStagedUvsByDefault) {
  auto mesh = MakeSquareMesh();

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = true;
  options.Resolution = 64u;
  options.Padding = 1u;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(result.Status);
  EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
  EXPECT_EQ(result.Diagnostics.BackendName, "fast-staged");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
  EXPECT_GE(result.OutputMesh.VertexCount(), mesh.VertexCount());
  EXPECT_EQ(result.OutputMesh.FaceCount(), mesh.FaceCount());
  EXPECT_GT(result.Diagnostics.ChartCount, 0u);
  EXPECT_GT(result.Diagnostics.AtlasWidth, 0u);
  EXPECT_GT(result.Diagnostics.AtlasHeight, 0u);
  EXPECT_EQ(
      result.Diagnostics.Quality.Status,
      Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);
  EXPECT_EQ(result.Diagnostics.Quality.FlippedElementCount, 0u);

  const auto outputUvs = result.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  ASSERT_TRUE(outputUvs.IsValid());
  for (const glm::vec2 uv : outputUvs.Vector()) {
    ExpectFiniteNormalizedUv(uv);
  }
}

TEST(UvAtlas, ExplicitXAtlasMethodGeneratesFiniteNormalizedUvs) {
  auto mesh = MakeSquareMesh();

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.PreserveValidAuthoredUvs = true;
  options.Method = Geometry::UvAtlas::UvAtlasMethod::XAtlas;
  options.BackendName = "xatlas";
  options.Resolution = 64u;
  options.Padding = 1u;

  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success)
      << Geometry::UvAtlas::ToString(result.Status);
  EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
  EXPECT_EQ(result.Diagnostics.BackendName, "xatlas");
  EXPECT_EQ(result.Diagnostics.RequestedMethod,
            Geometry::UvAtlas::UvAtlasMethod::XAtlas);
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::XAtlas);
  EXPECT_GE(result.OutputMesh.VertexCount(), mesh.VertexCount());
  EXPECT_EQ(result.OutputMesh.FaceCount(), mesh.FaceCount());
  EXPECT_GT(result.Diagnostics.ChartCount, 0u);
  EXPECT_GT(result.Diagnostics.AtlasWidth, 0u);
  EXPECT_GT(result.Diagnostics.AtlasHeight, 0u);
  EXPECT_EQ(
      result.Diagnostics.Quality.Status,
      Geometry::Parameterization::ParameterizationDiagnosticsStatus::Success);

  const auto outputUvs = result.OutputMesh.GetVertexProperty<glm::vec2>(
      Geometry::MeshUtils::kVertexTexcoordPropertyName);
  ASSERT_TRUE(outputUvs.IsValid());
  for (const glm::vec2 uv : outputUvs.Vector()) {
    ExpectFiniteNormalizedUv(uv);
  }
}

TEST(UvAtlas, SeamSplitXrefsPreserveVertexProperties) {
  auto mesh = MakeCubeMesh();
  auto colors =
      mesh.GetOrAddVertexProperty<glm::vec4>("v:color", glm::vec4{0.0f});
  for (std::size_t i = 0; i < mesh.VertexCount(); ++i) {
    colors.Vector()[i] =
        glm::vec4{static_cast<float>(i + 1u), static_cast<float>(i + 2u),
                  static_cast<float>(i + 3u), 1.0f};
  }

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.Resolution = 128u;
  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success);
  EXPECT_GT(result.OutputMesh.VertexCount(), mesh.VertexCount());
  ASSERT_EQ(result.SourceVertexForOutputVertex.size(),
            result.OutputMesh.VertexCount());

  const auto outputColors =
      result.OutputMesh.GetVertexProperty<glm::vec4>("v:color");
  ASSERT_TRUE(outputColors.IsValid());
  for (std::size_t i = 0; i < result.SourceVertexForOutputVertex.size(); ++i) {
    const std::uint32_t source = result.SourceVertexForOutputVertex[i];
    ASSERT_LT(source, mesh.VertexCount());
    EXPECT_EQ(outputColors[i], colors.Vector()[source]);
  }
}

TEST(UvAtlas, InvalidInputsReturnExplicitStatuses) {
  Geometry::UvAtlas::UvAtlasInput empty{};
  EXPECT_EQ(Geometry::UvAtlas::ResolveUvAtlas(empty).Status,
            Geometry::UvAtlas::UvAtlasStatus::EmptyInput);

  auto outOfRange = MakeSquareMesh();
  outOfRange.Face(0).Indices[1] = 99u;
  EXPECT_EQ(Geometry::UvAtlas::ResolveUvAtlas(
                Geometry::UvAtlas::BorrowInput(outOfRange))
                .Status,
            Geometry::UvAtlas::UvAtlasStatus::OutOfRangeIndex);

  auto nonFinite = MakeSquareMesh();
  nonFinite.Position(1).x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(Geometry::UvAtlas::ResolveUvAtlas(
                Geometry::UvAtlas::BorrowInput(nonFinite))
                .Status,
            Geometry::UvAtlas::UvAtlasStatus::NonFinitePosition);

  auto degenerate = MakeDegenerateMesh();
  EXPECT_EQ(Geometry::UvAtlas::ResolveUvAtlas(
                Geometry::UvAtlas::BorrowInput(degenerate))
                .Status,
            Geometry::UvAtlas::UvAtlasStatus::DegenerateInput);
}

TEST(UvAtlas, InvalidAuthoredUvsFallBackToDefaultBackend) {
  auto mesh = MakeSquareMesh();
  auto authoredUvs = SquareUvs();
  authoredUvs[1].x = std::numeric_limits<float>::quiet_NaN();

  Geometry::UvAtlas::UvAtlasOptions options{};
  options.Resolution = 64u;
  const auto result = Geometry::UvAtlas::ResolveUvAtlas(
      Geometry::UvAtlas::BorrowInput(mesh, authoredUvs), options);

  ASSERT_EQ(result.Status, Geometry::UvAtlas::UvAtlasStatus::Success);
  EXPECT_EQ(result.Provenance, Geometry::UvAtlas::UvAtlasProvenance::Generated);
  EXPECT_EQ(result.Diagnostics.BackendName, "fast-staged");
  EXPECT_EQ(result.Diagnostics.ActualMethod,
            Geometry::UvAtlas::UvAtlasMethod::FastStaged);
}
