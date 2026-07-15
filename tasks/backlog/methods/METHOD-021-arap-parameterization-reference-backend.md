---
id: METHOD-021
theme: I
depends_on: [GEOM-063, GEOM-064]
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
- No optimized/GPU backend before reference parity (`METHOD-025`/`METHOD-026`); no atlas/cutting change.

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
- Backend axis: `cpu_reference` only. `cpu_optimized` (progressive acceleration) deferred to `METHOD-025`; `gpu_vulkan_compute` deferred to `METHOD-026`.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/arap_parameterization/`.
- [ ] Fill `method.yaml` (`id: geometry.arap_parameterization`; `backends: [cpu_reference]`; metrics: `mean_symmetric_dirichlet`, `max_symmetric_dirichlet`, `flipped_element_count`, `iterations`, `runtime_ms`). `correctness_tests`/`benchmarks` resolve to real paths (or a `TODO:`/`TBD` prefix).
- [ ] Fill `paper.md` with the claim capture (local/global alternation, ARAP energy monotonic decrease, near-isometric result on developable input, fast convergence in a few iterations).
- [ ] Add `ArapParams` to `ParameterizationStrategy` and implement its visitor
      branch: Tutte initialization via the surface, then a bounded local/global
      loop using GEOM-064 rotation fit + cotangent SPD proxy solve, stopping on
      the tolerance/iteration fields owned by `ArapParams`.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts (fixed iteration order; seeded only where initialization needs it).
- [ ] Fail-closed on non-disk topology, non-triangle faces, empty/degenerate meshes, and non-finite input, reusing the `ParameterizationStatus` states; report flipped elements rather than silently accepting a folded map.
- [ ] No `src/geometry/CMakeLists.txt` change beyond what `GEOM-063`/`GEOM-064` already registered.

## Tests
- [ ] `tests/unit/geometry/Test.ArapParameterization.cpp` (`unit;geometry`).
- [ ] Energy monotonicity: reported ARAP/symmetric-Dirichlet energy is non-increasing across local/global iterations on a bumpy-disk fixture.
- [ ] Isometry: on a developable (near-flat) fixture ARAP recovers a near-isometric map (symmetric-Dirichlet close to the analytic minimum, distortion below a documented bound).
- [ ] Free-boundary win: on a stretched-cap fixture ARAP reports lower area distortion than fixed-boundary Harmonic on the same mesh.
- [ ] Determinism and fail-closed cases as listed above; flipped-element count is reported (not asserted zero).

## Docs
- [ ] `methods/geometry/arap_parameterization/README.md` with a backend-status table (`cpu_reference` → `METHOD-021`; optimized → `METHOD-025`; GPU → `METHOD-026`), guidance on ARAP vs LSCM/SLIM (isometric vs conformal vs injective), and known limitations (possible flips on non-developable input).
- [ ] Note the `Arap` strategy and its parameters in the `Geometry.Parameterization` interface docs; tick Pack 3 in `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Smoke benchmark manifest `benchmarks/geometry/manifests/arap_parameterization_reference_smoke.yaml` (`benchmark_id: geometry.arap_parameterization.smoke`) on deterministic disk fixtures; metrics restricted to the benchmark enum (`runtime_ms`, `quality_error_l2`).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `ArapParams` is selectable on the shared typed strategy variant alongside
      the existing LSCM and Harmonic/Tutte payloads.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records; the `GEOM-064` kernels back the local/global steps (no private optimization code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'ArapParameterization|Parameterization' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No optimized/GPU backend before reference parity; no performance claims without a baseline (`METHOD-025`).
- No private local-rotation/proxy/line-search re-implementation (consume `GEOM-064`).
- No arbitrary projection fallback for unsupported topology; no `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted` for the `Arap` reference strategy.
- `Operational` owned by `METHOD-025` and `METHOD-026` after reference parity;
  those tasks extend the config/apply/view-model and panel paths delivered by
  retired `RUNTIME-176`/`UI-036` with the optimized-CPU and GPU backend choices.
