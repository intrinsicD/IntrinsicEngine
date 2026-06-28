---
id: GEOM-019
theme: none
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
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus `benchmarks/geometry` using public geometry APIs only.
- Spawned by [`GEOM-011`](../../done/GEOM-011-parameterization-mapping-roadmap.md) and the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).
- Existing `Geometry.Parameterization` provides LSCM for disk-topology triangle meshes. This task adds the smallest new solver family after diagnostics: harmonic/Tutte embedding with explicit boundary policies.
- Depends on GEOM-018 for shared distortion/map-quality diagnostics. If GEOM-018 is not retired when this task starts, record any diagnostic subset used here as an explicit in-task assumption.
- Depends on retired GEOM-020 for the sparse direct SPD solver seam (`Geometry.Sparse` LDLT/LLT): the fixed-boundary Tutte/harmonic interior system is SPD and should use `Geometry.Sparse::SparseLDLT` by default. The existing `Geometry.Sparse` CG path from GEOM-008 remains an acceptable documented fallback only if a later slice plan records the solver choice and tolerance before implementation.

## Required changes
- [ ] Define public boundary-condition records for circle, square, arc-length, fixed/pinned vertices, and caller-provided boundary UVs.
- [ ] Add a harmonic/Tutte parameterization API for disk-topology triangle meshes with explicit params, result, status, and diagnostics records.
- [ ] Assemble and solve the interior Laplacian system through geometry-owned sparse infrastructure without exposing Eigen types in public APIs.
- [ ] Report unsupported topology and invalid boundary conditions instead of falling back to arbitrary projection.
- [ ] Integrate GEOM-018 diagnostics in the result record so flipped triangles and distortion are measured consistently.
- [ ] Keep the existing LSCM API reachable and behavior-compatible.

## Tests
- [ ] Add `unit;geometry` tests for a convex square disk with fixed square boundary where the center/interior vertex lands at the expected harmonic average.
- [ ] Add circle-boundary and arc-length boundary fixtures proving deterministic boundary placement.
- [ ] Add invalid-topology tests for closed meshes, non-disk meshes, multiple boundary loops, degenerate boundary loops, and insufficient vertices.
- [ ] Add invalid boundary-condition tests for duplicate pins, deleted pins, non-finite UVs, and mismatched caller-provided boundary arrays.
- [ ] Add flipped-triangle and distortion regression tests using GEOM-018 diagnostics.
- [ ] Add focused tests proving existing LSCM behavior remains reachable.

## Docs
- [ ] Update [`docs/architecture/parameterization-mapping-roadmap.md`](../../../docs/architecture/parameterization-mapping-roadmap.md) if boundary-policy or solver scope changes.
- [ ] Update [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md) with harmonic/Tutte solver and boundary-condition semantics if module surfaces change.
- [ ] Update benchmark docs/manifests if a new smoke benchmark ID is introduced.
- [ ] Regenerate `docs/api/generated/module_inventory.md` after module surface changes.

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
