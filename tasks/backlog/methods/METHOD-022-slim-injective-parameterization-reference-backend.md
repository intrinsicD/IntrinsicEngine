---
id: METHOD-022
theme: I
depends_on: [GEOM-063, GEOM-064, METHOD-021, RUNTIME-176, UI-036]
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
- Paper/method: Rabinovich, Poranne, Panozzo & Sorkine-Hornung, "Scalable
  Locally Injective Mappings", ACM TOG 36(2), 2017. This contract uses the
  paper's orientation-aware extension of symmetric Dirichlet: the usual
  finite expression for `det(J) > 0` and `+∞` otherwise. An implementation
  must not evaluate only singular values and accidentally accept a reflected
  triangle. SLIM iteratively builds a convex proxy, solves one
  weighted-Laplacian system, and takes a line search whose maximum step
  preserves positive signed area. Given a locally injective start, every
  accepted iterate remains locally injective.
- Method package: `methods/geometry/slim_parameterization/` (manifest-only; id `geometry.slim_parameterization`), following the `signed_heat` pattern — the reference lives in the shared `src/geometry` `Geometry.Parameterization` module.
- Surface gate: `GEOM-063` supplies the typed variant and shared
  result/diagnostics. This task defines `SlimParams`, adds it to the variant,
  and implements its visitor branch. Kernel gate: `GEOM-064` supplies the
  symmetric-Dirichlet energy/gradient, PSD proxy, and injective line search.
  The deterministic default bootstrap is flip-free Tutte through
  `ParameterizeMesh`. An ARAP result may be accepted only after the shared
  diagnostics prove it is locally injective; ARAP is never presumed
  flip-free.
- ARAP (`METHOD-021`) is the parity companion: on developable input SLIM and ARAP agree within tolerance; SLIM's distinguishing guarantee is zero flips on inputs where ARAP folds. Realizes Pack 4 of the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).

## Control surfaces
- Config/UI/Agent: none new — `SlimParams` becomes a real strategy alternative;
  this task extends the validated config/result model delivered by retired
  `RUNTIME-176` and the editor controls delivered by retired `UI-036` with the
  stable `Slim` token and its concrete parameters.

## Backends
- Backend axis: `cpu_reference` only. A Progressive Parameterizations
  `cpu_optimized` candidate is deferred to `METHOD-025`;
  `gpu_vulkan_compute` is deferred to `METHOD-026`.

## Slice plan
- **Slice A — intake/contract.** Freeze symmetric-Dirichlet/Jacobian
  conventions, injective-start precondition, area units, stopping/line-search
  rules, fixtures, tolerances, and failures.
- **Slice B — CPU reference.** Land the direct reweighted proxy loop against
  GEOM-064 and prove per-iterate injectivity/energy behavior.
- **Slice C — family integration/evidence.** Add the typed strategy branch,
  executable correctness smoke, config serialization extension, and docs after
  the direct reference is stable.

## Right-sizing
- Add one typed `SlimParams` branch to the existing strategy variant. Reuse
  GEOM-064 directly; do not add an optimizer service, backend registry, or
  private proxy/line-search abstraction.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/slim_parameterization/`.
- [ ] Fill `method.yaml` (`id: geometry.slim_parameterization`;
      `backends: [cpu_reference]`; metrics: `mean_symmetric_dirichlet`,
      `max_symmetric_dirichlet`, `flipped_element_count`, `min_signed_area`,
      `iterations`, `runtime_ms`). `correctness_tests` and `benchmarks`
      resolve to real paths before this task can retire.
- [ ] Fill `paper.md` with the objective/equations, Jacobian/signed-area units,
      injective-start precondition, initialization/stopping/line-search rules,
      diagnostics, tolerances, and failure states. Treat scalability as a later
      benchmark question, not a reference-task claim.
- [ ] Implement the `Slim` strategy body in `Geometry.Parameterization`: flip-free initialization via the surface, then a bounded reweighting loop — build the `GEOM-064` proxy from the current reference Jacobians, solve, and advance by the `GEOM-064` injective backtracking line search — stopping on energy-delta tolerance or max iterations.
- [ ] Guarantee: on a locally injective start, every iterate has zero flipped elements (asserted in tests); report `min_signed_area` so injectivity is observable in diagnostics.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts.
- [ ] Fail-closed on non-disk topology, non-triangle faces, empty/degenerate meshes, non-finite input, and an initialization that is already flipped (explicit status; no silent acceptance of a folded map).

## Tests
- [ ] `tests/unit/geometry/Test.SlimParameterization.cpp` (`unit;geometry`).
- [ ] Injectivity: freeze a high-distortion fixture on which at least one named
      baseline (fixed-boundary Harmonic or ARAP) demonstrably flips, record
      which baseline fails, and require SLIM to produce zero flipped elements
      (`FlippedElementCount == 0`, `min_signed_area > 0`).
- [ ] Energy monotonicity: reported symmetric-Dirichlet energy is non-increasing across iterations and converges.
- [ ] ARAP parity: on a developable fixture SLIM and `METHOD-021` ARAP agree within a documented tolerance.
- [ ] Distortion: SLIM symmetric-Dirichlet mean falls under a documented bound on a standard curved-cap fixture.
- [ ] Determinism and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/slim_parameterization/README.md` with a backend-status table (`cpu_reference` → `METHOD-022`; optimized → `METHOD-025`; GPU → `METHOD-026`), guidance on when to pick SLIM (default low-distortion + guaranteed-injective) vs ARAP/LSCM/BFF, and known limitations (cost per iteration, dependence on an injective start).
- [ ] Note the `Slim` strategy and parameters in the `Geometry.Parameterization` interface docs; tick Pack 4 in `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Executable smoke manifest
      `benchmarks/geometry/manifests/slim_parameterization_reference_smoke.yaml`
      (`benchmark_id: geometry.slim_parameterization.reference.smoke`) on a
      stable built-in high-distortion disk, with `intent: correctness`,
      explicit warmup/measured counts, metrics `runtime_ms` and
      `quality_error_l2`, and schema-valid `cpu_reference` result JSON.
      Energy/distortion, min signed area, flips, iterations/line search, and
      status belong in diagnostics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `SlimParams` is selectable on the shared typed strategy variant and
      produces a locally injective map from an injective start on all fixtures.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Emitted smoke result validates and reports injectivity/distortion quality
      and convergence diagnostics, not runtime alone.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records; the `GEOM-064` kernels back the proxy/energy/line-search (no private optimization code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'SlimParameterization|Parameterization|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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
- No optimized/GPU backend before reference parity; no performance/scalability claim without a baseline (`METHOD-025`).
- No private energy/proxy/line-search re-implementation (consume `GEOM-064`).
- No silent acceptance of a flipped/folded map; no `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Slim` reference strategy.
- `Operational` owned by `RUNTIME-176` for the delivered config/runtime path
  and by `UI-036` for the Sandbox panel, extended here with `Slim`.
  Optimized/GPU parity is owned separately by `METHOD-025`/`026`.
