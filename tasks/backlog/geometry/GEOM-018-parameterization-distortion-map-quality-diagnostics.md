---
id: GEOM-018
theme: none
depends_on: []
---
# GEOM-018 — Parameterization distortion and map-quality diagnostics

## Goal
- Add standalone parameterization and map-quality diagnostics so existing LSCM and future harmonic/Tutte, ARAP, SLIM, atlas, and map-storage tasks report comparable quality metrics.

## Non-goals
- No new parameterization solver in this task.
- No atlas segmentation, seam generation, or chart packing implementation.
- No renderer/material/UV asset pipeline integration.
- No GPU backend.
- No benchmark performance claims.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus `benchmarks/geometry` using public geometry APIs only.
- Spawned by [`GEOM-011`](../../done/GEOM-011-parameterization-mapping-roadmap.md) and the [parameterization/mapping roadmap](../../../docs/architecture/parameterization-mapping-roadmap.md).
- Existing `Geometry.Parameterization` reports LSCM conformal distortion and flipped-triangle counts, but future solvers and map records need a reusable diagnostics surface independent of one solver.
- Use `GEOM-005` API/numeric policy, `GEOM-007` robust predicate/tolerance policy where scale-sensitive classification is needed, `GEOM-008` numerical helpers only behind geometry-owned APIs, and `GEOM-009` benchmark manifest conventions.

## Required changes
- [ ] Define a public diagnostics record for UV parameterizations and surface maps, including evaluated/skipped face counts, flipped-element counts, conformal distortion, area/authalic distortion, symmetric Dirichlet energy, stretch, boundary distortion, and seam discontinuity fields where applicable.
- [ ] Add a deterministic diagnostics API that evaluates mesh positions plus per-vertex UVs without mutating the mesh.
- [ ] Reuse or adapt the existing LSCM quality calculation to call the shared diagnostics path without changing LSCM solver behavior.
- [ ] Define invalid-input handling for empty meshes, non-triangle faces if unsupported, deleted vertices/faces, degenerate 3D triangles, degenerate UV triangles, missing UVs, and non-finite positions/UVs.
- [ ] Add a small CPU-only geometry smoke benchmark manifest and runner case for diagnostics quality metrics, or document why the existing benchmark harness cannot yet host this pack.

## Tests
- [ ] Add `unit;geometry` tests for identity single-triangle and square-disk UV fixtures with expected near-zero distortion.
- [ ] Add stretched-rectangle tests with analytically predictable stretch/area distortion.
- [ ] Add flipped-UV tests that report flipped elements without crashing.
- [ ] Add degenerate 3D and degenerate UV triangle tests that report skipped/invalid counts.
- [ ] Add boundary-loop distortion tests proving deterministic boundary metrics.
- [ ] Add or update `benchmark;geometry` smoke validation using existing benchmark labels; introduce no new CTest labels unless both `tests/README.md` and `tests/CMakeLists.txt` are updated in the same change.

## Docs
- [ ] Update [`docs/architecture/parameterization-mapping-roadmap.md`](../../../docs/architecture/parameterization-mapping-roadmap.md) if diagnostics scope or metric names change.
- [ ] Update [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md) with the final diagnostics contract if public behavior changes.
- [ ] Update `benchmarks/geometry/README.md` or manifests if new benchmark IDs are introduced.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] Parameterization diagnostics are reusable outside LSCM and are deterministic for fixed inputs.
- [ ] Existing LSCM quality reporting remains reachable and is consistent with the shared diagnostics API.
- [ ] Tests cover identity, stretched, flipped, degenerate, and boundary fixtures.
- [ ] A CPU-only geometry smoke benchmark/manifest exists for this pack or a documented blocker is recorded in this task.
- [ ] The implementation preserves `geometry -> core` layering and benchmark dependencies remain limited to public geometry/method APIs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Parameterization|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement harmonic/Tutte, ARAP, SLIM, atlas segmentation, or map-storage solvers in this diagnostics task.
- Do not silently ignore flipped, degenerate, or invalid elements.
- Do not expose Eigen types through public geometry APIs.
- Do not introduce renderer/runtime/ECS/assets/platform/app dependencies.
- Do not claim performance improvements without a baseline comparison.

