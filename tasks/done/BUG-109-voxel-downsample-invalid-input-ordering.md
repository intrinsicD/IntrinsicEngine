---
id: BUG-109
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug109"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-07T22:43:05Z"
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this repair makes one geometry point-cloud free function fail closed on invalid input and emit a documented deterministic order. It publishes nothing to an ECS geometry source, changes no element-domain property, adjacency, support-radius, parameterization, or method-integration surface, and adds no reusable task-workflow contract."
---
# BUG-109 — Voxel downsampling invalid-input and deterministic-cell ordering

## Status

- Completed and retired on 2026-08-08 at `CPUContracted`, the declared target;
  no `Operational` follow-up is owed.
- `VoxelSize` now requires a finite, strictly positive value. The old guard was
  `VoxelSize <= 0.0f`, which NaN passes, so `1.0f / NaN` poisoned every
  coordinate before any conversion.
- A local `tryCellCoordinate` validates each position component. It keeps the
  original `std::floor(value * invVoxel)` expression bit-for-bit so cell
  assignment for valid input is unchanged, rejects a non-finite result, and
  range-checks in `double` before the `int` cast. The widening matters:
  `(float)INT_MAX` rounds up to 2^31, so a float comparison would admit one
  value past the representable maximum.
- Any unrepresentable point returns `std::nullopt` from inside the accumulation
  loop, which runs before the result is constructed, so no partial result can
  be observed. Invalid coordinates are never clamped into a cell.
- Occupied cell keys are collected and sorted ascending by x, then y, then z
  before emission, so output no longer depends on `std::unordered_map`
  iteration. Accumulation still runs in input order, so the sums themselves are
  untouched.
- Five regressions cover non-finite voxel sizes, non-finite positions on each
  axis, a finite coordinate that overflows the cell key, floor semantics across
  a negative cell boundary, and exact lexicographic order with repeat
  stability. All five fail against the unfixed source.
- Verified: 12/12 `PointCloud_Downsample` cases pass and the default CPU gate
  passes 4131/4131 with its expected GLFW/LeakSanitizer skip. Layering and
  test-layout gates report zero findings.
- This unblocks `GEOM-061`, which assumed this deterministic fail-closed
  baseline.
- Completion commit: this retirement commit.

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
- [x] Require a finite, strictly positive voxel size before computing its reciprocal.
- [x] Validate every input position component and the floored scaled coordinate before converting it to the chosen integral cell-key representation.
- [x] Return `std::nullopt` without publishing a partial result when any point cannot be represented safely.
- [x] Sort occupied cell keys lexicographically before appending reduced points; document the axis order and preserve accumulation in input order.
- [x] Preserve existing valid-input centroid and optional-attribute behavior.

## Tests
- [x] Add regressions for NaN, positive/negative infinity, and zero/negative voxel sizes.
- [x] Add non-finite-position and extreme finite-coordinate cases that would overflow the selected cell representation.
- [x] Pin floor semantics for negative coordinates around cell boundaries.
- [x] Assert exact lexicographic output order on a crafted multi-cell cloud and byte-stable results across repeated calls.
- [x] Keep existing centroid, normal, color, radius, and reduction-ratio cases passing unchanged.

## Docs
- [x] Document invalid-input and deterministic ordering semantics on `VoxelDownsample`.
- [x] Add this issue to `tasks/backlog/bugs/index.md` and record it as `GEOM-061`'s prerequisite.

## Acceptance criteria
- [x] No non-finite or out-of-range value reaches floating-to-integral cell conversion.
- [x] Invalid input returns no result and cannot expose partially reduced data.
- [x] Valid output order is independent of unordered-container iteration order.
- [x] Existing valid centroid and attribute results remain unchanged apart from their newly stable ordering.

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
