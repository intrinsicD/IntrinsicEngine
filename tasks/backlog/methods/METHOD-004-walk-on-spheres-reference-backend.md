---
id: METHOD-004
theme: I
depends_on: []
maturity_target: CPUContracted
---
# METHOD-004 — Walk on Stars PDE solver reference backend

## Goal
- Add a deterministic CPU reference Walk on Stars solver for the declared
  volumetric elliptic-PDE subset and mixed Dirichlet/Neumann boundary
  conditions, with analytic error/uncertainty diagnostics and seeded,
  thread-count-independent sampling.

## Non-goals
- No GPU backend in this task; design the API to be GPU-portable but ship CPU only.
- No neural variants (WoS-NN, learned guiding) — those are follow-up optimised backends.
- No replacement of grid / FEM solvers; WoS is a pointwise estimator, useful where the user wants the solution at a few points without a global solve.
- No projected surface-PDE formulation in this task; that is a distinct method
  contract and may open only after this reference exists.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/walk_on_stars/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #3.
- Uses existing `Geometry.BVH`/`Geometry.Raycast` for the selected mesh-domain
  WoSt visibility-silhouette and boundary-hit queries. `Geometry.SDF` supports
  the internal Dirichlet-only WoS comparison unless an adapter can satisfy the
  same explicit silhouette/ray contract.
- The gap analysis flags "stochastic reproducibility state" (P2) — this task is where the deterministic seeded-RNG contract gets defined.

## Variants and default selection

- **Selected — Walk on Stars** (Sawhney, Miller, Crane; SIGGRAPH 2023).
  Mixed-boundary WoSt is the one public strategy delivered here.
- **Internal comparison — vanilla Walk on Spheres** (Sawhney & Crane;
  SIGGRAPH 2020). A Dirichlet-only implementation may be retained privately as
  an analytic comparison, not exposed as a second strategy unless the intake
  finds a concrete caller.
- **Deferred — projected surface WoS, differential WoS, guiding, and
  off-centered variants.** Each changes the method claim and requires its own
  intake/evidence task. `METHOD-028` already owns the bounded guiding study.

## Slice plan

- **Slice A — intake/statistical contract.** Freeze supported equations and
  boundary conditions, units, domain-query assumptions, estimator definition,
  seed derivation, confidence-interval method, analytic fixtures, sample
  budgets, tolerances, and failure states.
- **Slice B — serial CPU reference.** Implement the deterministic single-point
  WoSt estimator and analytic tests without parallelism or acceleration.
- **Slice C — batch execution.** Add deterministic parallel batching only
  after serial truth is fixed; prove identical walk streams across supported
  thread counts.
- **Slice D — benchmark/docs.** Add the executable correctness smoke and
  schema-valid result before any optimized/GPU follow-up.

## Right-sizing

- The SDF and mesh query adapters justify one narrow domain-query seam. Do not
  add a general Monte Carlo PDE framework, sampler registry, or RNG service.
- Keep the reference correctness-first and serial in its canonical path;
  batching is an execution detail and never a second correctness oracle.

## Backends

- Backend axis: seeded deterministic `cpu_reference` only. `METHOD-028`
  evaluates a distinct guiding policy, not a replacement correctness backend;
  no GPU backend task is reserved.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/walk_on_stars/`.
- [ ] Fill `method.yaml` (`id: geometry.walk_on_stars`, metrics:
      `solution_variance`, `mean_walk_length`, `runtime_ms_per_sample`,
      `confidence_interval_width`).
- [ ] Fill `paper.md` with the selected equations/BCs, estimator and weighting
      equations, spatial/value units, query preconditions, stopping rules,
      confidence-interval definition, and failure diagnostics.

### Public API in `src/geometry`
- [ ] Add module `Geometry.WalkOnStars` in
      `src/geometry/Geometry.WalkOnStars.cppm` + `.cpp`.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::WalkOnStars {
    enum class BoundaryKind { Dirichlet, Neumann };
    struct BoundarySample {
      glm::dvec3 point;
      glm::dvec3 outward_normal;
      BoundaryKind kind;
      double distance;
    };
    struct DomainQueries {
      virtual ~DomainQueries() = default;
      virtual double SignedDistance(glm::dvec3 x) const = 0;
      virtual glm::dvec3 ClosestBoundaryPoint(glm::dvec3 x) const = 0;
      virtual glm::dvec3 BoundaryNormal(glm::dvec3 p) const = 0;
      // Required by WoSt to construct a valid star-shaped walk domain.
      virtual std::optional<BoundarySample>
          ClosestVisibilitySilhouette(glm::dvec3 x) const = 0;
      virtual std::optional<BoundarySample>
          FirstBoundaryHit(glm::dvec3 origin, glm::dvec3 direction,
                           double max_distance) const = 0;
    };
    struct BoundaryConditions {
      std::function<double(glm::dvec3)> dirichlet;   // value at boundary points
      std::function<double(glm::dvec3)> neumann;     // flux; WoSt only
      std::function<double(glm::dvec3)> source;      // Poisson RHS, optional
    };
    struct Params {
      uint32_t samples_per_point = 128;
      uint32_t max_steps = 256;
      double epsilon_shell = 1e-4;
      uint64_t seed = 0;          // deterministic
    };
    struct Estimate { double mean; double stderr_; uint32_t actual_steps_mean; };
    Estimate SolveAtPoint(const DomainQueries&, const BoundaryConditions&,
                          glm::dvec3 x, const Params&);
    std::vector<Estimate> SolveAtPoints(const DomainQueries&, const BoundaryConditions&,
                                        std::span<const glm::dvec3>, const Params&);
  }
  ```
- [ ] Register module in `src/geometry/CMakeLists.txt`; do not umbrella-export.

### Deterministic RNG contract
- [ ] Add `Geometry::Random::SplitMix64` / `PCG32` in `src/geometry/Geometry.Random.cppm` (if not already present from `GEOM-009` benchmark fixtures).
- [ ] Document: each walk stream is derived from
      `(seed, point_index, walk_index)` and each variate from
      `(step_index, draw_index)`, so multiple draws within one step remain
      distinct and scheduling/thread count cannot change the sample set.
- [ ] Parallel sampling must never seed from worker/thread identity or consume
      a shared stream; reductions use a fixed point/walk order.

### Query adapters
- [ ] `DomainQueriesFromMesh` — wraps `Geometry.BVH`/`Geometry.Raycast` with
      face normals and a deterministic closest-visibility-silhouette query;
      this is the full mixed-boundary WoSt adapter.
- [ ] `DomainQueriesFromSDF` — wraps `Geometry.SDF` for the internal
      Dirichlet-only WoS comparison. A mixed-boundary request fails closed as
      unsupported unless this adapter can supply the exact same
      visibility-silhouette and first-hit contract.
- [ ] Do not add the projected-surface adapter in this task.

### Benchmark
- [ ] Add an executable deterministic smoke with stable ID
      `geometry.walk_on_stars.reference.smoke`, a built-in analytic
      mixed-boundary dataset, `intent: correctness`, fixed seed/sample budget,
      explicit warmup/measured counts, and allowed metrics `runtime_ms` and
      `quality_error_l2`. Put variance, interval width, walk length, sample
      count, and coverage diagnostics in the result payload.
- [ ] Emit schema-valid result JSON with `backend: cpu_reference`; no external
      dataset and no performance claim.

## Tests
- [ ] `tests/unit/geometry/Test.WalkOnStars.cpp`.
- [ ] Analytic: Laplace problem on unit cube with `u(x)=x` on boundary — verify mean estimate agrees within 3σ for `samples=4096`.
- [ ] Convergence: `stderr_ ∝ 1/√samples` at fixed point.
- [ ] Determinism: same seed → bitwise identical mean across two runs and across `OMP_NUM_THREADS=1` vs `=4`.
- [ ] WoSt-only: half-Neumann / half-Dirichlet half-space — verify against
      closed form through the full visibility-silhouette query path.
- [ ] Query capability: a Dirichlet-only SDF adapter is accepted for the
      internal WoS comparison and rejects a mixed-boundary WoSt request with an
      explicit unsupported-query diagnostic.
- [ ] Fail closed on zero samples, zero max steps, invalid epsilon, non-finite
      query/BC values, queries outside the supported domain, and walk-budget
      exhaustion; freeze the statistical acceptance rule before assertions.

## Docs
- [ ] `methods/geometry/walk_on_stars/README.md`.
- [ ] Add a deterministic-RNG section to `docs/methods/reference-implementation-policy.md` (or new `docs/methods/stochastic-determinism.md`) capturing the seed-per-point contract.
- [ ] Register and document the stable correctness-smoke ID and built-in
      dataset under `benchmarks/geometry/`.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] Mixed-boundary Walk on Stars is the sole public strategy; vanilla Walk
      on Spheres remains an internal Dirichlet comparison.
- [ ] Analytic test passes with documented sample budget.
- [ ] Determinism test passes under thread-count variation.
- [ ] Smoke manifest/result validate and report analytic error plus uncertainty
      diagnostics for the exact seeded sample budget.
- [ ] Public API exposes only `glm` + scalar types (no Eigen / runtime types).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'WalkOnStars|SDF|Raycast|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No GPU backend before CPU reference parity.
- No neural / learned guiding in this task.
- No global per-thread RNG state; every variate must be derivable from
  `(seed, point_index, walk_index, step_index, draw_index)`.
- No use of `std::rand` / `rand()` / `std::default_random_engine` (non-portable).

## Maturity
- Target: `CPUContracted`. The CPU reference backend is the correctness oracle for any later optimized/GPU backend.
- No `Operational` follow-up is owed by this task; optimized CPU and GPU backends open as separate method tasks per `AGENTS.md` §6 once reference parity exists.
