// Parameterizes one disk-topology atlas chart for a selected distortion
// objective, returning locally injective UVs at unit area density.
module;

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.UvAtlas.ChartSolve;

export import Geometry.UvAtlas.Types;

export namespace Geometry::UvAtlas {
enum class UvChartSolveStatus : std::uint8_t {
  Success = 0,
  InvalidInput,
  // Not one connected, consistently oriented, manifold disk.
  NotDisk,
  // No locally injective initial map could be constructed.
  InitializationFailed,
  // The objective's own solve produced a flipped or collapsed triangle.
  NonInjective,
  OptimizationFailed,
  Cancelled,
};

struct UvChartSolveParams {
  UvAtlasDistortion Objective{UvAtlasDistortion::Both};
  // Outer iterations for Area (preconditioned descent) and Both (SLIM).
  std::uint32_t MaxIterations{40u};
  // Stop when an accepted step lowers the energy by less than this fraction.
  double RelativeEnergyTolerance{1.0e-4};
  // Weight mu of the MIPS anisotropy regularizer in the Area objective.
  double AreaPriorityConformalWeight{0.1};
  const std::atomic<bool> *CancelFlag{nullptr};
};

struct UvChartSolveResult {
  UvChartSolveStatus Status{UvChartSolveStatus::InvalidInput};
  // One UV per local vertex, uniformly scaled so the total UV area equals
  // the total surface area (unit density). Every triangle is positively
  // oriented in double precision; global overlap is not checked here.
  std::vector<glm::dvec2> Uvs{};
  // Solver identity, e.g. "tutte", "lscm", "lscm+slim",
  // "tutte+area_priority" or "single_triangle_exact".
  std::string Backend{};
  UvAtlasDistortion ActualObjective{UvAtlasDistortion::None};
  std::uint32_t Iterations{0u};
  bool Converged{false};
  double InitialEnergy{0.0};
  double FinalEnergy{0.0};
  std::string Detail{};

  [[nodiscard]] bool Succeeded() const noexcept {
    return Status == UvChartSolveStatus::Success;
  }
};

[[nodiscard]] const char *ToString(UvChartSolveStatus status) noexcept;

/// Solve one chart. `triangles` holds three local vertex indices per
/// triangle, consistently oriented. A single triangle is laid out exactly
/// (isometric), which is optimal for every objective. Larger charts must be
/// connected manifold disks:
/// - None: uniform-weight Tutte embedding onto a circle (no optimization).
/// - Angle: LSCM; a flipped LSCM result is NonInjective, never replaced.
/// - Area: LSCM (else Tutte) start, then Laplacian-preconditioned descent
///   on EvaluateAreaPriorityEnergy with the injective line search.
/// - Both: LSCM (else Tutte) start, then SLIM (Rabinovich et al. 2017)
///   reweighted-proxy iterations on symmetric Dirichlet.
[[nodiscard]] UvChartSolveResult
SolveUvChart(std::span<const glm::vec3> positions,
             std::span<const std::uint32_t> triangles,
             const UvChartSolveParams &params = {});
} // namespace Geometry::UvAtlas
