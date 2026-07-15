---
id: METHOD-022
theme: I
depends_on: [GEOM-063, GEOM-064, METHOD-021]
maturity_target: CPUContracted
---
# METHOD-022 — SLIM locally-injective parameterization reference backend

## Goal
- Add the Scalable Locally Injective Mappings (SLIM) variant to the shared parameterization surface: a free-boundary map that minimizes the symmetric-Dirichlet distortion energy while guaranteeing local injectivity (no triangle flips) through a reweighted proxy solve and a flip-preventing line search — the modern general-purpose workhorse strategy for the family.

## Non-goals
- No parallel parameterization family — SLIM adds a typed `SlimParams`
  alternative to the `Geometry.Parameterization` strategy variant from
  `GEOM-063`.
- No private optimization math — the reference-Jacobian proxy, the symmetric-Dirichlet energy/gradient, and the injective (flip-free) line search come from `GEOM-064`; SLIM owns the reweighting schedule and convergence policy only.
- No cutting/cone insertion — SLIM operates on a given disk-topology chart; seams/cones are upstream (`Geometry.UvAtlas`, and BFF cones in `METHOD-023`).
- No optimized/GPU backend before reference parity (`METHOD-025`/`METHOD-026`).

## Context
- Paper/method: Rabinovich, Poranne, Panozzo & Sorkine-Hornung, "Scalable Locally Injective Mappings", ACM TOG 36(2), 2017. SLIM minimizes a flip-preventing distortion energy (here the symmetric Dirichlet energy, which is infinite on degenerate/flipped elements) by iteratively building a convex proxy from a per-triangle reference Jacobian, solving one weighted-Laplacian system, and taking a line search whose maximum step preserves injectivity. Given a locally injective start it stays locally injective.
- Method package: `methods/geometry/slim_parameterization/` (manifest-only; id `geometry.slim_parameterization`), following the `signed_heat` pattern — the reference lives in the shared `src/geometry` `Geometry.Parameterization` module.
- Surface gate: `GEOM-063` supplies the typed variant and shared
  result/diagnostics. This task defines `SlimParams`, adds it to the variant,
  and implements its visitor branch. Kernel gate: `GEOM-064` supplies the
  symmetric-Dirichlet energy/gradient, PSD proxy, and injective line search.
  Bootstrap remains a flip-free Tutte or METHOD-021 ARAP result obtained through
  `ParameterizeMesh`.
- ARAP (`METHOD-021`) is the parity companion: on developable input SLIM and ARAP agree within tolerance; SLIM's distinguishing guarantee is zero flips on inputs where ARAP folds. Realizes Pack 4 of the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).

## Control surfaces
- Config/UI/Agent: none new — `SlimParams` becomes a real strategy alternative;
  this task extends the validated config/result model delivered by retired
  `RUNTIME-176` and the editor controls delivered by retired `UI-036` with the
  stable `Slim` token and its concrete parameters.

## Backends
- Backend axis: `cpu_reference` only. `cpu_optimized` (progressive/Anderson acceleration) deferred to `METHOD-025`; `gpu_vulkan_compute` deferred to `METHOD-026`.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/slim_parameterization/`.
- [ ] Fill `method.yaml` (`id: geometry.slim_parameterization`; `backends: [cpu_reference]`; metrics: `mean_symmetric_dirichlet`, `max_symmetric_dirichlet`, `flipped_element_count`, `min_signed_area`, `iterations`, `runtime_ms`). `correctness_tests`/`benchmarks` resolve to real paths (or `TODO:`/`TBD`).
- [ ] Fill `paper.md` with the claim capture (flip-preventing energy, reweighted proxy, injectivity-preserving line search, scalability, robustness on high-distortion input).
- [ ] Implement the `Slim` strategy body in `Geometry.Parameterization`: flip-free initialization via the surface, then a bounded reweighting loop — build the `GEOM-064` proxy from the current reference Jacobians, solve, and advance by the `GEOM-064` injective backtracking line search — stopping on energy-delta tolerance or max iterations.
- [ ] Guarantee: on a locally injective start, every iterate has zero flipped elements (asserted in tests); report `min_signed_area` so injectivity is observable in diagnostics.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts.
- [ ] Fail-closed on non-disk topology, non-triangle faces, empty/degenerate meshes, non-finite input, and an initialization that is already flipped (explicit status; no silent acceptance of a folded map).

## Tests
- [ ] `tests/unit/geometry/Test.SlimParameterization.cpp` (`unit;geometry`).
- [ ] Injectivity: on a high-distortion fixture where fixed-boundary Harmonic and/or ARAP produce flips, SLIM produces zero flipped elements (`FlippedElementCount == 0`, `min_signed_area > 0`).
- [ ] Energy monotonicity: reported symmetric-Dirichlet energy is non-increasing across iterations and converges.
- [ ] ARAP parity: on a developable fixture SLIM and `METHOD-021` ARAP agree within a documented tolerance.
- [ ] Distortion: SLIM symmetric-Dirichlet mean falls under a documented bound on a standard curved-cap fixture.
- [ ] Determinism and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/slim_parameterization/README.md` with a backend-status table (`cpu_reference` → `METHOD-022`; optimized → `METHOD-025`; GPU → `METHOD-026`), guidance on when to pick SLIM (default low-distortion + guaranteed-injective) vs ARAP/LSCM/BFF, and known limitations (cost per iteration, dependence on an injective start).
- [ ] Note the `Slim` strategy and parameters in the `Geometry.Parameterization` interface docs; tick Pack 4 in `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Smoke benchmark manifest `benchmarks/geometry/manifests/slim_parameterization_reference_smoke.yaml` (`benchmark_id: geometry.slim_parameterization.smoke`); metrics restricted to the benchmark enum (`runtime_ms`, `quality_error_l2`).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `SlimParams` is selectable on the shared typed strategy variant and
      produces a locally injective map from an injective start on all fixtures.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records; the `GEOM-064` kernels back the proxy/energy/line-search (no private optimization code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SlimParameterization|Parameterization' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance/scalability claim without a baseline (`METHOD-025`).
- No private energy/proxy/line-search re-implementation (consume `GEOM-064`).
- No silent acceptance of a flipped/folded map; no `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Slim` reference strategy.
- `Operational` owned by `METHOD-025` and `METHOD-026` after reference parity;
  those tasks extend the config/apply/view-model and panel paths delivered by
  retired `RUNTIME-176`/`UI-036` with the optimized-CPU and GPU backend choices.
