---
id: GEOM-019
theme: I
depends_on: [GEOM-018, GEOM-020]
---
# GEOM-019 — Harmonic/Tutte parameterization and boundary constraints

## Goal
- Add a deterministic harmonic/Tutte parameterization solver with explicit boundary-condition records, diagnostics, and tests, using the shared diagnostics from GEOM-018.

## Non-goals
- No ARAP, SLIM, ABF/ABF++, authalic, orbifold, or functional-map solver in this task.
- No atlas segmentation, seam generation, or chart packing implementation.
- No renderer/material/UV asset pipeline integration.
- No GPU backend.
- No benchmark performance claims.

## Context
- Status: implemented on branch `claude/intrinsic-engine-framework24-migrate-cew18n`;
  full `ctest` gate deferred to CI (see Status below).
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus `benchmarks/geometry` using public geometry APIs only.

## Status

- Implemented module `Geometry.Parameterization.Harmonic`
  (`src/geometry/Geometry.Parameterization.Harmonic.cppm` + `.cpp`):
  `HarmonicWeightType` (Cotangent harmonic default / Uniform Tutte),
  `HarmonicBoundaryPolicy` (Circle / Square with uniform or arc-length spacing /
  Custom caller-pinned), interior-pin support, full failure-state enum, and
  GEOM-018 `ParameterizationDiagnostics` in the result. The SPD interior system
  is solved with `Geometry.Sparse::SparseLDLT` (the GEOM-020 seam), factored once
  and back-substituted for u and v; no Eigen types cross the public API.
- GEOM-018 (diagnostics) and GEOM-020 (sparse SPD) are both retired, so the
  documented fallback clause is not needed; LDLT is used directly.
- Existing `Geometry.Parameterization::ComputeLSCM` is untouched and covered by a
  reachability test.
- Verified in-session: `check_layering`, `check_test_layout`, `check_doc_links`,
  `check_task_policy`, `validate_tasks` pass; module inventory regenerated (530
  modules); a standalone clang-20 + glm + Eigen sanity check confirms the
  square-fan harmonic/Tutte center lands exactly at (0.5, 0.5) for both weightings.
- Deferred to CI: `cmake --preset ci` + `ctest -R 'Parameterization|Sparse'` —
  the sandbox cannot bootstrap vcpkg (microsoft/vcpkg clone denied by the egress
  policy), so the full C++23-module build/test must run in CI before this task
  retires to `tasks/done/`.
- Spawned by [`GEOM-011`](../../archive/GEOM-011-parameterization-mapping-roadmap.md) and the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).
- Existing `Geometry.Parameterization` provides LSCM for disk-topology triangle meshes. This task adds the smallest new solver family after diagnostics: harmonic/Tutte embedding with explicit boundary policies.
- Depends on GEOM-018 for shared distortion/map-quality diagnostics. If GEOM-018 is not retired when this task starts, record any diagnostic subset used here as an explicit in-task assumption.
- Depends on retired GEOM-020 for the sparse direct SPD solver seam (`Geometry.Sparse` LDLT/LLT): the fixed-boundary Tutte/harmonic interior system is SPD and should use `Geometry.Sparse::SparseLDLT` by default. The existing `Geometry.Sparse` CG path from GEOM-008 remains an acceptable documented fallback only if a later slice plan records the solver choice and tolerance before implementation.

## Required changes
- [x] Boundary-condition records: `HarmonicBoundaryPolicy` (Circle/Square with
      uniform or arc-length spacing, Custom caller UVs) plus interior/boundary
      pin arrays.
- [x] Harmonic/Tutte API (`ComputeHarmonic`) for disk-topology triangle meshes
      with explicit `HarmonicParams`/`HarmonicResult`/`HarmonicStatus` and
      embedded diagnostics.
- [x] Interior Laplacian assembled from per-edge cotangent/uniform weights and
      solved via `Geometry.Sparse::SparseLDLT`; no Eigen types in the public API.
- [x] Structured failure states for unsupported topology and invalid boundary
      conditions (no arbitrary projection fallback).
- [x] GEOM-018 `ParameterizationDiagnostics` integrated in the result record.
- [x] Existing LSCM API kept reachable and behavior-compatible (reachability test).

## Tests
- [x] `tests/unit/geometry/Test.HarmonicParameterization.cpp` (`unit;geometry`):
      square-fan center lands at the harmonic average (0.5, 0.5) for cotangent
      and uniform weights.
- [x] Circle-boundary (on-circle + deterministic) and square arc-length boundary
      fixtures.
- [x] Invalid topology: closed mesh (`NotDiskTopology`), quad mesh
      (`NotTriangleMesh`). Multiple-loop / degenerate-loop are covered by the
      single-loop coverage check and the `< 3` boundary guard.
- [x] Invalid boundary conditions: mismatched arrays, duplicate pins, non-finite
      UVs, Custom boundary not fully pinned.
- [x] Tutte flip-free embedding asserted via `Diagnostics.FlippedElementCount`.
- [x] LSCM reachability test.
- [ ] (Deferred to CI) `cmake --preset ci && ctest -R 'Parameterization|Sparse'`
      — not runnable in the authoring sandbox (vcpkg egress blocked).

## Docs
- [x] Updated [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md)
      with the harmonic/Tutte solver, weightings, boundary policies, solver seam,
      and failure-state semantics.
- [x] Regenerated `docs/api/generated/module_inventory.md` (adds
      `Geometry.Parameterization.Harmonic`; 530 modules).
- [ ] (Not introduced) No new smoke benchmark ID in this slice; the
      parameterization-mapping roadmap pack boundary is unchanged.

## Acceptance criteria
- [ ] Harmonic/Tutte parameterization is available with explicit boundary policies and deterministic diagnostics.
- [ ] Unsupported topology, invalid pins, invalid UVs, singular systems, and non-convergence report structured failure states.
- [ ] GEOM-018 diagnostics are used or an explicit documented subset is provided if GEOM-018 is still open.
- [ ] Tests cover valid square/circle embeddings, invalid topology, invalid boundary conditions, and LSCM compatibility.
- [ ] The implementation preserves `geometry -> core` layering and benchmark dependencies remain limited to public geometry/method APIs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|Sparse|DEC' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement ARAP, SLIM, atlas segmentation, seam generation, chart packing, or functional maps in this harmonic/Tutte task.
- Do not silently project closed or non-disk meshes into UV space.
- Do not remove or silently replace the existing LSCM API.
- Do not expose Eigen types through public geometry APIs.
- Do not introduce renderer/runtime/ECS/assets/platform/app dependencies.

## Maturity
- Target: `CPUContracted` (deterministic CPU solver verified by the default gate).
- No `Operational` follow-up is owed; this task has no backend seam.
