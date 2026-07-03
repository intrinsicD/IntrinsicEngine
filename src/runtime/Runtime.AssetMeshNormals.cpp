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

module Extrinsic.Runtime.AssetMeshNormals;

import Extrinsic.Core.Error;
import Geometry.Mesh.Conversion;
import Geometry.MeshSoup;
import Geometry.Properties;
import Geometry.UvAtlas;

namespace Extrinsic::Runtime {
namespace {
constexpr const char *kPositionProperty = "v:point";
constexpr const char *kNormalProperty = "v:normal";
constexpr const char *kTexcoordProperty = "v:texcoord";
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

[[nodiscard]] bool HasValidTexcoords(const Geometry::HalfedgeMesh::Mesh &mesh) {
  const auto texcoords =
      mesh.VertexProperties().Get<glm::vec2>(kTexcoordProperty);
  if (!texcoords || texcoords.Vector().size() != mesh.VerticesSize()) {
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
  diagnostics.ResolvedTexcoordsValid = HasValidTexcoords(atlas.OutputMesh);
  diagnostics.SourceVertexCount = sourceVertexCount;
  diagnostics.SourceFaceCount = sourceFaceCount;
  diagnostics.ResolvedVertexCount = atlas.OutputMesh.VertexCount();
  diagnostics.ResolvedFaceCount = atlas.OutputMesh.FaceCount();
  diagnostics.SeamSplitVertexCount =
      diagnostics.ResolvedVertexCount > sourceVertexCount
          ? diagnostics.ResolvedVertexCount - sourceVertexCount
          : 0u;
  diagnostics.ChartCount = atlas.Diagnostics.ChartCount;
  diagnostics.AtlasWidth = atlas.Diagnostics.AtlasWidth;
  diagnostics.AtlasHeight = atlas.Diagnostics.AtlasHeight;
  return diagnostics;
}

[[nodiscard]] std::vector<std::uint32_t> MapOutputFacesToOriginalFaces(
    const std::span<const std::uint32_t> sourceFaceForOutputFace,
    const std::span<const std::uint32_t> originalFaceForTriangle) {
  std::vector<std::uint32_t> originalFaces(sourceFaceForOutputFace.size(), 0u);
  for (std::size_t i = 0u; i < sourceFaceForOutputFace.size(); ++i) {
    const std::uint32_t triangleFace = sourceFaceForOutputFace[i];
    originalFaces[i] = triangleFace < originalFaceForTriangle.size()
                           ? originalFaceForTriangle[triangleFace]
                           : triangleFace;
  }
  return originalFaces;
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

[[nodiscard]] Core::Expected<Geometry::HalfedgeMesh::Mesh>
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
        return std::move(*fallback);
      }
    }
    return Core::Err<Geometry::HalfedgeMesh::Mesh>(
        Core::ErrorCode::InvalidFormat);
  }

  CopySupportedVertexProperties(resolved.VertexProperties(), converted.Mesh,
                                resolved.VertexCount());
  WriteVertexNormalsRemapped(converted.Mesh, normals,
                             sourceVertexForOutputVertex);
  WriteSourceVertexXrefs(converted.Mesh, sourceVertexForOutputVertex);
  WriteSourceFaceXrefs(converted.Mesh, originalFaceForOutputFace);
  return std::move(converted.Mesh);
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

  if (!atlas.Succeeded() || !diagnostics.ResolvedTexcoordsValid) {
    if (options.UvResolution.FailurePolicy ==
        RuntimeMeshUvFailurePolicy::Required) {
      return Core::Err<RuntimeMeshMaterializationResult>(
          Core::ErrorCode::AssetInvalidData);
    }

    Geometry::MeshSoup::IndexedMesh optionalOutput = source->Mesh;
    std::vector<std::uint32_t> identityVertexXrefs(optionalOutput.VertexCount(),
                                                   0u);
    for (std::size_t i = 0u; i < identityVertexXrefs.size(); ++i) {
      identityVertexXrefs[i] = static_cast<std::uint32_t>(i);
    }
    static_cast<void>(Geometry::UvAtlas::CopySourceVertexPropertiesByXref(
        Geometry::ConstPropertySet(meshPayload.Vertices), identityVertexXrefs,
        optionalOutput.VertexProperties()));

    auto mesh = ConvertResolvedMeshToHalfedge(
        optionalOutput, identityVertexXrefs, source->OriginalFaceForTriangle,
        normals, options.AllowDisconnectedRenderableFallback);
    if (!mesh.has_value()) {
      return Core::Err<RuntimeMeshMaterializationResult>(mesh.error());
    }

    diagnostics.TexcoordProvenance = RuntimeMeshResolvedUvProvenance::None;
    diagnostics.ResolvedTexcoordsValid = HasValidTexcoords(*mesh);
    diagnostics.ResolvedVertexCount = mesh->VerticesSize();
    diagnostics.ResolvedFaceCount = mesh->FacesSize();
    return RuntimeMeshMaterializationResult{
        .Mesh = std::move(*mesh),
        .Diagnostics = diagnostics,
    };
  }

  if (atlas.SourceVertexForOutputVertex.size() !=
          atlas.OutputMesh.VertexCount() ||
      atlas.SourceFaceForOutputFace.size() != atlas.OutputMesh.FaceCount()) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  const std::vector<std::uint32_t> originalFaceForOutputFace =
      MapOutputFacesToOriginalFaces(atlas.SourceFaceForOutputFace,
                                    source->OriginalFaceForTriangle);
  auto mesh = ConvertResolvedMeshToHalfedge(
      atlas.OutputMesh, atlas.SourceVertexForOutputVertex,
      originalFaceForOutputFace, normals,
      options.AllowDisconnectedRenderableFallback);
  if (!mesh.has_value()) {
    return Core::Err<RuntimeMeshMaterializationResult>(mesh.error());
  }
  diagnostics.ResolvedTexcoordsValid = HasValidTexcoords(*mesh);
  diagnostics.ResolvedVertexCount = mesh->VerticesSize();
  diagnostics.ResolvedFaceCount = mesh->FacesSize();
  diagnostics.SeamSplitVertexCount =
      diagnostics.ResolvedVertexCount > diagnostics.SourceVertexCount
          ? diagnostics.ResolvedVertexCount - diagnostics.SourceVertexCount
          : 0u;
  if (!diagnostics.ResolvedTexcoordsValid &&
      options.UvResolution.FailurePolicy ==
          RuntimeMeshUvFailurePolicy::Required) {
    return Core::Err<RuntimeMeshMaterializationResult>(
        Core::ErrorCode::AssetInvalidData);
  }

  return RuntimeMeshMaterializationResult{
      .Mesh = std::move(*mesh),
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

  std::vector<std::uint32_t> identityVertexXrefs(source->Mesh.VertexCount(),
                                                 0u);
  for (std::size_t i = 0u; i < identityVertexXrefs.size(); ++i) {
    identityVertexXrefs[i] = static_cast<std::uint32_t>(i);
  }

  static_cast<void>(Geometry::UvAtlas::CopySourceVertexPropertiesByXref(
      Geometry::ConstPropertySet(meshPayload.Vertices), identityVertexXrefs,
      source->Mesh.VertexProperties()));

  return ConvertResolvedMeshToHalfedge(
      source->Mesh, identityVertexXrefs, source->OriginalFaceForTriangle,
      ResolveVertexNormals(meshPayload, positions.Vector(), faces.Vector()),
      options.AllowDisconnectedRenderableFallback);
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
