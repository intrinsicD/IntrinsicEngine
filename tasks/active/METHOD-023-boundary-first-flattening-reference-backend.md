---
id: METHOD-023
theme: I
depends_on: [GEOM-063]
maturity_target: CPUContracted
---
# METHOD-023 — Boundary First Flattening (BFF) reference backend

## Goal
- Add the Boundary First Flattening (BFF) disk variant to the shared
  parameterization surface: a deterministic conformal flattening that accepts
  automatic free-boundary data, approximate target boundary lengths, or target
  boundary exterior angles through a typed CPU-reference strategy.

## Non-goals
- No parallel parameterization family — BFF adds a typed `BffParams`
  alternative to the `Geometry.Parameterization` strategy variant from
  `GEOM-063`.
- No cone singularities or automatic cone placement. Paper-faithful cone BFF
  first cuts the mesh through the cones and duplicates seam/corner UV degrees
  of freedom; the current one-UV-per-mesh-vertex result cannot represent that
  output. Cone support may be reintroduced only with a seam-aware chart/cut
  result contract rather than approximated as an interior right-hand side.
- No optimized or GPU backend. The right-sized reference uses separate
  Dirichlet and grounded-Neumann factorizations plus the required backsolves;
  sparse subfactor extraction is deferred until benchmark evidence justifies
  that optimization. `METHOD-026` deliberately covers only iterative
  ARAP/SLIM strategies.
- No isometric/injective optimization (that is ARAP/SLIM).

## Context
- Paper/method: Sawhney & Crane, "Boundary First Flattening", ACM TOG
  37(1), Article 5, December 2017, DOI `10.1145/3132705`. BFF reduces disk
  flattening to the boundary through the
  Dirichlet-to-Neumann operator: boundary scale factors or boundary exterior
  angles determine the conjugate quantity, a best-fit planar boundary curve is
  reconstructed, and the interior is extended with cotangent-Laplacian solves.
  Arbitrary exact edge lengths are generally not integrable as vertex scale
  factors, so this task's `TargetLengths` mode uses the paper's documented
  approximate conversion and reports the achieved boundary-length error.
- Method package: `methods/geometry/boundary_first_flattening/` (manifest-only; id `geometry.boundary_first_flattening`), following the `signed_heat` pattern — the reference lives in the shared `src/geometry` `Geometry.Parameterization` module.
- Surface gate: `GEOM-063` supplies the typed variant and shared
  result/diagnostics. This task defines `BffParams`, adds it to the variant, and
  implements its visitor branch. It reuses the cotangent Laplacian/mass
  operators, `Geometry.Sparse::SparseLDLT`, and boundary-loop extraction.
- Control model: `BffParams` carries
  `BffBoundaryMode { AutomaticConformal, TargetLengths, TargetAngles }` and one
  boundary-order target array for non-automatic modes. `TargetLengths` contains
  one positive length per boundary edge; `TargetAngles` contains one finite
  exterior angle per boundary vertex and must sum to `2*pi` within the
  documented tolerance. `RUNTIME-176` serializes these concrete controls after
  this task lands.
- New SOTA pack beyond the original roadmap; recorded as an added pack in `docs/architecture/parameterization-mapping-roadmap.md`.

## Control surfaces
- Config/UI/Agent: none new in this task. The config-lane serialization and
  interactive editor controls are owned by `RUNTIME-176` / `UI-036`.

## Backends
- Backend axis: `cpu_reference` only. No `gpu_vulkan_compute` backend is planned — `METHOD-026` covers only the iterative strategies. The interior solves are linear and already fast, so no `cpu_optimized` is planned either unless a benchmark justifies it.

## Status
- Implementation and evidence complete on branch
  `codex/arch-006-completion`; owner: Codex. Ready to retire at
  `CPUContracted`.
- Dependency `GEOM-063` is retired. Paper intake and repository-seam audit are
  complete; cone and exact-length overclaims were removed before code landed.
- Slices 1-3 are complete: paper-derived contract, CPU reference, analytic and
  fail-closed tests, executable smoke benchmark, docs, and generated inventory.
- Independent implementation and benchmark reviews are resolved. Focused,
  benchmark, structural, and post-review default CPU gates pass.

## Required changes
- [x] Instantiate `methods/_template/` as
      `methods/geometry/boundary_first_flattening/` without introducing a
      parallel implementation target.
- [x] Fill `method.yaml` (`id: geometry.boundary_first_flattening`;
      `backends: [cpu_reference]`; metrics: `mean_conformal_distortion`,
      `max_conformal_distortion`, `boundary_length_error`, `runtime_ms`).
      `correctness_tests`/`benchmarks` resolve to real paths.
- [x] Fill `paper.md` with bounded claim capture: Dirichlet-to-Neumann
      boundary reduction, automatic scale factors, approximate length control,
      exterior-angle control, best-fit closure, and harmonic/holomorphic
      extension. Record cone cutting as unsupported by the current result
      shape.
- [x] Add `BffBoundaryMode`, `BffParams`, a detailed direct-result status, and
      `ComputeBFF` to `Geometry.Parameterization`; add `BffParams` to the
      existing typed strategy variant and map the direct status into the shared
      dispatch result.
- [x] Implement the disk BFF reference: assemble the cotangent Laplacian,
      apply Dirichlet-to-Neumann or Neumann-to-Dirichlet conversion, construct
      target boundary lengths, close the curve with the paper's weighted
      best-fit projection, and recover the interior with
      `Geometry.Sparse::SparseLDLT`. Use harmonic extension for prescribed
      sharp target angles and the holomorphic extension for automatic/length
      modes.
- [x] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs
      across repeated calls under the supported preset's single-threaded sparse
      solver path; cover all three boundary modes.
- [x] Fail closed with an explicit direct status on non-disk topology,
      non-triangle faces, empty/degenerate meshes, non-finite input, a target
      array whose length differs from the ordered boundary count, non-positive
      target lengths, target angles whose sum differs from `2*pi`, singular
      systems, and unusable diagnostics. No arbitrary projection fallback.

## Tests
- [x] `tests/unit/geometry/Test.BoundaryFirstFlattening.cpp` (`unit;geometry`).
- [x] Conformality: analytic planar and curved-disk fixtures produce finite,
      flip-free output under pinned absolute distortion bounds. Do not assert a
      BFF-versus-LSCM ordering that the paper does not guarantee.
- [x] Length control: the paper's approximate target-length conversion and
      best-fit closure produce a finite deterministic boundary with documented
      relative RMS `boundary_length_error`; a uniformly scaled planar fixture
      meets a tight bound and a non-integrable rectangle request reports its
      nonzero achieved error rather than claiming exact satisfaction.
- [x] Angle control: with `TargetAngles` prescribing four 90° corners on a square-cap fixture, the flattened boundary has the prescribed corner angles within tolerance.
- [x] Determinism and fail-closed cases (mismatched target array, invalid
      lengths, non-finite targets, and exterior-angle sum inconsistency) as
      listed above.

## Docs
- [x] `methods/geometry/boundary_first_flattening/README.md` with a
      backend-status table (`cpu_reference` -> `METHOD-023`; optimized/GPU ->
      none planned), control-mode guidance, and limitations (length targets are
      approximate; no cone/cut support; conformal, not area preserving).
- [x] Note the BFF strategy and modes in `Geometry.Parameterization` interface
      docs; add the bounded BFF pack to
      `docs/architecture/parameterization-mapping-roadmap.md`.
- [x] Add executable benchmark workload + header, runner registration, CMake
      registration, and manifest
      `benchmarks/geometry/manifests/boundary_first_flattening_reference_smoke.yaml`
      (`benchmark_id: geometry.boundary_first_flattening.smoke`). Use a
      deterministic built-in disk fixture, explicit warmup/measured counts,
      mean `runtime_ms`, worst measured conformal `quality_error_l2`, a runtime
      cap, failed-measured-iteration count, and success aggregated over every
      measured iteration.
- [x] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [x] `BffParams` is selectable on the shared typed strategy variant with
      `AutomaticConformal`, approximate `TargetLengths`, and `TargetAngles`
      modes; no unsupported cone payload is exposed.
- [x] Boundary length/angle control is verified against documented attainable
      tolerances; automatic mode meets absolute finite, flip, and distortion
      bounds on analytic fixtures.
- [x] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [x] Public API exposes only `std`/`glm`/geometry-owned records (no Eigen); layering holds (`geometry -> core`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'BoundaryFirstFlattening|Parameterization' --timeout 120
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -R 'IntrinsicBenchmarkSmoke.(Run|Validate)' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Evidence captured on 2026-07-15:

- `cmake --build --preset ci --target IntrinsicTests IntrinsicBenchmarkSmoke -j 4`
  completed with the Clang module toolchain.
- The exact post-review selection
  `BoundaryFirstFlattening|Parameterization|IntrinsicBenchmarkSmoke.(Run|Validate)`
  passed 43/43; the smoke run and result validation both passed.
- The default CPU-supported gate passed 3,726/3,726 in 374.73 seconds.
- Strict method and benchmark manifest validation passed for 9 and 25 files;
  task policy/state, task validation, layering, test layout, doc links, and
  `git diff --check` reported no findings.

## Forbidden changes
- No cone/cut implementation or automatic cone-placement optimizer.
- No GPU backend before reference parity; no `std::rand` or global RNG state.
- No cone approximation without a seam-aware cut/result contract; no arbitrary
  projection fallback for unsupported topology or inconsistent target angles;
  no Eigen types on the public surface.

## Maturity
- Target: `CPUContracted` for the `Bff` reference strategy.
- `Operational` owned by `RUNTIME-176`; visible editor operation is owned by
  `UI-036`. No GPU follow-up is owed: `METHOD-026` covers only iterative
  strategies, and this
  one-shot linear strategy stays CPU-only unless benchmark evidence justifies a
  separately scoped backend task.
