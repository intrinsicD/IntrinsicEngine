---
id: GEOM-061
theme: I
depends_on: [BUG-109]
maturity_target: CPUContracted
---
# GEOM-061 — Point-cloud grid-downsampling reduction strategies

## Goal
- Extend the existing voxel-grid downsampling in `Geometry.PointCloud.Utils` with selectable per-cell reduction strategies that return input point indices (first / last / closest-to-cell-center / closest-to-cell-centroid / seeded uniform-random), so downsampling can carry all vertex properties exactly instead of synthesizing centroid points.

## Non-goals
- No Poisson-disk criteria here — dart-throwing/progressive Poisson sampling is owned by the retired `METHOD-012` lineage.
- No streaming or out-of-core support.
- No behavior change to the corrected centroid semantics delivered by `BUG-109` or to existing callers.

## Context
- Owner/layer: `src/geometry`; `geometry -> core` only.
- Port source: framework24 `lib_bcg_framework/include/bcg_point_cloud_vertex_sampler_grid_*.h` plus `bcg_geometry_processing/include/GridSampler.h`. The old `medioids` spelling is not a conventional medoid: it selects the input point nearest the arithmetic cell mean. Port that behavior as `ClosestToCellCentroid`; the `union` sampler variants are composition sugar and are not ported.
- Retired `GEOM-016` hardened centroid voxel downsampling with deterministic ordering and stable tie-breaking; this task adds index-returning strategies under the same determinism and invalid-input contract.
- `BUG-109` first makes that intended baseline true in the current implementation by validating non-finite/out-of-range quantization and sorting occupied cells. This task must build on that fix rather than duplicate or bypass it.
- Index-returning selection is what makes downstream property transfer exact (select rows from every property), which the centroid path cannot do.

## Required changes
- [ ] Add a `GridReduction` strategy selector (`Centroid` = existing behavior, `First`, `Last`, `ClosestToCellCenter`, `ClosestToCellCentroid`, `RandomUniform`) to the voxel-downsampling surface in `Geometry.PointCloud.Utils` (or a focused sibling partition if the module split prefers it).
- [ ] Index-returning entry point: strategies other than `Centroid` return selected input indices in deterministic order with documented stable tie-breaking (lowest input index wins on equal keys).
- [ ] `RandomUniform` takes an explicit seed; identical `(seed, input, voxel size)` produce identical selections across runs and thread counts.
- [ ] Preserve the `BUG-109` fail-closed quantization contract and lexicographic cell order (empty cloud, non-finite/non-positive voxel size, non-finite positions, and unrepresentable cell coordinates).

## Tests
- [ ] Extend the `unit;geometry` point-cloud utils tests with a crafted multi-point-per-cell fixture asserting each strategy's selection semantics.
- [ ] Include a fixture where nearest-to-arithmetic-centroid differs from a pairwise-distance medoid and assert `ClosestToCellCentroid` follows the former source semantics.
- [ ] Tie-breaking: equal-distance candidates resolve to the lowest input index.
- [ ] Determinism: `RandomUniform` is bitwise stable for a fixed seed; all strategies are stable across two runs.
- [ ] Index validity: every returned index is in range and unique, one index per non-empty cell.

## Docs
- [ ] Interface documentation for the strategy semantics and tie-break contract.
- [ ] Regenerate the module inventory if the module surface changes.
- [ ] Update the port-gap cluster notes in `tasks/backlog/geometry/README.md`.

## Acceptance criteria
- [ ] All strategies are covered by tests in the default CPU gate.
- [ ] Existing centroid callers retain the corrected `BUG-109` values and deterministic ordering.
- [ ] Layering check passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'PointCloud' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No behavior change to the existing centroid downsampling path.
- No `std::rand` or global RNG state.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
