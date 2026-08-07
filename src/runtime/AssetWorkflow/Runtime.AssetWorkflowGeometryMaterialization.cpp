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
import Geometry.HalfedgeMesh.Utils;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;
import Geometry.UvAtlas;

namespace Extrinsic::Runtime {
namespace {
constexpr const char *kPositionProperty = "v:point";
constexpr const char *kNormalProperty = "v:normal";
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

// BUG-137 — the atlas output carries one vertex per (chart, source vertex)
// pair. Pull its UVs back onto the *source* corners so the seam can be stored
// as a UV fact instead of being baked into the entity mesh's topology.
struct AtlasCornerTexcoords {
  // Three entries per source triangle, in that triangle's corner order.
  std::vector<glm::vec2> CornerUvs{};
  // One representative UV per source vertex; only meaningful when `HasSeam` is
  // false, where it is also the exact per-vertex answer.
  std::vector<glm::vec2> VertexUvs{};
  std::size_t UnmappedCornerCount{0};
  bool HasSeam{false};
};

[[nodiscard]] std::optional<AtlasCornerTexcoords>
GatherAtlasCornerTexcoords(const Geometry::UvAtlas::UvAtlasResult &atlas,
                           const Geometry::MeshSoup::IndexedMesh &sourceMesh) {
  const auto outputUvs =
      atlas.OutputMesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty);
  if (!outputUvs ||
      outputUvs.Vector().size() != atlas.OutputMesh.VertexCount()) {
    return std::nullopt;
  }

  const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces =
      sourceMesh.Faces();
  const std::span<const Geometry::MeshSoup::PolygonFace> outputFaces =
      atlas.OutputMesh.Faces();
  if (atlas.SourceFaceForOutputFace.size() != outputFaces.size()) {
    return std::nullopt;
  }

  AtlasCornerTexcoords corners{};
  corners.CornerUvs.assign(sourceFaces.size() * 3u, glm::vec2{0.0f});
  std::vector<std::uint8_t> cornerAssigned(sourceFaces.size() * 3u, 0u);

  for (std::size_t outputFace = 0u; outputFace < outputFaces.size();
       ++outputFace) {
    const std::uint32_t sourceFace = atlas.SourceFaceForOutputFace[outputFace];
    if (sourceFace >= sourceFaces.size()) {
      return std::nullopt;
    }

    const std::vector<std::uint32_t> &outputIndices =
        outputFaces[outputFace].Indices;
    const std::vector<std::uint32_t> &sourceIndices =
        sourceFaces[sourceFace].Indices;
    if (outputIndices.size() != 3u || sourceIndices.size() != 3u) {
      return std::nullopt;
    }

    for (std::size_t k = 0u; k < 3u; ++k) {
      const std::uint32_t outputVertex = outputIndices[k];
      if (outputVertex >= atlas.SourceVertexForOutputVertex.size()) {
        return std::nullopt;
      }
      const std::uint32_t sourceVertex =
          atlas.SourceVertexForOutputVertex[outputVertex];

      // Chart splitting preserves winding, so corner k normally maps to corner
      // k; the search only covers a backend that rotated the triangle.
      std::size_t slot = 3u;
      if (sourceIndices[k] == sourceVertex) {
        slot = k;
      } else {
        for (std::size_t candidate = 0u; candidate < 3u; ++candidate) {
          if (sourceIndices[candidate] == sourceVertex) {
            slot = candidate;
            break;
          }
        }
      }
      if (slot >= 3u) {
        return std::nullopt;
      }

      const std::size_t corner = sourceFace * 3u + slot;
      corners.CornerUvs[corner] = outputUvs.Vector()[outputVertex];
      cornerAssigned[corner] = 1u;
    }
  }

  corners.VertexUvs.assign(sourceMesh.VertexCount(), glm::vec2{0.0f});
  std::vector<std::uint8_t> vertexAssigned(sourceMesh.VertexCount(), 0u);
  for (std::size_t face = 0u; face < sourceFaces.size(); ++face) {
    const std::vector<std::uint32_t> &indices = sourceFaces[face].Indices;
    for (std::size_t k = 0u; k < indices.size() && k < 3u; ++k) {
      const std::size_t corner = face * 3u + k;
      if (cornerAssigned[corner] == 0u) {
        continue;
      }
      const std::uint32_t vertex = indices[k];
      if (vertex >= corners.VertexUvs.size()) {
        return std::nullopt;
      }
      if (vertexAssigned[vertex] == 0u) {
        corners.VertexUvs[vertex] = corners.CornerUvs[corner];
        vertexAssigned[vertex] = 1u;
      } else if (corners.VertexUvs[vertex] != corners.CornerUvs[corner]) {
        corners.HasSeam = true;
      }
    }
  }

  // A face the atlas dropped leaves its corners without a UV. Inherit a
  // sibling corner at the same vertex so the buffer stays finite and no new
  // (vertex, UV) pair — and therefore no phantom seam — is invented.
  for (std::size_t face = 0u; face < sourceFaces.size(); ++face) {
    const std::vector<std::uint32_t> &indices = sourceFaces[face].Indices;
    for (std::size_t k = 0u; k < indices.size() && k < 3u; ++k) {
      const std::size_t corner = face * 3u + k;
      if (cornerAssigned[corner] != 0u) {
        continue;
      }
      ++corners.UnmappedCornerCount;
      const std::uint32_t vertex = indices[k];
      if (vertex < corners.VertexUvs.size() && vertexAssigned[vertex] != 0u) {
        corners.CornerUvs[corner] = corners.VertexUvs[vertex];
      }
    }
  }

  return corners;
}

// Writes `h:texcoord` for every halfedge of `mesh`, whose faces and vertices
// are index-identical to `sourceMesh`. Boundary halfedges carry no corner of
// their own, so they repeat a UV already present at their target vertex rather
// than a default that would read as an extra seam.
[[nodiscard]] bool
PublishCornerTexcoords(Geometry::HalfedgeMesh::Mesh &mesh,
                       const Geometry::MeshSoup::IndexedMesh &sourceMesh,
                       const AtlasCornerTexcoords &corners) {
  const std::span<const Geometry::MeshSoup::PolygonFace> sourceFaces =
      sourceMesh.Faces();
  if (mesh.FacesSize() != sourceFaces.size() ||
      mesh.VerticesSize() != sourceMesh.VertexCount()) {
    return false;
  }

  std::vector<glm::vec2> values(mesh.HalfedgesSize(), glm::vec2{0.0f});
  std::vector<std::uint8_t> written(mesh.HalfedgesSize(), 0u);
  std::vector<glm::vec2> uvForVertex(mesh.VerticesSize(), glm::vec2{0.0f});
  std::vector<std::uint8_t> vertexHasUv(mesh.VerticesSize(), 0u);

  for (std::size_t faceIndex = 0u; faceIndex < mesh.FacesSize(); ++faceIndex) {
    const Geometry::FaceHandle face{
        static_cast<Geometry::PropertyIndex>(faceIndex)};
    if (mesh.IsDeleted(face)) {
      continue;
    }

    const std::vector<std::uint32_t> &indices = sourceFaces[faceIndex].Indices;
    for (const Geometry::HalfedgeHandle halfedge :
         mesh.HalfedgesAroundFace(face)) {
      const Geometry::VertexHandle vertex = mesh.ToVertex(halfedge);
      std::size_t slot = indices.size();
      for (std::size_t k = 0u; k < indices.size() && k < 3u; ++k) {
        if (indices[k] == vertex.Index) {
          slot = k;
          break;
        }
      }
      if (slot >= 3u) {
        return false;
      }

      const glm::vec2 uv = corners.CornerUvs[faceIndex * 3u + slot];
      values[halfedge.Index] = uv;
      written[halfedge.Index] = 1u;
      uvForVertex[vertex.Index] = uv;
      vertexHasUv[vertex.Index] = 1u;
    }
  }

  for (std::size_t index = 0u; index < values.size(); ++index) {
    if (written[index] != 0u) {
      continue;
    }
    const Geometry::HalfedgeHandle halfedge{
        static_cast<Geometry::PropertyIndex>(index)};
    if (mesh.IsDeleted(halfedge)) {
      continue;
    }
    const Geometry::VertexHandle vertex = mesh.ToVertex(halfedge);
    if (mesh.IsValid(vertex) && vertexHasUv[vertex.Index] != 0u) {
      values[index] = uvForVertex[vertex.Index];
    }
  }

  auto property = mesh.HalfedgeProperties().GetOrAdd<glm::vec2>(
      std::string{kCornerTexcoordProperty}, glm::vec2{0.0f});
  if (property.Vector().size() != values.size()) {
    return false;
  }
  property.Vector() = std::move(values);

  // Leave exactly one authority behind: a stale authored `v:texcoord` copied
  // from the payload would otherwise disagree with the corner UVs.
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
  // per-corner soup. Such a mesh no longer shares vertex indices with its
  // input, so corner-indexed data cannot be mapped onto it.
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

  std::vector<glm::vec3> normals =
      ResolveVertexNormals(meshPayload, positions.Vector(), faces.Vector());

  const Geometry::UvAtlas::UvAtlasInput input{
      .Positions = positions.Vector(),
      .Faces = source->Mesh.Faces(),
      .AuthoredTexcoords = AuthoredTexcoordSpan(meshPayload),
      .VertexProperties = Geometry::ConstPropertySet(meshPayload.Vertices),
      .HasVertexProperties = true,
  };
  const Geometry::UvAtlas::UvAtlasDiagnostics authoredValidation =
      Geometry::UvAtlas::ValidateAuthoredUvs(input);
  Geometry::UvAtlas::UvAtlasResult atlas = Geometry::UvAtlas::ResolveUvAtlas(
      input, MakeUvAtlasOptions(options.UvResolution),
      options.UvResolution.Backend);

  RuntimeMeshMaterializationDiagnostics diagnostics =
      MakeRuntimeDiagnostics(atlas, authoredValidation.Status,
                             positions.Vector().size(), faces.Vector().size());

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

  const bool atlasUsable =
      atlas.Succeeded() && diagnostics.ResolvedTexcoordsValid;
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

  if (atlas.SourceVertexForOutputVertex.size() !=
          atlas.OutputMesh.VertexCount() ||
      atlas.SourceFaceForOutputFace.size() != atlas.OutputMesh.FaceCount()) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  const std::optional<AtlasCornerTexcoords> corners =
      GatherAtlasCornerTexcoords(atlas, source->Mesh);
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
