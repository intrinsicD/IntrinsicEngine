---
id: METHOD-024
theme: I
depends_on: [GEOM-063, GEOM-024]
maturity_target: CPUContracted
---
# METHOD-024 — Spectral Conformal Parameterization (SCP) reference backend

## Goal
- Add the Spectral Conformal Parameterization (SCP) variant to the shared parameterization surface: a free-boundary conformal map that pins **no** vertices — it is recovered as the smallest non-trivial generalized eigenvector of the conformal-energy matrix against a boundary-area matrix — so the family gains a state-of-the-art conformal strategy with lower distortion and no pin-placement artifacts compared to two-point-pinned LSCM.

## Non-goals
- No parallel parameterization family — SCP adds a typed `ScpParams`
  alternative to the `Geometry.Parameterization` strategy variant from
  `GEOM-063`.
- No private eigensolver — the generalized symmetric eigenproblem is solved through the `GEOM-024` `Geometry` eigensolver seam; SCP must not embed a private Spectra/LOBPCG path.
- No optimized or GPU backend — none is planned for this strategy: the map is a one-shot sparse generalized eigensolve, and the family GPU task `METHOD-026` deliberately covers only the iterative ARAP/SLIM strategies. A future GPU need opens its own method/backend task.
- No cone/cutting change.

## Context
- Paper/method: Mullen, Tong, Alliez & Desbrun, "Spectral Conformal Parameterization", Computer Graphics Forum 27(5), SGP 2008. SCP minimizes the LSCM conformal energy `E_C = E_D − A` (Dirichlet energy minus signed UV area) subject to a boundary-normalization constraint, which is a generalized eigenproblem `E_C x = λ B x` with `B` a boundary mass matrix; the smallest non-trivial eigenvector is the conformal map. Because no vertices are pinned, the map is free of the pin-induced distortion that LSCM incurs.
- Method package: `methods/geometry/spectral_conformal/` (manifest-only; id `geometry.spectral_conformal`), following the `signed_heat` pattern — the reference lives in the shared `src/geometry` `Geometry.Parameterization` module.
- Surface gate: `GEOM-063` supplies the typed variant and shared
  result/diagnostics. This task defines `ScpParams`, adds it to the variant, and
  implements its visitor branch. Solver gate: `GEOM-024` supplies the sparse
  symmetric generalized eigensolver used by the conformal-energy problem.
- LSCM (`Lscm` strategy) is the parity companion: on a well-conditioned disk SCP and LSCM agree up to a similarity transform, and SCP reports conformal distortion at or below LSCM. New SOTA pack recorded in `docs/architecture/parameterization-mapping-roadmap.md`.

## Control surfaces
- Config/UI/Agent: none new — `ScpParams` becomes a real strategy alternative;
  `RUNTIME-176` / `UI-036` own its stable config token and editor controls.

## Backends
- Backend axis: `cpu_reference` only. No `gpu_vulkan_compute` backend is planned — `METHOD-026` covers only the iterative strategies. The eigensolve is a one-shot spectral solve, so no `cpu_optimized` is planned either unless a benchmark justifies it.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/spectral_conformal/`.
- [ ] Fill `method.yaml` (`id: geometry.spectral_conformal`; `backends: [cpu_reference]`; metrics: `mean_conformal_distortion`, `max_conformal_distortion`, `flipped_element_count`, `eigen_iterations`, `runtime_ms`). `correctness_tests`/`benchmarks` resolve to real paths (or `TODO:`/`TBD`).
- [ ] Fill `paper.md` with the claim capture (conformal-energy generalized eigenproblem, pin-free map, lower distortion than pinned LSCM, deterministic spectral solve).
- [ ] Implement the `Scp` strategy body in `Geometry.Parameterization`: assemble the conformal-energy matrix `E_C` and the boundary-area matrix `B` as `Geometry.Sparse` structures, request the smallest non-trivial generalized eigenpair through `GEOM-024`, deflate the constant/translation null space deterministically, and normalize the resulting UVs.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts (seeded eigensolver init + fixed sign/normalization convention).
- [ ] Fail-closed on non-disk topology, non-triangle faces, empty/degenerate meshes, non-finite input, and eigensolver non-convergence (explicit status; no arbitrary projection).

## Tests
- [ ] `tests/unit/geometry/Test.SpectralConformalParameterization.cpp` (`unit;geometry`).
- [ ] LSCM parity: on a well-conditioned disk fixture SCP matches `Lscm` up to a similarity transform (aligned distortion within tolerance) and reports conformal distortion at or below LSCM.
- [ ] Pin-free advantage: on a fixture where LSCM's forced pins induce distortion, SCP reports strictly lower max conformal distortion.
- [ ] Flip-free on a convex fixture (`FlippedElementCount == 0`).
- [ ] Determinism (including eigenvector sign/normalization convention) and fail-closed cases as listed above.

## Docs
- [ ] `methods/geometry/spectral_conformal/README.md` with a backend-status table (`cpu_reference` → `METHOD-024`; optimized/GPU → none planned, recorded decision), guidance on SCP vs LSCM/BFF (pin-free conformal vs pinned conformal vs controllable conformal), and known limitations (spectral cost, conformal-only).
- [ ] Note the `Scp` strategy and parameters in the `Geometry.Parameterization` interface docs; add the SCP pack to `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Smoke benchmark manifest `benchmarks/geometry/manifests/spectral_conformal_reference_smoke.yaml` (`benchmark_id: geometry.spectral_conformal.smoke`); metrics restricted to the benchmark enum (`runtime_ms`, `quality_error_l2`).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `ScpParams` is selectable on the shared typed strategy variant; the map
      is pin-free and matches LSCM up to a similarity on the parity fixture.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records; the `GEOM-024` eigensolver backs the spectral solve (no private eigen code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SpectralConformal|Parameterization|Eigen' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No private generalized-eigensolver re-implementation (consume `GEOM-024`).
- No GPU backend before reference parity; no `std::rand` or global RNG state.
- No arbitrary projection fallback; no Eigen types on the public geometry surface.

## Maturity
- Target: `CPUContracted` for the `Scp` reference strategy.
- `Operational` owned by `RUNTIME-176`/`UI-036` — the shared facade, config lane, and panel make every implemented strategy reachable in `Engine::Run()`, including `Scp` once this task lands. No GPU follow-up is owed: `METHOD-026` covers only the iterative strategies, and this one-shot spectral strategy stays CPU-only by recorded decision.
- Blocked until `GEOM-024` retires (promote `GEOM-024` when SCP or `METHOD-006` is the next-priority spectral method).
