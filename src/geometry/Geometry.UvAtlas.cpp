module;

#include <algorithm>
#include <array>
#include <atomic>
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
import Geometry.UvAtlas.ChartSolve;
import Geometry.UvAtlas.Validation;

namespace Geometry::UvAtlas {
namespace {
constexpr std::uint32_t kInvalidIndex =
    std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kDefaultResolution = 1024u;
constexpr std::uint32_t kMaxResolution = 16384u;
constexpr std::uint32_t kMaxPadding = 256u;
constexpr double kMaxDistortionLimit = 1.0e6;
constexpr std::uint32_t kMaxChartBudget = 1u << 24u;
constexpr std::uint32_t kMaxIterationBudget = 10000u;
// Chart growth admits a face whose normal lies within 60 degrees of the
// growing chart's area-weighted mean normal. Larger cones are safe because
// every solved chart is gated and split on failure; this only seeds charts.
constexpr double kChartNormalConeCos = 0.5;
// Accepted charts stay this far (relative) below the requested bounds so
// float rounding of packed UVs cannot push them over the validator limit.
constexpr double kChartLimitMargin = 1.0e-3;
// Packing keeps chart boxes strictly inside [0, 1 - margin]^2.
constexpr double kPackMargin = 1.0e-6;

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

[[nodiscard]] bool CancelRequested(const UvAtlasOptions &options) noexcept {
  return options.CancelRequested ||
         (options.CancelFlag != nullptr &&
          options.CancelFlag->load(std::memory_order_relaxed));
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

// ---------------------------------------------------------------------------
// Generation preflight shared by the native and xatlas generators.
// ---------------------------------------------------------------------------

// Returns a failure result, or nullopt when generation may proceed.
[[nodiscard]] std::optional<UvAtlasResult>
PreflightGeneration(const UvAtlasInput &input, const UvAtlasOptions &options,
                    const std::string_view backendName,
                    UvAtlasDiagnostics &validation) {
  if (CancelRequested(options)) {
    return MakeFailure(input, UvAtlasStatus::Cancelled, backendName,
                       "cancel requested before atlas generation");
  }
  const UvAtlasOptionsValidation optionCheck = ValidateUvAtlasOptions(options);
  if (!optionCheck.Valid) {
    return MakeFailure(input, UvAtlasStatus::InvalidOptions, backendName,
                       optionCheck.Detail);
  }

  validation = ValidateUvAtlasInput(input);
  if (validation.Status != UvAtlasStatus::Success) {
    UvAtlasResult failure = MakeFailure(input, validation.Status, backendName,
                                        "invalid atlas input");
    failure.Diagnostics = validation;
    failure.Diagnostics.BackendName = std::string{backendName};
    failure.Diagnostics.BackendDetail = "invalid atlas input";
    return failure;
  }
  if (!input.FaceRegions.empty() &&
      input.FaceRegions.size() != input.Faces.size()) {
    return MakeFailure(input, UvAtlasStatus::InvalidRegionLabels, backendName,
                       "region label count (" +
                           std::to_string(input.FaceRegions.size()) +
                           ") does not equal the face count (" +
                           std::to_string(input.Faces.size()) + ")");
  }

  std::vector<std::uint32_t> degenerate;
  for (std::size_t f = 0u; f < input.Faces.size(); ++f) {
    const MeshSoup::PolygonFace &face = input.Faces[f];
    if (IsDegenerateAtlasTriangle(input.Positions[face.Indices[0]],
                                  input.Positions[face.Indices[1]],
                                  input.Positions[face.Indices[2]])) {
      degenerate.push_back(static_cast<std::uint32_t>(f));
    }
  }
  if (!degenerate.empty()) {
    UvAtlasResult failure = MakeFailure(
        input, UvAtlasStatus::DegenerateInput, backendName,
        std::to_string(degenerate.size()) +
            " zero-area source triangles cannot receive a valid UV map "
            "(first face " +
            std::to_string(degenerate.front()) + ")");
    failure.Diagnostics.DegenerateFaceCount = degenerate.size();
    failure.Diagnostics.InvalidFaces = std::move(degenerate);
    return failure;
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------
// Source topology: edge groups, chartable adjacency, cuts and regions.
// ---------------------------------------------------------------------------

struct SourceEdgeFaces {
  std::uint32_t A{kInvalidIndex};
  std::uint32_t B{kInvalidIndex};
  std::vector<std::uint32_t> Faces{};
  // Directed start vertex of this edge inside each face (parallel to Faces).
  std::vector<std::uint32_t> Starts{};
};

struct AtlasTopology {
  // Edge groups in first-seen (face, corner) order.
  std::vector<SourceEdgeFaces> Edges{};
  // Edge group of corner edge k -> k+1 of each face.
  std::vector<std::array<std::uint32_t, 3u>> FaceEdges{};
  // Exactly two faces with opposite winding and the same region label.
  std::vector<std::uint8_t> EdgeChartable{};
  // Shared by more than two faces, or by two faces with the same winding.
  std::vector<std::uint8_t> EdgeTopologyCut{};
  std::vector<std::uint32_t> FaceLabel{};
  // Edge-connected component (any label) and label component per face.
  std::vector<std::uint32_t> FaceConnectedComponent{};
  std::vector<std::uint32_t> FaceRegionComponent{};
  std::uint32_t RegionLabelCount{0};
  std::uint32_t RegionComponentCount{0};
  std::uint32_t ConnectedComponentCount{0};
  std::uint32_t NonManifoldEdgeCount{0};
  std::uint32_t InconsistentOrientationEdgeCount{0};

  [[nodiscard]] std::uint32_t Across(const std::uint32_t edge,
                                     const std::uint32_t face) const noexcept {
    const std::vector<std::uint32_t> &faces = Edges[edge].Faces;
    return faces[0] == face ? faces[1] : faces[0];
  }
};

[[nodiscard]] std::uint32_t FindRoot(std::vector<std::uint32_t> &parent,
                                     std::uint32_t value) {
  while (parent[value] != value) {
    parent[value] = parent[parent[value]];
    value = parent[value];
  }
  return value;
}

void UniteRoots(std::vector<std::uint32_t> &parent, const std::uint32_t a,
                const std::uint32_t b) {
  const std::uint32_t rootA = FindRoot(parent, a);
  const std::uint32_t rootB = FindRoot(parent, b);
  if (rootA != rootB) {
    parent[std::max(rootA, rootB)] = std::min(rootA, rootB);
  }
}

// Dense component ids in first-seen element order.
[[nodiscard]] std::uint32_t DenseComponents(std::vector<std::uint32_t> &parent,
                                            std::vector<std::uint32_t> &ids) {
  ids.assign(parent.size(), kInvalidIndex);
  std::vector<std::uint32_t> idOfRoot(parent.size(), kInvalidIndex);
  std::uint32_t count = 0u;
  for (std::size_t i = 0u; i < parent.size(); ++i) {
    const std::uint32_t root = FindRoot(parent, static_cast<std::uint32_t>(i));
    if (idOfRoot[root] == kInvalidIndex) {
      idOfRoot[root] = count++;
    }
    ids[i] = idOfRoot[root];
  }
  return count;
}

[[nodiscard]] AtlasTopology AnalyzeTopology(const UvAtlasInput &input) {
  const std::size_t faceCount = input.Faces.size();
  AtlasTopology topology{};
  topology.FaceLabel.resize(faceCount, 0u);
  if (!input.FaceRegions.empty()) {
    std::copy(input.FaceRegions.begin(), input.FaceRegions.end(),
              topology.FaceLabel.begin());
  }
  topology.FaceEdges.resize(faceCount);
  topology.Edges.reserve(faceCount * 3u / 2u + 1u);
  std::unordered_map<std::uint64_t, std::uint32_t> edgeByKey;
  edgeByKey.reserve(faceCount * 3u);
  for (std::size_t f = 0u; f < faceCount; ++f) {
    const MeshSoup::PolygonFace &face = input.Faces[f];
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const std::uint32_t start = face.Indices[corner];
      const std::uint32_t end = face.Indices[(corner + 1u) % 3u];
      const std::uint32_t a = std::min(start, end);
      const std::uint32_t b = std::max(start, end);
      const std::uint64_t key =
          (static_cast<std::uint64_t>(a) << 32u) | static_cast<std::uint64_t>(b);
      const auto [match, inserted] = edgeByKey.try_emplace(
          key, static_cast<std::uint32_t>(topology.Edges.size()));
      if (inserted) {
        topology.Edges.push_back(SourceEdgeFaces{.A = a, .B = b});
      }
      SourceEdgeFaces &edge = topology.Edges[match->second];
      edge.Faces.push_back(static_cast<std::uint32_t>(f));
      edge.Starts.push_back(start);
      topology.FaceEdges[f][corner] = match->second;
    }
  }

  const std::size_t edgeCount = topology.Edges.size();
  topology.EdgeChartable.assign(edgeCount, 0u);
  topology.EdgeTopologyCut.assign(edgeCount, 0u);
  std::vector<std::uint32_t> connected(faceCount);
  std::vector<std::uint32_t> regions(faceCount);
  std::iota(connected.begin(), connected.end(), 0u);
  std::iota(regions.begin(), regions.end(), 0u);
  for (std::size_t e = 0u; e < edgeCount; ++e) {
    const SourceEdgeFaces &edge = topology.Edges[e];
    if (edge.Faces.size() == 2u) {
      if (edge.Starts[0] == edge.Starts[1]) {
        ++topology.InconsistentOrientationEdgeCount;
        topology.EdgeTopologyCut[e] = 1u;
      } else if (topology.FaceLabel[edge.Faces[0]] ==
                 topology.FaceLabel[edge.Faces[1]]) {
        topology.EdgeChartable[e] = 1u;
      }
    } else if (edge.Faces.size() > 2u) {
      ++topology.NonManifoldEdgeCount;
      topology.EdgeTopologyCut[e] = 1u;
    }
    for (std::size_t i = 0u; i < edge.Faces.size(); ++i) {
      for (std::size_t j = i + 1u; j < edge.Faces.size(); ++j) {
        UniteRoots(connected, edge.Faces[i], edge.Faces[j]);
        if (topology.FaceLabel[edge.Faces[i]] ==
            topology.FaceLabel[edge.Faces[j]]) {
          UniteRoots(regions, edge.Faces[i], edge.Faces[j]);
        }
      }
    }
  }
  topology.ConnectedComponentCount =
      DenseComponents(connected, topology.FaceConnectedComponent);
  topology.RegionComponentCount =
      DenseComponents(regions, topology.FaceRegionComponent);

  std::vector<std::uint32_t> labels = topology.FaceLabel;
  std::sort(labels.begin(), labels.end());
  topology.RegionLabelCount = static_cast<std::uint32_t>(
      std::unique(labels.begin(), labels.end()) - labels.begin());
  return topology;
}

struct FaceFrame {
  glm::dvec3 Normal{0.0, 0.0, 1.0};
  glm::dvec3 Centroid{0.0};
  double Area{0.0};
};

[[nodiscard]] std::vector<FaceFrame> BuildFaceFrames(const UvAtlasInput &input) {
  std::vector<FaceFrame> frames;
  frames.reserve(input.Faces.size());
  for (const MeshSoup::PolygonFace &face : input.Faces) {
    const glm::dvec3 p0{input.Positions[face.Indices[0u]]};
    const glm::dvec3 p1{input.Positions[face.Indices[1u]]};
    const glm::dvec3 p2{input.Positions[face.Indices[2u]]};
    const glm::dvec3 cross = glm::cross(p1 - p0, p2 - p0);
    const double doubleArea = glm::length(cross);
    frames.push_back(FaceFrame{
        .Normal = doubleArea > 0.0 ? cross / doubleArea
                                   : glm::dvec3{0.0, 0.0, 1.0},
        .Centroid = (p0 + p1 + p2) / 3.0,
        .Area = 0.5 * doubleArea,
    });
  }
  return frames;
}

// True when `face` shares a cut edge with a face already owned by `id`.
// A chart never holds both sides of a cut, so cuts stay open seams.
[[nodiscard]] bool TouchesCutPartner(const AtlasTopology &topology,
                                     const std::uint32_t face,
                                     const std::span<const std::uint32_t> owner,
                                     const std::uint32_t id) {
  for (const std::uint32_t edge : topology.FaceEdges[face]) {
    if (topology.EdgeTopologyCut[edge] == 0u) {
      continue;
    }
    for (const std::uint32_t other : topology.Edges[edge].Faces) {
      if (other != face && owner[other] == id) {
        return true;
      }
    }
  }
  return false;
}

// Deterministic breadth-first growth over chartable edges from seeds in
// face order. coneCos <= -1 disables the normal cone (pure cut components).
[[nodiscard]] std::vector<std::vector<std::uint32_t>>
GrowCharts(const AtlasTopology &topology, const std::vector<FaceFrame> &frames,
           const double coneCos) {
  const std::size_t faceCount = frames.size();
  std::vector<std::uint32_t> owner(faceCount, kInvalidIndex);
  std::vector<std::vector<std::uint32_t>> charts;
  for (std::size_t seed = 0u; seed < faceCount; ++seed) {
    if (owner[seed] != kInvalidIndex) {
      continue;
    }
    const auto id = static_cast<std::uint32_t>(charts.size());
    std::vector<std::uint32_t> chart{static_cast<std::uint32_t>(seed)};
    owner[seed] = id;
    glm::dvec3 normalSum = frames[seed].Normal * frames[seed].Area;
    for (std::size_t cursor = 0u; cursor < chart.size(); ++cursor) {
      const std::uint32_t face = chart[cursor];
      for (const std::uint32_t edge : topology.FaceEdges[face]) {
        if (topology.EdgeChartable[edge] == 0u) {
          continue;
        }
        const std::uint32_t next = topology.Across(edge, face);
        if (owner[next] != kInvalidIndex) {
          continue;
        }
        if (coneCos > -1.0) {
          const double length = glm::length(normalSum);
          const glm::dvec3 axis =
              length > 0.0 ? normalSum / length : frames[seed].Normal;
          if (glm::dot(frames[next].Normal, axis) < coneCos) {
            continue;
          }
        }
        if (TouchesCutPartner(topology, next, owner, id)) {
          continue;
        }
        owner[next] = id;
        chart.push_back(next);
        normalSum += frames[next].Normal * frames[next].Area;
      }
    }
    std::sort(chart.begin(), chart.end());
    charts.push_back(std::move(chart));
  }
  return charts;
}

// Merge fragment proposals (fewer than kSmallProposalFaces faces) into a
// neighbouring proposal at least kMergeTargetRatio times larger whose mean
// normal faces the same half-space, choosing the longest shared chartable
// boundary. Cone growth leaves such fragments where a face deviates from
// every neighbouring chart; kept alone they fragment the atlas and can fall
// below one texel. Equal small charts (e.g. cube faces) are left alone.
// Chartable edges never cross a region, and merges never pair the two
// sides of a cut; merged charts still pass the solve/quality gate.
void MergeSmallProposals(const AtlasTopology &topology,
                         const std::vector<FaceFrame> &frames,
                         std::vector<std::vector<std::uint32_t>> &charts) {
  constexpr std::size_t kSmallProposalFaces = 8u;
  constexpr std::size_t kMergeTargetRatio = 4u;
  std::vector<std::uint32_t> owner(frames.size(), kInvalidIndex);
  std::vector<glm::dvec3> normalSum(charts.size(), glm::dvec3{0.0});
  for (std::size_t id = 0u; id < charts.size(); ++id) {
    for (const std::uint32_t face : charts[id]) {
      owner[face] = static_cast<std::uint32_t>(id);
      normalSum[id] += frames[face].Normal * frames[face].Area;
    }
  }
  std::unordered_map<std::uint32_t, std::uint32_t> sharedEdges;
  for (std::size_t id = 0u; id < charts.size(); ++id) {
    std::vector<std::uint32_t> &small = charts[id];
    if (small.empty() || small.size() >= kSmallProposalFaces) {
      continue;
    }
    sharedEdges.clear();
    for (const std::uint32_t face : small) {
      for (const std::uint32_t edge : topology.FaceEdges[face]) {
        if (topology.EdgeChartable[edge] == 0u) {
          continue;
        }
        const std::uint32_t other = owner[topology.Across(edge, face)];
        if (other != id) {
          ++sharedEdges[other];
        }
      }
    }
    std::uint32_t best = kInvalidIndex;
    std::uint32_t bestShared = 0u;
    for (const auto [candidate, shared] : sharedEdges) {
      if (shared < bestShared || (shared == bestShared && candidate > best)) {
        continue;
      }
      if (charts[candidate].size() < kMergeTargetRatio * small.size() ||
          charts[candidate].size() < kSmallProposalFaces ||
          !(glm::dot(normalSum[candidate], normalSum[id]) > 0.0)) {
        continue;
      }
      const bool cutsAllowed = std::none_of(
          small.begin(), small.end(), [&](const std::uint32_t face) {
            return TouchesCutPartner(topology, face, owner, candidate);
          });
      if (cutsAllowed) {
        best = candidate;
        bestShared = shared;
      }
    }
    if (best == kInvalidIndex) {
      continue;
    }
    std::vector<std::uint32_t> &target = charts[best];
    for (const std::uint32_t face : small) {
      owner[face] = best;
      target.push_back(face);
    }
    normalSum[best] += normalSum[id];
    std::sort(target.begin(), target.end());
    small.clear();
  }
  std::erase_if(charts, [](const std::vector<std::uint32_t> &chart) {
    return chart.empty();
  });
  std::sort(charts.begin(), charts.end(),
            [](const auto &a, const auto &b) { return a.front() < b.front(); });
}

// ---------------------------------------------------------------------------
// Native charts: local meshes, refinement splits and the quality gate.
// ---------------------------------------------------------------------------

struct ChartMesh {
  std::vector<std::uint32_t> Faces{};
  std::vector<std::uint32_t> SourceVertices{};
  std::vector<glm::vec3> Positions{};
  // Three local vertices per face in source corner order.
  std::vector<std::uint32_t> Triangles{};
};

// Local vertices are corner fans connected through chart-internal chartable
// edges, so bowtie vertices and cut edges separate without mutating the
// source. Vertices are numbered in first-seen (face, corner) order.
[[nodiscard]] ChartMesh BuildChartMesh(const UvAtlasInput &input,
                                       const AtlasTopology &topology,
                                       std::vector<std::uint32_t> faces,
                                       std::vector<std::uint32_t> &localFace) {
  ChartMesh mesh{};
  mesh.Faces = std::move(faces);
  const std::size_t count = mesh.Faces.size();
  for (std::size_t i = 0u; i < count; ++i) {
    localFace[mesh.Faces[i]] = static_cast<std::uint32_t>(i);
  }
  std::vector<std::uint32_t> corners(3u * count);
  std::iota(corners.begin(), corners.end(), 0u);
  for (std::size_t i = 0u; i < count; ++i) {
    const std::uint32_t face = mesh.Faces[i];
    const auto &indices = input.Faces[face].Indices;
    for (std::size_t k = 0u; k < 3u; ++k) {
      const std::uint32_t edge = topology.FaceEdges[face][k];
      if (topology.EdgeChartable[edge] == 0u) {
        continue;
      }
      const std::uint32_t other = topology.Across(edge, face);
      const std::uint32_t j = localFace[other];
      if (j == kInvalidIndex || j <= i) {
        continue;
      }
      const std::uint32_t a = indices[k];
      const std::uint32_t b = indices[(k + 1u) % 3u];
      const auto &otherIndices = input.Faces[other].Indices;
      for (std::size_t kg = 0u; kg < 3u; ++kg) {
        if (otherIndices[kg] == a) {
          UniteRoots(corners, static_cast<std::uint32_t>(3u * i + k),
                     static_cast<std::uint32_t>(3u * j + kg));
        } else if (otherIndices[kg] == b) {
          UniteRoots(corners, static_cast<std::uint32_t>(3u * i + (k + 1u) % 3u),
                     static_cast<std::uint32_t>(3u * j + kg));
        }
      }
    }
  }

  std::vector<std::uint32_t> vertexOfRoot(3u * count, kInvalidIndex);
  mesh.Triangles.resize(3u * count);
  for (std::size_t i = 0u; i < count; ++i) {
    const auto &indices = input.Faces[mesh.Faces[i]].Indices;
    for (std::size_t k = 0u; k < 3u; ++k) {
      const std::uint32_t root =
          FindRoot(corners, static_cast<std::uint32_t>(3u * i + k));
      if (vertexOfRoot[root] == kInvalidIndex) {
        vertexOfRoot[root] =
            static_cast<std::uint32_t>(mesh.SourceVertices.size());
        mesh.SourceVertices.push_back(indices[k]);
        mesh.Positions.push_back(input.Positions[indices[k]]);
      }
      mesh.Triangles[3u * i + k] = vertexOfRoot[root];
    }
  }
  for (const std::uint32_t face : mesh.Faces) {
    localFace[face] = kInvalidIndex;
  }
  return mesh;
}

// Split a rejected chart in two connected pieces: multi-source breadth-first
// growth over chart-internal chartable edges from the two faces at the ends
// of the centroid principal axis. Both pieces are non-empty and connected
// (no isolated fragments), and each is strictly smaller than the chart, so
// refinement terminates.
[[nodiscard]] std::array<std::vector<std::uint32_t>, 2u>
SplitChart(const AtlasTopology &topology, const std::vector<FaceFrame> &frames,
           const std::vector<std::uint32_t> &faces,
           std::vector<std::uint32_t> &group) {
  double totalArea = 0.0;
  glm::dvec3 mean{0.0};
  glm::dvec3 lo{std::numeric_limits<double>::max()};
  glm::dvec3 hi{std::numeric_limits<double>::lowest()};
  for (const std::uint32_t face : faces) {
    totalArea += frames[face].Area;
    mean += frames[face].Area * frames[face].Centroid;
    lo = glm::min(lo, frames[face].Centroid);
    hi = glm::max(hi, frames[face].Centroid);
  }
  mean /= totalArea;
  glm::dmat3 covariance{0.0};
  for (const std::uint32_t face : faces) {
    const glm::dvec3 d = frames[face].Centroid - mean;
    covariance += frames[face].Area * glm::outerProduct(d, d);
  }
  const glm::dvec3 extent = hi - lo;
  glm::dvec3 axis = extent.x >= extent.y && extent.x >= extent.z
                        ? glm::dvec3{1.0, 0.0, 0.0}
                        : (extent.y >= extent.z ? glm::dvec3{0.0, 1.0, 0.0}
                                                : glm::dvec3{0.0, 0.0, 1.0});
  for (int iteration = 0; iteration < 32; ++iteration) {
    const glm::dvec3 next = covariance * axis;
    const double length = glm::length(next);
    if (!(length > 0.0) || !std::isfinite(length)) {
      break;
    }
    axis = next / length;
  }

  // Seeds: extreme projections, ties broken by face order.
  std::uint32_t seedA = faces.front();
  std::uint32_t seedB = faces.back();
  double minProjection = std::numeric_limits<double>::max();
  double maxProjection = std::numeric_limits<double>::lowest();
  for (const std::uint32_t face : faces) {
    const double projection = glm::dot(frames[face].Centroid - mean, axis);
    if (projection < minProjection) {
      minProjection = projection;
      seedA = face;
    }
    if (projection > maxProjection) {
      maxProjection = projection;
      seedB = face;
    }
  }
  if (seedA == seedB) {
    seedA = faces.front();
    seedB = faces.back();
  }

  constexpr std::uint32_t kUnassigned = 2u;
  for (const std::uint32_t face : faces) {
    group[face] = kUnassigned;
  }
  std::array<std::vector<std::uint32_t>, 2u> pieces{};
  std::vector<std::uint32_t> queue{seedA, seedB};
  group[seedA] = 0u;
  group[seedB] = 1u;
  for (std::size_t cursor = 0u; cursor < queue.size(); ++cursor) {
    const std::uint32_t face = queue[cursor];
    const std::uint32_t side = group[face];
    pieces[side].push_back(face);
    for (const std::uint32_t edge : topology.FaceEdges[face]) {
      if (topology.EdgeChartable[edge] == 0u) {
        continue;
      }
      const std::uint32_t next = topology.Across(edge, face);
      if (group[next] != kUnassigned) {
        continue;
      }
      group[next] = side;
      queue.push_back(next);
    }
  }
  for (const std::uint32_t face : faces) {
    // Charts are connected, so nothing stays unassigned; if one ever did,
    // keep it (a later solve rejects the disconnected piece and splits it)
    // rather than dropping a source face.
    if (group[face] == kUnassigned) {
      pieces[0].push_back(face);
    }
    group[face] = kInvalidIndex;
  }
  for (std::vector<std::uint32_t> &piece : pieces) {
    std::sort(piece.begin(), piece.end());
  }
  if (pieces[1].front() < pieces[0].front()) {
    std::swap(pieces[0], pieces[1]);
  }
  return pieces;
}

enum class ChartRejection : std::uint8_t {
  None = 0,
  Topology,
  Solve,
  Distortion,
  Overlap,
};

struct ChartGate {
  bool Accepted{false};
  ChartRejection Rejection{ChartRejection::None};
  double MaxConformal{0.0};
  double MaxArea{0.0};
  std::string Reason{};
};

// Unit-density charts share the atlas-wide density, so these maxima equal
// the validator's density-normalized values once packed at one scale.
[[nodiscard]] ChartGate EvaluateChartGate(const ChartMesh &mesh,
                                          const std::vector<glm::dvec2> &uvs,
                                          const UvAtlasOptions &options) {
  ChartGate gate{};
  const std::size_t count = mesh.Faces.size();
  glm::dvec2 lo{std::numeric_limits<double>::max()};
  glm::dvec2 hi{std::numeric_limits<double>::lowest()};
  for (const glm::dvec2 uv : uvs) {
    lo = glm::min(lo, uv);
    hi = glm::max(hi, uv);
  }
  for (std::size_t t = 0u; t < count; ++t) {
    const std::uint32_t *tri = &mesh.Triangles[3u * t];
    const UvFaceDistortion metric = MeasureUvFaceDistortion(
        glm::dvec3{mesh.Positions[tri[0]]}, glm::dvec3{mesh.Positions[tri[1]]},
        glm::dvec3{mesh.Positions[tri[2]]}, uvs[tri[0]], uvs[tri[1]],
        uvs[tri[2]]);
    if (!metric.Valid) {
      gate.Rejection = ChartRejection::Overlap;
      gate.Reason = "collapsed or flipped triangle";
      return gate;
    }
    const double areaRatio = metric.SigmaMax * metric.SigmaMin;
    gate.MaxConformal =
        std::max(gate.MaxConformal, metric.SigmaMax / metric.SigmaMin);
    gate.MaxArea = std::max(gate.MaxArea, std::max(areaRatio, 1.0 / areaRatio));
  }
  if (gate.MaxConformal >
      options.MaxConformalDistortion * (1.0 - kChartLimitMargin)) {
    gate.Rejection = ChartRejection::Distortion;
    gate.Reason = "conformal distortion bound";
    return gate;
  }
  if (gate.MaxArea > options.MaxAreaDistortion * (1.0 - kChartLimitMargin)) {
    gate.Rejection = ChartRejection::Distortion;
    gate.Reason = "area distortion bound";
    return gate;
  }
  if (count > 1u) {
    // Chart-internal overlap, evaluated in float like the published UVs.
    const glm::dvec2 extent = hi - lo;
    const double scale = 1.0 / std::max({extent.x, extent.y, 1.0e-300});
    std::vector<glm::vec2> corners(3u * count);
    for (std::size_t c = 0u; c < corners.size(); ++c) {
      corners[c] = glm::vec2{(uvs[mesh.Triangles[c]] - lo) * scale};
    }
    for (std::size_t t = 0u; t < count; ++t) {
      if (ExactUvOrientation(corners[3u * t], corners[3u * t + 1u],
                             corners[3u * t + 2u]) <= 0) {
        gate.Rejection = ChartRejection::Overlap;
        gate.Reason = "triangle collapses in float precision";
        return gate;
      }
    }
    if (FindUvTriangleOverlaps(corners, 1u).OverlapPairCount > 0u) {
      gate.Rejection = ChartRejection::Overlap;
      gate.Reason = "chart boundary self-overlap";
      return gate;
    }
  }
  gate.Accepted = true;
  return gate;
}

struct AcceptedChart {
  ChartMesh Mesh{};
  std::vector<glm::dvec2> Uvs{};
  std::string Backend{};
  std::uint32_t Proposal{0};
  std::uint32_t Iterations{0};
  bool Converged{false};
  double MaxConformal{0.0};
  double MaxArea{0.0};
  glm::dvec2 Min{0.0};
  glm::dvec2 Extent{0.0};
};

// Rotate a chart so its principal UV axis is horizontal (a proper rotation,
// so orientation and distortion are unchanged), then record its box.
void AlignChart(AcceptedChart &chart, const bool rotateToAxis) {
  if (rotateToAxis && chart.Uvs.size() > 2u) {
    glm::dvec2 mean{0.0};
    for (const glm::dvec2 uv : chart.Uvs) {
      mean += uv;
    }
    mean /= static_cast<double>(chart.Uvs.size());
    double cxx = 0.0;
    double cxy = 0.0;
    double cyy = 0.0;
    for (const glm::dvec2 uv : chart.Uvs) {
      const glm::dvec2 d = uv - mean;
      cxx += d.x * d.x;
      cxy += d.x * d.y;
      cyy += d.y * d.y;
    }
    const double angle = 0.5 * std::atan2(2.0 * cxy, cxx - cyy);
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    for (glm::dvec2 &uv : chart.Uvs) {
      const glm::dvec2 d = uv - mean;
      uv = glm::dvec2{c * d.x + s * d.y, -s * d.x + c * d.y};
    }
  }
  glm::dvec2 lo{std::numeric_limits<double>::max()};
  glm::dvec2 hi{std::numeric_limits<double>::lowest()};
  for (const glm::dvec2 uv : chart.Uvs) {
    lo = glm::min(lo, uv);
    hi = glm::max(hi, uv);
  }
  chart.Min = lo;
  chart.Extent = hi - lo;
}

struct ChartPlacement {
  glm::dvec2 Origin{0.0};
  bool Rotated{false};
};

// Shelf packing of padded chart boxes at one common scale; boxes stay
// inside [0, 1 - kPackMargin]^2 without clamping.
[[nodiscard]] bool TryPackCharts(const std::vector<AcceptedChart> &charts,
                                 const double scale, const double padding,
                                 const bool allowRotation,
                                 std::vector<ChartPlacement> &placements) {
  placements.assign(charts.size(), ChartPlacement{});
  if (!(scale > 0.0)) {
    return false;
  }
  const double limit = 1.0 - kPackMargin;
  std::vector<std::uint32_t> order(charts.size());
  std::iota(order.begin(), order.end(), 0u);
  std::stable_sort(order.begin(), order.end(),
                   [&charts](const std::uint32_t a, const std::uint32_t b) {
                     const glm::dvec2 ea = charts[a].Extent;
                     const glm::dvec2 eb = charts[b].Extent;
                     const double sideA = std::max(ea.x, ea.y);
                     const double sideB = std::max(eb.x, eb.y);
                     if (sideA != sideB) {
                       return sideA > sideB;
                     }
                     return a < b;
                   });

  double cursorX = 0.0;
  double cursorY = 0.0;
  double shelfHeight = 0.0;
  for (const std::uint32_t index : order) {
    const glm::dvec2 extent = charts[index].Extent;
    const glm::dvec2 plain = extent * scale + glm::dvec2{2.0 * padding};
    const glm::dvec2 turned =
        glm::dvec2{extent.y, extent.x} * scale + glm::dvec2{2.0 * padding};
    const auto choose = [&](const double shelfX, bool &rotated,
                            glm::dvec2 &size) {
      const bool plainFits = shelfX + plain.x <= limit && plain.y <= limit;
      const bool turnedFits =
          allowRotation && shelfX + turned.x <= limit && turned.y <= limit;
      if (!plainFits && !turnedFits) {
        return false;
      }
      // Prefer the lower item so shelves stay short.
      rotated = turnedFits && (!plainFits || turned.y < plain.y);
      size = rotated ? turned : plain;
      return true;
    };
    bool rotated = false;
    glm::dvec2 size{0.0};
    if (!choose(cursorX, rotated, size)) {
      cursorY += shelfHeight;
      cursorX = 0.0;
      shelfHeight = 0.0;
      if (!choose(cursorX, rotated, size)) {
        return false;
      }
    }
    if (cursorY + size.y > limit) {
      return false;
    }
    placements[index] = ChartPlacement{
        .Origin = glm::dvec2{cursorX + padding, cursorY + padding},
        .Rotated = rotated,
    };
    cursorX += size.x;
    shelfHeight = std::max(shelfHeight, size.y);
  }
  return true;
}

struct PackOutcome {
  bool Packed{false};
  double Scale{0.0};
  std::vector<ChartPlacement> Placements{};
  std::string Detail{};
};

[[nodiscard]] PackOutcome PackCharts(const std::vector<AcceptedChart> &charts,
                                     const UvAtlasOptions &options,
                                     const std::uint32_t resolution) {
  PackOutcome outcome{};
  const double padding =
      static_cast<double>(options.Padding) / static_cast<double>(resolution);
  if (options.TexelsPerUnit > 0.0f) {
    outcome.Scale = static_cast<double>(options.TexelsPerUnit) /
                    static_cast<double>(resolution);
    outcome.Packed = TryPackCharts(charts, outcome.Scale, padding,
                                   options.RotateCharts, outcome.Placements);
    if (!outcome.Packed) {
      outcome.Detail = "charts do not fit one atlas at the requested texels "
                       "per unit";
    }
    return outcome;
  }

  double maxExtent = 0.0;
  for (const AcceptedChart &chart : charts) {
    maxExtent = std::max({maxExtent, chart.Extent.x, chart.Extent.y});
  }
  if (!(maxExtent > 0.0) || !std::isfinite(maxExtent)) {
    outcome.Detail = "charts have no extent";
    return outcome;
  }
  const double upper = (1.0 - kPackMargin - 2.0 * padding) / maxExtent;
  if (!(upper > 0.0)) {
    outcome.Detail = "padding leaves no room for charts";
    return outcome;
  }
  std::vector<ChartPlacement> candidate;
  if (TryPackCharts(charts, upper, padding, options.RotateCharts, candidate)) {
    outcome.Packed = true;
    outcome.Scale = upper;
    outcome.Placements = std::move(candidate);
    return outcome;
  }
  double lo = 0.0;
  double hi = upper;
  for (int iteration = 0; iteration < 40; ++iteration) {
    const double mid = 0.5 * (lo + hi);
    if (TryPackCharts(charts, mid, padding, options.RotateCharts, candidate)) {
      lo = mid;
      outcome.Packed = true;
      outcome.Scale = mid;
      outcome.Placements = candidate;
    } else {
      hi = mid;
    }
  }
  if (!outcome.Packed) {
    outcome.Detail = "chart gutters alone exceed the atlas; increase the "
                     "resolution or reduce padding";
  }
  return outcome;
}

[[nodiscard]] glm::vec2 PlaceUv(const AcceptedChart &chart,
                                const ChartPlacement &placement,
                                const double scale, const glm::dvec2 uv) {
  const glm::dvec2 local = uv - chart.Min;
  // uv - Min is exact and non-negative; Extent - local may round below zero.
  const glm::dvec2 packed =
      placement.Rotated
          ? glm::dvec2{local.y, std::max(0.0, chart.Extent.x - local.x)}
          : local;
  return glm::vec2{placement.Origin + packed * scale};
}

[[nodiscard]] Parameterization::ParameterizationDiagnostics
EvaluatePackedChartQuality(const ChartMesh &mesh,
                           const std::span<const glm::vec2> packedUvs) {
  const std::optional<HalfedgeMesh::Mesh> halfedge =
      MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(mesh.Positions,
                                                       mesh.Triangles);
  if (!halfedge) {
    return {};
  }
  return Parameterization::EvaluateParameterizationDiagnostics(*halfedge,
                                                               packedUvs);
}

void RecordSeams(const AtlasTopology &topology,
                 const std::span<const std::uint32_t> faceChart,
                 const std::span<const std::uint32_t> faceProposal,
                 UvAtlasResult &result) {
  for (std::size_t e = 0u; e < topology.Edges.size(); ++e) {
    const SourceEdgeFaces &edge = topology.Edges[e];
    if (edge.Faces.size() == 1u) {
      const std::uint32_t face = edge.Faces.front();
      result.SeamCuts.push_back(UvAtlasSeamCutRecord{
          .SourceVertexA = edge.A,
          .SourceVertexB = edge.B,
          .SourceFaceA = face,
          .SourceFaceB = kInvalidIndex,
          .ChartA = faceChart[face],
          .ChartB = kInvalidIndex,
          .Boundary = true,
          .Reason = UvAtlasSeamReason::MeshBoundary,
      });
      continue;
    }

    for (std::size_t i = 0u; i < edge.Faces.size(); ++i) {
      for (std::size_t j = i + 1u; j < edge.Faces.size(); ++j) {
        const std::uint32_t faceA = edge.Faces[i];
        const std::uint32_t faceB = edge.Faces[j];
        const std::uint32_t chartA = faceChart[faceA];
        const std::uint32_t chartB = faceChart[faceB];
        if (chartA == chartB) {
          continue;
        }
        UvAtlasSeamReason reason = UvAtlasSeamReason::ChartBoundary;
        if (topology.FaceRegionComponent[faceA] !=
            topology.FaceRegionComponent[faceB]) {
          reason = UvAtlasSeamReason::RegionBoundary;
        } else if (topology.EdgeTopologyCut[e] != 0u) {
          reason = UvAtlasSeamReason::TopologyCut;
        } else if (!faceProposal.empty() &&
                   faceProposal[faceA] == faceProposal[faceB]) {
          reason = UvAtlasSeamReason::RefinementSplit;
        }
        result.SeamCuts.push_back(UvAtlasSeamCutRecord{
            .SourceVertexA = edge.A,
            .SourceVertexB = edge.B,
            .SourceFaceA = faceA,
            .SourceFaceB = faceB,
            .ChartA = chartA,
            .ChartB = chartB,
            .Boundary = false,
            .Reason = reason,
        });
      }
    }
  }
  result.Diagnostics.SeamCutCount = static_cast<std::uint32_t>(std::count_if(
      result.SeamCuts.begin(), result.SeamCuts.end(),
      [](const UvAtlasSeamCutRecord &seam) { return !seam.Boundary; }));
  result.Diagnostics.BoundarySeamCount =
      static_cast<std::uint32_t>(std::count_if(
          result.SeamCuts.begin(), result.SeamCuts.end(),
          [](const UvAtlasSeamCutRecord &seam) { return seam.Boundary; }));
}

void CopyTopologyDiagnostics(const AtlasTopology &topology,
                             UvAtlasDiagnostics &diagnostics) {
  diagnostics.RegionLabelCount = topology.RegionLabelCount;
  diagnostics.RegionComponentCount = topology.RegionComponentCount;
  diagnostics.ConnectedComponentCount = topology.ConnectedComponentCount;
  diagnostics.NonManifoldEdgeCount = topology.NonManifoldEdgeCount;
  diagnostics.InconsistentOrientationEdgeCount =
      topology.InconsistentOrientationEdgeCount;
}

// Map OutputMesh faces back to source corners. The output triangle must be
// a rotation of its source face (same winding) over the same source
// vertices; every source face must be covered exactly once.
[[nodiscard]] bool GatherCornerUvs(const UvAtlasInput &input,
                                   const UvAtlasResult &result,
                                   std::vector<glm::vec2> &corners,
                                   std::vector<std::uint32_t> &faceCharts,
                                   std::string &detail) {
  const std::size_t faceCount = input.Faces.size();
  const MeshSoup::IndexedMesh &mesh = result.OutputMesh;
  const auto uvs = mesh.VertexProperties().Get<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName);
  if (!uvs || uvs.Vector().size() < mesh.VertexCount()) {
    detail = "output mesh has no complete texcoord property";
    return false;
  }
  if (result.SourceVertexForOutputVertex.size() != mesh.VertexCount() ||
      result.SourceFaceForOutputFace.size() != mesh.FaceCount() ||
      result.OutputFaceChart.size() != mesh.FaceCount()) {
    detail = "output cross-reference sizes do not match the output mesh";
    return false;
  }
  if (mesh.FaceCount() != faceCount) {
    detail = "output face count (" + std::to_string(mesh.FaceCount()) +
             ") does not equal the source face count (" +
             std::to_string(faceCount) + ")";
    return false;
  }
  corners.assign(3u * faceCount, glm::vec2{0.0f});
  faceCharts.assign(faceCount, kInvalidIndex);
  std::vector<std::uint8_t> seen(faceCount, 0u);
  for (std::size_t o = 0u; o < faceCount; ++o) {
    const std::uint32_t source = result.SourceFaceForOutputFace[o];
    if (source >= faceCount || seen[source] != 0u) {
      detail = "output face " + std::to_string(o) +
               " does not map to a distinct source face";
      return false;
    }
    seen[source] = 1u;
    const MeshSoup::PolygonFace &outputFace = mesh.Faces()[o];
    const MeshSoup::PolygonFace &sourceFace = input.Faces[source];
    if (outputFace.Indices.size() != 3u || sourceFace.Indices.size() != 3u) {
      detail = "non-triangle output face " + std::to_string(o);
      return false;
    }
    std::array<std::uint32_t, 3u> sourceOfCorner{};
    for (std::size_t k = 0u; k < 3u; ++k) {
      const std::uint32_t vertex = outputFace.Indices[k];
      if (vertex >= mesh.VertexCount()) {
        detail = "output face " + std::to_string(o) + " index out of range";
        return false;
      }
      sourceOfCorner[k] = result.SourceVertexForOutputVertex[vertex];
    }
    bool matched = false;
    for (std::size_t shift = 0u; shift < 3u && !matched; ++shift) {
      matched = true;
      for (std::size_t k = 0u; k < 3u; ++k) {
        matched = matched &&
                  sourceOfCorner[(k + shift) % 3u] == sourceFace.Indices[k];
      }
      if (matched) {
        for (std::size_t k = 0u; k < 3u; ++k) {
          corners[3u * source + k] =
              uvs[outputFace.Indices[(k + shift) % 3u]];
        }
      }
    }
    if (!matched) {
      detail = "output face " + std::to_string(o) +
               " corners do not match source face " + std::to_string(source);
      return false;
    }
    faceCharts[source] = result.OutputFaceChart[o];
  }
  return true;
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

void CopyVertexProperties(const UvAtlasInput &input,
                          const UvAtlasOptions &options,
                          UvAtlasResult &result) {
  if (!input.HasVertexProperties || !options.CopySourceVertexProperties) {
    return;
  }
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

void AttachDiagnostics(UvAtlasResult &result, const UvAtlasInput &input,
                       const UvAtlasOptions &options,
                       const std::string_view backendName) {
  result.Diagnostics.Status = result.Status;
  result.Diagnostics.Provenance = result.Provenance;
  result.Diagnostics.RequestedMethod = options.Method;
  result.Diagnostics.RequestedDistortion = options.Distortion;
  result.Diagnostics.RequestedMaxConformalDistortion =
      options.MaxConformalDistortion;
  result.Diagnostics.RequestedMaxAreaDistortion = options.MaxAreaDistortion;
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

// Authored UVs are per source vertex, so every edge-connected component is
// one UV-continuous chart; each face also gets its region component.
[[nodiscard]] UvAtlasResult
BuildPreservedResult(const UvAtlasInput &input,
                     const UvAtlasDiagnostics &validation,
                     const AtlasTopology &topology) {
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
  result.Diagnostics.ChartCount = topology.ConnectedComponentCount;
  CopyTopologyDiagnostics(topology, result.Diagnostics);

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
  result.SourceCornerUvs.reserve(input.Faces.size() * 3u);
  for (std::size_t faceIndex = 0; faceIndex < input.Faces.size(); ++faceIndex) {
    const MeshSoup::PolygonFace &face = input.Faces[faceIndex];
    (void)result.OutputMesh.AddTriangle(face.Indices[0], face.Indices[1],
                                        face.Indices[2]);
    result.SourceFaceForOutputFace.push_back(
        static_cast<std::uint32_t>(faceIndex));
    result.OutputFaceChart.push_back(
        topology.FaceConnectedComponent[faceIndex]);
    for (const MeshSoup::Index index : face.Indices) {
      result.SourceCornerUvs.push_back(input.AuthoredTexcoords[index]);
    }
  }
  result.SourceFaceChart = topology.FaceConnectedComponent;
  result.SourceFaceRegionComponent = topology.FaceRegionComponent;

  FinalizeUvBounds(result.Diagnostics, input.AuthoredTexcoords);
  result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
  result.Diagnostics.OutputVertexCount = result.OutputMesh.VertexCount();
  result.Diagnostics.OutputFaceCount = result.OutputMesh.FaceCount();
  return result;
}

// ---------------------------------------------------------------------------
// Native region-constrained generator.
// ---------------------------------------------------------------------------

[[nodiscard]] UvAtlasResult
GenerateWithFastStaged(const UvAtlasInput &input,
                       const UvAtlasOptions &options) {
  constexpr std::string_view kBackend = "fast-staged";
  UvAtlasDiagnostics validation{};
  if (std::optional<UvAtlasResult> failure =
          PreflightGeneration(input, options, kBackend, validation)) {
    return std::move(*failure);
  }

  const std::size_t faceCount = input.Faces.size();
  const std::uint32_t resolution =
      options.Resolution == 0u ? kDefaultResolution : options.Resolution;
  const AtlasTopology topology = AnalyzeTopology(input);
  const std::vector<FaceFrame> frames = BuildFaceFrames(input);
  const auto failWith = [&](const UvAtlasStatus status,
                            const std::string &detail) {
    UvAtlasResult failure = MakeFailure(input, status, kBackend, detail);
    CopyTopologyDiagnostics(topology, failure.Diagnostics);
    return failure;
  };

  std::vector<std::vector<std::uint32_t>> proposals =
      GrowCharts(topology, frames, kChartNormalConeCos);
  MergeSmallProposals(topology, frames, proposals);
  if (proposals.size() > options.MaxCharts) {
    return failWith(UvAtlasStatus::ResourceLimitExceeded,
                    std::to_string(proposals.size()) +
                        " disjoint chart proposals exceed MaxCharts (" +
                        std::to_string(options.MaxCharts) + ")");
  }

  struct Candidate {
    std::vector<std::uint32_t> Faces{};
    std::uint32_t Proposal{0};
  };
  std::vector<Candidate> pending;
  pending.reserve(proposals.size());
  for (std::size_t i = 0u; i < proposals.size(); ++i) {
    pending.push_back(Candidate{.Faces = std::move(proposals[i]),
                                .Proposal = static_cast<std::uint32_t>(i)});
  }
  const auto initialChartCount = static_cast<std::uint32_t>(pending.size());

  UvChartSolveParams solveParams{};
  solveParams.Objective = options.Distortion;
  solveParams.MaxIterations = options.MaxIterations;
  solveParams.CancelFlag = options.CancelFlag;

  std::vector<AcceptedChart> accepted;
  std::vector<std::uint32_t> scratch(faceCount, kInvalidIndex);
  std::uint32_t rejectedCount = 0u;
  std::uint32_t splitCount = 0u;
  struct {
    std::uint32_t Topology{0};
    std::uint32_t Solve{0};
    std::uint32_t Distortion{0};
    std::uint32_t Overlap{0};
  } rejections{};
  for (std::size_t cursor = 0u; cursor < pending.size(); ++cursor) {
    if (CancelRequested(options)) {
      return failWith(UvAtlasStatus::Cancelled,
                      "cancel requested during chart solves");
    }
    Candidate candidate = std::move(pending[cursor]);
    ChartMesh mesh =
        BuildChartMesh(input, topology, std::move(candidate.Faces), scratch);
    UvChartSolveResult solve =
        SolveUvChart(mesh.Positions, mesh.Triangles, solveParams);
    if (solve.Status == UvChartSolveStatus::Cancelled) {
      return failWith(UvAtlasStatus::Cancelled,
                      "cancel requested during chart optimization");
    }
    ChartGate gate{};
    if (solve.Succeeded()) {
      gate = EvaluateChartGate(mesh, solve.Uvs, options);
    } else {
      gate.Rejection = solve.Status == UvChartSolveStatus::NotDisk
                           ? ChartRejection::Topology
                           : ChartRejection::Solve;
      gate.Reason = std::string{"solve "} + ToString(solve.Status);
    }
    if (gate.Accepted) {
      accepted.push_back(AcceptedChart{
          .Mesh = std::move(mesh),
          .Uvs = std::move(solve.Uvs),
          .Backend = std::move(solve.Backend),
          .Proposal = candidate.Proposal,
          .Iterations = solve.Iterations,
          .Converged = solve.Converged,
          .MaxConformal = gate.MaxConformal,
          .MaxArea = gate.MaxArea,
      });
      continue;
    }

    ++rejectedCount;
    switch (gate.Rejection) {
    case ChartRejection::Topology:
      ++rejections.Topology;
      break;
    case ChartRejection::Solve:
      ++rejections.Solve;
      break;
    case ChartRejection::Distortion:
      ++rejections.Distortion;
      break;
    case ChartRejection::Overlap:
    case ChartRejection::None:
      ++rejections.Overlap;
      break;
    }
    if (mesh.Faces.size() <= 1u) {
      // An exact single triangle has unit distortion; reaching this means
      // the triangle cannot be represented at all.
      return failWith(UvAtlasStatus::BackendFailed,
                      "single-triangle chart for face " +
                          std::to_string(mesh.Faces.front()) +
                          " was rejected: " + gate.Reason);
    }
    std::array<std::vector<std::uint32_t>, 2u> pieces =
        SplitChart(topology, frames, mesh.Faces, scratch);
    ++splitCount;
    const std::size_t queued = pending.size() - cursor - 1u;
    if (accepted.size() + queued + pieces.size() > options.MaxCharts) {
      UvAtlasResult failure = failWith(
          UvAtlasStatus::ResourceLimitExceeded,
          "refining rejected charts (last: " + gate.Reason +
              ") would exceed MaxCharts (" + std::to_string(options.MaxCharts) +
              ")");
      failure.Diagnostics.RejectedChartCount = rejectedCount;
      failure.Diagnostics.RefinementSplitCount = splitCount;
      return failure;
    }
    for (std::vector<std::uint32_t> &piece : pieces) {
      pending.push_back(
          Candidate{.Faces = std::move(piece), .Proposal = candidate.Proposal});
    }
  }

  std::sort(accepted.begin(), accepted.end(),
            [](const AcceptedChart &a, const AcceptedChart &b) {
              return a.Mesh.Faces.front() < b.Mesh.Faces.front();
            });
  for (AcceptedChart &chart : accepted) {
    AlignChart(chart, options.RotateChartsToAxis);
  }
  const PackOutcome pack = PackCharts(accepted, options, resolution);
  if (!pack.Packed) {
    return failWith(options.TexelsPerUnit > 0.0f
                        ? UvAtlasStatus::ResourceLimitExceeded
                        : UvAtlasStatus::UnderResolved,
                    pack.Detail);
  }

  UvAtlasResult result{};
  result.Status = UvAtlasStatus::Success;
  result.Provenance = UvAtlasProvenance::Generated;
  result.Diagnostics = validation;
  result.Diagnostics.Status = UvAtlasStatus::Success;
  result.Diagnostics.Provenance = UvAtlasProvenance::Generated;
  result.Diagnostics.ActualMethod = UvAtlasMethod::FastStaged;
  result.Diagnostics.ActualDistortion = options.Distortion;
  result.Diagnostics.BackendName = std::string{kBackend};
  result.Diagnostics.BackendDetail =
      std::string{"region-constrained normal-cone chart growth, "} +
      ToString(options.Distortion) +
      " chart objective with validated split refinement, common-density "
      "shelf packing";
  result.Diagnostics.ChartCount = static_cast<std::uint32_t>(accepted.size());
  result.Diagnostics.AtlasWidth = resolution;
  result.Diagnostics.AtlasHeight = resolution;
  result.Diagnostics.AtlasCount = 1u;
  result.Diagnostics.TexelsPerUnit =
      static_cast<float>(pack.Scale * static_cast<double>(resolution));
  CopyTopologyDiagnostics(topology, result.Diagnostics);
  result.Diagnostics.InitialChartCount = initialChartCount;
  result.Diagnostics.RejectedChartCount = rejectedCount;
  result.Diagnostics.RefinementSplitCount = splitCount;
  result.Diagnostics.RejectedTopologyChartCount = rejections.Topology;
  result.Diagnostics.RejectedSolveChartCount = rejections.Solve;
  result.Diagnostics.RejectedDistortionChartCount = rejections.Distortion;
  result.Diagnostics.RejectedOverlapChartCount = rejections.Overlap;

  result.SourceVertexForOutputVertex.reserve(faceCount * 3u);
  result.SourceFaceForOutputFace.reserve(faceCount);
  result.OutputFaceChart.reserve(faceCount);
  result.Charts.reserve(accepted.size());
  result.SourceCornerUvs.assign(3u * faceCount, glm::vec2{0.0f});
  result.SourceFaceChart.assign(faceCount, kInvalidIndex);
  result.SourceFaceRegionComponent = topology.FaceRegionComponent;
  std::vector<std::uint32_t> faceProposal(faceCount, kInvalidIndex);
  std::vector<glm::vec2> outputUvs;
  outputUvs.reserve(faceCount * 3u);

  for (std::size_t chartId = 0u; chartId < accepted.size(); ++chartId) {
    const AcceptedChart &chart = accepted[chartId];
    const ChartPlacement &placement = pack.Placements[chartId];
    const auto outputVertexStart =
        static_cast<std::uint32_t>(result.OutputMesh.VertexCount());
    const auto outputFaceStart =
        static_cast<std::uint32_t>(result.SourceFaceForOutputFace.size());
    glm::vec2 uvMin{std::numeric_limits<float>::max()};
    glm::vec2 uvMax{std::numeric_limits<float>::lowest()};
    std::vector<glm::vec2> packedUvs;
    packedUvs.reserve(chart.Uvs.size());
    for (std::size_t local = 0u; local < chart.Mesh.SourceVertices.size();
         ++local) {
      const std::uint32_t sourceVertex = chart.Mesh.SourceVertices[local];
      (void)result.OutputMesh.AddVertex(input.Positions[sourceVertex]);
      result.SourceVertexForOutputVertex.push_back(sourceVertex);
      const glm::vec2 uv =
          PlaceUv(chart, placement, pack.Scale, chart.Uvs[local]);
      packedUvs.push_back(uv);
      outputUvs.push_back(uv);
      uvMin = glm::min(uvMin, uv);
      uvMax = glm::max(uvMax, uv);
    }

    for (std::size_t chartFace = 0u; chartFace < chart.Mesh.Faces.size();
         ++chartFace) {
      const std::uint32_t sourceFace = chart.Mesh.Faces[chartFace];
      std::array<std::uint32_t, 3u> outputFace{};
      for (std::size_t corner = 0u; corner < 3u; ++corner) {
        const std::uint32_t local = chart.Mesh.Triangles[3u * chartFace + corner];
        outputFace[corner] = outputVertexStart + local;
        result.SourceCornerUvs[3u * sourceFace + corner] = packedUvs[local];
      }
      (void)result.OutputMesh.AddTriangle(outputFace[0u], outputFace[1u],
                                          outputFace[2u]);
      result.SourceFaceForOutputFace.push_back(sourceFace);
      result.OutputFaceChart.push_back(static_cast<std::uint32_t>(chartId));
      result.SourceFaceChart[sourceFace] = static_cast<std::uint32_t>(chartId);
      faceProposal[sourceFace] = chart.Proposal;
    }

    const std::uint32_t firstFace = chart.Mesh.Faces.front();
    const bool singleTriangle = chart.Mesh.Faces.size() == 1u;
    result.Diagnostics.SingleTriangleChartCount += singleTriangle ? 1u : 0u;
    result.Diagnostics.OptimizationIterationCount += chart.Iterations;
    result.Diagnostics.UnconvergedChartCount += chart.Converged ? 0u : 1u;
    result.Charts.push_back(UvAtlasChartRecord{
        .ChartId = static_cast<std::uint32_t>(chartId),
        .SourceFaceStart = firstFace,
        .SourceFaceCount = static_cast<std::uint32_t>(chart.Mesh.Faces.size()),
        .OutputFaceStart = outputFaceStart,
        .OutputFaceCount = static_cast<std::uint32_t>(
            result.SourceFaceForOutputFace.size() - outputFaceStart),
        .OutputVertexStart = outputVertexStart,
        .OutputVertexCount = static_cast<std::uint32_t>(
            result.OutputMesh.VertexCount() - outputVertexStart),
        .UvMin = uvMin,
        .UvMax = uvMax,
        .ParameterizationBackend = chart.Backend,
        .Quality = EvaluatePackedChartQuality(chart.Mesh, packedUvs),
        .RegionLabel = topology.FaceLabel[firstFace],
        .RegionComponent = topology.FaceRegionComponent[firstFace],
        .Objective = options.Distortion,
        .OptimizationIterations = chart.Iterations,
        .OptimizationConverged = chart.Converged,
        .MaxConformalDistortion = chart.MaxConformal,
        .MaxAreaDistortion = chart.MaxArea,
    });
  }

  CopyVertexProperties(input, options, result);
  auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
  for (std::size_t i = 0; i < outputUvs.size(); ++i) {
    texcoords.Vector()[i] = outputUvs[i];
  }

  RecordSeams(topology, result.SourceFaceChart, faceProposal, result);
  FinalizeUvBounds(result.Diagnostics, outputUvs);
  result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
  AttachDiagnostics(result, input, options, kBackend);
  return result;
}

// ---------------------------------------------------------------------------
// xatlas generator with region materials.
// ---------------------------------------------------------------------------

[[nodiscard]] const char *XAtlasChartTypeName(const xatlas::ChartType type) {
  switch (type) {
  case xatlas::ChartType::Planar:
    return "xatlas_planar";
  case xatlas::ChartType::Ortho:
    return "xatlas_ortho";
  case xatlas::ChartType::LSCM:
    return "xatlas_lscm";
  case xatlas::ChartType::Piecewise:
    return "xatlas_piecewise";
  case xatlas::ChartType::Invalid:
    return "xatlas_invalid";
  }
  return "xatlas";
}

// Per-chart records for xatlas output, mirroring the native chart table.
// Faces with no chart are left to the validator's coverage check.
void RecordXAtlasCharts(const UvAtlasInput &input, const xatlas::Mesh &output,
                        const AtlasTopology &topology,
                        const std::span<const glm::vec2> outputUvs,
                        UvAtlasResult &result) {
  const std::size_t chartCount = output.chartCount;
  std::vector<std::vector<std::uint32_t>> chartFaces(chartCount);
  for (std::size_t o = 0u; o < result.OutputFaceChart.size(); ++o) {
    const std::uint32_t chart = result.OutputFaceChart[o];
    if (chart < chartCount) {
      chartFaces[chart].push_back(static_cast<std::uint32_t>(o));
    }
  }
  const auto &outputFaces = result.OutputMesh.Faces();
  for (std::size_t chart = 0u; chart < chartCount; ++chart) {
    const std::vector<std::uint32_t> &faces = chartFaces[chart];
    if (faces.empty()) {
      continue;
    }
    ChartMesh local{};
    std::unordered_map<std::uint32_t, std::uint32_t> localOfOutput;
    std::vector<glm::vec2> localUvs;
    glm::vec2 uvMin{std::numeric_limits<float>::max()};
    glm::vec2 uvMax{std::numeric_limits<float>::lowest()};
    std::uint32_t firstVertex = kInvalidIndex;
    std::uint32_t lastVertex = 0u;
    for (const std::uint32_t o : faces) {
      local.Faces.push_back(result.SourceFaceForOutputFace[o]);
      for (const MeshSoup::Index vertex : outputFaces[o].Indices) {
        const auto [it, inserted] = localOfOutput.try_emplace(
            vertex, static_cast<std::uint32_t>(local.Positions.size()));
        if (inserted) {
          local.Positions.push_back(
              input.Positions[result.SourceVertexForOutputVertex[vertex]]);
          localUvs.push_back(outputUvs[vertex]);
          uvMin = glm::min(uvMin, outputUvs[vertex]);
          uvMax = glm::max(uvMax, outputUvs[vertex]);
          firstVertex = std::min(firstVertex, vertex);
          lastVertex = std::max(lastVertex, vertex);
        }
        local.Triangles.push_back(it->second);
      }
    }
    const std::uint32_t sourceFace = local.Faces.front();
    result.Charts.push_back(UvAtlasChartRecord{
        .ChartId = static_cast<std::uint32_t>(chart),
        .SourceFaceStart = sourceFace,
        .SourceFaceCount = static_cast<std::uint32_t>(faces.size()),
        .OutputFaceStart = faces.front(),
        .OutputFaceCount = static_cast<std::uint32_t>(faces.size()),
        .OutputVertexStart = firstVertex,
        .OutputVertexCount = lastVertex + 1u - firstVertex,
        .UvMin = uvMin,
        .UvMax = uvMax,
        .ParameterizationBackend =
            XAtlasChartTypeName(output.chartArray[chart].type),
        .Quality = EvaluatePackedChartQuality(local, localUvs),
        .RegionLabel = sourceFace < topology.FaceLabel.size()
                           ? topology.FaceLabel[sourceFace]
                           : 0u,
        .RegionComponent = sourceFace < topology.FaceRegionComponent.size()
                               ? topology.FaceRegionComponent[sourceFace]
                               : 0u,
        .Objective = UvAtlasDistortion::Angle,
    });
  }
}

[[nodiscard]] UvAtlasResult GenerateWithXAtlas(const UvAtlasInput &input,
                                               const UvAtlasOptions &options) {
  constexpr std::string_view kBackend = "xatlas";
  UvAtlasDiagnostics validation{};
  if (std::optional<UvAtlasResult> failure =
          PreflightGeneration(input, options, kBackend, validation)) {
    return std::move(*failure);
  }
  if (options.Distortion != UvAtlasDistortion::Angle) {
    return MakeFailure(input, UvAtlasStatus::ObjectiveUnsupported, kBackend,
                       std::string{"xatlas parameterizes charts with its own "
                                   "LSCM/orthographic solver and supports "
                                   "only the angle objective, not '"} +
                           ToString(options.Distortion) + "'");
  }

  std::vector<std::uint32_t> indices;
  if (!ExtractTriangleIndices(input, indices)) {
    return MakeFailure(input, UvAtlasStatus::BackendRejectedInput, kBackend,
                       "failed to extract triangle indices");
  }

  // Materials are cut components: region components further separated so
  // no material holds both sides of a non-manifold or mis-wound edge.
  // xatlas welds coincident vertices, so vertex duplication alone cannot
  // express those cuts; distinct materials can.
  const AtlasTopology topology = AnalyzeTopology(input);
  const std::vector<std::vector<std::uint32_t>> cutComponents =
      GrowCharts(topology, BuildFaceFrames(input), -2.0);
  std::vector<std::uint32_t> materials(input.Faces.size(), 0u);
  for (std::size_t component = 0u; component < cutComponents.size();
       ++component) {
    for (const std::uint32_t face : cutComponents[component]) {
      materials[face] = static_cast<std::uint32_t>(component);
    }
  }

  xatlas::Atlas *atlas = xatlas::Create();
  if (atlas == nullptr) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, kBackend,
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
  meshDecl.faceMaterialData = materials.data();

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
    return MakeFailure(input, UvAtlasStatus::BackendRejectedInput, kBackend,
                       xatlas::StringForEnum(addMeshError));
  }

  xatlas::ChartOptions chartOptions{};
  chartOptions.useInputMeshUvs = useAuthoredHints;
  // Accepted atlases require positive orientation.
  chartOptions.fixWinding = true;

  xatlas::PackOptions packOptions{};
  packOptions.padding = options.Padding;
  packOptions.texelsPerUnit = options.TexelsPerUnit;
  packOptions.resolution = options.Resolution;
  packOptions.bilinear = options.Bilinear;
  packOptions.blockAlign = options.BlockAlign;
  packOptions.bruteForce = options.BruteForcePacking;
  packOptions.rotateChartsToAxis = options.RotateChartsToAxis;
  // xatlas "rotates" packed charts by swapping u and v, a reflection that
  // mirrors the chart and flips every triangle; accepted atlases require
  // positive orientation, so that transpose is never enabled.
  packOptions.rotateCharts = false;

  xatlas::ComputeCharts(atlas, chartOptions);
  xatlas::PackCharts(atlas, packOptions);
  // One UV square holds one atlas; shrink an estimated density until the
  // charts fit one sub-atlas instead of overlapping sub-atlases.
  for (int attempt = 0; atlas->atlasCount > 1u &&
                        options.TexelsPerUnit <= 0.0f && attempt < 12;
       ++attempt) {
    packOptions.texelsPerUnit = atlas->texelsPerUnit * 0.85f;
    xatlas::PackCharts(atlas, packOptions);
  }
  if (atlas->atlasCount > 1u) {
    return MakeFailure(input, UvAtlasStatus::ResourceLimitExceeded, kBackend,
                       "xatlas needed " + std::to_string(atlas->atlasCount) +
                           " sub-atlases; one UV square cannot hold them");
  }
  if (atlas->meshCount == 0u || atlas->meshes == nullptr) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, kBackend,
                       "xatlas produced no output meshes");
  }
  if (atlas->width == 0u || atlas->height == 0u) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, kBackend,
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
  result.Diagnostics.ActualDistortion = UvAtlasDistortion::Angle;
  result.Diagnostics.BackendName = std::string{kBackend};
  result.Diagnostics.BackendDetail =
      "jpcy/xatlas f700c7790aaa030e794b52ba7791a05c085faf0c with region "
      "cut-component materials";
  result.Diagnostics.ChartCount = atlas->chartCount;
  result.Diagnostics.AtlasWidth = atlas->width;
  result.Diagnostics.AtlasHeight = atlas->height;
  result.Diagnostics.AtlasCount = std::max(atlas->atlasCount, 1u);
  result.Diagnostics.TexelsPerUnit = atlas->texelsPerUnit;
  CopyTopologyDiagnostics(topology, result.Diagnostics);

  std::vector<glm::vec2> outputUvs;
  outputUvs.reserve(output.vertexCount);
  result.SourceVertexForOutputVertex.reserve(output.vertexCount);
  for (std::uint32_t vertexIndex = 0; vertexIndex < output.vertexCount;
       ++vertexIndex) {
    const xatlas::Vertex &vertex = output.vertexArray[vertexIndex];
    if (vertex.xref >= input.Positions.size()) {
      return MakeFailure(input, UvAtlasStatus::BackendFailed, kBackend,
                         "xatlas output vertex xref out of range");
    }

    (void)result.OutputMesh.AddVertex(input.Positions[vertex.xref]);
    result.SourceVertexForOutputVertex.push_back(vertex.xref);
    outputUvs.emplace_back(vertex.uv[0] / static_cast<float>(atlas->width),
                           vertex.uv[1] / static_cast<float>(atlas->height));
  }

  CopyVertexProperties(input, options, result);
  auto texcoords = result.OutputMesh.GetOrAddVertexProperty<glm::vec2>(
      MeshUtils::kVertexTexcoordPropertyName, glm::vec2{0.0f});
  for (std::size_t i = 0; i < outputUvs.size(); ++i) {
    texcoords.Vector()[i] = outputUvs[i];
  }

  if (output.indexCount % 3u != 0u) {
    return MakeFailure(input, UvAtlasStatus::BackendFailed, kBackend,
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
      return MakeFailure(input, UvAtlasStatus::BackendFailed, kBackend,
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

  RecordXAtlasCharts(input, output, topology, outputUvs, result);

  std::string gatherDetail;
  std::vector<std::uint32_t> faceCharts;
  if (GatherCornerUvs(input, result, result.SourceCornerUvs, faceCharts,
                      gatherDetail)) {
    result.SourceFaceChart = std::move(faceCharts);
    RecordSeams(topology, result.SourceFaceChart, {}, result);
  } else {
    // Validation reports the mismatch; keep no partial corner table.
    result.SourceCornerUvs.clear();
  }
  result.SourceFaceRegionComponent = topology.FaceRegionComponent;

  FinalizeUvBounds(result.Diagnostics, outputUvs);
  result.Diagnostics.Quality = EvaluateQuality(result.OutputMesh);
  AttachDiagnostics(result, input, options, kBackend);
  return result;
}

// Objective identity and independent acceptance for a generated result.
void FinalizeGenerated(const UvAtlasInput &input, const UvAtlasOptions &options,
                       UvAtlasResult &result) {
  if (result.Status != UvAtlasStatus::Success ||
      result.Provenance != UvAtlasProvenance::Generated) {
    return;
  }
  const auto reject = [&result](const UvAtlasStatus status,
                                const std::string &detail) {
    result.Status = status;
    result.Diagnostics.Status = status;
    result.Diagnostics.BackendDetail = detail;
  };
  if (result.Diagnostics.ActualDistortion != options.Distortion) {
    reject(UvAtlasStatus::ObjectiveUnsupported,
           std::string{"backend '"} + result.Diagnostics.BackendName +
               "' solved the '" + ToString(result.Diagnostics.ActualDistortion) +
               "' objective, not the requested '" +
               ToString(options.Distortion) + "'");
    return;
  }
  result.Diagnostics.Validation = ValidateUvAtlasResult(input, result, options);
  if (!result.Diagnostics.Validation.Passed()) {
    reject(result.Diagnostics.Validation.Status,
           "atlas rejected by independent validation: " +
               result.Diagnostics.Validation.Detail);
    return;
  }
  // Publish the correspondence the validator just accepted, so caller
  // backends cannot hand out incomplete or inconsistent source-face maps.
  std::vector<glm::vec2> corners;
  std::vector<std::uint32_t> faceCharts;
  std::string detail;
  if (GatherCornerUvs(input, result, corners, faceCharts, detail)) {
    result.SourceCornerUvs = std::move(corners);
    result.SourceFaceChart = std::move(faceCharts);
  }
  if (result.SourceFaceRegionComponent.size() != input.Faces.size()) {
    result.SourceFaceRegionComponent =
        AnalyzeTopology(input).FaceRegionComponent;
  }
}

[[nodiscard]] bool IsInputRejection(const UvAtlasStatus status) noexcept {
  switch (status) {
  case UvAtlasStatus::EmptyInput:
  case UvAtlasStatus::MissingPositions:
  case UvAtlasStatus::MissingFaces:
  case UvAtlasStatus::NonTriangleFace:
  case UvAtlasStatus::OutOfRangeIndex:
  case UvAtlasStatus::NonFinitePosition:
  case UvAtlasStatus::DegenerateInput:
  case UvAtlasStatus::InvalidOptions:
  case UvAtlasStatus::InvalidRegionLabels:
  case UvAtlasStatus::Cancelled:
    return true;
  default:
    return false;
  }
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
  case UvAtlasStatus::InvalidOptions:
    return "invalid_options";
  case UvAtlasStatus::InvalidRegionLabels:
    return "invalid_region_labels";
  case UvAtlasStatus::ObjectiveUnsupported:
    return "objective_unsupported";
  case UvAtlasStatus::ResourceLimitExceeded:
    return "resource_limit_exceeded";
  case UvAtlasStatus::QualityLimitNotMet:
    return "quality_limit_not_met";
  case UvAtlasStatus::UnderResolved:
    return "under_resolved";
  case UvAtlasStatus::ValidationFailed:
    return "validation_failed";
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

const char *ToString(const UvAtlasDistortion distortion) noexcept {
  switch (distortion) {
  case UvAtlasDistortion::None:
    return "none";
  case UvAtlasDistortion::Angle:
    return "angle";
  case UvAtlasDistortion::Area:
    return "area";
  case UvAtlasDistortion::Both:
    return "both";
  }
  return "unknown";
}

const char *ToString(const UvAtlasSeamReason reason) noexcept {
  switch (reason) {
  case UvAtlasSeamReason::MeshBoundary:
    return "mesh_boundary";
  case UvAtlasSeamReason::RegionBoundary:
    return "region_boundary";
  case UvAtlasSeamReason::TopologyCut:
    return "topology_cut";
  case UvAtlasSeamReason::ChartBoundary:
    return "chart_boundary";
  case UvAtlasSeamReason::RefinementSplit:
    return "refinement_split";
  }
  return "unknown";
}

std::optional<UvAtlasMethod>
ParseUvAtlasMethod(const std::string_view token) noexcept {
  for (const UvAtlasMethod method :
       {UvAtlasMethod::None, UvAtlasMethod::Authored, UvAtlasMethod::XAtlas,
        UvAtlasMethod::FastStaged}) {
    if (token == ToString(method)) {
      return method;
    }
  }
  return std::nullopt;
}

std::optional<UvAtlasDistortion>
ParseUvAtlasDistortion(const std::string_view token) noexcept {
  for (const UvAtlasDistortion distortion :
       {UvAtlasDistortion::None, UvAtlasDistortion::Angle,
        UvAtlasDistortion::Area, UvAtlasDistortion::Both}) {
    if (token == ToString(distortion)) {
      return distortion;
    }
  }
  return std::nullopt;
}

UvAtlasOptionsValidation ValidateUvAtlasOptions(const UvAtlasOptions &options) {
  const auto invalid = [](std::string detail) {
    return UvAtlasOptionsValidation{.Valid = false, .Detail = std::move(detail)};
  };
  if (options.Resolution > kMaxResolution) {
    return invalid("resolution must be at most " +
                   std::to_string(kMaxResolution));
  }
  const std::uint32_t resolution =
      options.Resolution == 0u ? kDefaultResolution : options.Resolution;
  if (options.Padding > kMaxPadding || 2u * options.Padding >= resolution) {
    return invalid("padding must be at most " + std::to_string(kMaxPadding) +
                   " texels and leave room inside the atlas");
  }
  if (!std::isfinite(options.TexelsPerUnit) || options.TexelsPerUnit < 0.0f) {
    return invalid("texels per unit must be finite and non-negative");
  }
  const auto validLimit = [](const double value) {
    return std::isfinite(value) && value >= 1.0 && value <= kMaxDistortionLimit;
  };
  if (!validLimit(options.MaxConformalDistortion) ||
      !validLimit(options.MaxAreaDistortion)) {
    return invalid("distortion limits must be finite and within [1, 1e6]");
  }
  if (options.MaxCharts == 0u || options.MaxCharts > kMaxChartBudget) {
    return invalid("MaxCharts must be within [1, " +
                   std::to_string(kMaxChartBudget) + "]");
  }
  if (options.MaxIterations > kMaxIterationBudget) {
    return invalid("MaxIterations must be at most " +
                   std::to_string(kMaxIterationBudget));
  }
  if (options.Distortion != UvAtlasDistortion::None &&
      options.Distortion != UvAtlasDistortion::Angle &&
      options.Distortion != UvAtlasDistortion::Area &&
      options.Distortion != UvAtlasDistortion::Both) {
    return invalid("unknown distortion objective");
  }
  if ((options.Distortion == UvAtlasDistortion::Area ||
       options.Distortion == UvAtlasDistortion::Both) &&
      options.MaxIterations == 0u) {
    return invalid(std::string{"the '"} + ToString(options.Distortion) +
                   "' objective needs at least one optimizer iteration");
  }
  return UvAtlasOptionsValidation{.Valid = true, .Detail = {}};
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
    if (IsDegenerateAtlasTriangle(input.Positions[face.Indices[0]],
                                  input.Positions[face.Indices[1]],
                                  input.Positions[face.Indices[2]])) {
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

UvAtlasValidationReport ValidateUvAtlasResult(const UvAtlasInput &input,
                                              const UvAtlasResult &result,
                                              const UvAtlasOptions &options) {
  UvAtlasValidationReport report{};
  std::vector<glm::vec2> corners;
  std::vector<std::uint32_t> faceCharts;
  std::string detail;
  if (!GatherCornerUvs(input, result, corners, faceCharts, detail)) {
    report.Evaluated = true;
    report.Status = UvAtlasStatus::ValidationFailed;
    report.FaceCount = input.Faces.size();
    report.Detail = "source correspondence: " + detail;
    return report;
  }
  if (!result.SourceCornerUvs.empty() && result.SourceCornerUvs != corners) {
    report.Evaluated = true;
    report.Status = UvAtlasStatus::ValidationFailed;
    report.FaceCount = input.Faces.size();
    report.Detail = "SourceCornerUvs disagree with the output mesh";
    return report;
  }
  return ValidateUvAtlasCorners(
      UvAtlasValidationInput{
          .Positions = input.Positions,
          .Faces = input.Faces,
          .FaceRegions = input.FaceRegions,
          .CornerUvs = corners,
          .FaceCharts = faceCharts,
      },
      UvAtlasValidationOptions{
          .MaxConformalDistortion = options.MaxConformalDistortion,
          .MaxAreaDistortion = options.MaxAreaDistortion,
          .AtlasWidth = result.Diagnostics.AtlasWidth,
          .AtlasHeight = result.Diagnostics.AtlasHeight,
      });
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
  if (CancelRequested(options)) {
    UvAtlasResult failure =
        MakeFailure(input, UvAtlasStatus::Cancelled, options.BackendName,
                    "cancel requested before atlas resolution");
    AttachDiagnostics(failure, input, options, options.BackendName);
    failure.Diagnostics.ActualMethod = UvAtlasMethod::None;
    return failure;
  }

  // Set when valid authored UVs were not preserved because they cannot honor
  // the supplied region labels; reported on the generated result.
  std::string declinedAuthored;
  if (options.PreserveValidAuthoredUvs && !options.ForceRegenerate) {
    const UvAtlasDiagnostics authoredValidation = ValidateAuthoredUvs(input);
    // Mismatched region labels fall through to the generation preflight,
    // which rejects them.
    const bool regionsUsable = input.FaceRegions.empty() ||
                               input.FaceRegions.size() == input.Faces.size();
    if (authoredValidation.Status == UvAtlasStatus::Success && regionsUsable) {
      const AtlasTopology topology = AnalyzeTopology(input);
      if (topology.RegionComponentCount == topology.ConnectedComponentCount) {
        UvAtlasResult preserved =
            BuildPreservedResult(input, authoredValidation, topology);
        AttachDiagnostics(preserved, input, options, "authored");
        return preserved;
      }
      declinedAuthored =
          "valid authored UVs not preserved: " +
          std::to_string(topology.ConnectedComponentCount) +
          " UV-continuous authored charts span " +
          std::to_string(topology.RegionComponentCount) +
          " region components; ";
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
  FinalizeGenerated(input, options, result);
  result.Diagnostics.BackendDetail.insert(0u, declinedAuthored);
  const bool fallbackRequested =
      result.Status != UvAtlasStatus::Success &&
      !IsInputRejection(result.Status) &&
      options.Method == UvAtlasMethod::FastStaged &&
      options.AllowXAtlasFallback &&
      std::string_view{backend->Name} != "xatlas";
  if (!fallbackRequested) {
    return result;
  }
  if (options.Distortion != UvAtlasDistortion::Angle) {
    // A fallback may not silently relax the requested objective.
    result.Diagnostics.FallbackReason =
        std::string{"xatlas fallback not attempted: it supports only the "
                    "angle objective, requested '"} +
        ToString(options.Distortion) + "'";
    return result;
  }

  UvAtlasBackend fallbackBackend = DefaultXAtlasBackend();
  UvAtlasResult fallback = fallbackBackend.Generate(input, options);
  AttachDiagnostics(fallback, input, options, fallbackBackend.Name);
  FinalizeGenerated(input, options, fallback);
  fallback.Diagnostics.BackendDetail.insert(0u, declinedAuthored);
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
} // namespace Geometry::UvAtlas
