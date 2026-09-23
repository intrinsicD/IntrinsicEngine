module;

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

module Geometry.UvAtlas.ChartSolve;

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Utils;
import Geometry.Properties;
import Geometry.Parameterization;
import Geometry.Parameterization.Optimize;
import Geometry.Sparse;
import Geometry.UvAtlas.Types;

namespace Geometry::UvAtlas {
namespace {
// Relative proximal weight that pins the translational null space of the
// SLIM/Laplacian normal matrices without visibly biasing a step.
constexpr double kRelativeProximalWeight = 1.0e-8;
// Energies are evaluated on unit-area charts; below this absolute decrease
// scale an exactly optimal start (for example a planar chart) is converged.
constexpr double kEnergyFloor = 1.0e-10;

[[nodiscard]] bool CancelRequested(const UvChartSolveParams &params) noexcept {
  return params.CancelFlag != nullptr &&
         params.CancelFlag->load(std::memory_order_relaxed);
}

[[nodiscard]] UvChartSolveResult Failure(const UvChartSolveStatus status,
                                         std::string detail) {
  UvChartSolveResult result{};
  result.Status = status;
  result.Detail = std::move(detail);
  return result;
}

[[nodiscard]] double SignedArea(const glm::dvec2 a, const glm::dvec2 b,
                                const glm::dvec2 c) noexcept {
  const glm::dvec2 e1 = b - a;
  const glm::dvec2 e2 = c - a;
  return 0.5 * (e1.x * e2.y - e1.y * e2.x);
}

// +1 when every triangle is positive, -1 when every triangle is negative
// (a mirrored but otherwise valid map), 0 otherwise.
[[nodiscard]] int UniformOrientation(const std::span<const glm::dvec2> uvs,
                                     const std::span<const std::uint32_t> tris) {
  bool anyPositive = false;
  bool anyNonPositive = false;
  bool anyNonNegative = false;
  for (std::size_t t = 0u; t + 2u < tris.size(); t += 3u) {
    const double area = SignedArea(uvs[tris[t]], uvs[tris[t + 1u]],
                                   uvs[tris[t + 2u]]);
    if (!std::isfinite(area)) {
      return 0;
    }
    anyPositive = anyPositive || area > 0.0;
    anyNonPositive = anyNonPositive || !(area > 0.0);
    anyNonNegative = anyNonNegative || !(area < 0.0);
  }
  if (!anyNonPositive) {
    return 1;
  }
  if (!anyNonNegative && !anyPositive) {
    return -1;
  }
  return 0;
}

// Accept a solver's per-vertex UVs as a locally injective start, undoing a
// global mirror. Returns false for mixed or degenerate orientation.
[[nodiscard]] bool AcceptInjective(const std::span<const glm::vec2> solverUvs,
                                   const std::span<const std::uint32_t> tris,
                                   const std::size_t vertexCount,
                                   std::vector<glm::dvec2> &out) {
  if (solverUvs.size() < vertexCount) {
    return false;
  }
  out.resize(vertexCount);
  for (std::size_t v = 0u; v < vertexCount; ++v) {
    out[v] = glm::dvec2{solverUvs[v]};
    if (!std::isfinite(out[v].x) || !std::isfinite(out[v].y)) {
      return false;
    }
  }
  const int orientation = UniformOrientation(out, tris);
  if (orientation == 0) {
    return false;
  }
  if (orientation < 0) {
    for (glm::dvec2 &uv : out) {
      uv.x = -uv.x;
    }
  }
  return true;
}

[[nodiscard]] double TotalUvArea(const std::span<const glm::dvec2> uvs,
                                 const std::span<const std::uint32_t> tris) {
  double total = 0.0;
  for (std::size_t t = 0u; t + 2u < tris.size(); t += 3u) {
    total += SignedArea(uvs[tris[t]], uvs[tris[t + 1u]], uvs[tris[t + 2u]]);
  }
  return total;
}

// Uniformly scale UVs so their total area equals targetArea.
[[nodiscard]] bool NormalizeArea(std::vector<glm::dvec2> &uvs,
                                 const std::span<const std::uint32_t> tris,
                                 const double targetArea) {
  const double area = TotalUvArea(uvs, tris);
  if (!(area > 0.0) || !std::isfinite(area) || !(targetArea > 0.0)) {
    return false;
  }
  const double scale = std::sqrt(targetArea / area);
  if (!std::isfinite(scale) || !(scale > 0.0)) {
    return false;
  }
  for (glm::dvec2 &uv : uvs) {
    uv *= scale;
  }
  return true;
}

// kRelativeProximalWeight times the mean diagonal of the area-weighted
// Dirichlet matrix sum_f A_f g g^T, computed from the reference alone.
[[nodiscard]] double
ProximalWeight(const Parameterization::OptimizationReference &reference) {
  double trace = 0.0;
  for (const Parameterization::OptimizationTriangleReference &face :
       reference.Faces) {
    if (!face.Active) {
      continue;
    }
    for (const glm::dvec2 gradient : face.Gradients) {
      trace += face.Area * glm::dot(gradient, gradient);
    }
  }
  const double mean =
      reference.VertexStorageCount > 0u
          ? trace / static_cast<double>(reference.VertexStorageCount)
          : 1.0;
  return kRelativeProximalWeight * (mean > 0.0 ? mean : 1.0);
}

// Solve normal * x = rhs for the blocked [u..., v...] layout. Returns an
// empty string on success, otherwise the failing stage.
[[nodiscard]] std::string SolveBlocked(const Sparse::SparseMatrix &normal,
                                       const std::span<const double> rhs,
                                       std::vector<double> &solution) {
  Sparse::SparseLDLT ldlt;
  const Sparse::SparseFactorizationDiagnostics factored = ldlt.factor(normal);
  if (!factored.Succeeded()) {
    return "factorization status " +
           std::to_string(static_cast<int>(factored.Status)) +
           " (smallest pivot " + std::to_string(factored.SmallestAbsolutePivot) +
           ")";
  }
  solution.assign(rhs.size(), 0.0);
  if (!ldlt.solve(rhs, solution).Succeeded() ||
      !std::all_of(solution.begin(), solution.end(),
                   [](const double value) { return std::isfinite(value); })) {
    return "back-substitution failed";
  }
  return {};
}

struct OptimizeOutcome {
  bool Ran{false};
  bool Converged{false};
  std::uint32_t Iterations{0u};
  double InitialEnergy{0.0};
  double FinalEnergy{0.0};
  bool Cancelled{false};
  std::string Detail{};
};

// SLIM: local SVD fits, symmetric-Dirichlet reweighted proxy solve, then the
// orientation-preserving Armijo search along (proxy minimizer - current).
[[nodiscard]] OptimizeOutcome
RunSlim(const Parameterization::OptimizationReference &reference,
        std::vector<glm::dvec2> &uvs, const UvChartSolveParams &params) {
  namespace P = Parameterization;
  OptimizeOutcome outcome{};
  const P::SymmetricDirichletResult initial =
      P::EvaluateSymmetricDirichlet(reference, uvs);
  if (!initial.Succeeded()) {
    outcome.Detail = "initial map is outside the symmetric Dirichlet domain";
    return outcome;
  }
  outcome.Ran = true;
  outcome.InitialEnergy = initial.TotalEnergy;
  outcome.FinalEnergy = initial.TotalEnergy;
  const std::size_t n = uvs.size();
  const double lambda = ProximalWeight(reference);
  std::vector<double> solution;
  std::vector<glm::dvec2> direction(n);
  for (std::uint32_t iteration = 0u; iteration < params.MaxIterations;
       ++iteration) {
    if (CancelRequested(params)) {
      outcome.Cancelled = true;
      return outcome;
    }
    const P::LocalFitResult fits = P::FitLocalModels(reference, uvs);
    if (!fits.Succeeded()) {
      outcome.Detail = "local SVD fit failed";
      return outcome;
    }
    const P::ProxySystem proxy =
        P::AssembleProxySystem(reference, uvs, fits,
                               P::ProxyEnergy::SymmetricDirichlet, lambda);
    if (!proxy.Succeeded()) {
      outcome.Detail = "SLIM proxy assembly status " +
                       std::to_string(static_cast<int>(proxy.Status));
      return outcome;
    }
    if (const std::string failure =
            SolveBlocked(proxy.NormalMatrix, proxy.RightHandSide, solution);
        !failure.empty()) {
      outcome.Detail = "SLIM proxy solve: " + failure;
      return outcome;
    }
    for (std::size_t v = 0u; v < n; ++v) {
      direction[v] = glm::dvec2{solution[v], solution[n + v]} - uvs[v];
    }
    const P::InjectiveLineSearchResult step =
        P::FindInjectiveDirichletStep(reference, uvs, direction);
    if (step.Status == P::OptimizationStatus::NoDescentDirection) {
      outcome.Converged = true;
      return outcome;
    }
    if (!step.Succeeded()) {
      outcome.Detail = "SLIM line search found no acceptable step";
      return outcome;
    }
    for (std::size_t v = 0u; v < n; ++v) {
      uvs[v] += step.Step * direction[v];
    }
    ++outcome.Iterations;
    const double previous = outcome.FinalEnergy;
    outcome.FinalEnergy = step.AcceptedEnergy;
    if (previous - step.AcceptedEnergy <=
        params.RelativeEnergyTolerance *
            std::max(std::abs(previous), kEnergyFloor)) {
      outcome.Converged = true;
      return outcome;
    }
  }
  return outcome;
}

// Area priority: descent direction -L^-1 grad E with the fixed area-weighted
// Dirichlet (W=I proxy) matrix L, factored once, and the same injective
// Armijo search. This is a quadratic-proxy (Sobolev) gradient method, not a
// published named solver.
[[nodiscard]] OptimizeOutcome
RunAreaPriority(const Parameterization::OptimizationReference &reference,
                std::vector<glm::dvec2> &uvs, const UvChartSolveParams &params) {
  namespace P = Parameterization;
  OptimizeOutcome outcome{};
  const double mu = params.AreaPriorityConformalWeight;
  const P::AreaPriorityEnergyResult initial =
      P::EvaluateAreaPriorityEnergy(reference, uvs, mu);
  if (!initial.Succeeded()) {
    outcome.Detail = "initial map is outside the area-priority domain";
    return outcome;
  }
  const P::LocalFitResult fits = P::FitLocalModels(reference, uvs);
  if (!fits.Succeeded()) {
    outcome.Detail = "local SVD fit failed";
    return outcome;
  }
  const P::ProxySystem laplacian =
      P::AssembleProxySystem(reference, uvs, fits, P::ProxyEnergy::Arap,
                             ProximalWeight(reference));
  Sparse::SparseLDLT ldlt;
  if (!laplacian.Succeeded() || !ldlt.factor(laplacian.NormalMatrix).Succeeded()) {
    outcome.Detail = "preconditioner factorization failed";
    return outcome;
  }

  outcome.Ran = true;
  outcome.InitialEnergy = initial.TotalEnergy;
  outcome.FinalEnergy = initial.TotalEnergy;
  const std::size_t n = uvs.size();
  std::vector<double> rhs(2u * n, 0.0);
  std::vector<double> solution(2u * n, 0.0);
  std::vector<glm::dvec2> direction(n);
  for (std::uint32_t iteration = 0u; iteration < params.MaxIterations;
       ++iteration) {
    if (CancelRequested(params)) {
      outcome.Cancelled = true;
      return outcome;
    }
    const P::AreaPriorityEnergyResult energy =
        P::EvaluateAreaPriorityEnergy(reference, uvs, mu);
    if (!energy.Succeeded()) {
      outcome.Detail = "area-priority energy evaluation failed";
      return outcome;
    }
    for (std::size_t v = 0u; v < n; ++v) {
      rhs[v] = -energy.Gradient[v].x;
      rhs[n + v] = -energy.Gradient[v].y;
    }
    if (!ldlt.solve(rhs, solution).Succeeded()) {
      outcome.Detail = "preconditioner solve failed";
      return outcome;
    }
    for (std::size_t v = 0u; v < n; ++v) {
      direction[v] = glm::dvec2{solution[v], solution[n + v]};
    }
    const P::InjectiveLineSearchResult step =
        P::FindInjectiveAreaPriorityStep(reference, uvs, direction, mu);
    if (step.Status == P::OptimizationStatus::NoDescentDirection) {
      outcome.Converged = true;
      return outcome;
    }
    if (!step.Succeeded()) {
      outcome.Detail = "area-priority line search found no acceptable step";
      return outcome;
    }
    for (std::size_t v = 0u; v < n; ++v) {
      uvs[v] += step.Step * direction[v];
    }
    ++outcome.Iterations;
    const double previous = outcome.FinalEnergy;
    outcome.FinalEnergy = step.AcceptedEnergy;
    if (previous - step.AcceptedEnergy <=
        params.RelativeEnergyTolerance *
            std::max(std::abs(previous), kEnergyFloor)) {
      outcome.Converged = true;
      return outcome;
    }
  }
  return outcome;
}

// Faces of a freshly built halfedge mesh must reproduce the input triangles
// (same order and winding); the builder may otherwise drop or flip faces.
[[nodiscard]] bool MatchesInput(const HalfedgeMesh::Mesh &mesh,
                                const std::span<const std::uint32_t> tris) {
  const std::size_t triangleCount = tris.size() / 3u;
  if (mesh.FacesSize() != triangleCount || mesh.FaceCount() != triangleCount) {
    return false;
  }
  for (std::size_t t = 0u; t < triangleCount; ++t) {
    const FaceHandle face{static_cast<PropertyIndex>(t)};
    HalfedgeHandle h = mesh.Halfedge(face);
    std::uint32_t cycle[3]{};
    for (std::uint32_t &vertex : cycle) {
      vertex = static_cast<std::uint32_t>(mesh.ToVertex(h).Index);
      h = mesh.NextHalfedge(h);
    }
    if (h != mesh.Halfedge(face)) {
      return false;
    }
    // ToVertex order starting at the face halfedge is a rotation of input.
    bool rotated = false;
    for (std::size_t shift = 0u; shift < 3u && !rotated; ++shift) {
      rotated = cycle[0] == tris[3u * t + shift] &&
                cycle[1] == tris[3u * t + (shift + 1u) % 3u] &&
                cycle[2] == tris[3u * t + (shift + 2u) % 3u];
    }
    if (!rotated) {
      return false;
    }
  }
  return true;
}

// Connected manifold with Euler characteristic one and a boundary is a disk.
[[nodiscard]] bool IsDisk(const std::span<const std::uint32_t> tris,
                          const std::size_t vertexCount) {
  const std::size_t triangleCount = tris.size() / 3u;
  std::unordered_set<std::uint64_t> edges;
  edges.reserve(triangleCount * 3u);
  std::vector<std::uint32_t> parent(vertexCount);
  for (std::size_t v = 0u; v < vertexCount; ++v) {
    parent[v] = static_cast<std::uint32_t>(v);
  }
  const auto find = [&parent](std::uint32_t v) {
    while (parent[v] != v) {
      parent[v] = parent[parent[v]];
      v = parent[v];
    }
    return v;
  };
  std::vector<std::uint8_t> used(vertexCount, 0u);
  for (std::size_t t = 0u; t < triangleCount; ++t) {
    for (std::size_t corner = 0u; corner < 3u; ++corner) {
      const std::uint32_t a = tris[3u * t + corner];
      const std::uint32_t b = tris[3u * t + (corner + 1u) % 3u];
      used[a] = 1u;
      edges.insert((static_cast<std::uint64_t>(std::min(a, b)) << 32u) |
                   std::max(a, b));
      const std::uint32_t ra = find(a);
      const std::uint32_t rb = find(b);
      if (ra != rb) {
        parent[std::max(ra, rb)] = std::min(ra, rb);
      }
    }
  }
  std::size_t usedCount = 0u;
  std::size_t roots = 0u;
  for (std::size_t v = 0u; v < vertexCount; ++v) {
    if (used[v] != 0u) {
      ++usedCount;
      roots += find(static_cast<std::uint32_t>(v)) == v ? 1u : 0u;
    }
  }
  const auto euler = static_cast<long long>(usedCount) -
                     static_cast<long long>(edges.size()) +
                     static_cast<long long>(triangleCount);
  return usedCount == vertexCount && roots == 1u && euler == 1;
}
} // namespace

const char *ToString(const UvChartSolveStatus status) noexcept {
  switch (status) {
  case UvChartSolveStatus::Success:
    return "success";
  case UvChartSolveStatus::InvalidInput:
    return "invalid_input";
  case UvChartSolveStatus::NotDisk:
    return "not_disk";
  case UvChartSolveStatus::InitializationFailed:
    return "initialization_failed";
  case UvChartSolveStatus::NonInjective:
    return "non_injective";
  case UvChartSolveStatus::OptimizationFailed:
    return "optimization_failed";
  case UvChartSolveStatus::Cancelled:
    return "cancelled";
  }
  return "unknown";
}

UvChartSolveResult SolveUvChart(const std::span<const glm::vec3> positions,
                                const std::span<const std::uint32_t> triangles,
                                const UvChartSolveParams &params) {
  if (positions.empty() || triangles.empty() || triangles.size() % 3u != 0u) {
    return Failure(UvChartSolveStatus::InvalidInput, "empty chart");
  }
  if (CancelRequested(params)) {
    return Failure(UvChartSolveStatus::Cancelled, "cancel requested");
  }
  for (const std::uint32_t index : triangles) {
    if (index >= positions.size()) {
      return Failure(UvChartSolveStatus::InvalidInput,
                     "chart index out of range");
    }
  }
  for (const glm::vec3 p : positions) {
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
      return Failure(UvChartSolveStatus::InvalidInput,
                     "chart position is not finite");
    }
  }

  const std::size_t vertexCount = positions.size();
  const std::size_t triangleCount = triangles.size() / 3u;
  double surfaceArea = 0.0;
  glm::dvec3 centroid{0.0};
  for (const glm::vec3 p : positions) {
    centroid += glm::dvec3{p};
  }
  centroid /= static_cast<double>(vertexCount);
  for (std::size_t t = 0u; t < triangleCount; ++t) {
    const glm::dvec3 p0{positions[triangles[3u * t]]};
    const glm::dvec3 p1{positions[triangles[3u * t + 1u]]};
    const glm::dvec3 p2{positions[triangles[3u * t + 2u]]};
    surfaceArea += 0.5 * glm::length(glm::cross(p1 - p0, p2 - p0));
  }
  if (!(surfaceArea > 0.0) || !std::isfinite(surfaceArea)) {
    return Failure(UvChartSolveStatus::InvalidInput,
                   "chart has no surface area");
  }

  UvChartSolveResult result{};
  if (triangleCount == 1u) {
    // Exact isometric layout; zero distortion satisfies every objective.
    if (vertexCount != 3u) {
      return Failure(UvChartSolveStatus::InvalidInput,
                     "single-triangle chart must have three vertices");
    }
    const glm::dvec3 p0{positions[triangles[0]]};
    const glm::dvec3 p1{positions[triangles[1]]};
    const glm::dvec3 p2{positions[triangles[2]]};
    const glm::dvec3 e1 = p1 - p0;
    const glm::dvec3 e2 = p2 - p0;
    const double e1Length = glm::length(e1);
    const glm::dvec3 normal = glm::cross(e1, e2);
    const double normalLength = glm::length(normal);
    if (!(e1Length > 0.0) || !(normalLength > 0.0)) {
      return Failure(UvChartSolveStatus::InvalidInput, "degenerate triangle");
    }
    const glm::dvec3 xAxis = e1 / e1Length;
    const glm::dvec3 yAxis = glm::cross(normal / normalLength, xAxis);
    result.Uvs.assign(3u, glm::dvec2{0.0});
    result.Uvs[triangles[1]] = glm::dvec2{e1Length, 0.0};
    result.Uvs[triangles[2]] =
        glm::dvec2{glm::dot(e2, xAxis), glm::dot(e2, yAxis)};
    if (!(TotalUvArea(result.Uvs, triangles) > 0.0)) {
      return Failure(UvChartSolveStatus::NonInjective,
                     "isometric triangle layout collapsed");
    }
    result.Status = UvChartSolveStatus::Success;
    result.Backend = "single_triangle_exact";
    result.ActualObjective = params.Objective;
    result.Converged = true;
    return result;
  }

  if (!IsDisk(triangles, vertexCount)) {
    return Failure(UvChartSolveStatus::NotDisk,
                   "chart is not one connected disk (Euler characteristic)");
  }

  // Solve in a translated, area-normalized frame so absolute thresholds in
  // the shared kernels act on O(1) quantities for every unit system.
  const double lengthScale = std::sqrt(surfaceArea);
  std::vector<glm::vec3> local(vertexCount);
  for (std::size_t v = 0u; v < vertexCount; ++v) {
    local[v] = glm::vec3{(glm::dvec3{positions[v]} - centroid) / lengthScale};
  }
  const std::optional<HalfedgeMesh::Mesh> mesh =
      MeshUtils::BuildHalfedgeMeshFromIndexedTriangles(local, triangles);
  if (!mesh || mesh->VerticesSize() != vertexCount ||
      !MatchesInput(*mesh, triangles)) {
    return Failure(UvChartSolveStatus::NotDisk,
                   "chart is not a consistently oriented manifold");
  }

  std::vector<glm::dvec2> uvs;
  std::string initializer;
  const auto tryLscm = [&]() {
    Parameterization::ParameterizationParams lscmParams{};
    lscmParams.UseDirectSolver = true;
    const auto lscm = Parameterization::ComputeLSCM(*mesh, lscmParams);
    return lscm && lscm->Converged &&
           AcceptInjective(lscm->UVs, triangles, vertexCount, uvs);
  };
  const auto tryTutte = [&]() {
    Parameterization::HarmonicParams harmonicParams{};
    harmonicParams.Weights = Parameterization::HarmonicWeightType::Uniform;
    harmonicParams.Boundary = Parameterization::HarmonicBoundaryPolicy::Circle;
    harmonicParams.ArcLengthSpacing = true;
    const auto harmonic = Parameterization::ComputeHarmonic(*mesh, harmonicParams);
    return harmonic &&
           harmonic->Status == Parameterization::HarmonicStatus::Success &&
           AcceptInjective(harmonic->UVs, triangles, vertexCount, uvs);
  };

  switch (params.Objective) {
  case UvAtlasDistortion::None:
    if (!tryTutte()) {
      return Failure(UvChartSolveStatus::InitializationFailed,
                     "Tutte embedding was not locally injective");
    }
    initializer = "tutte";
    break;
  case UvAtlasDistortion::Angle:
    if (!tryLscm()) {
      return Failure(UvChartSolveStatus::NonInjective,
                     "LSCM did not converge to a locally injective map");
    }
    initializer = "lscm";
    break;
  case UvAtlasDistortion::Area:
  case UvAtlasDistortion::Both:
    if (tryLscm()) {
      initializer = "lscm";
    } else if (tryTutte()) {
      initializer = "tutte";
    } else {
      return Failure(UvChartSolveStatus::InitializationFailed,
                     "neither LSCM nor Tutte produced a locally injective start");
    }
    break;
  }
  if (!NormalizeArea(uvs, triangles, 1.0)) {
    return Failure(UvChartSolveStatus::NonInjective,
                   "initial map has no positive UV area");
  }

  result.Backend = initializer;
  if (params.Objective == UvAtlasDistortion::Area ||
      params.Objective == UvAtlasDistortion::Both) {
    if (params.MaxIterations == 0u) {
      return Failure(UvChartSolveStatus::OptimizationFailed,
                     "objective requires at least one iteration");
    }
    const Parameterization::OptimizationReference reference =
        Parameterization::PrepareOptimizationReference(*mesh);
    if (!reference.Succeeded()) {
      return Failure(UvChartSolveStatus::OptimizationFailed,
                     "optimization reference rejected the chart");
    }
    const bool slim = params.Objective == UvAtlasDistortion::Both;
    const OptimizeOutcome outcome = slim
                                        ? RunSlim(reference, uvs, params)
                                        : RunAreaPriority(reference, uvs, params);
    if (outcome.Cancelled) {
      return Failure(UvChartSolveStatus::Cancelled, "cancel requested");
    }
    if (!outcome.Ran) {
      return Failure(UvChartSolveStatus::OptimizationFailed, outcome.Detail);
    }
    result.Backend += slim ? "+slim" : "+area_priority";
    result.Iterations = outcome.Iterations;
    result.Converged = outcome.Converged;
    result.InitialEnergy = outcome.InitialEnergy;
    result.FinalEnergy = outcome.FinalEnergy;
    result.Detail = outcome.Detail;
  } else {
    result.Converged = true;
  }

  if (UniformOrientation(uvs, triangles) != 1 ||
      !NormalizeArea(uvs, triangles, surfaceArea)) {
    return Failure(UvChartSolveStatus::NonInjective,
                   "final map is not locally injective");
  }
  result.Uvs = std::move(uvs);
  result.Status = UvChartSolveStatus::Success;
  result.ActualObjective = params.Objective;
  return result;
}
} // namespace Geometry::UvAtlas
