// UV-atlas outcome enums shared by generators and copied editor results.
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
};

enum class UvAtlasProvenance : std::uint8_t {
  None = 0,
  AuthoredPreserved,
  Generated,
};

} // namespace Geometry::UvAtlas
