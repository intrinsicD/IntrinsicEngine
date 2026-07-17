---
id: BUG-109
theme: G
depends_on: []
maturity_target: CPUContracted
---
# BUG-109 — Voxel downsampling invalid-input and deterministic-cell ordering

## Goal
- Make `Geometry.PointCloud.Utils::VoxelDownsample` fail closed before unsafe quantization and emit valid cells in a documented deterministic order.

## Non-goals
- No new reduction strategy; `GEOM-061` owns first/last/closest/medoid/seeded-random selection.
- No streaming, out-of-core, parallel, or GPU implementation.
- No change to valid centroid, normal, color, or radius reduction semantics.

## Context
- Symptom: the implementation rejects only `VoxelSize <= 0`, so NaN/Inf sizes and non-finite positions reach `floor` and integral conversion; sufficiently large finite coordinates can also exceed the cell-index range. Output order follows `std::unordered_map` iteration.
- Expected behavior: reject invalid size/positions and unrepresentable cell coordinates before conversion, then emit occupied cells in one documented lexicographic key order.
- Impact: malformed inputs can trigger undefined or implementation-defined conversion behavior, and otherwise identical runs/builds need not return the same point ordering.
- This task restores the deterministic/fail-closed baseline that open `GEOM-061` already assumes and therefore gates that feature task.

## Required changes
- [ ] Require a finite, strictly positive voxel size before computing its reciprocal.
- [ ] Validate every input position component and the floored scaled coordinate before converting it to the chosen integral cell-key representation.
- [ ] Return `std::nullopt` without publishing a partial result when any point cannot be represented safely.
- [ ] Sort occupied cell keys lexicographically before appending reduced points; document the axis order and preserve accumulation in input order.
- [ ] Preserve existing valid-input centroid and optional-attribute behavior.

## Tests
- [ ] Add regressions for NaN, positive/negative infinity, and zero/negative voxel sizes.
- [ ] Add non-finite-position and extreme finite-coordinate cases that would overflow the selected cell representation.
- [ ] Pin floor semantics for negative coordinates around cell boundaries.
- [ ] Assert exact lexicographic output order on a crafted multi-cell cloud and byte-stable results across repeated calls.
- [ ] Keep existing centroid, normal, color, radius, and reduction-ratio cases passing unchanged.

## Docs
- [ ] Document invalid-input and deterministic ordering semantics on `VoxelDownsample`.
- [ ] Add this issue to `tasks/backlog/bugs/index.md` and record it as `GEOM-061`'s prerequisite.

## Acceptance criteria
- [ ] No non-finite or out-of-range value reaches floating-to-integral cell conversion.
- [ ] Invalid input returns no result and cannot expose partially reduced data.
- [ ] Valid output order is independent of unordered-container iteration order.
- [ ] Existing valid centroid and attribute results remain unchanged apart from their newly stable ordering.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloud_Downsample' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Silently clamping invalid coordinates into a cell.
- Relying on a particular standard-library hash iteration order.
- Folding `GEOM-061`'s strategy expansion into this correctness fix.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
