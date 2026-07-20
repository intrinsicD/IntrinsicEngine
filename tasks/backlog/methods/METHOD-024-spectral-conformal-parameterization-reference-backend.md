---
id: METHOD-024
theme: I
depends_on: [GEOM-063, GEOM-024, RUNTIME-176, UI-036]
maturity_target: CPUContracted
---
# METHOD-024 — Spectral Conformal Parameterization (SCP) reference backend

## Goal
- Add the Spectral Conformal Parameterization (SCP) variant to the shared
  parameterization surface: a free-boundary conformal map that pins **no**
  vertices. Recover the smallest positive eigenpair of the conformal-energy
  problem after the boundary-centroid/translation null space and singular
  interior block are removed (equivalently, the largest reciprocal eigenpair
  in the paper's modified formulation).

## Non-goals
- No parallel parameterization family — SCP adds a typed `ScpParams`
  alternative to the `Geometry.Parameterization` strategy variant from
  `GEOM-063`.
- No private eigensolver — the generalized symmetric eigenproblem is solved through the `GEOM-024` `Geometry` eigensolver seam; SCP must not embed a private Spectra/LOBPCG path.
- No optimized or GPU backend — none is planned for this strategy: the map is a one-shot sparse generalized eigensolve, and the family GPU task `METHOD-026` deliberately covers only the iterative ARAP/SLIM strategies. A future GPU need opens its own method/backend task.
- No cone/cutting change.

## Context
- Paper/method: Mullen, Tong, Alliez & Desbrun, "Spectral Conformal
  Parameterization", Computer Graphics Forum 27(5), SGP 2008. SCP minimizes
  the LSCM conformal energy `E_C = E_D − A` subject to zero boundary centroid
  and unit boundary moment. In the full GEP `E_C x = λ B x`, `B` is singular
  on interior vertices and the translation modes are null; the smallest
  positive eigenpair is equivalent to the largest eigenpair of the reciprocal
  modified formulation used in the paper.
- Method package: `methods/geometry/spectral_conformal/` (manifest-only; id `geometry.spectral_conformal`), following the `signed_heat` pattern — the reference lives in the shared `src/geometry` `Geometry.Parameterization` module.
- Surface gate: `GEOM-063` supplies the typed variant and shared
  result/diagnostics. This task defines `ScpParams`, adds it to the variant, and
  implements its visitor branch. Solver gate: `GEOM-024` supplies the sparse
  symmetric generalized eigensolver, but accepts only an SPD mass matrix.
  Therefore this task must construct a mathematically equivalent
  boundary-reduced/nullspace-deflated problem before calling it; passing the
  singular full boundary mass to GEOM-024 is forbidden.
- LSCM (`Lscm` strategy) is the parity companion: on a well-conditioned disk SCP and LSCM agree up to a similarity transform, and SCP reports conformal distortion at or below LSCM. New SOTA pack recorded in `docs/architecture/parameterization-mapping-roadmap.md`.

## Control surfaces
- Config/UI/Agent: none new — `ScpParams` becomes a real strategy alternative;
  this task extends the validated config/result model delivered by retired
  `RUNTIME-176` and the editor controls delivered by retired `UI-036` with the
  stable `Scp` token and its concrete parameters.

## Backends
- Backend axis: `cpu_reference` only. No `gpu_vulkan_compute` backend is planned — `METHOD-026` covers only the iterative strategies. The eigensolve is a one-shot spectral solve, so no `cpu_optimized` is planned either unless a benchmark justifies it.

## Slice plan
- **Slice A — intake/contract.** Freeze conformal-energy/boundary-mass
  conventions, the exact boundary reduction/null-space deflation, reciprocal
  versus direct eigenvalue convention, eigenvector selection/sign/
  normalization, UV/area units, fixtures, tolerances, and failure states.
- **Slice B — direct CPU reference.** Assemble/solve the generalized
  eigenproblem through GEOM-024 and prove similarity-invariant analytic
  correctness before family integration.
- **Slice C — family integration/evidence.** Add `ScpParams`, config/editor
  serialization extensions, executable correctness smoke, and docs.

## Right-sizing
- Add one typed `ScpParams` branch and consume GEOM-024 directly. Do not add a
  spectral backend abstraction, eigensolver wrapper, or family-wide backend
  token for this one-shot method.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/spectral_conformal/`.
- [ ] Fill `method.yaml` (`id: geometry.spectral_conformal`;
      `backends: [cpu_reference]`; metrics: `mean_conformal_distortion`,
      `max_conformal_distortion`, `flipped_element_count`,
      `eigen_iterations`, `runtime_ms`). `correctness_tests` and `benchmarks`
      resolve to real paths before this task can retire.
- [ ] Fill `paper.md` with the objective/equations, matrix/null-space
      conventions, UV/area units, eigenpair selection/normalization, solver
      tolerances, assumptions, and explicit failure states. Bound any
      LSCM comparison to declared fixtures rather than stating a universal
      ordering.
- [ ] Implement the `Scp` strategy body in `Geometry.Parameterization`:
      assemble full `E_C` and boundary mass `B`, eliminate/reconstruct interior
      variables and project out the two translation modes so the matrix pair
      passed to `GEOM-024` has an SPD mass, solve the smallest positive
      eigenpair (or explicitly mapped reciprocal-largest convention), and
      normalize the UVs to zero boundary centroid/unit boundary moment.
- [ ] Report the residual of the reconstructed UV vector in the original full
      constrained GEP, not only the reduced solver residual.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts (seeded eigensolver init + fixed sign/normalization convention).
- [ ] Fail-closed on non-disk topology, non-triangle faces, empty/degenerate meshes, non-finite input, and eigensolver non-convergence (explicit status; no arbitrary projection).

## Tests
- [ ] `tests/unit/geometry/Test.SpectralConformalParameterization.cpp` (`unit;geometry`).
- [ ] LSCM parity: on a well-conditioned disk fixture SCP matches `Lscm` up to a similarity transform (aligned distortion within tolerance) and reports conformal distortion at or below LSCM.
- [ ] Pin-free advantage: on a fixture where LSCM's forced pins induce distortion, SCP reports strictly lower max conformal distortion.
- [ ] Flip-free on a convex fixture (`FlippedElementCount == 0`).
- [ ] Determinism (including eigenvector sign/normalization convention) and fail-closed cases as listed above.
- [ ] Reduction contract: the mass matrix actually passed to GEOM-024 is SPD,
      the two translation modes are absent, and the reconstructed full vector
      satisfies the original GEP residual tolerance. Singular reduction fails
      closed rather than relaxing GEOM-024's contract.

## Docs
- [ ] `methods/geometry/spectral_conformal/README.md` with a backend-status table (`cpu_reference` → `METHOD-024`; optimized/GPU → none planned, recorded decision), guidance on SCP vs LSCM/BFF (pin-free conformal vs pinned conformal vs controllable conformal), and known limitations (spectral cost, conformal-only).
- [ ] Note the `Scp` strategy and parameters in the `Geometry.Parameterization` interface docs; add the SCP pack to `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Executable smoke manifest
      `benchmarks/geometry/manifests/spectral_conformal_reference_smoke.yaml`
      (`benchmark_id: geometry.spectral_conformal.reference.smoke`) on a stable
      built-in disk, with `intent: correctness`, explicit warmup/measured
      counts, metrics `runtime_ms` and `quality_error_l2`, and schema-valid
      `cpu_reference` result JSON. Similarity-aligned conformal error, flips,
      eigen residual/iterations, normalization, and status belong in
      diagnostics.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `ScpParams` is selectable on the shared typed strategy variant; the map
      is pin-free and matches LSCM up to a similarity on the parity fixture.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Emitted smoke result validates and reports conformal/eigensolver quality,
      not runtime alone.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records; the `GEOM-024` eigensolver backs the spectral solve (no private eigen code).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'SpectralConformal|Parameterization|Eigen|IntrinsicBenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 180
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
- No private generalized-eigensolver re-implementation (consume `GEOM-024`).
- No optimized/GPU backend in this task; no `std::rand` or global RNG state.
- No arbitrary projection fallback; no Eigen types on the public geometry surface.

## Maturity
- Target: `CPUContracted` for the `Scp` reference strategy.
- `Operational` owned by `RUNTIME-176` for the delivered config/runtime path
  and by `UI-036` for the Sandbox panel, extended here with `Scp`. No backend
  follow-up is owed: `METHOD-026` covers only iterative strategies, and this
  one-shot spectral strategy stays CPU-only unless benchmark evidence opens a
  separate task.
- Blocked until `GEOM-024` retires (promote `GEOM-024` when SCP or `METHOD-006` is the next-priority spectral method).
