---
id: GEOM-016
theme: none
depends_on: []
maturity_target: CPUContracted
completed_on: 2026-06-29
---
# GEOM-016 — Point-cloud filtering and density diagnostics contracts

## Status
- Completed at `CPUContracted`. Implemented across commits `09bb1a8` (explicit
  outlier-removal operators) and `1f1ff67` (`KNeighbors` overflow guard).
  Commit: this commit (formal GEOM-016 retirement).
- `Geometry.PointCloud.Utils` now exposes explicit `RemoveStatisticalOutliers`
  and `RemoveRadiusOutliers` operators returning an `OutlierRemovalResult`
  (owned filtered cloud + ascending kept/rejected index partitions + `Status`
  taxonomy + statistical-distance diagnostics), alongside the pre-existing
  voxel/random downsampling, bilateral filtering, outlier-score, KDE, and
  radius-estimation surfaces this task hardened.
- Coverage: `tests/unit/geometry/Test.PointCloudOutlierRemoval.cpp`
  (`unit;geometry`, registered in `tests/CMakeLists.txt`) and the smoke
  benchmark `benchmarks/geometry/Bench_PointCloudFilteringSmoke.cpp` with
  manifest `benchmarks/geometry/manifests/geometry_pointcloud_filtering_smoke.yaml`.
- This CPU-only contract has no backend seam; no `Operational` follow-up is owed.
  The editor wire-up of these operators is the separate `UI-027`.

## Goal
- [x] Harden the generic point-cloud filtering/downsampling/outlier-removal pack into deterministic `src/geometry` APIs with explicit diagnostics, tests, and a smoke benchmark manifest.

## Non-goals
- No descriptor, keypoint, registration, or reconstruction implementation in this task.
- No GPU backend.
- No renderer/runtime/ECS/assets/platform/app integration.
- No performance win claims; benchmark output is smoke/quality evidence only.

## Context
- Status: done (retired 2026-06-29).
- Owning subsystem/layer: `geometry` (`geometry -> core` only) plus `benchmarks/geometry` using public geometry APIs only.
- Spawned by [`GEOM-010`](GEOM-010-point-cloud-algorithm-pack-roadmap.md) and the [point-cloud algorithm roadmap](../../docs/architecture/point-cloud-algorithm-roadmap.md).
- Existing surfaces in `Geometry.PointCloud.Utils` include voxel downsampling, random subsampling, bilateral filtering, outlier-score estimation, kernel-density estimation, and radius estimation; this task hardens their contracts and fills the missing explicit removal APIs rather than replacing the module.
- Use `GEOM-005` API/numeric policy, `GEOM-007` robust predicate/tolerance policy where scale-sensitive decisions are needed, `GEOM-008` numerical helpers only behind geometry-owned APIs, and `GEOM-009` benchmark manifest conventions.

## Required changes
- [x] Audit `Geometry.PointCloud.Utils` public APIs and normalize result diagnostics for filtering, downsampling, density, radius, and outlier operations. New APIs land in `Geometry.PointCloud.Utils` (no new module); split a sub-module only if the implementation unit grows past the interface-hygiene bar in `AGENTS.md` §5.
- [x] Add explicit statistical-outlier-removal and radius-outlier-removal APIs that return owned result clouds or stable kept/rejected index lists with diagnostics.
- [x] Preserve deterministic ordering for voxel/downsampling/removal outputs, including stable tie-breaking for equal voxel or distance keys.
- [x] Preserve seed-controlled deterministic behavior for random subsampling and report the seed in diagnostics.
- [x] Define invalid-input handling for empty clouds, one-point clouds, duplicate/coincident points, non-finite positions, non-positive voxel sizes/radii, and insufficient neighbor counts.
- [x] Add or update a smoke benchmark manifest and runner case under `benchmarks/geometry/` for the filtering/outlier pack.

## Tests
- [x] Add or update `unit;geometry` tests for uniform-grid voxel occupancy and deterministic centroid order.
- [x] Add outlier-removal tests using a two-cluster fixture plus isolated outliers with expected kept/rejected counts.
- [x] Add edge-case tests for empty/one-point clouds, duplicate points, non-finite inputs, invalid radii/voxel sizes, and insufficient neighbor support.
- [x] Add deterministic random-subsampling tests proving identical selected indices for identical `(points, seed, target)` and different deterministic output for a different seed where applicable.
- [x] Add or update `benchmark;geometry` smoke validation using existing benchmark labels; introduce no new CTest labels unless both `tests/README.md` and `tests/CMakeLists.txt` are updated in the same change.

## Docs
- [x] Update [`docs/architecture/point-cloud-algorithm-roadmap.md`](../../docs/architecture/point-cloud-algorithm-roadmap.md) if implementation scope or pack boundaries change.
- [x] Update [`docs/architecture/geometry.md`](../../docs/architecture/geometry.md) with the final filtering/outlier diagnostics contract if public behavior changes.
- [x] Update `benchmarks/geometry/README.md` or manifests if new benchmark IDs are introduced.
- [x] Regenerate `docs/api/generated/module_inventory.md` if module surfaces change.

## Acceptance criteria
- [x] Filtering/downsampling/outlier APIs have explicit success/failure diagnostics and deterministic output contracts.
- [x] Statistical and radius outlier removal are available without requiring callers to interpret raw score properties themselves.
- [x] Tests cover deterministic ordering, seeded behavior, invalid inputs, and known outlier fixtures.
- [x] A CPU-only geometry smoke benchmark/manifest exists for this pack without performance claims.
- [x] The implementation preserves `geometry -> core` layering and benchmark dependencies remain limited to public geometry/method APIs.

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

## Maturity
- Target: `CPUContracted` (contract hardening of an existing CPU pack).
- No `Operational` follow-up is owed; this task has no backend seam.

