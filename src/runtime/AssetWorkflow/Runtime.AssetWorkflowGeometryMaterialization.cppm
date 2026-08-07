module;

#include <cstddef>
#include <cstdint>
#include <string>

export module Extrinsic.Runtime.AssetWorkflowGeometryMaterialization;

import Extrinsic.Core.Error;
export import Geometry.HalfedgeMesh;
export import Geometry.HalfedgeMesh.IO;
export import Geometry.UvAtlas;

export namespace Extrinsic::Runtime {
enum class RuntimeMeshResolvedUvProvenance : std::uint8_t {
  None = 0,
  AuthoredPreserved,
  GeneratedAtlas,
};

enum class RuntimeMeshUvFailurePolicy : std::uint8_t {
  Required = 0,
  Optional,
};

struct RuntimeMeshUvResolutionOptions {
  bool PreserveValidAuthoredUvs{true};
  bool ForceRegenerate{false};
  RuntimeMeshUvFailurePolicy FailurePolicy{
      RuntimeMeshUvFailurePolicy::Required};
  std::uint32_t Resolution{1024u};
  std::uint32_t Padding{2u};
  float TexelsPerUnit{0.0f};
  Geometry::UvAtlas::UvAtlasMethod Method{
      Geometry::UvAtlas::UvAtlasMethod::FastStaged};
  bool AllowXAtlasFallback{true};
  const Geometry::UvAtlas::UvAtlasBackend *Backend{nullptr};
};

struct RuntimeMeshMaterializationDiagnostics {
  RuntimeMeshResolvedUvProvenance TexcoordProvenance{
      RuntimeMeshResolvedUvProvenance::None};
  Geometry::UvAtlas::UvAtlasStatus UvAtlasStatus{
      Geometry::UvAtlas::UvAtlasStatus::EmptyInput};
  bool AuthoredTexcoordsValid{false};
  bool AuthoredTexcoordsRejected{false};
  bool ResolvedTexcoordsValid{false};
  // BUG-137 — true when the resolved UVs live on the halfedge/corner domain
  // (`h:texcoord`) because the atlas produced at least one seam. False means
  // the mesh carries per-vertex `v:texcoord` exactly as it did before.
  bool TexcoordsOnCornerDomain{false};
  std::size_t SourceVertexCount{0};
  std::size_t SourceFaceCount{0};
  // Counts of the *materialized entity mesh*. These now equal the source
  // counts for a manifold input: a UV seam no longer splits the mesh.
  std::size_t ResolvedVertexCount{0};
  std::size_t ResolvedFaceCount{0};
  // BUG-137 — extra vertices an indexed GPU upload must emit to carry the UV
  // seams, i.e. the duplication that used to be applied to the ECS mesh and is
  // now confined to the vertex buffer. Zero when UVs are vertex-owned. This
  // replaces the old `SeamSplitVertexCount`, which measured the same seams in
  // the wrong place and was never surfaced anywhere.
  std::size_t GpuSplitVertexCount{0};
  // Source corners the atlas produced no UV for (dropped degenerate faces).
  // They inherit a sibling corner's UV rather than a hole, so a non-zero count
  // is a quality signal rather than a failure.
  std::size_t UnmappedCornerCount{0};
  std::uint32_t ChartCount{0};
  std::uint32_t AtlasWidth{0};
  std::uint32_t AtlasHeight{0};
  std::string AtlasBackendName{};
};

struct RuntimeMeshMaterializationResult {
  Geometry::HalfedgeMesh::Mesh Mesh{};
  RuntimeMeshMaterializationDiagnostics Diagnostics{};
};

struct RuntimeMeshMaterializationOptions {
  bool AllowDisconnectedRenderableFallback{false};
  RuntimeMeshUvResolutionOptions UvResolution{};
};

struct RuntimeMeshGeometryOnlyOptions {
  bool AllowDisconnectedRenderableFallback{false};
};

[[nodiscard]] bool MeshPayloadHasValidVertexTexcoords(
    const Geometry::MeshIO::MeshIOResult &meshPayload) noexcept;

[[nodiscard]] Core::Expected<RuntimeMeshMaterializationResult>
BuildRuntimeHalfedgeMeshMaterialization(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    RuntimeMeshMaterializationOptions options = {});

[[nodiscard]] Core::Expected<Geometry::HalfedgeMesh::Mesh>
BuildRuntimeHalfedgeMeshGeometryOnly(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    RuntimeMeshGeometryOnlyOptions options = {});

[[nodiscard]] Core::Expected<Geometry::HalfedgeMesh::Mesh>
BuildRuntimeHalfedgeMeshWithNormals(
    const Geometry::MeshIO::MeshIOResult &meshPayload,
    RuntimeMeshMaterializationOptions options = {});
} // namespace Extrinsic::Runtime
