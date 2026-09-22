// UV-atlas outcome, method and objective enums shared by generators and copied editor results.
module;

#include <cstdint>

export module Geometry.UvAtlas.Types;

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
  InvalidOptions,
  InvalidRegionLabels,
  // The selected backend cannot run the requested distortion objective.
  ObjectiveUnsupported,
  // The chart budget (MaxCharts) or a fixed texel density cannot be met.
  ResourceLimitExceeded,
  // A density-normalized distortion bound was not met.
  QualityLimitNotMet,
  // A chart covers less than one texel of the requested atlas.
  UnderResolved,
  // Independent validation found missing/duplicated corners, non-finite,
  // out-of-bounds, non-positive or overlapping UV triangles, or a chart
  // that crosses a region.
  ValidationFailed,
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

// Per-chart distortion objective. None constructs a valid map without an
// optimization objective (not zero distortion); Angle minimizes conformal
// distortion (LSCM); Area minimizes the engine's area-priority energy;
// Both minimizes symmetric Dirichlet with SLIM iterations.
enum class UvAtlasDistortion : std::uint8_t {
  None = 0,
  Angle,
  Area,
  Both,
};

// Why a source edge is a UV seam or chart boundary.
enum class UvAtlasSeamReason : std::uint8_t {
  MeshBoundary = 0,
  RegionBoundary,
  // Non-manifold or inconsistently oriented source edge.
  TopologyCut,
  // Boundary between charts grown inside one region.
  ChartBoundary,
  // Boundary introduced by splitting a rejected chart.
  RefinementSplit,
};

} // namespace Geometry::UvAtlas
