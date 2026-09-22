// Mesh UV-atlas generation, region-constrained charting, independent
// acceptance validation and property transfer, with shared outcome enums.
module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.UvAtlas;

import Geometry.Properties;
import Geometry.MeshSoup;
import Geometry.Parameterization.Types;
export import Geometry.UvAtlas.Types;
export import Geometry.UvAtlas.Validation;

export namespace Geometry::UvAtlas {
struct UvAtlasInput {
  std::span<const glm::vec3> Positions{};
  std::span<const MeshSoup::PolygonFace> Faces{};
  std::span<const glm::vec2> AuthoredTexcoords{};
  ConstPropertySet VertexProperties{};
  bool HasVertexProperties{false};
  // Optional frozen region label per source face (empty = one region).
  // Generated charts never cross a label boundary or join two connected
  // components of one label.
  std::span<const std::uint32_t> FaceRegions{};
};

struct UvAtlasOptions {
  bool PreserveValidAuthoredUvs{true};
  bool ForceRegenerate{false};
  bool CopySourceVertexProperties{true};
  bool UseAuthoredUvsAsChartHints{true};
  bool CancelRequested{false};

  // Square atlas extent in texels (0 selects 1024) and per-chart gutter.
  std::uint32_t Resolution{1024u};
  std::uint32_t Padding{2u};
  // Fixed texel density; 0 fits the largest common density into the atlas.
  float TexelsPerUnit{0.0f};

  bool Bilinear{true};
  bool BlockAlign{false};
  bool BruteForcePacking{false};
  bool RotateChartsToAxis{true};
  // Allow 90-degree chart rotation while packing (native packer only; the
  // xatlas packer can only transpose, which would mirror charts).
  bool RotateCharts{true};

  UvAtlasMethod Method{UvAtlasMethod::FastStaged};
  bool AllowXAtlasFallback{true};
  std::string BackendName{"fast-staged"};

  // Per-chart objective. xatlas supports only Angle.
  UvAtlasDistortion Distortion{UvAtlasDistortion::Both};
  // Hard acceptance bounds on density-normalized per-face distortion:
  // singular-value ratio, and max(a, 1/a) of the area ratio a relative to
  // the atlas-wide density. Finite, in [1, 1e6].
  double MaxConformalDistortion{10.0};
  double MaxAreaDistortion{10.0};
  // Chart budget, including charts created by refinement splits.
  std::uint32_t MaxCharts{16384u};
  // Outer optimizer iterations per chart solve (Area and Both need >= 1).
  std::uint32_t MaxIterations{40u};
  // Optional caller-owned flag polled between chart solves and iterations.
  const std::atomic<bool> *CancelFlag{nullptr};
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

  UvAtlasDistortion RequestedDistortion{UvAtlasDistortion::Both};
  // Objective every chart was actually solved with; None when not generated.
  UvAtlasDistortion ActualDistortion{UvAtlasDistortion::None};
  double RequestedMaxConformalDistortion{0.0};
  double RequestedMaxAreaDistortion{0.0};

  std::uint32_t RegionLabelCount{0};
  std::uint32_t RegionComponentCount{0};
  std::uint32_t ConnectedComponentCount{0};
  std::uint32_t NonManifoldEdgeCount{0};
  std::uint32_t InconsistentOrientationEdgeCount{0};
  // Native charting: grown proposals, splits of rejected charts, rejected
  // chart solves/quality gates, exact single-triangle charts, and optimizer
  // iterations/unconverged charts summed over accepted charts.
  std::uint32_t InitialChartCount{0};
  std::uint32_t RefinementSplitCount{0};
  std::uint32_t RejectedChartCount{0};
  // Rejections by cause: not a disk, solver/initializer failure, distortion
  // bound, or self-overlap/float collapse.
  std::uint32_t RejectedTopologyChartCount{0};
  std::uint32_t RejectedSolveChartCount{0};
  std::uint32_t RejectedDistortionChartCount{0};
  std::uint32_t RejectedOverlapChartCount{0};
  std::uint32_t SingleTriangleChartCount{0};
  std::uint32_t OptimizationIterationCount{0};
  std::uint32_t UnconvergedChartCount{0};
  // Source faces that made generation impossible (e.g. degenerate).
  std::vector<std::uint32_t> InvalidFaces{};

  // Independent acceptance report for generated atlases.
  UvAtlasValidationReport Validation{};

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
  std::uint32_t RegionLabel{0};
  std::uint32_t RegionComponent{0};
  UvAtlasDistortion Objective{UvAtlasDistortion::None};
  std::uint32_t OptimizationIterations{0};
  bool OptimizationConverged{false};
  // Density-normalized maxima measured when the chart was accepted.
  double MaxConformalDistortion{0.0};
  double MaxAreaDistortion{0.0};
};

struct UvAtlasSeamCutRecord {
  std::uint32_t SourceVertexA{0};
  std::uint32_t SourceVertexB{0};
  std::uint32_t SourceFaceA{0};
  std::uint32_t SourceFaceB{0};
  std::uint32_t ChartA{0};
  std::uint32_t ChartB{0};
  bool Boundary{false};
  UvAtlasSeamReason Reason{UvAtlasSeamReason::MeshBoundary};
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
  // Three UVs per source face in source face and corner order; the exact
  // corner-domain publication of OutputMesh.
  std::vector<glm::vec2> SourceCornerUvs{};
  std::vector<std::uint32_t> SourceFaceChart{};
  std::vector<std::uint32_t> SourceFaceRegionComponent{};

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
[[nodiscard]] const char *ToString(UvAtlasDistortion distortion) noexcept;
[[nodiscard]] const char *ToString(UvAtlasSeamReason reason) noexcept;
[[nodiscard]] std::optional<UvAtlasMethod>
ParseUvAtlasMethod(std::string_view token) noexcept;
[[nodiscard]] std::optional<UvAtlasDistortion>
ParseUvAtlasDistortion(std::string_view token) noexcept;

struct UvAtlasOptionsValidation {
  bool Valid{false};
  std::string Detail{};
};

/// Canonical option preflight shared by generators, runtime config and UI.
[[nodiscard]] UvAtlasOptionsValidation
ValidateUvAtlasOptions(const UvAtlasOptions &options);

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

/// Independently re-derive per-source-corner UVs from OutputMesh and its
/// cross-references, then run ValidateUvAtlasCorners with the requested
/// bounds and atlas extent. ResolveUvAtlas applies this to every generated
/// result, including caller-supplied backends.
[[nodiscard]] UvAtlasValidationReport
ValidateUvAtlasResult(const UvAtlasInput &input, const UvAtlasResult &result,
                      const UvAtlasOptions &options);

[[nodiscard]] UvAtlasBackend DefaultXAtlasBackend() noexcept;
[[nodiscard]] UvAtlasBackend DefaultFastStagedBackend() noexcept;

[[nodiscard]] UvAtlasResult
ResolveUvAtlas(const UvAtlasInput &input, const UvAtlasOptions &options = {},
               const UvAtlasBackend *backend = nullptr);
} // namespace Geometry::UvAtlas
