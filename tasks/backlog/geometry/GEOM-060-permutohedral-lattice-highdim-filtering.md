---
id: GEOM-060
theme: I
depends_on: []
maturity_target: CPUContracted
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: [repo.source-documentation, geometry.element-domain-sources]
---
# GEOM-060 — Permutohedral lattice fast high-dimensional filtering seam

## Goal
- Add a geometry-owned permutohedral-lattice filter (splat → blur → slice) for approximate high-dimensional Gaussian filtering, verified against brute-force Gaussian filtering on small fixtures.

## Non-goals
- No GPU backend.
- No consumer rewiring in this task: existing bilateral filters keep their brute-force paths; adopting the lattice as a fast path is per-consumer follow-up work.
- No speedup claim without a benchmark manifest and baseline comparison (benchmark policy).

## Context
- RUNTIME-224 already integrates the bilateral supplied-neighbor reducer and
  CPU/Vulkan spatial choices. A justified lattice filter would extend that
  existing operation, preserving its neighborhood/kernel semantics, rather
  than create a second bilateral processing workflow.

- Owner/layer: `src/geometry`; `geometry -> core` only.
- Paper: Adams, Baek, Davis — "Fast High-Dimensional Filtering Using the Permutohedral Lattice", Computer Graphics Forum (Eurographics) 2010.
- Port source: framework24 `lib_bcg_framework/include/bcg_permutohedral_lattice.h` (untested in bcg; this port adds a brute-force oracle test).
- Named future consumers: the Coherent Point Drift lattice E-step (METHOD-058, FilterReg-style, for the middle sigma phase that METHOD-049/053 leave dense) and bilateral point-cloud/mesh filtering fast paths (`Geometry.PointCloud.Utils`, retired `GEOM-042`).
- 2026-09-28 planning (Fable 5.1, Codex): METHOD-058 needs asymmetric stages - splat at one point set and slice at another (sources to targets and back) - vector values (four channels: 1 and x), double accumulation, and unnormalized Gaussian sums; the constant-signal invariance below applies to the normalized filter only. Expose both.

## Spatial acceleration consideration

The shared point LBVH is 3D Euclidean nearest/radius infrastructure; it does not
implement high-dimensional Gaussian filtering or replace the permutohedral
lattice. A consumer may later compare a spatial-neighborhood method only with its
own metric/support/error contract. Keep this lattice reference and the no-
consumer-rewiring scope intact.

See the [shared spatial-index consumer inventory](../../../docs/architecture/spatial-index-consumers.md).

## Required changes
- [ ] Add module `Geometry.PermutohedralLattice` (`.cppm` + `.cpp`): build the lattice from d-dimensional feature vectors and expose a one-call `Filter(features, values)` entry plus the reusable splat/blur/slice stages.
- [ ] Support feature dimensions at least 2 through 6 with documented accuracy behavior versus dimension.
- [ ] Asymmetric splat/slice (splat features A, slice features B), multi-channel values, unnormalized and normalized results (METHOD-058).
- [ ] Deterministic: identical inputs produce bitwise-identical outputs (fixed traversal order; no hash-iteration-order dependence in results).
- [ ] Fail-closed on empty inputs, mismatched feature/value counts, and non-finite features.
- [ ] Register the module in `src/geometry/CMakeLists.txt`.

## Tests
- [ ] `tests/unit/geometry/Test.PermutohedralLattice.cpp` with `unit;geometry` labels.
- [ ] Oracle: filtered output matches brute-force Gaussian filtering within documented tolerance on small-N fixtures for d = 2, 3, and 5.
- [ ] Constant-signal invariance: filtering a constant value field returns the constant within tolerance.
- [ ] Determinism: two runs produce bitwise-identical output.
- [ ] Degenerate inputs return explicit failure states.

## Docs
- [ ] Interface documentation including the accuracy-versus-dimension note and the brute-force-oracle contract.
- [ ] Regenerate the module inventory.
- [ ] Update the port-gap cluster notes in `tasks/backlog/geometry/README.md`.

## Acceptance criteria
- [ ] Oracle tolerance tests pass for the listed dimensions in the default CPU gate.
- [ ] Public surface exposes only `std`/`glm`/scalar types.
- [ ] Layering check passes.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'Permutohedral' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- No performance claims without a benchmark manifest plus baseline.
- No consumer migration in this task.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed until a consumer adopts the fast path (that adoption opens as its own task).
