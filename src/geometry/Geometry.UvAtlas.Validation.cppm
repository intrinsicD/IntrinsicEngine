// Independent acceptance checks for per-source-corner UV atlases: exact float
// orientation, global triangle overlap, bounds, regions, density-normalized
// distortion and texel resolution.
module;

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>

#include <glm/glm.hpp>

export module Geometry.UvAtlas.Validation;

import Geometry.MeshSoup;
import Geometry.UvAtlas.Types;

export namespace Geometry::UvAtlas {
inline constexpr std::uint32_t kInvalidUvAtlasIndex =
    std::numeric_limits<std::uint32_t>::max();

/// Exact sign (-1, 0, +1) of the doubled signed area of the float triangle
/// (a, b, c). Float products are exact in double and the six-term sum is
/// evaluated as a nonoverlapping expansion, so no filter band is involved.
[[nodiscard]] int ExactUvOrientation(glm::vec2 a, glm::vec2 b,
                                     glm::vec2 c) noexcept;

/// Scale-free degeneracy test shared by atlas input validation and the
/// validator: |(p1-p0) x (p2-p0)| <= 1e-12 * (longest edge)^2, or any
/// non-finite coordinate. Unit changes do not alter the classification.
[[nodiscard]] bool IsDegenerateAtlasTriangle(glm::vec3 p0, glm::vec3 p1,
                                             glm::vec3 p2) noexcept;

struct UvFaceDistortion {
  bool Valid{false};
  double SurfaceArea{0.0};
  // Signed UV area; Valid requires it to be positive.
  double UvArea{0.0};
  // Singular values of the surface-to-UV Jacobian, SigmaMax >= SigmaMin > 0.
  double SigmaMax{0.0};
  double SigmaMin{0.0};
};

/// Jacobian singular values of one triangle mapped from its own surface
/// frame to the given 2D coordinates (any uniform unit, e.g. texels).
[[nodiscard]] UvFaceDistortion
MeasureUvFaceDistortion(glm::dvec3 p0, glm::dvec3 p1, glm::dvec3 p2,
                        glm::dvec2 w0, glm::dvec2 w1, glm::dvec2 w2) noexcept;

struct UvTriangleOverlapReport {
  std::size_t TriangleCount{0};
  std::size_t CandidatePairCount{0};
  std::size_t OverlapPairCount{0};
  // True when the search stopped after maxReportedOverlaps overlaps.
  bool OverlapCountSaturated{false};
  std::uint32_t FirstOverlapA{kInvalidUvAtlasIndex};
  std::uint32_t FirstOverlapB{kInvalidUvAtlasIndex};
};

/// Count pairs of triangles whose open interiors intersect. `corners` holds
/// three UVs per triangle; every triangle must already have positive exact
/// orientation (others are ignored). Candidate pairs come from a BVH over
/// triangle bounds; each is decided exactly by a separating edge test, so
/// shared edges/vertices of adjacent triangles are not overlaps.
[[nodiscard]] UvTriangleOverlapReport
FindUvTriangleOverlaps(std::span<const glm::vec2> corners,
                       std::size_t maxReportedOverlaps = 64u);

struct UvAtlasValidationOptions {
  double MaxConformalDistortion{10.0};
  double MaxAreaDistortion{10.0};
  // Pixel extent used for density and resolution checks. Zero width or
  // height evaluates distortion in the unit UV square and skips texel checks.
  std::uint32_t AtlasWidth{0u};
  std::uint32_t AtlasHeight{0u};
  // Charts covering fewer texels than this are under-resolved.
  double MinChartTexelArea{1.0};
  std::size_t MaxReportedOverlaps{64u};
};

struct UvAtlasValidationInput {
  std::span<const glm::vec3> Positions{};
  std::span<const MeshSoup::PolygonFace> Faces{};
  // Optional per-source-face region labels (empty = one label).
  std::span<const std::uint32_t> FaceRegions{};
  // Three UVs per source face, in source face and corner order.
  std::span<const glm::vec2> CornerUvs{};
  // Chart id per source face; required for region and resolution checks.
  std::span<const std::uint32_t> FaceCharts{};
};

struct UvAtlasValidationReport {
  bool Evaluated{false};
  UvAtlasStatus Status{UvAtlasStatus::ValidationFailed};
  std::string Detail{};

  std::size_t FaceCount{0};
  std::size_t InvalidSourceFaceCount{0};
  std::size_t NonFiniteUvCount{0};
  std::size_t OutOfBoundsUvCount{0};
  // Zero (collapsed in float) plus negative (flipped) orientations.
  std::size_t NonPositiveOrientationCount{0};
  std::size_t CollapsedUvTriangleCount{0};
  std::size_t FlippedUvTriangleCount{0};
  std::uint32_t FirstNonPositiveFace{kInvalidUvAtlasIndex};

  std::size_t RegionLabelCount{0};
  std::size_t RegionComponentCount{0};
  std::size_t ChartCount{0};
  // Charts containing faces of more than one region label or component.
  std::size_t RegionCrossingChartCount{0};

  UvTriangleOverlapReport Overlaps{};

  // Singular values of the pixel-space Jacobian divided by the one global
  // density sqrt(total pixel area / total surface area).
  double MaxConformalDistortion{0.0};
  double MeanConformalDistortion{0.0};
  double MaxAreaDistortion{0.0};
  double MeanAreaDistortion{0.0};
  std::uint32_t WorstConformalFace{kInvalidUvAtlasIndex};
  std::uint32_t WorstAreaFace{kInvalidUvAtlasIndex};
  std::size_t ConformalLimitViolationCount{0};
  std::size_t AreaLimitViolationCount{0};
  double TexelsPerUnit{0.0};

  std::size_t UnderResolvedChartCount{0};
  std::size_t SubTexelFaceCount{0};
  double MinChartTexelArea{0.0};
  // Charts with no texel center (i + 0.5, j + 0.5) strictly inside one of
  // their triangles. Centers exactly on an edge are not counted, so this is
  // conservative with respect to top-left-rule rasterization.
  std::size_t ChartsWithoutTexelCenterCount{0};

  [[nodiscard]] bool Passed() const noexcept {
    return Evaluated && Status == UvAtlasStatus::Success;
  }
};

/// Validate an atlas expressed as per-source-corner UVs. The check does not
/// trust generator bookkeeping: it recomputes orientation, overlap, region
/// components, distortion and texel coverage from the source geometry.
/// Status is the most severe failing class (ValidationFailed, then
/// QualityLimitNotMet, then UnderResolved) with the detail of its first
/// finding.
[[nodiscard]] UvAtlasValidationReport
ValidateUvAtlasCorners(const UvAtlasValidationInput &input,
                       const UvAtlasValidationOptions &options = {});
} // namespace Geometry::UvAtlas
