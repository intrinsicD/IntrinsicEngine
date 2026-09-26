---
id: BUG-223
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive recording of an unrelated unit-test failure; no performance or method claim.
contract_schema: 1
contracts: []
contract_review: Correctness diagnosis of an existing scalar-field method under its current contract; no new engine or integration contract.
---
# BUG-223 — Watershed extrema tests report zero basins

## Goal

Find why the watershed scalar-field extrema path reports no minima or basins
in two unit tests, and restore the tested behavior without weakening the
tests.

## Evidence

Observed on 2026-09-25/26 at `8f428c0` (branch
`claude/implicit-smoothing-property-celejg`). The ci presets could not
configure in that session because vcpkg dependency downloads were blocked
(BUG-065). The tests ran in a standalone clang-20 build of the geometry module
closure with system Eigen 3.4.0, glm and GTest 1.14 (libstdc++ 14). They have
**not** yet been confirmed on the canonical `ci` preset.

- `ScalarfieldExtrema.WatershedRidgeSeparatesTwoPitsOnMeshEdges`
  (`Test.ScalarfieldExtrema.cpp:438`): `Extract` succeeds, but
  `Diagnostic.DescendingBasins` is 0 (expected 2) and `DescendingBasin` is
  empty (expected one entry per vertex).
- `ScalarfieldExtrema.WatershedPersistenceMergesShallowBasins`
  (`Test.ScalarfieldExtrema.cpp:497`): `DescendingBasins` is 0 (expected 2),
  `Minima` is 0 (expected at least 3), and the kept-basin case also reports 0.
- Both fail identically in Debug and Release builds of only the
  `Test.ScalarfieldExtrema.cpp` closure (79 files). That closure contains none
  of the files changed on the branch, so the branch's smoothing and spatial-index
  changes are not involved. All other 1865 tests in that geometry build pass.

Whether this is a real regression since `9219842` (which added watershed
extraction) or an environment difference is not yet established.

## Acceptance criteria

- [ ] Run both tests on the canonical `ci` preset and record whether they fail there.
- [ ] If they fail: identify the first failing revision and the root cause in the watershed path, fix it, and keep both tests unchanged.
- [ ] If they pass on `ci` only: identify the environment-dependent behavior (dependency version, uninitialized state or undefined behavior) and make the method independent of it.

## Verification

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R ScalarfieldExtrema --timeout 60
```
