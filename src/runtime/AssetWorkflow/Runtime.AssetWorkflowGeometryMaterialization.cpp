module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.AssetWorkflowGeometryMaterialization;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;
import Geometry.UvAtlas;

namespace Extrinsic::Runtime {
namespace {
constexpr const char *kPositionProperty = "v:point";
constexpr const char *kNormalProperty = "v:normal";
constexpr const char *kCornerNormalProperty = "h:normal";
constexpr const char *kTexcoordProperty = "v:texcoord";
constexpr const char *kCornerTexcoordProperty =
    Geometry::MeshUtils::kHalfedgeTexcoordPropertyName;
constexpr const char *kFaceVerticesProperty = "f:vertices";
constexpr const char *kSourceVertexProperty = "v:source_vertex";
constexpr const char *kSourceFaceProperty = "f:source_face";

[[nodiscard]] bool
ShouldCopyVertexProperty(const std::string_view name) noexcept {
  return name != kPositionProperty && name != kNormalProperty;
}

template <typename T>
void CopyVertexProperty(const Geometry::ConstPropertySet &source,
                        Geometry::PropertySet &target,
                        const std::string_view name,
                        const std::size_t vertexCount) {
  if (!ShouldCopyVertexProperty(name)) {
    return;
  }

  const auto property = source.Get<T>(name);
  if (!property || property.Vector().size() != vertexCount) {
    return;
  }

  auto targetProperty = target.GetOrAdd<T>(std::string{name}, T{});
  targetProperty.Vector() = property.Vector();
}

template <typename T>
void CopyVertexPropertyRemapped(
    const Geometry::ConstPropertySet &source, Geometry::PropertySet &target,
    const std::string_view name,
    const std::span<const std::uint32_t> sourceVertexForTargetVertex) {
  if (!ShouldCopyVertexProperty(name)) {
    return;
  }

  const auto property = source.Get<T>(name);
  if (!property) {
    return;
  }
  const auto &values = property.Vector();
  for (const std::uint32_t sourceIndex : sourceVertexForTargetVertex) {
    if (sourceIndex >= values.size()) {
      return;
    }
  }

  auto targetProperty = target.GetOrAdd<T>(std::string{name}, T{});
  auto &out = targetProperty.Vector();
  out.resize(sourceVertexForTargetVertex.size());
  for (std::size_t i = 0u; i < sourceVertexForTargetVertex.size(); ++i) {
    out[i] = values[sourceVertexForTargetVertex[i]];
  }
}

void CopySupportedVertexProperties(const Geometry::ConstPropertySet &source,
                                   Geometry::HalfedgeMesh::Mesh &mesh,
                                   const std::size_t vertexCount) {
  Geometry::PropertySet &target = mesh.VertexProperties();
  for (const std::string &name : source.Properties()) {
    CopyVertexProperty<glm::vec2>(source, target, name, vertexCount);
    CopyVertexProperty<glm::vec3>(source, target, name, vertexCount);
    CopyVertexProperty<glm::vec4>(source, target, name, vertexCount);
    CopyVertexProperty<float>(source, target, name, vertexCount);
    CopyVertexProperty<double>(source, target, name, vertexCount);
    CopyVertexProperty<std::uint32_t>(source, target, name, vertexCount);
    CopyVertexProperty<std::int32_t>(source, target, name, vertexCount);
    CopyVertexProperty<bool>(source, target, name, vertexCount);
  }
}

void CopySupportedVertexPropertiesRemapped(
    const Geometry::ConstPropertySet &source,
    Geometry::HalfedgeMesh::Mesh &mesh,
    const std::span<const std::uint32_t> sourceVertexForTargetVertex) {
  Geometry::PropertySet &target = mesh.VertexProperties();
  for (const std::string &name : source.Properties()) {
    CopyVertexPropertyRemapped<glm::vec2>(source, target, name,
                                          sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<glm::vec3>(source, target, name,
                                          sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<glm::vec4>(source, target, name,
                                          sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<float>(source, target, name,
                                      sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<double>(source, target, name,
                                       sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<std::uint32_t>(source, target, name,
                                              sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<std::int32_t>(source, target, name,
                                             sourceVertexForTargetVertex);
    CopyVertexPropertyRemapped<bool>(source, target, name,
                                     sourceVertexForTargetVertex);
  }
}

void WriteVertexNormalsRemapped(
    Geometry::HalfedgeMesh::Mesh &mesh, const std::vector<glm::vec3> &normals,
    const std::span<const std::uint32_t> sourceVertexForTargetVertex) {
  if (normals.empty()) {
    return;
  }
  if (mesh.VerticesSize() != sourceVertexForTargetVertex.size()) {
    return;
  }

  auto normalProperty = mesh.VertexProperties().GetOrAdd<glm::vec3>(
      std::string{kNormalProperty}, glm::vec3{0.0f});
  auto &out = normalProperty.Vector();
  out.resize(sourceVertexForTargetVertex.size());
  for (std::size_t i = 0u; i < sourceVertexForTargetVertex.size(); ++i) {
    const std::uint32_t sourceIndex = sourceVertexForTargetVertex[i];
    out[i] =
        sourceIndex < normals.size() ? normals[sourceIndex] : glm::vec3{0.0f};
  }
}

void WriteSourceVertexXrefs(
    Geometry::HalfedgeMesh::Mesh &mesh,
    const std::span<const std::uint32_t> sourceVertexForTargetVertex) {
  if (mesh.VerticesSize() != sourceVertexForTargetVertex.size()) {
    return;
  }

  auto property = mesh.VertexProperties().GetOrAdd<std::uint32_t>(
      std::string{kSourceVertexProperty}, 0u);
  auto &out = property.Vector();
  out.assign(sourceVertexForTargetVertex.begin(),
             sourceVertexForTargetVertex.end());
}

void WriteSourceFaceXrefs(
    Geometry::HalfedgeMesh::Mesh &mesh,
    const std::span<const std::uint32_t> sourceFaceForTargetFace) {
  if (mesh.FacesSize() != sourceFaceForTargetFace.size()) {
    return;
  }

  auto property = mesh.FaceProperties().GetOrAdd<std::uint32_t>(
      std::string{kSourceFaceProperty}, 0u);
  auto &out = property.Vector();
  out.assign(sourceFaceForTargetFace.begin(), sourceFaceForTargetFace.end());
}

[[nodiscard]] std::vector<glm::vec3> ComputeAreaWeightedVertexNormals(
    const std::vector<glm::vec3> &positions,
    const std::vector<std::vector<std::uint32_t>> &faces) {
  std::vector<glm::vec3> normals(positions.size(), glm::vec3{0.0f});

  for (const std::vector<std::uint32_t> &face : faces) {
    if (face.size() < 3u) {
      continue;
    }

    const std::uint32_t root = face[0];
    for (std::size_t i = 1u; i + 1u < face.size(); ++i) {
      const std::uint32_t a = root;
      const std::uint32_t b = face[i];
      const std::uint32_t c = face[i + 1u];
      if (a >= positions.size() || b >= positions.size() ||
          c >= positions.size()) {
        continue;
      }

      const glm::vec3 weightedNormal =
          glm::cross(positions[b] - positions[a], positions[c] - positions[a]);
      const float length = glm::length(weightedNormal);
      if (!std::isfinite(length) || length <= 1.0e-12f) {
        continue;
      }

      normals[a] += weightedNormal;
      normals[b] += weightedNormal;
      normals[c] += weightedNormal;
    }
  }

  for (glm::vec3 &normal : normals) {
    const float length = glm::length(normal);
    if (std::isfinite(length) && length > 1.0e-6f) {
      normal /= length;
    } else {
      normal = glm::vec3{0.0f};
    }
  }

  return normals;
}

[[nodiscard]] std::vector<glm::vec3>
ResolveVertexNormals(const Geometry::MeshIO::MeshIOResult &meshPayload,
                     const std::vector<glm::vec3> &positions,
                     const std::vector<std::vector<std::uint32_t>> &faces) {
  const auto explicitNormals =
      meshPayload.Vertices.Get<glm::vec3>(kNormalProperty);
  if (explicitNormals && explicitNormals.Vector().size() == positions.size()) {
    return explicitNormals.Vector();
  }

  return ComputeAreaWeightedVertexNormals(positions, faces);
}

[[nodiscard]] bool IsFinite(const glm::vec2 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFinite(const glm::vec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool AllFinite(const std::vector<glm::vec2> &values) noexcept {
  for (const glm::vec2 value : values) {
    if (!IsFinite(value)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool AllFinite(const std::vector<glm::vec3> &values) noexcept {
  for (const glm::vec3 value : values) {
    if (!IsFinite(value)) {
      return false;
    }
  }
  return true;
}

// BUG-137 — UVs may be owned by the corner domain now, so validity follows the
// canonical resolution order instead of assuming `v:texcoord`.
[[nodiscard]] bool HasValidTexcoords(const Geometry::HalfedgeMesh::Mesh &mesh) {
  switch (Geometry::MeshUtils::ResolveTexcoordDomain(mesh)) {
  case Geometry::MeshUtils::TexcoordDomain::Halfedge:
    return AllFinite(mesh.HalfedgeProperties()
                         .Get<glm::vec2>(kCornerTexcoordProperty)
                         .Vector());
  case Geometry::MeshUtils::TexcoordDomain::Vertex:
    return AllFinite(
        mesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty).Vector());
  case Geometry::MeshUtils::TexcoordDomain::None:
  default:
    return false;
  }
}

[[nodiscard]] bool
HasValidTexcoords(const Geometry::MeshSoup::IndexedMesh &mesh) {
  const auto texcoords =
      mesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty);
  if (!texcoords || texcoords.Vector().size() != mesh.VertexCount()) {
    return false;
  }
  for (const glm::vec2 texcoord : texcoords.Vector()) {
    if (!IsFinite(texcoord)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool
HasValidTexcoords(const Geometry::MeshIO::MeshIOResult &meshPayload) noexcept {
  const auto texcoords = meshPayload.Vertices.Get<glm::vec2>(kTexcoordProperty);
  if (!texcoords || texcoords.Vector().size() != meshPayload.Vertices.Size()) {
    return false;
  }
  for (const glm::vec2 texcoord : texcoords.Vector()) {
    if (!IsFinite(texcoord)) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool CanUseDisconnectedRenderableFallback(
    const Geometry::Mesh::Conversion::ToHalfedgeMeshResult
        &converted) noexcept {
  bool hasRenderableTopologyFailure = false;
  for (const Geometry::Mesh::Conversion::ConversionDiagnostic &diagnostic :
       converted.Diagnostics) {
    if (diagnostic.Severity != Geometry::MeshSoup::ValidationSeverity::Error) {
      continue;
    }

    if (diagnostic.Kind ==
        Geometry::Mesh::Conversion::ConversionDiagnosticKind::AddFaceFailed) {
      hasRenderableTopologyFailure = true;
      continue;
    }

    if (diagnostic.Kind != Geometry::Mesh::Conversion::
                               ConversionDiagnosticKind::ValidationDiagnostic) {
      return false;
    }

    if (diagnostic.ValidationKind ==
            Geometry::MeshSoup::ValidationDiagnosticKind::NonManifoldEdge ||
        diagnostic.ValidationKind ==
            Geometry::MeshSoup::ValidationDiagnosticKind::InconsistentWinding) {
      hasRenderableTopologyFailure = true;
      continue;
    }

    return false;
  }
  return hasRenderableTopologyFailure;
}

struct TriangulatedSourceMesh {
  Geometry::MeshSoup::IndexedMesh Mesh{};
  std::vector<std::uint32_t> OriginalFaceForTriangle{};
};

[[nodiscard]] Core::Expected<TriangulatedSourceMesh>
BuildTriangulatedSourceMesh(
    const std::vector<glm::vec3> &positions,
    const std::vector<std::vector<std::uint32_t>> &faces) {
  TriangulatedSourceMesh source{};
  for (const glm::vec3 position : positions) {
    if (!IsFinite(position)) {
      return Core::Err<TriangulatedSourceMesh>(
          Core::ErrorCode::AssetInvalidData);
    }
    static_cast<void>(source.Mesh.AddVertex(position));
  }

  for (std::size_t faceIndex = 0u; faceIndex < faces.size(); ++faceIndex) {
    const std::vector<std::uint32_t> &face = faces[faceIndex];
    if (face.size() < 3u) {
      return Core::Err<TriangulatedSourceMesh>(Core::ErrorCode::InvalidFormat);
    }
    for (const std::uint32_t index : face) {
      if (index >= positions.size()) {
        return Core::Err<TriangulatedSourceMesh>(Core::ErrorCode::OutOfRange);
      }
    }

    const std::uint32_t root = face[0];
    for (std::size_t i = 1u; i + 1u < face.size(); ++i) {
      static_cast<void>(source.Mesh.AddTriangle(root, face[i], face[i + 1u]));
      source.OriginalFaceForTriangle.push_back(
          static_cast<std::uint32_t>(faceIndex));
    }
  }

  return source;
}

[[nodiscard]] std::span<const glm::vec2> AuthoredTexcoordSpan(
    const Geometry::MeshIO::MeshIOResult &meshPayload) noexcept {
  const auto texcoords = meshPayload.Vertices.Get<glm::vec2>(kTexcoordProperty);
  if (!texcoords) {
    return {};
  }
  return texcoords.Vector();
}

[[nodiscard]] Geometry::UvAtlas::UvAtlasOptions
MakeUvAtlasOptions(const RuntimeMeshUvResolutionOptions &options) {
  Geometry::UvAtlas::UvAtlasOptions atlasOptions{};
  atlasOptions.PreserveValidAuthoredUvs = options.PreserveValidAuthoredUvs;
  atlasOptions.ForceRegenerate = options.ForceRegenerate;
  atlasOptions.CopySourceVertexProperties = true;
  atlasOptions.Resolution = options.Resolution;
  atlasOptions.Padding = options.Padding;
  atlasOptions.TexelsPerUnit = options.TexelsPerUnit;
  atlasOptions.Method = options.Method;
  atlasOptions.AllowXAtlasFallback = options.AllowXAtlasFallback;
  atlasOptions.BackendName =
      options.Method == Geometry::UvAtlas::UvAtlasMethod::XAtlas
          ? "xatlas"
          : "fast-staged";
  return atlasOptions;
}

[[nodiscard]] RuntimeMeshResolvedUvProvenance ToRuntimeProvenance(
    const Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept {
  switch (provenance) {
  case Geometry::UvAtlas::UvAtlasProvenance::AuthoredPreserved:
    return RuntimeMeshResolvedUvProvenance::AuthoredPreserved;
  case Geometry::UvAtlas::UvAtlasProvenance::Generated:
    return RuntimeMeshResolvedUvProvenance::GeneratedAtlas;
  case Geometry::UvAtlas::UvAtlasProvenance::None:
    return RuntimeMeshResolvedUvProvenance::None;
  }
  return RuntimeMeshResolvedUvProvenance::None;
}

[[nodiscard]] RuntimeMeshMaterializationDiagnostics
MakeRuntimeDiagnostics(const Geometry::UvAtlas::UvAtlasResult &atlas,
                       const Geometry::UvAtlas::UvAtlasStatus authoredStatus,
                       const std::size_t sourceVertexCount,
                       const std::size_t sourceFaceCount) {
  RuntimeMeshMaterializationDiagnostics diagnostics{};
  diagnostics.TexcoordProvenance = ToRuntimeProvenance(atlas.Provenance);
  diagnostics.UvAtlasStatus = atlas.Status;
  diagnostics.AuthoredTexcoordsValid =
      authoredStatus == Geometry::UvAtlas::UvAtlasStatus::Success;
  diagnostics.AuthoredTexcoordsRejected =
      authoredStatus != Geometry::UvAtlas::UvAtlasStatus::Success &&
      authoredStatus != Geometry::UvAtlas::UvAtlasStatus::MissingAuthoredUvs;
  // The atlas output is only the UV carrier now; whether the *entity* mesh has
  // usable UVs is decided after publication, so the resolved counts below are
  // filled in by the caller from the materialized mesh.
  diagnostics.ResolvedTexcoordsValid = HasValidTexcoords(atlas.OutputMesh);
  diagnostics.SourceVertexCount = sourceVertexCount;
  diagnostics.SourceFaceCount = sourceFaceCount;
  diagnostics.ChartCount = atlas.Diagnostics.ChartCount;
  diagnostics.AtlasWidth = atlas.Diagnostics.AtlasWidth;
  diagnostics.AtlasHeight = atlas.Diagnostics.AtlasHeight;
  diagnostics.AtlasBackendName = atlas.Diagnostics.BackendName;
  return diagnostics;
}

// BUG-137 — the atlas-to-corner mapping and its finalization are shared with
// the editor's UV regeneration command (BUG-147), so they live next to the
// corner walk in `Runtime.MeshSurfaceTopology` rather than here. `AtlasCornerTexcoords`
// is that shared record.
using AtlasCornerTexcoords = MeshCornerTexcoords;

[[nodiscard]] std::optional<AtlasCornerTexcoords>
GatherAtlasCornerTexcoords(const Geometry::UvAtlas::UvAtlasResult &atlas,
                           const Geometry::MeshSoup::IndexedMesh &sourceMesh) {
  const auto outputUvs =
      atlas.OutputMesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty);
  if (!outputUvs) {
    return std::nullopt;
  }

  AtlasCornerTexcoords corners{};
  if (!GatherSplitMeshCornerTexcoords(
          atlas.OutputMesh, outputUvs.Vector(), atlas.SourceFaceForOutputFace,
          atlas.SourceVertexForOutputVertex, sourceMesh.Faces(),
          sourceMesh.VertexCount(), corners)) {
    return std::nullopt;
  }
  return corners;
}

// BUG-137 — OBJ stores UVs per corner natively, so a payload that already
// carries authored corner UVs must be preserved rather than re-atlased. The
// payload corner index is flattened over the *polygon* face list, while the
// entity mesh is built from the fan triangulation of those polygons; this
// walks the same fan order `BuildTriangulatedSourceMesh` uses.
[[nodiscard]] std::optional<AtlasCornerTexcoords> GatherAuthoredCornerTexcoords(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    const std::vector<std::vector<std::uint32_t>> &polygons,
    const Geometry::MeshSoup::IndexedMesh &sourceMesh) {
  const auto authored =
      meshPayload.Halfedges.Get<glm::vec2>(kCornerTexcoordProperty);
  if (!authored) {
    return std::nullopt;
  }
  const std::vector<glm::vec2> &values = authored.Vector();

  std::size_t expectedCorners = 0u;
  for (const std::vector<std::uint32_t> &polygon : polygons) {
    expectedCorners += polygon.size();
  }
  if (values.size() != expectedCorners || values.empty()) {
    return std::nullopt;
  }
  if (!AllFinite(values)) {
    return std::nullopt;
  }

  const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces =
      sourceMesh.Faces();
  AtlasCornerTexcoords corners{};
  corners.CornerUvs.assign(sourceFaces.size() * 3u, glm::vec2{0.0f});
  std::vector<std::uint8_t> cornerAssigned(sourceFaces.size() * 3u, 0u);

  std::size_t base = 0u;
  std::size_t triangle = 0u;
  for (const std::vector<std::uint32_t> &polygon : polygons) {
    for (std::size_t i = 1u; i + 1u < polygon.size(); ++i) {
      if (triangle >= sourceFaces.size()) {
        return std::nullopt;
      }
      corners.CornerUvs[triangle * 3u + 0u] = values[base];
      corners.CornerUvs[triangle * 3u + 1u] = values[base + i];
      corners.CornerUvs[triangle * 3u + 2u] = values[base + i + 1u];
      cornerAssigned[triangle * 3u + 0u] = 1u;
      cornerAssigned[triangle * 3u + 1u] = 1u;
      cornerAssigned[triangle * 3u + 2u] = 1u;
      ++triangle;
    }
    base += polygon.size();
  }
  if (triangle != sourceFaces.size()) {
    return std::nullopt;
  }

  if (!FinalizeMeshCornerTexcoords(corners, cornerAssigned, sourceFaces,
                                  sourceMesh.VertexCount())) {
    return std::nullopt;
  }
  return corners;
}

// OBJ `vn` indices use the same flattened polygon-corner convention as UVs.
// Preserve them through the source fan triangulation without changing the
// source vertex table.
[[nodiscard]] std::optional<std::vector<glm::vec3>>
GatherAuthoredCornerNormals(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    const std::vector<std::vector<std::uint32_t>> &polygons,
    const Geometry::MeshSoup::IndexedMesh &sourceMesh) {
  const auto authored =
      meshPayload.Halfedges.Get<glm::vec3>(kCornerNormalProperty);
  if (!authored) {
    return std::nullopt;
  }
  const std::vector<glm::vec3> &values = authored.Vector();

  std::size_t expectedCorners = 0u;
  for (const std::vector<std::uint32_t> &polygon : polygons) {
    expectedCorners += polygon.size();
  }
  if (values.empty() || values.size() != expectedCorners ||
      !AllFinite(values)) {
    return std::nullopt;
  }

  std::vector<glm::vec3> corners;
  corners.reserve(sourceMesh.FaceCount() * 3u);
  std::size_t base = 0u;
  for (const std::vector<std::uint32_t> &polygon : polygons) {
    for (std::size_t i = 1u; i + 1u < polygon.size(); ++i) {
      corners.push_back(values[base]);
      corners.push_back(values[base + i]);
      corners.push_back(values[base + i + 1u]);
    }
    base += polygon.size();
  }
  if (corners.size() != sourceMesh.FaceCount() * 3u) {
    return std::nullopt;
  }
  return corners;
}

// Publishes the atlas's per-corner UVs onto `mesh` through the shared corner
// mapping, then leaves exactly one UV authority behind: a stale authored
// `v:texcoord` copied from the payload would otherwise disagree with — and win
// nothing against — the corner UVs.
[[nodiscard]] bool
PublishCornerTexcoords(Geometry::HalfedgeMesh::Mesh &mesh,
                       const Geometry::MeshSoup::IndexedMesh &sourceMesh,
                       const AtlasCornerTexcoords &corners) {
  if (!PublishMeshCornerTexcoords(mesh, sourceMesh.Faces(),
                                  sourceMesh.VertexCount(),
                                  corners.CornerUvs)) {
    return false;
  }

  if (auto stale = mesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty)) {
    mesh.VertexProperties().Remove(stale);
  }
  return true;
}

void PublishVertexTexcoords(Geometry::HalfedgeMesh::Mesh &mesh,
                            const std::vector<glm::vec2> &uvPerVertex) {
  if (mesh.VerticesSize() != uvPerVertex.size()) {
    return;
  }
  auto property = mesh.VertexProperties().GetOrAdd<glm::vec2>(
      std::string{kTexcoordProperty}, glm::vec2{0.0f});
  property.Vector() = uvPerVertex;
}

void PublishVertexNormals(Geometry::HalfedgeMesh::Mesh &mesh,
                          const std::vector<glm::vec3> &normalPerVertex) {
  if (mesh.VerticesSize() != normalPerVertex.size()) {
    return;
  }
  auto property = mesh.VertexProperties().GetOrAdd<glm::vec3>(
      std::string{kNormalProperty}, glm::vec3{0.0f, 0.0f, 1.0f});
  property.Vector() = normalPerVertex;
}

[[nodiscard]] std::size_t
CountGpuSplitVertices(const Geometry::HalfedgeMesh::Mesh &mesh) {
  const std::size_t uploaded =
      Geometry::MeshUtils::CountTexcoordSplitVertices(mesh);
  const std::size_t vertices = mesh.VertexCount();
  return uploaded > vertices ? uploaded - vertices : 0u;
}

[[nodiscard]] std::vector<std::uint32_t>
MakeIdentityXrefs(const std::size_t count) {
  std::vector<std::uint32_t> xrefs(count, 0u);
  for (std::size_t i = 0u; i < count; ++i) {
    xrefs[i] = static_cast<std::uint32_t>(i);
  }
  return xrefs;
}

[[nodiscard]] std::optional<Geometry::HalfedgeMesh::Mesh>
BuildDisconnectedRenderableMesh(
    const Geometry::MeshSoup::IndexedMesh &source,
    const std::span<const std::uint32_t> sourceVertexForOutputVertex,
    const std::span<const std::uint32_t> sourceFaceForOutputFace,
    const std::vector<glm::vec3> &normals) {
  if (source.VertexCount() == 0u || source.FaceCount() == 0u ||
      source.VertexCount() != sourceVertexForOutputVertex.size()) {
    return std::nullopt;
  }

  Geometry::HalfedgeMesh::Mesh mesh;
  std::vector<Geometry::VertexHandle> faceVertices;
  std::vector<std::uint32_t> outputVertexForTargetVertex;
  std::vector<std::uint32_t> sourceVertexForTargetVertex;

  for (const Geometry::MeshSoup::PolygonFace &face : source.Faces()) {
    if (face.Indices.size() < 3u) {
      return std::nullopt;
    }

    faceVertices.clear();
    faceVertices.reserve(face.Indices.size());
    for (const std::uint32_t index : face.Indices) {
      if (index >= source.VertexCount()) {
        return std::nullopt;
      }
      const Geometry::VertexHandle vertex =
          mesh.AddVertex(source.Position(index));
      if (!vertex.IsValid()) {
        return std::nullopt;
      }
      outputVertexForTargetVertex.push_back(index);
      sourceVertexForTargetVertex.push_back(sourceVertexForOutputVertex[index]);
      faceVertices.push_back(vertex);
    }

    if (!mesh.AddFace(faceVertices).has_value()) {
      return std::nullopt;
    }
  }

  CopySupportedVertexPropertiesRemapped(source.VertexProperties(), mesh,
                                        outputVertexForTargetVertex);
  WriteVertexNormalsRemapped(mesh, normals, sourceVertexForTargetVertex);
  WriteSourceVertexXrefs(mesh, sourceVertexForTargetVertex);
  WriteSourceFaceXrefs(mesh, sourceFaceForOutputFace);
  return mesh;
}

struct ResolvedHalfedgeMesh {
  Geometry::HalfedgeMesh::Mesh Mesh{};
  // True when the input could not form a halfedge mesh and was rebuilt as a
  // per-corner soup. Such a mesh no longer shares source vertex indices;
  // corner data must be published as vertex data in face-corner order.
  bool UsedDisconnectedFallback{false};
};

[[nodiscard]] Core::Expected<ResolvedHalfedgeMesh>
ConvertResolvedMeshToHalfedge(
    const Geometry::MeshSoup::IndexedMesh &resolved,
    const std::span<const std::uint32_t> sourceVertexForOutputVertex,
    const std::span<const std::uint32_t> originalFaceForOutputFace,
    const std::vector<glm::vec3> &normals,
    const bool allowDisconnectedRenderableFallback) {
  auto converted = Geometry::Mesh::Conversion::ToHalfedgeMesh(resolved);
  if (!converted.Succeeded()) {
    if (allowDisconnectedRenderableFallback &&
        CanUseDisconnectedRenderableFallback(converted)) {
      if (std::optional<Geometry::HalfedgeMesh::Mesh> fallback =
              BuildDisconnectedRenderableMesh(
                  resolved, sourceVertexForOutputVertex,
                  originalFaceForOutputFace, normals)) {
        return ResolvedHalfedgeMesh{
            .Mesh = std::move(*fallback),
            .UsedDisconnectedFallback = true,
        };
      }
    }
    return Core::Err<ResolvedHalfedgeMesh>(Core::ErrorCode::InvalidFormat);
  }

  CopySupportedVertexProperties(resolved.VertexProperties(), converted.Mesh,
                                resolved.VertexCount());
  WriteVertexNormalsRemapped(converted.Mesh, normals,
                             sourceVertexForOutputVertex);
  WriteSourceVertexXrefs(converted.Mesh, sourceVertexForOutputVertex);
  WriteSourceFaceXrefs(converted.Mesh, originalFaceForOutputFace);
  return ResolvedHalfedgeMesh{
      .Mesh = std::move(converted.Mesh),
      .UsedDisconnectedFallback = false,
  };
}

} // namespace

bool MeshPayloadHasValidVertexTexcoords(
    const Geometry::MeshIO::MeshIOResult &meshPayload) noexcept {
  return HasValidTexcoords(meshPayload);
}

Core::Expected<RuntimeMeshMaterializationResult>
BuildRuntimeHalfedgeMeshMaterialization(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    const RuntimeMeshMaterializationOptions options) {
  const auto positions = meshPayload.Vertices.Get<glm::vec3>(kPositionProperty);
  if (!positions || positions.Vector().empty()) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  const auto faces =
      meshPayload.Faces.Get<std::vector<std::uint32_t>>(kFaceVerticesProperty);
  if (!faces || faces.Vector().empty()) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  auto source = BuildTriangulatedSourceMesh(positions.Vector(), faces.Vector());
  if (!source.has_value()) {
    return Core::Err<RuntimeMeshMaterializationResult>(source.error());
  }

  const bool hasAuthoredCornerNormals =
      meshPayload.Halfedges.Exists(kCornerNormalProperty);
  const std::optional<std::vector<glm::vec3>> authoredCornerNormals =
      GatherAuthoredCornerNormals(meshPayload, faces.Vector(), source->Mesh);
  if (hasAuthoredCornerNormals && !authoredCornerNormals.has_value()) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  std::vector<glm::vec3> normals =
      ResolveVertexNormals(meshPayload, positions.Vector(), faces.Vector());

  // BUG-137 — authored per-corner UVs (OBJ's native encoding) are preserved
  // as-is. Re-atlasing them would discard the file's own parameterization, and
  // they cannot be validated as vertex-domain UVs because a seam vertex has
  // more than one.
  const bool preferAuthored = options.UvResolution.PreserveValidAuthoredUvs &&
                              !options.UvResolution.ForceRegenerate;
  const std::optional<AtlasCornerTexcoords> authoredCorners =
      preferAuthored ? GatherAuthoredCornerTexcoords(
                           meshPayload, faces.Vector(), source->Mesh)
                     : std::nullopt;

  Geometry::UvAtlas::UvAtlasResult atlas{};
  RuntimeMeshMaterializationDiagnostics diagnostics{};
  if (authoredCorners.has_value()) {
    diagnostics.TexcoordProvenance =
        RuntimeMeshResolvedUvProvenance::AuthoredPreserved;
    diagnostics.UvAtlasStatus = Geometry::UvAtlas::UvAtlasStatus::Success;
    diagnostics.AuthoredTexcoordsValid = true;
    diagnostics.ResolvedTexcoordsValid = true;
    diagnostics.SourceVertexCount = positions.Vector().size();
    diagnostics.SourceFaceCount = faces.Vector().size();
    diagnostics.AtlasBackendName = "authored-corners";
  } else {
    const Geometry::UvAtlas::UvAtlasInput input{
        .Positions = positions.Vector(),
        .Faces = source->Mesh.Faces(),
        .AuthoredTexcoords = AuthoredTexcoordSpan(meshPayload),
        .VertexProperties = Geometry::ConstPropertySet(meshPayload.Vertices),
        .HasVertexProperties = true,
    };
    const Geometry::UvAtlas::UvAtlasDiagnostics authoredValidation =
        Geometry::UvAtlas::ValidateAuthoredUvs(input);
    atlas = Geometry::UvAtlas::ResolveUvAtlas(
        input, MakeUvAtlasOptions(options.UvResolution),
        options.UvResolution.Backend);
    diagnostics =
        MakeRuntimeDiagnostics(atlas, authoredValidation.Status,
                               positions.Vector().size(), faces.Vector().size());
  }

  // BUG-137 — the entity mesh is always built from the source topology. A UV
  // seam is a UV fact, not a topology fact: publishing the atlas *output* mesh
  // here is what replaced a closed manifold import with a chart-split soup.
  // The seam now lives on the corner domain and is de-indexed at GPU upload.
  const std::vector<std::uint32_t> identityVertexXrefs =
      MakeIdentityXrefs(source->Mesh.VertexCount());
  Geometry::MeshSoup::IndexedMesh entityMesh = source->Mesh;
  static_cast<void>(Geometry::UvAtlas::CopySourceVertexPropertiesByXref(
      Geometry::ConstPropertySet(meshPayload.Vertices), identityVertexXrefs,
      entityMesh.VertexProperties()));

  const bool atlasUsable = authoredCorners.has_value() ||
                           (atlas.Succeeded() &&
                            diagnostics.ResolvedTexcoordsValid);
  if (!atlasUsable && options.UvResolution.FailurePolicy ==
                          RuntimeMeshUvFailurePolicy::Required) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  auto resolved = ConvertResolvedMeshToHalfedge(
      entityMesh, identityVertexXrefs, source->OriginalFaceForTriangle, normals,
      options.AllowDisconnectedRenderableFallback);
  if (!resolved.has_value()) {
    return Core::Err<RuntimeMeshMaterializationResult>(resolved.error());
  }

  Geometry::HalfedgeMesh::Mesh &mesh = resolved->Mesh;
  diagnostics.ResolvedVertexCount = mesh.VerticesSize();
  diagnostics.ResolvedFaceCount = mesh.FacesSize();

  if (authoredCornerNormals.has_value()) {
    if (resolved->UsedDisconnectedFallback) {
      if (mesh.VerticesSize() != authoredCornerNormals->size()) {
        return Core::Err<RuntimeMeshMaterializationResult>(
            Core::ErrorCode::AssetInvalidData);
      }
      PublishVertexNormals(mesh, *authoredCornerNormals);
    } else if (!PublishMeshCornerNormals(
                   mesh, source->Mesh.Faces(), source->Mesh.VertexCount(),
                   *authoredCornerNormals)) {
      return Core::Err<RuntimeMeshMaterializationResult>(
          Core::ErrorCode::AssetInvalidData);
    }
  }

  if (!atlasUsable) {
    // The atlas-failure path keeps the same topology guarantee; it simply has
    // no UVs to publish beyond whatever the payload authored.
    diagnostics.TexcoordProvenance = RuntimeMeshResolvedUvProvenance::None;
    diagnostics.ResolvedTexcoordsValid = HasValidTexcoords(mesh);
    return RuntimeMeshMaterializationResult{
        .Mesh = std::move(mesh),
        .Diagnostics = diagnostics,
    };
  }

  if (!authoredCorners.has_value() &&
      (atlas.SourceVertexForOutputVertex.size() !=
           atlas.OutputMesh.VertexCount() ||
       atlas.SourceFaceForOutputFace.size() != atlas.OutputMesh.FaceCount())) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  const std::optional<AtlasCornerTexcoords> corners =
      authoredCorners.has_value()
          ? authoredCorners
          : GatherAtlasCornerTexcoords(atlas, source->Mesh);
  if (corners.has_value()) {
    diagnostics.UnmappedCornerCount = corners->UnmappedCornerCount;
    if (resolved->UsedDisconnectedFallback) {
      // The fallback already emits one vertex per corner, so the corner UVs
      // *are* per-vertex UVs there and need no seam encoding.
      PublishVertexTexcoords(mesh, corners->CornerUvs);
    } else if (corners->HasSeam) {
      diagnostics.TexcoordsOnCornerDomain =
          PublishCornerTexcoords(mesh, source->Mesh, *corners);
      if (!diagnostics.TexcoordsOnCornerDomain) {
        PublishVertexTexcoords(mesh, corners->VertexUvs);
      }
    } else {
      PublishVertexTexcoords(mesh, corners->VertexUvs);
    }
  }

  diagnostics.ResolvedTexcoordsValid = HasValidTexcoords(mesh);
  diagnostics.GpuSplitVertexCount = diagnostics.TexcoordsOnCornerDomain
                                        ? CountGpuSplitVertices(mesh)
                                        : 0u;
  if (!diagnostics.ResolvedTexcoordsValid &&
      options.UvResolution.FailurePolicy ==
          RuntimeMeshUvFailurePolicy::Required) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  return RuntimeMeshMaterializationResult{
      .Mesh = std::move(mesh),
      .Diagnostics = diagnostics,
  };
}

Core::Expected<Geometry::HalfedgeMesh::Mesh>
BuildRuntimeHalfedgeMeshGeometryOnly(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    const RuntimeMeshGeometryOnlyOptions options) {
  const auto positions = meshPayload.Vertices.Get<glm::vec3>(kPositionProperty);
  if (!positions || positions.Vector().empty()) {
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(
        Core::ErrorCode::AssetInvalidData);
  }

  const auto faces =
      meshPayload.Faces.Get<std::vector<std::uint32_t>>(kFaceVerticesProperty);
  if (!faces || faces.Vector().empty()) {
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(
        Core::ErrorCode::AssetInvalidData);
  }

  auto source = BuildTriangulatedSourceMesh(positions.Vector(), faces.Vector());
  if (!source.has_value()) {
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(source.error());
  }

  const bool hasAuthoredCornerNormals =
      meshPayload.Halfedges.Exists(kCornerNormalProperty);
  const std::optional<std::vector<glm::vec3>> authoredCornerNormals =
      GatherAuthoredCornerNormals(meshPayload, faces.Vector(), source->Mesh);
  if (hasAuthoredCornerNormals && !authoredCornerNormals.has_value()) {
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(
        Core::ErrorCode::AssetInvalidData);
  }

  const std::vector<std::uint32_t> identityVertexXrefs =
      MakeIdentityXrefs(source->Mesh.VertexCount());

  static_cast<void>(Geometry::UvAtlas::CopySourceVertexPropertiesByXref(
      Geometry::ConstPropertySet(meshPayload.Vertices), identityVertexXrefs,
      source->Mesh.VertexProperties()));

  auto resolved = ConvertResolvedMeshToHalfedge(
      source->Mesh, identityVertexXrefs, source->OriginalFaceForTriangle,
      ResolveVertexNormals(meshPayload, positions.Vector(), faces.Vector()),
      options.AllowDisconnectedRenderableFallback);
  if (!resolved.has_value()) {
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(resolved.error());
  }
  if (authoredCornerNormals.has_value()) {
    if (resolved->UsedDisconnectedFallback) {
      if (resolved->Mesh.VerticesSize() != authoredCornerNormals->size()) {
        return Core::Err<Geometry::HalfedgeMesh::Mesh>(
            Core::ErrorCode::AssetInvalidData);
      }
      PublishVertexNormals(resolved->Mesh, *authoredCornerNormals);
    } else if (!PublishMeshCornerNormals(
                   resolved->Mesh, source->Mesh.Faces(),
                   source->Mesh.VertexCount(), *authoredCornerNormals)) {
      return Core::Err<Geometry::HalfedgeMesh::Mesh>(
          Core::ErrorCode::AssetInvalidData);
    }
  }
  return std::move(resolved->Mesh);
}

Core::Expected<Geometry::HalfedgeMesh::Mesh>
BuildRuntimeHalfedgeMeshWithNormals(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    const RuntimeMeshMaterializationOptions options) {
  auto result = BuildRuntimeHalfedgeMeshMaterialization(meshPayload, options);
  if (!result.has_value()) {
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(result.error());
  }
  return std::move(result->Mesh);
}
} // namespace Extrinsic::Runtime
