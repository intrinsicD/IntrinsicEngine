module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <xatlas.h>

module Geometry.UvAtlas;

import Geometry.Properties;
import Geometry.MeshSoup;
import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Parameterization;
import Geometry.Parameterization.Diagnostics;
import Geometry.Parameterization.Harmonic;

namespace Geometry::UvAtlas {
namespace {
constexpr double kDegenerateAreaEpsilon = 1.0e-12;
constexpr std::uint32_t kInvalidIndex =
    std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool AllUvsFinite(const std::span<const glm::vec2> uvs) noexcept {
  return std::all_of(uvs.begin(), uvs.end(),
                     [](const glm::vec2 uv) { return IsFinite(uv); });
}

[[nodiscard]] UvAtlasDiagnostics MakeDiagnostics(const UvAtlasInput &input) {
  UvAtlasDiagnostics diagnostics{};
  diagnostics.InputVertexCount = input.Positions.size();
  diagnostics.InputFaceCount = input.Faces.size();
  return diagnostics;
}

[[nodiscard]] UvAtlasResult MakeFailure(const UvAtlasInput &input,
                                        const UvAtlasStatus status,
                                        const std::string_view backendName,
                                        const std::string_view detail = {}) {
  UvAtlasResult result{};
  result.Status = status;
  result.Provenance = UvAtlasProvenance::None;
  result.Diagnostics = MakeDiagnostics(input);
  result.Diagnostics.Status = status;
  result.Diagnostics.Provenance = UvAtlasProvenance::None;
  result.Diagnostics.BackendName = std::string{backendName};
  result.Diagnostics.BackendDetail = std::string{detail};
  return result;
}

[[nodiscard]] bool ExtractTriangleIndices(const UvAtlasInput &input,
                                          std::vector<std::uint32_t> &indices) {
  indices.clear();
  indices.reserve(input.Faces.size() * 3u);
  for (const MeshSoup::PolygonFace &face : input.Faces) {
    if (face.Indices.size() != 3u) {
      return false;
    }
    for (const MeshSoup::Index index : face.Indices) {
      if (index >= input.Positions.size()) {
        return false;
      }
      indices.push_back(index);
    }
  }
  return true;
}

[[nodiscard]] std::vector<glm::vec2>
ExtractTexcoords(const MeshSoup::IndexedMesh &mesh) {
  std::vector<glm::vec2> texcoords(mesh.VertexCount(), glm::vec2{0.0f});
  const auto uvProperty = mesh.VertexProperties().Get<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName);
  if (!uvProperty) {
    return texcoords;
  }
  const std::size_t count =
      std::min(texcoords.size(), uvProperty.Vector().size());
  for (std::size_t i = 0; i < count; ++i) {
    texcoords[i] = uvProperty[i];
  }
  return texcoords;
}

void FinalizeUvBounds(UvAtlasDiagnostics &diagnostics,
                      const std::span<const glm::vec2> uvs) {
  if (uvs.empty()) {
    diagnostics.NormalizedUvMin = glm::vec2{0.0f};
    diagnostics.NormalizedUvMax = glm::vec2{0.0f};
    return;
  }

  glm::vec2 minUv{std::numeric_limits<float>::max()};
  glm::vec2 maxUv{std::numeric_limits<float>::lowest()};
  for (const glm::vec2 uv : uvs) {
    minUv = glm::min(minUv, uv);
    maxUv = glm::max(maxUv, uv);
  }
  diagnostics.NormalizedUvMin = minUv;
  diagnostics.NormalizedUvMax = maxUv;
}

[[nodiscard]] Parameterization::ParameterizationDiagnostics
EvaluateQuality(const MeshSoup::IndexedMesh &mesh) {
  std::vector<std::uint32_t> indices;
  indices.reserve(mesh.FaceCount() * 3u);
  for (const MeshSoup::PolygonFace &face : mesh.Faces()) {
    if (face.Indices.size() != 3u) {
      return {};
    }
    indices.push_back(face.Indices[0]);
    indices.push_back(face.Indices[1]);
    indices.push_back(face.Indices[2]);
  }

  const auto halfedge = MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(
      mesh.Positions(), indices);
  if (!halfedge) {
    return {};
  }

  const std::vector<glm::vec2> texcoords = ExtractTexcoords(mesh);
  return Parameterization::EvaluateParameterizationDiagnostics(*halfedge,
                                                               texcoords);
}

struct FastChartFace {
  std::array<std::uint32_t, 3u> Vertices{};
  glm::vec3 Normal{0.0f, 0.0f, 1.0f};
  float DoubleArea{0.0f};
};

struct FastChartProposal {
  std::uint32_t ChartId{0};
  std::vector<std::uint32_t> SourceFaces{};
  glm::vec3 Origin{0.0f};
  glm::vec3 Normal{0.0f, 0.0f, 1.0f};
  glm::vec3 UAxis{1.0f, 0.0f, 0.0f};
  glm::vec3 VAxis{0.0f, 1.0f, 0.0f};
  glm::vec2 LocalMin{0.0f};
  glm::vec2 LocalMax{0.0f};
  glm::vec2 LocalExtent{0.0f};
};

struct FastChartParameterization {
  std::uint32_t ChartId{0};
  std::vector<std::uint32_t> SourceVertices{};
  std::vector<std::uint32_t> SourceVertexToLocal{};
  std::vector<glm::vec3> Positions{};
  std::vector<std::uint32_t> Indices{};
  std::vector<glm::vec2> LocalUvs{};
  glm::vec2 LocalMin{0.0f};
  glm::vec2 LocalMax{0.0f};
  glm::vec2 LocalExtent{0.0f};
  std::string Backend{"projection"};
  Parameterization::ParameterizationDiagnostics Quality{};
};

struct FastChartPlacement {
  glm::vec2 Origin{0.0f};
  float Scale{1.0f};
  bool Rotated{false};
};

struct SourceEdgeFaces {
  std::uint32_t A{kInvalidIndex};
  std::uint32_t B{kInvalidIndex};
  std::vector<std::uint32_t> Faces{};
};

[[nodiscard]] glm::vec3 AnyPerpendicular(const glm::vec3 normal) noexcept {
  const glm::vec3 axis = std::abs(normal.z) < 0.9f
                             ? glm::vec3{0.0f, 0.0f, 1.0f}
                             : glm::vec3{0.0f, 1.0f, 0.0f};
  return glm::normalize(glm::cross(axis, normal));
}

[[nodiscard]] float
MeshScale(const std::span<const glm::vec3> positions) noexcept {
  if (positions.empty()) {
    return 1.0f;
  }

  glm::vec3 minPosition = positions.front();
  glm::vec3 maxPosition = positions.front();
  for (const glm::vec3 position : positions) {
    minPosition = glm::min(minPosition, position);
    maxPosition = glm::max(maxPosition, position);
  }

  return std::max(glm::length(maxPosition - minPosition), 1.0f);
}

[[nodiscard]] std::vector<SourceEdgeFaces>
BuildSourceEdgeFaceGroups(const UvAtlasInput &input) {
  std::vector<SourceEdgeFaces> groups;
  groups.reserve(input.Faces.size() * 3u);
  std::unordered_map<std::uint64_t, std::size_t> groupIndexByKey;
  groupIndexByKey.reserve(input.Faces.size() * 3u);

  for (std::size_t faceIndex = 0u; faceIndex < input.Faces.size();
       ++faceIndex) {
    const MeshSoup::PolygonFace &face = input.Faces[faceIndex];
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const std::uint32_t rawA = face.Indices[corner];
      const std::uint32_t rawB = face.Indices[(corner + 1u) % 3u];
      const std::uint32_t a = std::min(rawA, rawB);
      const std::uint32_t b = std::max(rawA, rawB);
      const std::uint64_t key = (static_cast<std::uint64_t>(a) << 32u) |
                                static_cast<std::uint64_t>(b);

      const auto [match, inserted] =
          groupIndexByKey.try_emplace(key, groups.size());
      if (inserted) {
        groups.push_back(SourceEdgeFaces{
            .A = a,
            .B = b,
            .Faces = {static_cast<std::uint32_t>(faceIndex)},
        });
      } else {
        groups[match->second].Faces.push_back(
            static_cast<std::uint32_t>(faceIndex));
      }
    }
  }

  return groups;
}

[[nodiscard]] std::vector<std::vector<std::uint32_t>>
BuildFaceAdjacency(const std::vector<SourceEdgeFaces> &edgeGroups,
                   const std::size_t faceCount) {
  std::vector<std::vector<std::uint32_t>> adjacency(faceCount);
  for (const SourceEdgeFaces &edge : edgeGroups) {
    for (std::size_t i = 0u; i < edge.Faces.size(); ++i) {
      for (std::size_t j = i + 1u; j < edge.Faces.size(); ++j) {
        const std::uint32_t a = edge.Faces[i];
        const std::uint32_t b = edge.Faces[j];
        adjacency[a].push_back(b);
        adjacency[b].push_back(a);
      }
    }
  }

  for (std::vector<std::uint32_t> &neighbors : adjacency) {
    std::sort(neighbors.begin(), neighbors.end());
    neighbors.erase(std::unique(neighbors.begin(), neighbors.end()),
                    neighbors.end());
  }
  return adjacency;
}

[[nodiscard]] std::vector<FastChartFace>
BuildFastChartFaces(const UvAtlasInput &input) {
  std::vector<FastChartFace> faces;
  faces.reserve(input.Faces.size());
  for (const MeshSoup::PolygonFace &face : input.Faces) {
    const glm::vec3 p0 = input.Positions[face.Indices[0u]];
    const glm::vec3 p1 = input.Positions[face.Indices[1u]];
    const glm::vec3 p2 = input.Positions[face.Indices[2u]];
    const glm::vec3 cross = glm::cross(p1 - p0, p2 - p0);
    const float doubleArea = glm::length(cross);
    faces.push_back(FastChartFace{
        .Vertices = {face.Indices[0u], face.Indices[1u], face.Indices[2u]},
        .Normal = doubleArea > 0.0f ? cross / doubleArea
                                    : glm::vec3{0.0f, 0.0f, 1.0f},
        .DoubleArea = doubleArea,
    });
  }
  return faces;
}

[[nodiscard]] bool
FaceFitsPlanarChart(const UvAtlasInput &input, const FastChartFace &candidate,
                    const glm::vec3 seedNormal, const glm::vec3 seedPoint,
                    const float planeDistanceEpsilon) noexcept {
  constexpr float kNormalCosThreshold = 0.9659258f; // 15 degrees.
  if (glm::dot(candidate.Normal, seedNormal) < kNormalCosThreshold) {
    return false;
  }

  for (const std::uint32_t sourceVertex : candidate.Vertices) {
    const float distance = std::abs(
        glm::dot(input.Positions[sourceVertex] - seedPoint, seedNormal));
    if (distance > planeDistanceEpsilon) {
      return false;
    }
  }
  return true;
}

void FinalizeFastChartProjection(const UvAtlasInput &input,
                                 const std::vector<FastChartFace> &faces,
                                 FastChartProposal &chart) {
  glm::vec3 normalSum{0.0f};
  float bestEdgeLengthSq = 0.0f;
  glm::vec3 bestAxis{1.0f, 0.0f, 0.0f};

  chart.Origin = input.Positions[faces[chart.SourceFaces.front()].Vertices[0u]];
  for (const std::uint32_t faceIndex : chart.SourceFaces) {
    const FastChartFace &face = faces[faceIndex];
    normalSum += face.Normal * face.DoubleArea;
  }

  const float normalLength = glm::length(normalSum);
  chart.Normal = normalLength > 1.0e-6f
                     ? normalSum / normalLength
                     : faces[chart.SourceFaces.front()].Normal;

  for (const std::uint32_t faceIndex : chart.SourceFaces) {
    const FastChartFace &face = faces[faceIndex];
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const glm::vec3 a = input.Positions[face.Vertices[corner]];
      const glm::vec3 b = input.Positions[face.Vertices[(corner + 1u) % 3u]];
      glm::vec3 edge = b - a;
      edge -= glm::dot(edge, chart.Normal) * chart.Normal;
      const float lengthSq = glm::dot(edge, edge);
      if (lengthSq > bestEdgeLengthSq) {
        bestEdgeLengthSq = lengthSq;
        bestAxis = edge;
      }
    }
  }

  chart.UAxis = bestEdgeLengthSq > 1.0e-12f
                    ? bestAxis / std::sqrt(bestEdgeLengthSq)
                    : AnyPerpendicular(chart.Normal);
  chart.VAxis = glm::normalize(glm::cross(chart.Normal, chart.UAxis));

  chart.LocalMin = glm::vec2{std::numeric_limits<float>::max()};
  chart.LocalMax = glm::vec2{std::numeric_limits<float>::lowest()};
  for (const std::uint32_t faceIndex : chart.SourceFaces) {
    const FastChartFace &face = faces[faceIndex];
    for (const std::uint32_t sourceVertex : face.Vertices) {
      const glm::vec3 relative = input.Positions[sourceVertex] - chart.Origin;
      const glm::vec2 local{
          glm::dot(relative, chart.UAxis),
          glm::dot(relative, chart.VAxis),
      };
      chart.LocalMin = glm::min(chart.LocalMin, local);
      chart.LocalMax = glm::max(chart.LocalMax, local);
    }
  }
  chart.LocalExtent = chart.LocalMax - chart.LocalMin;
}

[[nodiscard]] std::vector<FastChartProposal> BuildFastChartProposals(
    const UvAtlasInput &input, const std::vector<FastChartFace> &faces,
    const std::vector<std::vector<std::uint32_t>> &adjacency) {
  const float planeDistanceEpsilon = MeshScale(input.Positions) * 1.0e-4f;
  std::vector<bool> visited(input.Faces.size(), false);
  std::vector<FastChartProposal> charts;
  charts.reserve(input.Faces.size());

  for (std::size_t seed = 0u; seed < input.Faces.size(); ++seed) {
    if (visited[seed]) {
      continue;
    }

    FastChartProposal chart{};
    chart.ChartId = static_cast<std::uint32_t>(charts.size());
    const glm::vec3 seedNormal = faces[seed].Normal;
    const glm::vec3 seedPoint = input.Positions[faces[seed].Vertices[0u]];

    std::vector<std::uint32_t> pending;
    pending.push_back(static_cast<std::uint32_t>(seed));
    visited[seed] = true;

    for (std::size_t cursor = 0u; cursor < pending.size(); ++cursor) {
      const std::uint32_t faceIndex = pending[cursor];
      chart.SourceFaces.push_back(faceIndex);

      for (const std::uint32_t neighbor : adjacency[faceIndex]) {
        if (visited[neighbor]) {
          continue;
        }
        if (!FaceFitsPlanarChart(input, faces[neighbor], seedNormal, seedPoint,
                                 planeDistanceEpsilon)) {
          continue;
        }
        visited[neighbor] = true;
        pending.push_back(neighbor);
      }
    }

    std::sort(chart.SourceFaces.begin(), chart.SourceFaces.end());
    FinalizeFastChartProjection(input, faces, chart);
    charts.push_back(std::move(chart));
  }

  return charts;
}

[[nodiscard]] bool
FinalizeParameterizationBounds(FastChartParameterization &parameterization) {
  if (parameterization.LocalUvs.empty()) {
    return false;
  }

  glm::vec2 localMin{std::numeric_limits<float>::max()};
  glm::vec2 localMax{std::numeric_limits<float>::lowest()};
  for (const glm::vec2 uv : parameterization.LocalUvs) {
    if (!IsFinite(uv)) {
      return false;
    }
    localMin = glm::min(localMin, uv);
    localMax = glm::max(localMax, uv);
  }

  const glm::vec2 extent = localMax - localMin;
  if (!IsFinite(extent) || extent.x <= 1.0e-8f || extent.y <= 1.0e-8f) {
    return false;
  }

  parameterization.LocalMin = localMin;
  parameterization.LocalMax = localMax;
  parameterization.LocalExtent = extent;
  return true;
}

[[nodiscard]] bool
AcceptSolverUvs(FastChartParameterization &parameterization,
                const HalfedgeMesh::Mesh &mesh,
                const std::span<const glm::vec2> candidateUvs,
                const std::string_view backend) {
  if (candidateUvs.size() < parameterization.SourceVertices.size()) {
    return false;
  }

  std::vector<glm::vec2> localUvs(
      candidateUvs.begin(),
      candidateUvs.begin() +
          static_cast<std::ptrdiff_t>(parameterization.SourceVertices.size()));
  FastChartParameterization candidate = parameterization;
  candidate.LocalUvs = std::move(localUvs);
  if (!FinalizeParameterizationBounds(candidate)) {
    return false;
  }

  const auto diagnostics =
      Parameterization::EvaluateParameterizationDiagnostics(mesh,
                                                            candidate.LocalUvs);
  if (diagnostics.Status !=
          Parameterization::ParameterizationDiagnosticsStatus::Success ||
      !diagnostics.HasUsableFaces() || diagnostics.HasInvalidInput() ||
      diagnostics.FlippedElementCount > 0u) {
    return false;
  }

  parameterization.LocalUvs = std::move(candidate.LocalUvs);
  parameterization.LocalMin = candidate.LocalMin;
  parameterization.LocalMax = candidate.LocalMax;
  parameterization.LocalExtent = candidate.LocalExtent;
  parameterization.Backend = std::string{backend};
  parameterization.Quality = diagnostics;
  return true;
}

[[nodiscard]] FastChartParameterization
BuildFastChartParameterization(const UvAtlasInput &input,
                               const FastChartProposal &chart) {
  FastChartParameterization parameterization{};
  parameterization.ChartId = chart.ChartId;
  parameterization.SourceVertexToLocal.assign(input.Positions.size(),
                                              kInvalidIndex);
  parameterization.Indices.reserve(chart.SourceFaces.size() * 3u);

  for (const std::uint32_t sourceFace : chart.SourceFaces) {
    const MeshSoup::PolygonFace &face = input.Faces[sourceFace];
    for (const std::uint32_t sourceVertex : face.Indices) {
      std::uint32_t localIndex =
          parameterization.SourceVertexToLocal[sourceVertex];
      if (localIndex == kInvalidIndex) {
        localIndex =
            static_cast<std::uint32_t>(parameterization.SourceVertices.size());
        parameterization.SourceVertexToLocal[sourceVertex] = localIndex;
        parameterization.SourceVertices.push_back(sourceVertex);
        parameterization.Positions.push_back(input.Positions[sourceVertex]);
      }
      parameterization.Indices.push_back(localIndex);
    }
  }

  const std::optional<HalfedgeMesh::Mesh> halfedge =
      MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(
          parameterization.Positions, parameterization.Indices);
  if (halfedge) {
    Parameterization::ParameterizationParams lscmParams{};
    if (const auto lscm = Parameterization::ComputeLSCM(*halfedge, lscmParams);
        lscm && lscm->Converged &&
        AcceptSolverUvs(parameterization, *halfedge, lscm->UVs, "lscm")) {
      return parameterization;
    }

    Parameterization::HarmonicParams harmonicParams{};
    harmonicParams.Weights = Parameterization::HarmonicWeightType::Uniform;
    harmonicParams.Boundary = Parameterization::HarmonicBoundaryPolicy::Square;
    harmonicParams.ArcLengthSpacing = true;
    if (const auto harmonic =
            Parameterization::ComputeHarmonic(*halfedge, harmonicParams);
        harmonic &&
        harmonic->Status == Parameterization::HarmonicStatus::Success &&
        AcceptSolverUvs(parameterization, *halfedge, harmonic->UVs,
                        "harmonic_tutte")) {
      return parameterization;
    }
  }

  parameterization.LocalUvs.clear();
  parameterization.LocalUvs.reserve(parameterization.SourceVertices.size());
  for (const std::uint32_t sourceVertex : parameterization.SourceVertices) {
    const glm::vec3 relative = input.Positions[sourceVertex] - chart.Origin;
    parameterization.LocalUvs.push_back(glm::vec2{
        glm::dot(relative, chart.UAxis),
        glm::dot(relative, chart.VAxis),
    });
  }
  parameterization.Backend = "projection";
  if (FinalizeParameterizationBounds(parameterization) && halfedge) {
    parameterization.Quality =
        Parameterization::EvaluateParameterizationDiagnostics(
            *halfedge, parameterization.LocalUvs);
  }
  return parameterization;
}

[[nodiscard]] glm::vec2
ApplyPlacement(const FastChartParameterization &parameterization,
               const FastChartPlacement &placement,
               const glm::vec2 localUv) noexcept {
  const glm::vec2 zeroBased = localUv - parameterization.LocalMin;
  const glm::vec2 packed =
      placement.Rotated
          ? glm::vec2{zeroBased.y, parameterization.LocalExtent.x - zeroBased.x}
          : zeroBased;
  return placement.Origin + packed * placement.Scale;
}

[[nodiscard]] Parameterization::ParameterizationDiagnostics
EvaluatePackedChartQuality(const FastChartParameterization &parameterization,
                           const FastChartPlacement &placement) {
  const std::optional<HalfedgeMesh::Mesh> halfedge =
      MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(
          parameterization.Positions, parameterization.Indices);
  if (!halfedge) {
    return {};
  }

  std::vector<glm::vec2> packedUvs;
  packedUvs.reserve(parameterization.LocalUvs.size());
  for (const glm::vec2 localUv : parameterization.LocalUvs) {
    packedUvs.push_back(ApplyPlacement(parameterization, placement, localUv));
  }
  return Parameterization::EvaluateParameterizationDiagnostics(*halfedge,
                                                               packedUvs);
}

[[nodiscard]] glm::vec2
PackedItemSize(const FastChartParameterization &parameterization,
               const float scale, const float padding,
               const bool rotated) noexcept {
  const glm::vec2 extent = rotated ? glm::vec2{parameterization.LocalExtent.y,
                                               parameterization.LocalExtent.x}
                                   : parameterization.LocalExtent;
  return extent * scale + glm::vec2{2.0f * padding};
}

[[nodiscard]] bool TryPackFastCharts(
    const std::vector<FastChartParameterization> &parameterizations,
    const float scale, const float padding, const bool allowRotation,
    std::vector<FastChartPlacement> &placements) {
  placements.assign(parameterizations.size(), FastChartPlacement{});
  if (scale <= 0.0f) {
    return false;
  }

  std::vector<std::uint32_t> order(parameterizations.size());
  std::iota(order.begin(), order.end(), 0u);
  std::stable_sort(
      order.begin(), order.end(),
      [&parameterizations](const std::uint32_t a, const std::uint32_t b) {
        const glm::vec2 ea = parameterizations[a].LocalExtent;
        const glm::vec2 eb = parameterizations[b].LocalExtent;
        const float sideA = std::max(ea.x, ea.y);
        const float sideB = std::max(eb.x, eb.y);
        if (sideA != sideB) {
          return sideA > sideB;
        }
        const float areaA = ea.x * ea.y;
        const float areaB = eb.x * eb.y;
        if (areaA != areaB) {
          return areaA > areaB;
        }
        return parameterizations[a].ChartId < parameterizations[b].ChartId;
      });

  float cursorX = 0.0f;
  float cursorY = 0.0f;
  float shelfHeight = 0.0f;
  constexpr float epsilon = 1.0e-6f;

  for (const std::uint32_t index : order) {
    const FastChartParameterization &parameterization =
        parameterizations[index];
    const glm::vec2 unrotatedSize =
        PackedItemSize(parameterization, scale, padding, false);
    const glm::vec2 rotatedSize =
        PackedItemSize(parameterization, scale, padding, true);

    auto chooseForShelf = [&](const float shelfX, bool &rotated,
                              glm::vec2 &size) -> bool {
      bool unrotatedFits = shelfX + unrotatedSize.x <= 1.0f + epsilon &&
                           unrotatedSize.y <= 1.0f + epsilon;
      bool rotatedFits = allowRotation &&
                         shelfX + rotatedSize.x <= 1.0f + epsilon &&
                         rotatedSize.y <= 1.0f + epsilon;
      if (!unrotatedFits && !rotatedFits) {
        return false;
      }
      if (rotatedFits &&
          (!unrotatedFits || rotatedSize.y < unrotatedSize.y - epsilon ||
           (std::abs(rotatedSize.y - unrotatedSize.y) <= epsilon &&
            rotatedSize.x < unrotatedSize.x))) {
        rotated = true;
        size = rotatedSize;
        return true;
      }

      rotated = false;
      size = unrotatedSize;
      return true;
    };

    bool rotated = false;
    glm::vec2 itemSize{0.0f};
    if (!chooseForShelf(cursorX, rotated, itemSize)) {
      cursorY += shelfHeight;
      cursorX = 0.0f;
      shelfHeight = 0.0f;
      if (!chooseForShelf(cursorX, rotated, itemSize)) {
        return false;
      }
    }

    if (cursorY + itemSize.y > 1.0f + epsilon) {
      return false;
    }

    placements[index] = FastChartPlacement{
        .Origin = glm::vec2{cursorX + padding, cursorY + padding},
        .Scale = scale,
        .Rotated = rotated,
    };
    cursorX += itemSize.x;
    shelfHeight = std::max(shelfHeight, itemSize.y);
  }

  return cursorY + shelfHeight <= 1.0f + epsilon;
}

[[nodiscard]] std::vector<FastChartPlacement>
PackFastCharts(const std::vector<FastChartParameterization> &parameterizations,
               const UvAtlasOptions &options, const std::uint32_t chartCount,
               const std::uint32_t resolution) {
  float maxExtent = 0.0f;
  for (const FastChartParameterization &parameterization : parameterizations) {
    maxExtent = std::max(maxExtent, parameterization.LocalExtent.x);
    maxExtent = std::max(maxExtent, parameterization.LocalExtent.y);
  }
  if (maxExtent <= 0.0f) {
    return {};
  }

  const float requestedPadding =
      static_cast<float>(options.Padding) / static_cast<float>(resolution);
  const float gridSide = static_cast<float>(std::max<std::uint32_t>(
      1u, static_cast<std::uint32_t>(
              std::ceil(std::sqrt(static_cast<float>(chartCount))))));
  const float padding = std::clamp(requestedPadding, 0.0f, 0.20f / gridSide);
  const float upperScale =
      std::max(1.0e-6f, (1.0f - 2.0f * padding) / maxExtent);

  std::vector<FastChartPlacement> best;
  if (TryPackFastCharts(parameterizations, upperScale, padding,
                        options.RotateCharts, best)) {
    return best;
  }

  float lo = 0.0f;
  float hi = upperScale;
  for (int iteration = 0; iteration < 32; ++iteration) {
    const float mid = (lo + hi) * 0.5f;
    std::vector<FastChartPlacement> candidate;
    if (TryPackFastCharts(parameterizations, mid, padding, options.RotateCharts,
                          candidate)) {
      lo = mid;
      best = std::move(candidate);
    } else {
      hi = mid;
    }
  }

  return best;
}

void RecordFastStagedSeams(const std::vector<SourceEdgeFaces> &edgeGroups,
                           const std::span<const std::uint32_t> sourceFaceChart,
                           UvAtlasResult &result) {
  for (const SourceEdgeFaces &edge : edgeGroups) {
    if (edge.Faces.size() == 1u) {
      const std::uint32_t face = edge.Faces.front();
      result.SeamCuts.push_back(UvAtlasSeamCutRecord{
          .SourceVertexA = edge.A,
          .SourceVertexB = edge.B,
          .SourceFaceA = face,
          .SourceFaceB = kInvalidIndex,
          .ChartA = sourceFaceChart[face],
          .ChartB = kInvalidIndex,
          .Boundary = true,
      });
      continue;
    }

    for (std::size_t i = 0u; i < edge.Faces.size(); ++i) {
      for (std::size_t j = i + 1u; j < edge.Faces.size(); ++j) {
        const std::uint32_t faceA = edge.Faces[i];
        const std::uint32_t faceB = edge.Faces[j];
        const std::uint32_t chartA = sourceFaceChart[faceA];
        const std::uint32_t chartB = sourceFaceChart[faceB];
        if (chartA == chartB) {
          continue;
        }
        result.SeamCuts.push_back(UvAtlasSeamCutRecord{
            .SourceVertexA = edge.A,
            .SourceVertexB = edge.B,
            .SourceFaceA = faceA,
            .SourceFaceB = faceB,
            .ChartA = chartA,
            .ChartB = chartB,
            .Boundary = false,
        });
      }
    }
  }
}

template <class T>
void CopyTypedProperty(const ConstPropertySet &source, const std::string &name,
                       const std::span<const std::uint32_t> xrefs,
                       PropertySet &target,
                       VertexPropertyCopyDiagnostics &diagnostics) {
  const auto sourceProperty = source.Get<T>(name);
  if (!sourceProperty) {
    return;
  }

  auto targetProperty = target.GetOrAdd<T>(name, T{});
  bool copiedAny = false;
  for (std::size_t outputIndex = 0; outputIndex < xrefs.size(); ++outputIndex) {
    const std::uint32_t sourceIndex = xrefs[outputIndex];
    if (sourceIndex >= sourceProperty.Vector().size()) {
      ++diagnostics.XrefOutOfRangeCount;
      continue;
    }
    targetProperty[outputIndex] = sourceProperty[sourceIndex];
    copiedAny = true;
  }
  if (copiedAny) {
    ++diagnostics.CopiedPropertyCount;
  } else {
    ++diagnostics.SkippedPropertyCount;
  }
}

[[nodiscard]] bool TryCopyKnownPropertyType(
    const ConstPropertySet &source, const std::string &name,
    const std::span<const std::uint32_t> xrefs, PropertySet &target,
    VertexPropertyCopyDiagnostics &diagnostics) {
  const std::size_t copiedBefore = diagnostics.CopiedPropertyCount;
  const std::size_t skippedBefore = diagnostics.SkippedPropertyCount;

  CopyTypedProperty<float>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<double>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<std::uint32_t>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<std::int32_t>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<bool>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<glm::vec2>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<glm::vec3>(source, name, xrefs, target, diagnostics);
  CopyTypedProperty<glm::vec4>(source, name, xrefs, target, diagnostics);

  return diagnostics.CopiedPropertyCount != copiedBefore ||
         diagnostics.SkippedPropertyCount != skippedBefore;
}

void AttachDiagnostics(UvAtlasResult &result, const UvAtlasInput &input,
                       const UvAtlasOptions &options,
                       const std::string_view backendName) {
  result.Diagnostics.Status = result.Status;
  result.Diagnostics.Provenance = result.Provenance;
  result.Diagnostics.RequestedMethod = options.Method;
  result.Diagnostics.InputVertexCount = input.Positions.size();
  result.Diagnostics.InputFaceCount = input.Faces.size();
  result.Diagnostics.OutputVertexCount = result.OutputMesh.VertexCount();
  result.Diagnostics.OutputFaceCount = result.OutputMesh.FaceCount();
  if (result.Diagnostics.ActualMethod == UvAtlasMethod::None) {
    if (result.Provenance == UvAtlasProvenance::AuthoredPreserved) {
      result.Diagnostics.ActualMethod = UvAtlasMethod::Authored;
    } else if (std::string_view{backendName} == "xatlas") {
      result.Diagnostics.ActualMethod = UvAtlasMethod::XAtlas;
    } else {
      result.Diagnostics.ActualMethod = options.Method;
    }
  }
  if (result.Diagnostics.BackendName.empty()) {
    result.Diagnostics.BackendName = std::string{backendName};
  }
  if (result.Diagnostics.AtlasWidth == 0u && options.Resolution > 0u &&
      result.Provenance == UvAtlasProvenance::Generated) {
    result.Diagnostics.AtlasWidth = options.Resolution;
    result.Diagnostics.AtlasHeight = options.Resolution;
  }
}

[[nodiscard]] UvAtlasResult
BuildPreservedResult(const UvAtlasInput &input,
                     const UvAtlasDiagnostics &validation) {
  UvAtlasResult result{};
  result.Status = UvAtlasStatus::Success;
  result.Provenance = UvAtlasProvenance::AuthoredPreserved;
  result.Diagnostics = validation;
  result.Diagnostics.Status = UvAtlasStatus::Success;
  result.Diagnostics.Provenance = UvAtlasProvenance::AuthoredPreserved;
  result.Diagnostics.ActualMethod = UvAtlasMethod::Authored;
  result.Diagnostics.BackendName = "authored";
  result.Diagnostics.BackendDetail = "preserved valid authored texcoords";
  result.Diagnostics.PreservedAuthoredUvCount = input.AuthoredTexcoords.size();
  result.Diagnostics.ChartCount = input.Faces.empty() ? 0u : 1u;

  result.SourceVertexForOutputVertex.reserve(input.Positions.size());
  for (std::size_t i = 0; i < input.Positions.size(); ++i) {
    (void)result.OutputMesh.AddVertex(input.Positions[i]);
    result.SourceVertexForOutputVertex.push_back(static_cast<std::uint32_t>(i));
  }

  if (input.HasVertexProperties) {
    const auto copyDiagnostics = CopySourceVertexPropertiesByXref(
        input.VertexProperties, result.SourceVertexForOutputVertex,
        result.OutputMesh.VertexProperties());
    result.Diagnostics.CopiedVertexPropertyCount =
        copyDiagnostics.CopiedPropertyCount;
    result.Diagnostics.SkippedVertexPropertyCount =
        copyDiagnostics.SkippedPropertyCount;
    result.Diagnostics.PropertyXrefOutOfRangeCount =
        copyDiagnostics.XrefOutOfRangeCount;
  }

  auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
  for (std::size_t i = 0; i < input.AuthoredTexcoords.size(); ++i) {
    texcoords.Vector()[i] = input.AuthoredTexcoords[i];
  }

  result.SourceFaceForOutputFace.reserve(input.Faces.size());
  result.OutputFaceChart.reserve(input.Faces.size());
  for (std::size_t faceIndex = 0; faceIndex < input.Faces.size(); ++faceIndex) {
    const MeshSoup::PolygonFace &face = input.Faces[faceIndex];
    (void)result.OutputMesh.AddTriangle(face.Indices[0], face.Indices[1],
                                        face.Indices[2]);
    result.SourceFaceForOutputFace.push_back(
        static_cast<std::uint32_t>(faceIndex));
    result.OutputFaceChart.push_back(0u);
  }

  FinalizeUvBounds(result.Diagnostics, input.AuthoredTexcoords);
  result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
  result.Diagnostics.OutputVertexCount = result.OutputMesh.VertexCount();
  result.Diagnostics.OutputFaceCount = result.OutputMesh.FaceCount();
  return result;
}

[[nodiscard]] UvAtlasResult
GenerateWithFastStaged(const UvAtlasInput &input,
                       const UvAtlasOptions &options) {
  if (options.CancelRequested) {
    return MakeFailure(input, UvAtlasStatus::Cancelled, "fast-staged",
                       "cancel requested before fast staged generation");
  }

  UvAtlasDiagnostics validation = ValidateUvAtlasInput(input);
  if (validation.Status != UvAtlasStatus::Success) {
    UvAtlasResult failure = MakeFailure(input, validation.Status, "fast-staged",
                                        "invalid atlas input");
    failure.Diagnostics = validation;
    failure.Diagnostics.BackendName = "fast-staged";
    return failure;
  }
  if (validation.DegenerateFaceCount > 0u) {
    UvAtlasResult failure = MakeFailure(
        input, UvAtlasStatus::BackendRejectedInput, "fast-staged",
        "fast staged charting requires every triangle to be non-degenerate");
    failure.Diagnostics = validation;
    failure.Diagnostics.Status = failure.Status;
    failure.Diagnostics.BackendName = "fast-staged";
    return failure;
  }

  const std::size_t faceCount = input.Faces.size();
  const std::uint32_t resolution =
      options.Resolution == 0u ? 1024u : options.Resolution;
  const std::vector<FastChartFace> fastFaces = BuildFastChartFaces(input);
  const std::vector<SourceEdgeFaces> edgeGroups =
      BuildSourceEdgeFaceGroups(input);
  const std::vector<std::vector<std::uint32_t>> adjacency =
      BuildFaceAdjacency(edgeGroups, faceCount);
  std::vector<FastChartProposal> charts =
      BuildFastChartProposals(input, fastFaces, adjacency);
  if (charts.empty()) {
    return MakeFailure(input, UvAtlasStatus::BackendRejectedInput,
                       "fast-staged",
                       "fast staged chart proposal produced no charts");
  }

  for (const FastChartProposal &chart : charts) {
    if (chart.SourceFaces.empty()) {
      return MakeFailure(input, UvAtlasStatus::BackendRejectedInput,
                         "fast-staged",
                         "fast staged chart produced no source faces");
    }
  }

  const auto chartCount = static_cast<std::uint32_t>(charts.size());
  std::vector<FastChartParameterization> chartParameterizations;
  chartParameterizations.reserve(charts.size());
  for (const FastChartProposal &chart : charts) {
    FastChartParameterization parameterization =
        BuildFastChartParameterization(input, chart);
    if (!IsFinite(parameterization.LocalExtent) ||
        parameterization.LocalExtent.x <= 0.0f ||
        parameterization.LocalExtent.y <= 0.0f) {
      return MakeFailure(input, UvAtlasStatus::BackendRejectedInput,
                         "fast-staged",
                         "fast staged chart parameterization produced a "
                         "degenerate local uv domain");
    }
    chartParameterizations.push_back(std::move(parameterization));
  }

  const std::vector<FastChartPlacement> placements =
      PackFastCharts(chartParameterizations, options, chartCount, resolution);
  if (placements.size() != chartParameterizations.size()) {
    return MakeFailure(input, UvAtlasStatus::BackendRejectedInput,
                       "fast-staged",
                       "fast staged shelf packing could not place every chart");
  }

  UvAtlasResult result{};
  result.Status = UvAtlasStatus::Success;
  result.Provenance = UvAtlasProvenance::Generated;
  result.Diagnostics = validation;
  result.Diagnostics.Status = UvAtlasStatus::Success;
  result.Diagnostics.Provenance = UvAtlasProvenance::Generated;
  result.Diagnostics.ActualMethod = UvAtlasMethod::FastStaged;
  result.Diagnostics.BackendName = "fast-staged";
  result.Diagnostics.BackendDetail =
      "deterministic planar multi-face chart growth with LSCM/harmonic chart "
      "parameterization and TABI-inspired shelf packing";
  result.Diagnostics.ChartCount = chartCount;
  result.Diagnostics.AtlasWidth = resolution;
  result.Diagnostics.AtlasHeight = resolution;
  result.Diagnostics.AtlasCount = 1u;

  result.SourceVertexForOutputVertex.reserve(faceCount * 3u);
  result.SourceFaceForOutputFace.reserve(faceCount);
  result.OutputFaceChart.reserve(faceCount);
  result.Charts.reserve(charts.size());
  std::vector<glm::vec2> outputUvs;
  outputUvs.reserve(faceCount * 3u);
  std::vector<std::uint32_t> sourceFaceChart(faceCount, kInvalidIndex);

  for (const FastChartProposal &chart : charts) {
    const FastChartParameterization &parameterization =
        chartParameterizations[chart.ChartId];
    const FastChartPlacement &placement = placements[chart.ChartId];

    const std::uint32_t outputVertexStart =
        static_cast<std::uint32_t>(result.OutputMesh.VertexCount());
    const std::uint32_t outputFaceStart =
        static_cast<std::uint32_t>(result.SourceFaceForOutputFace.size());
    glm::vec2 uvMin{std::numeric_limits<float>::max()};
    glm::vec2 uvMax{std::numeric_limits<float>::lowest()};

    std::vector<std::uint32_t> sourceVertexToOutput(input.Positions.size(),
                                                    kInvalidIndex);
    for (const std::uint32_t sourceFace : chart.SourceFaces) {
      const MeshSoup::PolygonFace &face = input.Faces[sourceFace];
      std::array<std::uint32_t, 3u> outputFace{};
      for (std::size_t corner = 0u; corner < 3u; ++corner) {
        const std::uint32_t sourceVertex = face.Indices[corner];
        std::uint32_t outputVertex = sourceVertexToOutput[sourceVertex];
        if (outputVertex == kInvalidIndex) {
          outputVertex =
              static_cast<std::uint32_t>(result.OutputMesh.VertexCount());
          sourceVertexToOutput[sourceVertex] = outputVertex;
          (void)result.OutputMesh.AddVertex(input.Positions[sourceVertex]);
          result.SourceVertexForOutputVertex.push_back(sourceVertex);

          const std::uint32_t localIndex =
              parameterization.SourceVertexToLocal[sourceVertex];
          const glm::vec2 uv =
              localIndex != kInvalidIndex &&
                      localIndex < parameterization.LocalUvs.size()
                  ? ApplyPlacement(parameterization, placement,
                                   parameterization.LocalUvs[localIndex])
                  : glm::vec2{0.0f};
          outputUvs.push_back(glm::clamp(uv, glm::vec2{0.0f}, glm::vec2{1.0f}));
          uvMin = glm::min(uvMin, outputUvs.back());
          uvMax = glm::max(uvMax, outputUvs.back());
        }

        outputFace[corner] = outputVertex;
      }

      (void)result.OutputMesh.AddTriangle(outputFace[0u], outputFace[1u],
                                          outputFace[2u]);
      result.SourceFaceForOutputFace.push_back(sourceFace);
      result.OutputFaceChart.push_back(chart.ChartId);
      sourceFaceChart[sourceFace] = chart.ChartId;
    }

    result.Charts.push_back(UvAtlasChartRecord{
        .ChartId = chart.ChartId,
        .SourceFaceStart = chart.SourceFaces.front(),
        .SourceFaceCount = static_cast<std::uint32_t>(chart.SourceFaces.size()),
        .OutputFaceStart = outputFaceStart,
        .OutputFaceCount = static_cast<std::uint32_t>(
            result.SourceFaceForOutputFace.size() - outputFaceStart),
        .OutputVertexStart = outputVertexStart,
        .OutputVertexCount = static_cast<std::uint32_t>(
            result.OutputMesh.VertexCount() - outputVertexStart),
        .UvMin = uvMin,
        .UvMax = uvMax,
        .ParameterizationBackend = parameterization.Backend,
        .Quality = EvaluatePackedChartQuality(parameterization, placement),
    });
  }

  if (input.HasVertexProperties && options.CopySourceVertexProperties) {
    const auto copyDiagnostics = CopySourceVertexPropertiesByXref(
        input.VertexProperties, result.SourceVertexForOutputVertex,
        result.OutputMesh.VertexProperties());
    result.Diagnostics.CopiedVertexPropertyCount =
        copyDiagnostics.CopiedPropertyCount;
    result.Diagnostics.SkippedVertexPropertyCount =
        copyDiagnostics.SkippedPropertyCount;
    result.Diagnostics.PropertyXrefOutOfRangeCount =
        copyDiagnostics.XrefOutOfRangeCount;
  }

  auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
  for (std::size_t i = 0; i < outputUvs.size(); ++i) {
    texcoords.Vector()[i] = outputUvs[i];
  }

  RecordFastStagedSeams(edgeGroups, sourceFaceChart, result);
  result.Diagnostics.SeamCutCount = static_cast<std::uint32_t>(std::count_if(
      result.SeamCuts.begin(), result.SeamCuts.end(),
      [](const UvAtlasSeamCutRecord &seam) { return !seam.Boundary; }));
  result.Diagnostics.BoundarySeamCount =
      static_cast<std::uint32_t>(std::count_if(
          result.SeamCuts.begin(), result.SeamCuts.end(),
          [](const UvAtlasSeamCutRecord &seam) { return seam.Boundary; }));

  FinalizeUvBounds(result.Diagnostics, outputUvs);
  result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
  AttachDiagnostics(result, input, options, "fast-staged");
  return result;
}

[[nodiscard]] UvAtlasResult GenerateWithXAtlas(const UvAtlasInput &input,
                                               const UvAtlasOptions &options) {
  if (options.CancelRequested) {
    return MakeFailure(input, UvAtlasStatus::Cancelled, "xatlas",
                       "cancel requested before xatlas generation");
  }

  UvAtlasDiagnostics validation = ValidateUvAtlasInput(input);
  if (validation.Status != UvAtlasStatus::Success) {
    UvAtlasResult failure =
        MakeFailure(input, validation.Status, "xatlas", "invalid atlas input");
    failure.Diagnostics = validation;
    failure.Diagnostics.BackendName = "xatlas";
    return failure;
  }

  std::vector<std::uint32_t> indices;
  if (!ExtractTriangleIndices(input, indices)) {
    return MakeFailure(input, UvAtlasStatus::BackendRejectedInput, "xatlas",
                       "failed to extract triangle indices");
  }

  xatlas::Atlas *atlas = xatlas::Create();
  if (atlas == nullptr) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas",
                       "xatlas::Create returned null");
  }

  struct AtlasGuard {
    xatlas::Atlas *Atlas{nullptr};
    ~AtlasGuard() {
      if (Atlas != nullptr) {
        xatlas::Destroy(Atlas);
      }
    }
  } guard{atlas};

  xatlas::MeshDecl meshDecl{};
  meshDecl.vertexPositionData = input.Positions.data();
  meshDecl.vertexPositionStride = sizeof(glm::vec3);
  meshDecl.vertexCount = static_cast<std::uint32_t>(input.Positions.size());
  meshDecl.indexData = indices.data();
  meshDecl.indexCount = static_cast<std::uint32_t>(indices.size());
  meshDecl.indexFormat = xatlas::IndexFormat::UInt32;

  const bool useAuthoredHints =
      options.UseAuthoredUvsAsChartHints &&
      input.AuthoredTexcoords.size() == input.Positions.size() &&
      AllUvsFinite(input.AuthoredTexcoords);
  if (useAuthoredHints) {
    meshDecl.vertexUvData = input.AuthoredTexcoords.data();
    meshDecl.vertexUvStride = sizeof(glm::vec2);
  }

  const xatlas::AddMeshError addMeshError = xatlas::AddMesh(atlas, meshDecl);
  if (addMeshError != xatlas::AddMeshError::Success) {
    return MakeFailure(input, UvAtlasStatus::BackendRejectedInput, "xatlas",
                       xatlas::StringForEnum(addMeshError));
  }

  xatlas::ChartOptions chartOptions{};
  chartOptions.useInputMeshUvs = useAuthoredHints;
  chartOptions.fixWinding = options.FixWinding;

  xatlas::PackOptions packOptions{};
  packOptions.maxChartSize = options.MaxChartSize;
  packOptions.padding = options.Padding;
  packOptions.texelsPerUnit = options.TexelsPerUnit;
  packOptions.resolution = options.Resolution;
  packOptions.bilinear = options.Bilinear;
  packOptions.blockAlign = options.BlockAlign;
  packOptions.bruteForce = options.BruteForcePacking;
  packOptions.rotateChartsToAxis = options.RotateChartsToAxis;
  packOptions.rotateCharts = options.RotateCharts;

  xatlas::Generate(atlas, chartOptions, packOptions);
  if (atlas->meshCount == 0u || atlas->meshes == nullptr) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas",
                       "xatlas produced no output meshes");
  }
  if (atlas->width == 0u || atlas->height == 0u) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas",
                       "xatlas produced a zero-sized atlas");
  }

  const xatlas::Mesh &output = atlas->meshes[0];
  UvAtlasResult result{};
  result.Status = UvAtlasStatus::Success;
  result.Provenance = UvAtlasProvenance::Generated;
  result.Diagnostics = validation;
  result.Diagnostics.Status = UvAtlasStatus::Success;
  result.Diagnostics.Provenance = UvAtlasProvenance::Generated;
  result.Diagnostics.ActualMethod = UvAtlasMethod::XAtlas;
  result.Diagnostics.BackendName = "xatlas";
  result.Diagnostics.BackendDetail =
      "jpcy/xatlas f700c7790aaa030e794b52ba7791a05c085faf0c";
  result.Diagnostics.ChartCount = atlas->chartCount;
  result.Diagnostics.AtlasWidth = atlas->width;
  result.Diagnostics.AtlasHeight = atlas->height;
  result.Diagnostics.AtlasCount = atlas->atlasCount;
  result.Diagnostics.TexelsPerUnit = atlas->texelsPerUnit;

  std::vector<glm::vec2> outputUvs;
  outputUvs.reserve(output.vertexCount);
  result.SourceVertexForOutputVertex.reserve(output.vertexCount);
  for (std::uint32_t vertexIndex = 0; vertexIndex < output.vertexCount;
       ++vertexIndex) {
    const xatlas::Vertex &vertex = output.vertexArray[vertexIndex];
    if (vertex.xref >= input.Positions.size()) {
      return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas",
                         "xatlas output vertex xref out of range");
    }

    (void)result.OutputMesh.AddVertex(input.Positions[vertex.xref]);
    result.SourceVertexForOutputVertex.push_back(vertex.xref);
    outputUvs.emplace_back(vertex.uv[0] / static_cast<float>(atlas->width),
                           vertex.uv[1] / static_cast<float>(atlas->height));
  }

  if (input.HasVertexProperties && options.CopySourceVertexProperties) {
    const auto copyDiagnostics = CopySourceVertexPropertiesByXref(
        input.VertexProperties, result.SourceVertexForOutputVertex,
        result.OutputMesh.VertexProperties());
    result.Diagnostics.CopiedVertexPropertyCount =
        copyDiagnostics.CopiedPropertyCount;
    result.Diagnostics.SkippedVertexPropertyCount =
        copyDiagnostics.SkippedPropertyCount;
    result.Diagnostics.PropertyXrefOutOfRangeCount =
        copyDiagnostics.XrefOutOfRangeCount;
  }

  auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
  for (std::size_t i = 0; i < outputUvs.size(); ++i) {
    texcoords.Vector()[i] = outputUvs[i];
  }

  if (output.indexCount % 3u != 0u) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas",
                       "xatlas output index count is not triangular");
  }

  const std::size_t outputFaceCount = output.indexCount / 3u;
  result.SourceFaceForOutputFace.reserve(outputFaceCount);
  result.OutputFaceChart.reserve(outputFaceCount);
  for (std::size_t faceIndex = 0; faceIndex < outputFaceCount; ++faceIndex) {
    const std::uint32_t i0 = output.indexArray[faceIndex * 3u + 0u];
    const std::uint32_t i1 = output.indexArray[faceIndex * 3u + 1u];
    const std::uint32_t i2 = output.indexArray[faceIndex * 3u + 2u];
    if (i0 >= output.vertexCount || i1 >= output.vertexCount ||
        i2 >= output.vertexCount) {
      return MakeFailure(input, UvAtlasStatus::BackendFailed, "xatlas",
                         "xatlas output index out of range");
    }
    (void)result.OutputMesh.AddTriangle(i0, i1, i2);
    result.SourceFaceForOutputFace.push_back(
        faceIndex < input.Faces.size() ? static_cast<std::uint32_t>(faceIndex)
                                       : kInvalidIndex);

    const xatlas::Vertex &firstVertex = output.vertexArray[i0];
    result.OutputFaceChart.push_back(
        firstVertex.chartIndex >= 0
            ? static_cast<std::uint32_t>(firstVertex.chartIndex)
            : kInvalidIndex);
  }

  FinalizeUvBounds(result.Diagnostics, outputUvs);
  result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
  AttachDiagnostics(result, input, options, "xatlas");
  return result;
}
} // namespace

const char *ToString(const UvAtlasStatus status) noexcept {
  switch (status) {
  case UvAtlasStatus::Success:
    return "success";
  case UvAtlasStatus::EmptyInput:
    return "empty_input";
  case UvAtlasStatus::MissingPositions:
    return "missing_positions";
  case UvAtlasStatus::MissingFaces:
    return "missing_faces";
  case UvAtlasStatus::MissingAuthoredUvs:
    return "missing_authored_uvs";
  case UvAtlasStatus::NonTriangleFace:
    return "non_triangle_face";
  case UvAtlasStatus::OutOfRangeIndex:
    return "out_of_range_index";
  case UvAtlasStatus::NonFinitePosition:
    return "non_finite_position";
  case UvAtlasStatus::NonFiniteAuthoredUv:
    return "non_finite_authored_uv";
  case UvAtlasStatus::DegenerateInput:
    return "degenerate_input";
  case UvAtlasStatus::InvalidAuthoredUvs:
    return "invalid_authored_uvs";
  case UvAtlasStatus::BackendUnavailable:
    return "backend_unavailable";
  case UvAtlasStatus::BackendRejectedInput:
    return "backend_rejected_input";
  case UvAtlasStatus::BackendFailed:
    return "backend_failed";
  case UvAtlasStatus::Cancelled:
    return "cancelled";
  }
  return "unknown";
}

const char *ToString(const UvAtlasProvenance provenance) noexcept {
  switch (provenance) {
  case UvAtlasProvenance::None:
    return "none";
  case UvAtlasProvenance::AuthoredPreserved:
    return "authored_preserved";
  case UvAtlasProvenance::Generated:
    return "generated";
  }
  return "unknown";
}

const char *ToString(const UvAtlasMethod method) noexcept {
  switch (method) {
  case UvAtlasMethod::None:
    return "none";
  case UvAtlasMethod::Authored:
    return "authored";
  case UvAtlasMethod::XAtlas:
    return "xatlas";
  case UvAtlasMethod::FastStaged:
    return "fast_staged";
  }
  return "unknown";
}

UvAtlasInput
BorrowInput(const MeshSoup::IndexedMesh &mesh,
            const std::span<const glm::vec2> authoredTexcoords) noexcept {
  return UvAtlasInput{
      .Positions = mesh.Positions(),
      .Faces = mesh.Faces(),
      .AuthoredTexcoords = authoredTexcoords,
      .VertexProperties = mesh.VertexProperties(),
      .HasVertexProperties = true,
  };
}

UvAtlasDiagnostics ValidateUvAtlasInput(const UvAtlasInput &input) {
  UvAtlasDiagnostics diagnostics = MakeDiagnostics(input);

  if (input.Positions.empty() && input.Faces.empty()) {
    diagnostics.Status = UvAtlasStatus::EmptyInput;
    return diagnostics;
  }
  if (input.Positions.empty()) {
    diagnostics.Status = UvAtlasStatus::MissingPositions;
    return diagnostics;
  }
  if (input.Faces.empty()) {
    diagnostics.Status = UvAtlasStatus::MissingFaces;
    return diagnostics;
  }

  for (const glm::vec3 position : input.Positions) {
    if (!IsFinite(position)) {
      ++diagnostics.NonFinitePositionCount;
    }
  }

  for (const MeshSoup::PolygonFace &face : input.Faces) {
    if (face.Indices.size() != 3u) {
      ++diagnostics.NonTriangleFaceCount;
      continue;
    }

    bool faceOutOfRange = false;
    bool faceNonFinite = false;
    for (const MeshSoup::Index index : face.Indices) {
      if (index >= input.Positions.size()) {
        ++diagnostics.OutOfRangeIndexCount;
        faceOutOfRange = true;
      } else if (!IsFinite(input.Positions[index])) {
        faceNonFinite = true;
      }
    }

    if (faceOutOfRange || faceNonFinite) {
      continue;
    }
    if (MeshUtils::TriangleArea(
            input.Positions[face.Indices[0]], input.Positions[face.Indices[1]],
            input.Positions[face.Indices[2]]) <= kDegenerateAreaEpsilon) {
      ++diagnostics.DegenerateFaceCount;
    }
  }

  if (diagnostics.NonTriangleFaceCount > 0u) {
    diagnostics.Status = UvAtlasStatus::NonTriangleFace;
  } else if (diagnostics.OutOfRangeIndexCount > 0u) {
    diagnostics.Status = UvAtlasStatus::OutOfRangeIndex;
  } else if (diagnostics.NonFinitePositionCount > 0u) {
    diagnostics.Status = UvAtlasStatus::NonFinitePosition;
  } else if (diagnostics.DegenerateFaceCount == input.Faces.size()) {
    diagnostics.Status = UvAtlasStatus::DegenerateInput;
  } else {
    diagnostics.Status = UvAtlasStatus::Success;
  }
  return diagnostics;
}

UvAtlasDiagnostics ValidateAuthoredUvs(const UvAtlasInput &input) {
  UvAtlasDiagnostics diagnostics = ValidateUvAtlasInput(input);
  if (diagnostics.Status != UvAtlasStatus::Success) {
    return diagnostics;
  }
  if (input.AuthoredTexcoords.empty() ||
      input.AuthoredTexcoords.size() != input.Positions.size()) {
    diagnostics.Status = UvAtlasStatus::MissingAuthoredUvs;
    return diagnostics;
  }

  for (const glm::vec2 uv : input.AuthoredTexcoords) {
    if (!IsFinite(uv)) {
      ++diagnostics.NonFiniteAuthoredUvCount;
    }
  }
  if (diagnostics.NonFiniteAuthoredUvCount > 0u) {
    diagnostics.Status = UvAtlasStatus::NonFiniteAuthoredUv;
    return diagnostics;
  }

  std::vector<std::uint32_t> indices;
  if (!ExtractTriangleIndices(input, indices)) {
    diagnostics.Status = UvAtlasStatus::InvalidAuthoredUvs;
    return diagnostics;
  }

  const auto halfedge = MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(
      input.Positions, indices);
  if (!halfedge) {
    diagnostics.Status = UvAtlasStatus::InvalidAuthoredUvs;
    return diagnostics;
  }

  diagnostics.Quality = Parameterization::EvaluateParameterizationDiagnostics(
      *halfedge, input.AuthoredTexcoords);
  if (diagnostics.Quality.Status !=
      Parameterization::ParameterizationDiagnosticsStatus::Success) {
    diagnostics.Status = UvAtlasStatus::InvalidAuthoredUvs;
    return diagnostics;
  }

  FinalizeUvBounds(diagnostics, input.AuthoredTexcoords);
  diagnostics.Status = UvAtlasStatus::Success;
  return diagnostics;
}

VertexPropertyCopyDiagnostics CopySourceVertexPropertiesByXref(
    const ConstPropertySet &source,
    const std::span<const std::uint32_t> sourceVertexForOutputVertex,
    PropertySet &target) {
  VertexPropertyCopyDiagnostics diagnostics{};

  for (const std::string &name : source.Properties()) {
    if (name == "v:point" || name == MeshUtils::kVertexTexcoordPropertyName) {
      continue;
    }

    if (!TryCopyKnownPropertyType(source, name, sourceVertexForOutputVertex,
                                  target, diagnostics)) {
      ++diagnostics.SkippedPropertyCount;
    }
  }

  return diagnostics;
}

UvAtlasBackend DefaultXAtlasBackend() noexcept {
  return UvAtlasBackend{.Name = "xatlas", .Generate = &GenerateWithXAtlas};
}

UvAtlasBackend DefaultFastStagedBackend() noexcept {
  return UvAtlasBackend{.Name = "fast-staged",
                        .Generate = &GenerateWithFastStaged};
}

UvAtlasResult ResolveUvAtlas(const UvAtlasInput &input,
                             const UvAtlasOptions &options,
                             const UvAtlasBackend *backend) {
  if (options.CancelRequested) {
    UvAtlasResult failure =
        MakeFailure(input, UvAtlasStatus::Cancelled, options.BackendName,
                    "cancel requested before atlas resolution");
    AttachDiagnostics(failure, input, options, options.BackendName);
    failure.Diagnostics.ActualMethod = UvAtlasMethod::None;
    return failure;
  }

  if (options.PreserveValidAuthoredUvs && !options.ForceRegenerate) {
    const UvAtlasDiagnostics authoredValidation = ValidateAuthoredUvs(input);
    if (authoredValidation.Status == UvAtlasStatus::Success) {
      UvAtlasResult preserved = BuildPreservedResult(input, authoredValidation);
      AttachDiagnostics(preserved, input, options, "authored");
      return preserved;
    }

    const UvAtlasDiagnostics baseValidation = ValidateUvAtlasInput(input);
    if (baseValidation.Status != UvAtlasStatus::Success) {
      UvAtlasResult failure =
          MakeFailure(input, baseValidation.Status, options.BackendName,
                      "invalid atlas input");
      failure.Diagnostics = baseValidation;
      failure.Diagnostics.BackendName = options.BackendName;
      AttachDiagnostics(failure, input, options, options.BackendName);
      failure.Diagnostics.ActualMethod = UvAtlasMethod::None;
      return failure;
    }
  }

  UvAtlasBackend defaultBackend{};
  if (backend == nullptr) {
    defaultBackend = options.Method == UvAtlasMethod::FastStaged
                         ? DefaultFastStagedBackend()
                         : DefaultXAtlasBackend();
    backend = &defaultBackend;
  }
  if (backend->Generate == nullptr) {
    UvAtlasResult failure = MakeFailure(
        input, UvAtlasStatus::BackendUnavailable, options.BackendName,
        "atlas backend has no generate function");
    AttachDiagnostics(failure, input, options, backend->Name);
    failure.Diagnostics.ActualMethod = UvAtlasMethod::None;
    return failure;
  }

  UvAtlasResult result = backend->Generate(input, options);
  AttachDiagnostics(result, input, options, backend->Name);
  if (result.Status != UvAtlasStatus::Success &&
      options.Method == UvAtlasMethod::FastStaged &&
      options.AllowXAtlasFallback &&
      std::string_view{backend->Name} != "xatlas") {
    UvAtlasBackend fallbackBackend = DefaultXAtlasBackend();
    UvAtlasResult fallback = fallbackBackend.Generate(input, options);
    AttachDiagnostics(fallback, input, options, fallbackBackend.Name);
    fallback.Diagnostics.ActualMethod =
        fallback.Status == UvAtlasStatus::Success
            ? UvAtlasMethod::XAtlas
            : fallback.Diagnostics.ActualMethod;
    fallback.Diagnostics.UsedFallback = true;
    fallback.Diagnostics.FallbackReason =
        std::string{"fast staged backend '"} + std::string{backend->Name} +
        "' returned " + ToString(result.Status) + "; used xatlas fallback";
    return fallback;
  }
  return result;
}
} // namespace Geometry::UvAtlas
