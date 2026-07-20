---
id: METHOD-021
theme: I
depends_on: [GEOM-063, GEOM-064, RUNTIME-176, UI-036]
maturity_target: CPUContracted
---
# METHOD-021 — ARAP (local/global) parameterization reference backend

## Goal
- Add the as-rigid-as-possible (ARAP) variant to the shared parameterization surface: a free-boundary, low-isometric-distortion UV map computed by alternating a per-triangle local rotation fit with a global cotangent-Laplacian Poisson solve, so the family gains its first isometric (length-preserving) strategy alongside the existing conformal (LSCM) and fixed-boundary (Tutte/Harmonic) strategies.

## Non-goals
- No parallel parameterization family — ARAP adds a typed `ArapParams`
  alternative to the `Geometry.Parameterization` strategy variant introduced by
  `GEOM-063`.
- No private optimization math — the local rotation fit and the SPD global proxy come from `GEOM-064` (`Geometry.Parameterization.Optimize`); ARAP owns only the outer local/global schedule and initialization.
- No injectivity guarantee — ARAP may flip on hard inputs; the flip-free guarantee is SLIM (`METHOD-022`). ARAP reports flipped-element counts honestly.
- No optimized or GPU backend before reference parity; no atlas/cutting change.
  `METHOD-026` owns GPU parity. No optimized ARAP task exists: a future one
  must name an ARAP-objective-preserving acceleration and earn its own gate.

## Context
- Paper/method: Liu, Zhang, Xu, Gotsman & Gortler, "A Local/Global Approach to Mesh Parameterization", Computer Graphics Forum 27(5), SGP 2008. The map minimizes an ARAP (rigid) energy: each local step fits the closest rotation to every triangle's 2D Jacobian; each global step solves one cotangent-weighted Poisson system for the UVs given those target rotations. Energy is non-increasing across iterations.
- Method package: `methods/geometry/arap_parameterization/` (manifest-only; id `geometry.arap_parameterization`), following the `signed_heat` pattern — the executable reference lives in the shared `src/geometry` `Geometry.Parameterization` module, not in the package.
- Surface gate: `GEOM-063` supplies the typed strategy variant,
  `ParameterizeResult`, and normalized GEOM-018 diagnostics. This task defines
  `ArapParams`, adds it to the variant, and implements its visitor branch.
  Kernel gate: `GEOM-064` supplies `FitLocalRotations` (local step) and the
  cotangent SPD proxy solve (global step); ARAP must not re-implement either.
- Initialization uses a valid flip-free embedding from the same surface — Tutte (`HarmonicWeightType::Uniform`) on a convex boundary — obtained through `ParameterizeMesh`, so ARAP reuses the family's own bootstrap rather than a private one.
- Realizes Pack 3 of the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).

## Control surfaces
- Config/UI/Agent: none new — `ArapParams` becomes a real strategy alternative;
  this task extends the validated config/result model delivered by retired
  `RUNTIME-176` and the editor controls delivered by retired `UI-036` with the
  stable `Arap` token and its concrete parameters.

## Backends
- Backend axis: `cpu_reference` only. No `cpu_optimized` token is planned;
  `gpu_vulkan_compute` is deferred to `METHOD-026`.

## Slice plan
- **Slice A — intake/contract.** Freeze ARAP energy/Jacobian conventions,
  initialization, free-boundary translation/rotation gauge, coordinate/area
  units, stopping rules, diagnostics, fixtures, and tolerances before
  implementation.
- **Slice B — CPU reference.** Land the bounded local/global loop against
  GEOM-064 with analytic energy/isometry tests.
- **Slice C — family integration/evidence.** Add the typed strategy branch,
  executable correctness smoke, config serialization extension, and docs only
  after the direct reference is stable.

## Right-sizing
- Add one typed `ArapParams` branch to the existing strategy variant; do not
  create a parameterization backend interface, optimizer service, or private
  kernel layer.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/arap_parameterization/`.
- [ ] Fill `method.yaml` (`id: geometry.arap_parameterization`;
      `backends: [cpu_reference]`; metrics: `arap_energy`,
      `mean_symmetric_dirichlet`, `max_symmetric_dirichlet`,
      `flipped_element_count`, `iterations`, `runtime_ms`).
      `correctness_tests` and `benchmarks` resolve to real paths before this
      task can retire.
- [ ] Fill `paper.md` with the objective/equations, local-frame/Jacobian and UV
      units, initialization and stopping rules, diagnostics, assumptions,
      numerical tolerances, and explicit failure states. Do not encode an
      unmeasured convergence-speed claim.
- [ ] Add `ArapParams` to `ParameterizationStrategy` and implement its visitor
      branch: Tutte initialization via the surface, then a bounded local/global
      loop using GEOM-064 rotation fit + cotangent SPD proxy solve, stopping on
      the tolerance/iteration fields owned by `ArapParams`.
- [ ] Remove the free-boundary null modes with one deterministic,
      shape-neutral gauge convention (centroid at the origin plus a fixed
      orientation/sign rule). Do not pin arbitrary boundary vertices or let
      sparse-solver null-space behavior choose the pose.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts (fixed iteration order; seeded only where initialization needs it).
- [ ] Fail-closed on non-disk topology, non-triangle faces, empty/degenerate meshes, and non-finite input, reusing the `ParameterizationStatus` states; report flipped elements rather than silently accepting a folded map.
- [ ] No `src/geometry/CMakeLists.txt` change beyond what `GEOM-063`/`GEOM-064` already registered.

## Tests
- [ ] `tests/unit/geometry/Test.ArapParameterization.cpp` (`unit;geometry`).
- [ ] Energy monotonicity: the actual reported ARAP objective is
      non-increasing across local/global iterations on a bumpy-disk fixture.
      Symmetric-Dirichlet distortion is reported separately and is not
      incorrectly asserted to be the optimized objective.
- [ ] Isometry: on a developable (near-flat) fixture ARAP recovers a near-isometric map (symmetric-Dirichlet close to the analytic minimum, distortion below a documented bound).
- [ ] Free-boundary win: on a stretched-cap fixture ARAP reports lower area distortion than fixed-boundary Harmonic on the same mesh.
- [ ] Determinism and fail-closed cases as listed above; flipped-element count is reported (not asserted zero).

## Docs
- [ ] `methods/geometry/arap_parameterization/README.md` with a backend-status
      table (`cpu_reference` → `METHOD-021`; optimized → none planned; GPU →
      `METHOD-026`), guidance on ARAP vs LSCM/SLIM, and known limitations
      (possible flips on non-developable input). A future optimized backend
      requires an ARAP-objective-preserving method task.
- [ ] Note the `Arap` strategy and its parameters in the `Geometry.Parameterization` interface docs; tick Pack 3 in `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Executable smoke manifest
      `benchmarks/geometry/manifests/arap_parameterization_reference_smoke.yaml`
      (`benchmark_id: geometry.arap_parameterization.reference.smoke`) on a
      stable built-in disk fixture, with `intent: correctness`, explicit
      warmup/measured counts, metrics `runtime_ms` and `quality_error_l2`, and
      schema-valid `cpu_reference` result JSON. Energy/isometry/distortion,
      flips, iterations, initialization, and status belong in diagnostics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `ArapParams` is selectable on the shared typed strategy variant alongside
      the existing LSCM and Harmonic/Tutte payloads.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Emitted smoke result validates and reports energy/distortion quality and
      convergence diagnostics, not runtime alone.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records; the `GEOM-064` kernels back the local/global steps (no private optimization code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'ArapParameterization|Parameterization|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/agents/validate_method_manifests.py
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/validate_tasks.py --root tasks --strict
```

## Forbidden changes
- No GPU backend before reference parity; no `cpu_optimized` token without a
  separate ARAP-objective-preserving method and paired evidence.
- No private local-rotation/proxy/line-search re-implementation (consume `GEOM-064`).
- No arbitrary projection fallback for unsupported topology; no `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Arap` reference strategy.
- `Operational` owned by `RUNTIME-176` for the delivered config/runtime path
  and by `UI-036` for the Sandbox panel, extended here with `Arap`.
  GPU parity is owned by `METHOD-026`; no optimized CPU path is currently
  planned.
