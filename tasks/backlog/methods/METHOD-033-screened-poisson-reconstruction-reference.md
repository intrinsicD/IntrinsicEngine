---
id: METHOD-033
theme: I
depends_on: []
maturity_target: CPUContracted
workflow_schema: 1
workflow_profile: claim-grade
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [geometry.element-domain-sources, method.engine-integration]
---
# METHOD-033 — Screened Poisson surface reconstruction reference backend

## Goal
- Add a CPU reference screened Poisson surface reconstruction: splat oriented per-point normals into a uniform-grid vector field, solve the screened Poisson equation for the indicator function, and extract the iso-surface as a watertight triangle mesh. Standalone reconstruction capability complementing the existing Hoppe SDF path, and the inner solver required by the iPSR orientation baseline (`METHOD-034`) in the `METHOD-032` publication track.

## Non-goals
- No adaptive-octree discretization — the reference solves on a uniform `Grid::DenseGrid` (memory `O(res^3)`, documented); the adaptive/optimized solver is a later task and is not owed by this one.
- No GPU backend, no multigrid performance work; fixed-iteration CG only.
- No changes to the existing `Geometry.SurfaceReconstruction` Hoppe path — both reconstruction methods coexist.
- No normal estimation or orientation. Standalone reconstruction-quality
  assertions require the declared oriented-input fixture contract; finite,
  count-matched nonzero but inconsistently directed normals may be accepted
  as explicitly contracted iterative intermediates for METHOD-034. That does
  not imply a correct or watertight reconstruction from arbitrary directions.

## Context
- Paper/method: Kazhdan, Bolitho, Hoppe — "Poisson Surface Reconstruction", SGP 2006; Kazhdan, Hoppe — "Screened Poisson Surface Reconstruction", TOG 2013.
- Method package: `methods/geometry/screened_poisson/`.
- Public module in `src/geometry` (engine-grade capability with two present consumers: engine reconstruction users and `METHOD-034`): `Geometry.SurfaceReconstruction.Poisson`.
- Reference simplification: uniform-grid finite-difference discretization instead of the papers' adaptive octree FEM — clarity over scale; fidelity implications documented in the package README.
- Reuse: `Geometry.Grid` (`DenseGrid`) for the discretization, `Geometry.MarchingCubes` for iso-extraction, `Geometry.KDTree` for splatting neighborhoods, `Geometry.PointCloud.SurfaceSampling` for fixtures. The exported `Geometry.LinearSolver`/`Geometry.Sparse` surface targets assembled sparse systems; if it does not fit the grid solve, a method-internal fixed-iteration CG over the grid stencil is in-contract (documented choice, not exported).
- Operator decision (2026-09-05): reconcile METHOD-033/METHOD-034 through
  primary-source intake before either implementation. Prefer one shared
  reconstruction primitive whose accepted iterative input and output/failure
  states are explicit; do not add a bypass, duplicate solver, or an untested
  unconditional oriented-input rejection. The uniform-grid simplification's
  suitability for iPSR remains a question for that intake, not an established
  fidelity result.

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Paired finite position/normal `glm::vec3` properties or spans, count-matched on one resolved element domain; no topology or point-cloud provenance requirement. |
| Compatible entity sources | Compatible mesh vertex/face/edge/halfedge, graph node/edge/halfedge, and point-cloud point properties. Runtime binding is deferred to METHOD-033A. |
| RuntimeModule | Deferred to METHOD-033A; METHOD-034 directly consumes the CPU API as a separate research baseline. |
| Config/agent | Typed Poisson params now; validated serializable runtime controls deferred to METHOD-033A. |
| UI | Deferred to METHOD-033A; discovery must use the same property preflight on every compatible source. |
| Publication | Return a new float-position mesh. Never overwrite the richer source entity/property domains; explicit runtime ownership and history are deferred to METHOD-033A. |
| End-to-end tests | CPU quality/failure tests and METHOD-034 intermediate-input compatibility here; METHOD-033A owns planning and task allocation for engine publication/control-surface tests. |

## Required changes
- [ ] Before implementation, review both original reconstruction papers and
      the iPSR paper plus relevant follow-ups. Freeze shared input validity,
      iterative-intermediate semantics, solver/extraction failure statuses,
      and the bounded standalone quality claims. Confirm the proposed uniform
      grid is a compatible baseline or revise the two notes before coding.
- [ ] Clone `methods/_template/` to `methods/geometry/screened_poisson/`; fill `method.yaml` (`id: geometry.screened_poisson`; status `reference`; metrics: `reconstruction_mean_distance`, `watertight_fraction`, `genus_match`, `solver_residual`, `runtime_ms`) and `paper.md`.
- [ ] Add module `Geometry.SurfaceReconstruction.Poisson` (`.cppm` + `.cpp`): `PoissonParams` (grid resolution, screening weight, splat radius factor, iso value policy, CG iteration count/tolerance), `Reconstruct(points, normals, params)` returning mesh plus diagnostics (residual, iterations, grid occupancy, non-finite/rejected input counts).
- [ ] Deterministic: fixed iteration counts and traversal order; identical `(input, params)` produce bitwise-identical output.
- [ ] Fail-closed with explicit statuses: empty/too-small input, missing/count-mismatched/zero-length normals, non-finite data, resolution bounds exceeded.
- [ ] Register the module in `src/geometry/CMakeLists.txt` (alphabetical, no new link dependency).
- [ ] Smoke benchmark manifest on synthetic fixtures using existing permitted
      metrics with explicitly defined quality-error formulas. Put
      reconstruction distance, watertight/genus checks, and solver residual
      in structured diagnostics; do not widen the global metric allowlist
      merely to match a package-local result name.

## Tests
- [ ] `tests/unit/geometry/Test.SurfaceReconstructionPoisson.cpp` with `unit;geometry` labels.
- [ ] Sphere and torus fixtures (ground-truth oriented samples via `SampleTriangleMeshSurface`): closed two-manifold output, mean input-to-mesh distance under a documented bound, Euler-characteristic genus check (sphere χ=2, torus χ=0).
- [ ] Determinism and all fail-closed cases listed above.
- [ ] A seeded inconsistent-normal fixture reaches the shared iterative-input
      path without an orientation-only precondition rejection. Pin its actual
      output or explicit solver/extraction failure; do not require arbitrary
      random normals to produce a valid final surface. Count/finite/zero-norm
      checks remain mandatory.

## Docs
- [ ] `methods/geometry/screened_poisson/README.md` — parameter guidance and limitations (uniform-grid memory scaling, no adaptivity, screening simplifications vs the 2013 paper).
- [ ] Regenerate the module inventory.

## Acceptance criteria
- [ ] Reference implementation present, deterministic, fail-closed per the listed statuses; tests pass in the default CPU gate.
- [ ] `method.yaml` validates; benchmark smoke manifest validates and runs with quality metrics.
- [ ] Public API type discipline: `std`/`glm`/scalar types plus the engine's own geometry types (mesh/cloud results in-contract); no third-party or method-internal solver types exported.
- [ ] METHOD-034's initial input is covered by the frozen shared contract,
      without claiming its intermediate reconstruction has final quality.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Poisson|SurfaceReconstruction' --timeout 120
python3 tools/agents/validate_method_manifests.py
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No behavior changes to the existing `Geometry.SurfaceReconstruction` (Hoppe) module.
- No performance claims without baseline comparison; no external datasets in smoke tests.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` (reference correctness under the default CPU gate).
- No `Operational` follow-up is owed by this task; an adaptive/optimized solver opens as a separate task only after reference evidence exists.
