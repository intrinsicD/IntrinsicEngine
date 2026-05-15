# METHOD-004 — Walk on Spheres / Walk on Stars PDE solver reference backend

## Goal
- Add a CPU reference Monte Carlo solver for elliptic PDEs (Laplace, Poisson, screened-Poisson, biharmonic) on volumetric and surface domains. The solver uses only `(closest point, signed distance, ray)` queries, all of which already exist in the engine, and is embarrassingly parallel — a natural future GPU backend.

## Non-goals
- No GPU backend in this task; design the API to be GPU-portable but ship CPU only.
- No neural variants (WoS-NN, learned guiding) — those are follow-up optimised backends.
- No replacement of grid / FEM solvers; WoS is a pointwise estimator, useful where the user wants the solution at a few points without a global solve.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` and `methods/geometry`.
- Method package: `methods/geometry/walk_on_spheres/`.
- Seeded by [`docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md`](../../../docs/reviews/2026-05-15-arxiv-geometry-paper-survey.md) Tier 1 #3.
- Uses existing `Geometry.SDF` (signed distance), `Geometry.BVH` / `Geometry.KDTree` (closest point), `Geometry.Raycast` (for WoSt star steps).
- **Hard dependency:** [`GEOM-015`](../geometry/GEOM-015-common-method-package-infrastructure.md) for `Geometry::Random` (deterministic RNG with seed-stream contract), `Geometry::ClosestPointOracle`, `Geometry::Diagnostics`.

## Shared infrastructure consumed / extracted

This task **consumes** (depends on) from [`GEOM-015`](../geometry/GEOM-015-common-method-package-infrastructure.md):

- `Geometry::Random::SplitMix64` + `PCG32` and the seed-stream contract `(seed, point_index, walk_index, step_index)` — this task does **not** define its own RNG.
- `Geometry::ClosestPointOracle` (used as the surface-PDE oracle in variant C and as the closest-boundary-point query for vanilla WoS / WoSt).
- `Geometry::Diagnostics` for the `Estimate` aggregation summary.

## Variants and default selection

Mark `[x]` next to the variant that should be the **public-facing default backend**. Unmarked variants become opt-in capabilities or follow-up tasks.

- [ ] **A — Walk on Stars (Sawhney, Miller, Crane; SIGGRAPH 2023).** Handles Dirichlet **and** Neumann boundary conditions; supersedes vanilla WoS for any non-trivial BC. Recommended default.
- [ ] **B — Vanilla Walk on Spheres (Sawhney & Crane, SIGGRAPH 2020).** Dirichlet only; simpler. Pick if only Dirichlet problems are required in year-1 scope.
- [ ] **C — Projected Walk on Spheres for surface PDEs (Sugimoto, Chen, Jiang, Batty, Hachisuka; arXiv:2410.03844).** Solves PDEs on surfaces via closest-point projection; pairs naturally with [`METHOD-003`](METHOD-003-closest-point-method-pde-reference-backend.md). Mark as a follow-up unless surface PDEs are the primary year-1 use case.

Optional variance-reduction add-ons (treat as separate sub-tasks once the baseline lands; do **not** mark default here):

- Differential Walk on Spheres (arXiv:2405.12964) — derivatives w.r.t. parameters.
- Path Guiding for Monte Carlo PDE Solvers (arXiv:2410.18944) — neural variance reduction.
- Off-Centered WoS-Type Solvers (arXiv:2510.25152) — statistical weighting.

Default recommendation: **A** (WoSt) since it subsumes B and is the current state of the art for general boundary conditions.

## Required changes

### Method package scaffolding
- [ ] Clone `methods/_template/` to `methods/geometry/walk_on_spheres/`.
- [ ] Fill `method.yaml` (`id: geometry.walk_on_spheres`, metrics: `solution_variance`, `mean_walk_length`, `runtime_ms_per_sample`, `confidence_interval_width`).
- [ ] Fill `paper.md`.

### Public API in `src/geometry`
- [ ] Add module `Geometry.WalkOnSpheres` in `src/geometry/Geometry.WalkOnSpheres.cppm` + `.cpp`.
- [ ] The domain queries are provided by `Geometry::ClosestPointOracle` (from GEOM-015), augmented with the explicit `SignedDistance` / `Bounds` methods that oracle already exposes. Do **not** define a new oracle interface here.
- [ ] Public surface (sketch):
  ```cpp
  namespace Geometry::WalkOnSpheres {
    struct BoundaryFunctions {
      std::function<double(glm::dvec3)> dirichlet;   // value at boundary points
      std::function<double(glm::dvec3)> neumann;     // flux; WoSt only
      std::function<double(glm::dvec3)> source;      // Poisson RHS f(x); optional
    };
    enum class Variant { WalkOnSpheres, WalkOnStars, ProjectedWoS };
    struct Params {
      Variant variant = Variant::WalkOnStars;
      uint32_t samples_per_point = 128;
      uint32_t max_steps = 256;
      double epsilon_shell = 1e-4;
      uint64_t seed = 0;
      // For Poisson source-term integration: number of strata for the Green's-function ball sample.
      uint32_t source_strata_per_step = 1;
    };
    struct Estimate { double mean; double stderr_; uint32_t actual_steps_mean; Geometry::Diagnostics diagnostics; };
    Estimate SolveAtPoint(const Geometry::ClosestPointOracle&, const BoundaryFunctions&,
                          glm::dvec3 x, const Params&);
    std::vector<Estimate> SolveAtPoints(const Geometry::ClosestPointOracle&, const BoundaryFunctions&,
                                        std::span<const glm::dvec3>, const Params&);
  }
  ```
- [ ] Register module in `src/geometry/CMakeLists.txt`; do not umbrella-export.
- [ ] **Poisson source-term sampling:** for non-zero `source`, each walk step must take `source_strata_per_step` uniform samples inside the current ball and accumulate `f(y) * G(x, y, R)` where `G` is the Green's function for the ball of radius `R`. This is the standard WoS / WoSt extension for the Poisson term and must be in the implementation steps below, not omitted.
- [ ] **WoSt star-step:** for Neumann boundaries, the walk uses `Geometry::Raycast` to intersect the ball's bounding sphere with the Neumann boundary and reflect; document this explicitly in the implementation comments.

### Deterministic RNG contract
- [ ] Use `Geometry::Random` from `GEOM-015`. Do **not** introduce a local RNG type.
- [ ] Per-point per-walk per-step seeding contract: `state = SplitMix64(seed, point_index, walk_index, step_index)`. This guarantees deterministic output regardless of thread count.
- [ ] Add a regression test that varies `OMP_NUM_THREADS` between 1 and 8 and asserts bitwise output equality.

## Tests
- [ ] `tests/unit/geometry/Test.WalkOnSpheres.cpp`.
- [ ] Analytic: Laplace problem on unit cube with `u(x)=x` on boundary — verify mean estimate agrees within 3σ for `samples=4096`.
- [ ] Convergence: `stderr_ ∝ 1/√samples` at fixed point.
- [ ] Determinism: same seed → bitwise identical mean across two runs and across `OMP_NUM_THREADS=1` vs `=4`.
- [ ] WoSt-only: half-Neumann / half-Dirichlet half-space — verify against closed form (variant A).
- [ ] Surface-PDE-only: Laplace–Beltrami on sphere — verify against eigenfunction (variant C).

## Docs
- [ ] `methods/geometry/walk_on_spheres/README.md`.
- [ ] Add a deterministic-RNG section to `docs/methods/reference-implementation-policy.md` (or new `docs/methods/stochastic-determinism.md`) capturing the seed-per-point contract.
- [ ] Regenerate module inventory.

## Acceptance criteria
- [ ] One variant marked default.
- [ ] Analytic test passes with documented sample budget.
- [ ] Determinism test passes under thread-count variation.
- [ ] Public API exposes only `glm` + scalar types (no Eigen / runtime types).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'WalkOnSpheres|SDF|Raycast' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No GPU backend before CPU reference parity.
- No neural / learned guiding in this task.
- No global per-thread RNG state; RNG must be derivable from `(seed, point_index, walk_index, step_index)`.
- No use of `std::rand` / `rand()` / `std::default_random_engine` (non-portable).
