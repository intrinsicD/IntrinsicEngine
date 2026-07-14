---
id: METHOD-023
theme: I
depends_on: [GEOM-063]
maturity_target: CPUContracted
---
# METHOD-023 — Boundary First Flattening (BFF) reference backend

## Goal
- Add the Boundary First Flattening (BFF) variant to the shared parameterization surface: an interactive conformal (angle-preserving) flattening that lets the caller directly prescribe boundary data — either target boundary lengths or target boundary exterior angles (curvature) — with optional interior cone singularities, so the family gains the state-of-the-art *controllable* conformal strategy that maps directly onto the UI's "full control over the parameterization" requirement.

## Non-goals
- No parallel parameterization family — BFF adds a typed `BffParams`
  alternative to the `Geometry.Parameterization` strategy variant from
  `GEOM-063`.
- No automatic cone-placement solver — cones are caller-supplied here (curvature prescribed at supplied interior vertices); an automatic cone-count/placement optimizer is a named future follow-up, not this task.
- No optimized or GPU backend — none is planned for this strategy: the flattening is a pair of one-shot sparse LDLT solves, and the family GPU task `METHOD-026` deliberately covers only the iterative ARAP/SLIM strategies. A future GPU need opens its own method/backend task.
- No isometric/injective optimization (that is ARAP/SLIM).

## Context
- Paper/method: Sawhney & Crane, "Boundary First Flattening", ACM TOG 37(1), 2018. BFF reduces flattening to the boundary via the Dirichlet-to-Neumann (Poincaré–Steklov) operator: prescribing either boundary scale factors (from target lengths) or boundary geodesic curvature (from target exterior angles) determines the conjugate quantity, after which the interior is recovered by two sparse cotangent-Laplacian solves and a boundary curve is integrated. Cone singularities are prescribed interior curvature. The result is discretely conformal and the boundary is directly controllable — the property that makes it the interactive-editing method in modern tools.
- Method package: `methods/geometry/boundary_first_flattening/` (manifest-only; id `geometry.boundary_first_flattening`), following the `signed_heat` pattern — the reference lives in the shared `src/geometry` `Geometry.Parameterization` module.
- Surface gate: `GEOM-063` supplies the typed variant and shared
  result/diagnostics. This task defines `BffParams`, adds it to the variant, and
  implements its visitor branch. It reuses the cotangent Laplacian/mass
  operators, `Geometry.Sparse::SparseLDLT`, and boundary-loop extraction.
- Control model: the `Bff` strategy payload carries a `BoundaryTarget { AutomaticConformal, TargetLengths, TargetAngles }` mode, the per-boundary-vertex target array for the non-automatic modes, and a caller-supplied cone list (interior vertex ids + prescribed curvature). These are exactly the knobs `RUNTIME-176`/`UI-036` expose for interactive control; the default `AutomaticConformal` mode needs no user input and yields the free-boundary conformal disk.
- New SOTA pack beyond the original roadmap; recorded as an added pack in `docs/architecture/parameterization-mapping-roadmap.md`.

## Control surfaces
- Config/UI/Agent: none new in this task — the `BoundaryTarget` mode, boundary target arrays, and cone list are added to the `Bff` strategy payload on the existing surface. The config-lane serialization and the interactive editor controls that drive them are owned by `RUNTIME-176` / `UI-036`.

## Backends
- Backend axis: `cpu_reference` only. No `gpu_vulkan_compute` backend is planned — `METHOD-026` covers only the iterative strategies. The interior solves are linear and already fast, so no `cpu_optimized` is planned either unless a benchmark justifies it.

## Required changes
- [ ] Clone `methods/_template/` to `methods/geometry/boundary_first_flattening/`.
- [ ] Fill `method.yaml` (`id: geometry.boundary_first_flattening`; `backends: [cpu_reference]`; metrics: `mean_conformal_distortion`, `max_conformal_distortion`, `boundary_length_error`, `cone_count`, `runtime_ms`). `correctness_tests`/`benchmarks` resolve to real paths (or `TODO:`/`TBD`).
- [ ] Fill `paper.md` with the claim capture (Dirichlet-to-Neumann boundary reduction, direct length/angle boundary control, cone singularities, discrete conformality, interactivity).
- [ ] Implement the `Bff` strategy body in `Geometry.Parameterization`: assemble the cotangent Laplacian, build/apply the boundary Dirichlet-to-Neumann map, solve for the conjugate boundary quantity under the selected `BoundaryTarget` mode, recover the interior via two `SparseLDLT` solves, and integrate the boundary curve; apply supplied cone curvatures as interior boundary conditions.
- [ ] Deterministic: identical `(mesh, params)` produce bitwise-identical UVs across runs and thread counts.
- [ ] Fail-closed on non-disk topology (cones prescribe interior curvature on a disk; they do not repair topology — cutting a closed mesh through cones stays with the `Geometry.UvAtlas` charting path), non-triangle faces, empty/degenerate meshes, non-finite input, a boundary-target array whose length ≠ boundary-vertex count, and a Gauss–Bonnet-inconsistent prescribed-curvature set (explicit status; no arbitrary projection).

## Tests
- [ ] `tests/unit/geometry/Test.BoundaryFirstFlattening.cpp` (`unit;geometry`).
- [ ] Conformality: on a curved-cap fixture BFF (`AutomaticConformal`) reports low conformal distortion, at or below pinned LSCM on the same mesh.
- [ ] Length control: with `TargetLengths` prescribing a rectangle boundary, the flattened boundary edge lengths match the targets within a documented tolerance (`boundary_length_error` under bound).
- [ ] Angle control: with `TargetAngles` prescribing four 90° corners on a square-cap fixture, the flattened boundary has the prescribed corner angles within tolerance.
- [ ] Cones: prescribing a single interior cone reduces reported area distortion on a cone-like fixture versus the no-cone flattening.
- [ ] Determinism and fail-closed cases (mismatched target array length, Gauss–Bonnet-inconsistent curvatures) as listed above.

## Docs
- [ ] `methods/geometry/boundary_first_flattening/README.md` with a backend-status table (`cpu_reference` → `METHOD-023`; optimized/GPU → none planned, recorded decision), guidance on BFF's control modes and when conformal+controllable beats SLIM's isometric map, and known limitations (caller-supplied cones only; conformal, not area-preserving).
- [ ] Note the `Bff` strategy, its `BoundaryTarget` modes, and the cone list in the `Geometry.Parameterization` interface docs; add the BFF pack to `docs/architecture/parameterization-mapping-roadmap.md`.
- [ ] Smoke benchmark manifest `benchmarks/geometry/manifests/boundary_first_flattening_reference_smoke.yaml` (`benchmark_id: geometry.boundary_first_flattening.smoke`); metrics restricted to the benchmark enum (`runtime_ms`, `quality_error_l2`).
- [ ] Regenerate `docs/api/generated/module_inventory.md` if the module surface changes.

## Acceptance criteria
- [ ] `BffParams` is selectable on the shared typed strategy variant with
      `AutomaticConformal`/`TargetLengths`/`TargetAngles` modes and caller cones.
- [ ] Boundary length/angle control is verified within tolerance; automatic mode is conformal at or below LSCM distortion.
- [ ] All correctness tests pass in the default CPU gate; benchmark smoke manifest validates and runs.
- [ ] Public API exposes only `std`/`glm`/geometry-owned records (no Eigen); layering holds (`geometry -> core`).

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'BoundaryFirstFlattening|Parameterization' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/benchmark/validate_benchmark_manifests.py
python3 tools/agents/validate_method_manifests.py --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No automatic cone-placement optimizer in this task (named future follow-up).
- No GPU backend before reference parity; no `std::rand` or global RNG state.
- No arbitrary projection fallback for unsupported topology or inconsistent prescribed curvature; no Eigen types on the public surface.

## Maturity
- Target: `CPUContracted` for the `Bff` reference strategy.
- `Operational` owned by `RUNTIME-176`/`UI-036` — the shared facade, config lane, and panel make every implemented strategy reachable in `Engine::Run()`, including `Bff` once this task lands. No GPU follow-up is owed: `METHOD-026` covers only the iterative strategies, and this one-shot linear strategy stays CPU-only by recorded decision.
