---
id: GEOM-016
theme: none
depends_on: []
---
# GEOM-016 — Point-cloud filtering and density diagnostics contracts

## Goal
- Harden the generic point-cloud filtering/downsampling/outlier-removal pack into deterministic `src/geometry` APIs with explicit diagnostics, tests, and a smoke benchmark manifest.

## Non-goals
- No descriptor, keypoint, registration, or reconstruction implementation in this task.
- No GPU backend.
- No renderer/runtime/ECS/assets/platform/app integration.
- No performance win claims; benchmark output is smoke/quality evidence only.

## Context
- Status: backlog.
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus `benchmarks/geometry` using public geometry APIs only.
- Spawned by [`GEOM-010`](../../done/GEOM-010-point-cloud-algorithm-pack-roadmap.md) and the [point-cloud algorithm roadmap](../../../docs/architecture/point-cloud-algorithm-roadmap.md).
- Existing surfaces in `Geometry.PointCloud.Utils` include voxel downsampling, random subsampling, bilateral filtering, outlier-score estimation, kernel-density estimation, and radius estimation; this task hardens their contracts and fills the missing explicit removal APIs rather than replacing the module.
- Use `GEOM-005` API/numeric policy, `GEOM-007` robust predicate/tolerance policy where scale-sensitive decisions are needed, `GEOM-008` numerical helpers only behind geometry-owned APIs, and `GEOM-009` benchmark manifest conventions.

## Required changes
- [ ] Audit `Geometry.PointCloud.Utils` public APIs and normalize result diagnostics for filtering, downsampling, density, radius, and outlier operations.
- [ ] Add explicit statistical-outlier-removal and radius-outlier-removal APIs that return owned result clouds or stable kept/rejected index lists with diagnostics.
- [ ] Preserve deterministic ordering for voxel/downsampling/removal outputs, including stable tie-breaking for equal voxel or distance keys.
- [ ] Preserve seed-controlled deterministic behavior for random subsampling and report the seed in diagnostics.
- [ ] Define invalid-input handling for empty clouds, one-point clouds, duplicate/coincident points, non-finite positions, non-positive voxel sizes/radii, and insufficient neighbor counts.
- [ ] Add or update a smoke benchmark manifest and runner case under `benchmarks/geometry/` for the filtering/outlier pack.

## Tests
- [ ] Add or update `unit;geometry` tests for uniform-grid voxel occupancy and deterministic centroid order.
- [ ] Add outlier-removal tests using a two-cluster fixture plus isolated outliers with expected kept/rejected counts.
- [ ] Add edge-case tests for empty/one-point clouds, duplicate points, non-finite inputs, invalid radii/voxel sizes, and insufficient neighbor support.
- [ ] Add deterministic random-subsampling tests proving identical selected indices for identical `(points, seed, target)` and different deterministic output for a different seed where applicable.
- [ ] Add or update `benchmark;geometry` smoke validation using existing benchmark labels; introduce no new CTest labels unless both `tests/README.md` and `tests/CMakeLists.txt` are updated in the same change.

## Docs
- [ ] Update [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../../docs/architecture/point-cloud-algorithm-roadmap.md) if implementation scope or pack boundaries change.
- [ ] Update [`docs/architecture/geometry.md`](../../../docs/architecture/geometry.md) with the final filtering/outlier diagnostics contract if public behavior changes.
- [ ] Update `benchmarks/geometry/README.md` or manifests if new benchmark IDs are introduced.
- [ ] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [ ] Filtering/downsampling/outlier APIs have explicit success/failure diagnostics and deterministic output contracts.
- [ ] Statistical and radius outlier removal are available without requiring callers to interpret raw score properties themselves.
- [ ] Tests cover deterministic ordering, seeded behavior, invalid inputs, and known outlier fixtures.
- [ ] A CPU-only geometry smoke benchmark/manifest exists for this pack without performance claims.
- [ ] The implementation preserves `geometry -> core` layering and benchmark dependencies remain limited to public geometry/method APIs.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloud|BenchmarkSmoke' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_manifests.py --root benchmarks --strict
python3 tools/repo/generate_module_inventory.py --root src --out docs/api/generated/module_inventory.md
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Do not implement descriptors, keypoints, registration, or reconstruction in this filtering/density task.
- Do not silently mutate borrowed input clouds for APIs documented as returning filtered owned results.
- Do not introduce renderer/runtime/ECS/assets/platform/app dependencies.
- Do not claim performance improvements without a baseline comparison.

