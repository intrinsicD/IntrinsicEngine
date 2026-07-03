module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.UvAtlas;

import Geometry.Properties;
import Geometry.MeshSoup;
import Geometry.Parameterization.Diagnostics;

export namespace Geometry::UvAtlas {
enum class UvAtlasStatus : std::uint8_t {
  Success = 0,
  EmptyInput,
  MissingPositions,
  MissingFaces,
  MissingAuthoredUvs,
  NonTriangleFace,
  OutOfRangeIndex,
  NonFinitePosition,
  NonFiniteAuthoredUv,
  DegenerateInput,
  InvalidAuthoredUvs,
  BackendUnavailable,
  BackendRejectedInput,
  BackendFailed,
  Cancelled,
};

enum class UvAtlasProvenance : std::uint8_t {
  None = 0,
  AuthoredPreserved,
  Generated,
};

enum class UvAtlasMethod : std::uint8_t {
  None = 0,
  Authored,
  XAtlas,
  FastStaged,
};

struct UvAtlasInput {
  std::span<const glm::vec3> Positions{};
  std::span<const MeshSoup::PolygonFace> Faces{};
  std::span<const glm::vec2> AuthoredTexcoords{};
  ConstPropertySet VertexProperties{};
  bool HasVertexProperties{false};
};

struct UvAtlasOptions {
  bool PreserveValidAuthoredUvs{true};
  bool ForceRegenerate{false};
  bool CopySourceVertexProperties{true};
  bool UseAuthoredUvsAsChartHints{true};
  bool FixWinding{false};
  bool CancelRequested{false};

  std::uint32_t Resolution{1024u};
  std::uint32_t Padding{2u};
  std::uint32_t MaxChartSize{0u};
  float TexelsPerUnit{0.0f};

  bool Bilinear{true};
  bool BlockAlign{false};
  bool BruteForcePacking{false};
  bool RotateChartsToAxis{true};
  bool RotateCharts{true};

  UvAtlasMethod Method{UvAtlasMethod::FastStaged};
  bool AllowXAtlasFallback{true};
  std::string BackendName{"fast-staged"};
};

struct UvAtlasDiagnostics {
  UvAtlasStatus Status{UvAtlasStatus::EmptyInput};
  UvAtlasProvenance Provenance{UvAtlasProvenance::None};
  UvAtlasMethod RequestedMethod{UvAtlasMethod::FastStaged};
  UvAtlasMethod ActualMethod{UvAtlasMethod::None};
  std::string BackendName{};
  std::string BackendDetail{};
  bool UsedFallback{false};
  std::string FallbackReason{};

  std::size_t InputVertexCount{0};
  std::size_t InputFaceCount{0};
  std::size_t OutputVertexCount{0};
  std::size_t OutputFaceCount{0};

  std::size_t NonTriangleFaceCount{0};
  std::size_t OutOfRangeIndexCount{0};
  std::size_t NonFinitePositionCount{0};
  std::size_t NonFiniteAuthoredUvCount{0};
  std::size_t DegenerateFaceCount{0};
  std::size_t PreservedAuthoredUvCount{0};

  std::size_t CopiedVertexPropertyCount{0};
  std::size_t SkippedVertexPropertyCount{0};
  std::size_t PropertyXrefOutOfRangeCount{0};

  std::uint32_t ChartCount{0};
  std::uint32_t SeamCutCount{0};
  std::uint32_t BoundarySeamCount{0};
  std::uint32_t AtlasWidth{0};
  std::uint32_t AtlasHeight{0};
  std::uint32_t AtlasCount{0};
  float TexelsPerUnit{0.0f};

  glm::vec2 NormalizedUvMin{};
  glm::vec2 NormalizedUvMax{};

  Parameterization::ParameterizationDiagnostics Quality{};

  [[nodiscard]] bool Succeeded() const noexcept {
    return Status == UvAtlasStatus::Success;
  }
};

struct UvAtlasChartRecord {
  std::uint32_t ChartId{0};
  std::uint32_t SourceFaceStart{0};
  std::uint32_t SourceFaceCount{0};
  std::uint32_t OutputFaceStart{0};
  std::uint32_t OutputFaceCount{0};
  std::uint32_t OutputVertexStart{0};
  std::uint32_t OutputVertexCount{0};
  glm::vec2 UvMin{0.0f};
  glm::vec2 UvMax{0.0f};
  std::string ParameterizationBackend{};
  Parameterization::ParameterizationDiagnostics Quality{};
};

struct UvAtlasSeamCutRecord {
  std::uint32_t SourceVertexA{0};
  std::uint32_t SourceVertexB{0};
  std::uint32_t SourceFaceA{0};
  std::uint32_t SourceFaceB{0};
  std::uint32_t ChartA{0};
  std::uint32_t ChartB{0};
  bool Boundary{false};
};

struct UvAtlasResult {
  UvAtlasStatus Status{UvAtlasStatus::EmptyInput};
  UvAtlasProvenance Provenance{UvAtlasProvenance::None};
  MeshSoup::IndexedMesh OutputMesh{};
  std::vector<std::uint32_t> SourceVertexForOutputVertex{};
  std::vector<std::uint32_t> SourceFaceForOutputFace{};
  std::vector<std::uint32_t> OutputFaceChart{};
  std::vector<UvAtlasChartRecord> Charts{};
  std::vector<UvAtlasSeamCutRecord> SeamCuts{};
  UvAtlasDiagnostics Diagnostics{};

  [[nodiscard]] bool Succeeded() const noexcept {
    return Status == UvAtlasStatus::Success;
  }
};

struct UvAtlasBackend {
  std::string_view Name{};
  UvAtlasResult (*Generate)(const UvAtlasInput &input,
                            const UvAtlasOptions &options){nullptr};
};

struct VertexPropertyCopyDiagnostics {
  std::size_t CopiedPropertyCount{0};
  std::size_t SkippedPropertyCount{0};
  std::size_t XrefOutOfRangeCount{0};
};

[[nodiscard]] const char *ToString(UvAtlasStatus status) noexcept;
[[nodiscard]] const char *ToString(UvAtlasProvenance provenance) noexcept;
[[nodiscard]] const char *ToString(UvAtlasMethod method) noexcept;

[[nodiscard]] UvAtlasInput
BorrowInput(const MeshSoup::IndexedMesh &mesh,
            std::span<const glm::vec2> authoredTexcoords = {}) noexcept;

[[nodiscard]] UvAtlasDiagnostics
ValidateUvAtlasInput(const UvAtlasInput &input);
[[nodiscard]] UvAtlasDiagnostics ValidateAuthoredUvs(const UvAtlasInput &input);

[[nodiscard]] VertexPropertyCopyDiagnostics CopySourceVertexPropertiesByXref(
    const ConstPropertySet &source,
    std::span<const std::uint32_t> sourceVertexForOutputVertex,
    PropertySet &target);

[[nodiscard]] UvAtlasBackend DefaultXAtlasBackend() noexcept;
[[nodiscard]] UvAtlasBackend DefaultFastStagedBackend() noexcept;

[[nodiscard]] UvAtlasResult
ResolveUvAtlas(const UvAtlasInput &input, const UvAtlasOptions &options = {},
               const UvAtlasBackend *backend = nullptr);
} // namespace Geometry::UvAtlas
