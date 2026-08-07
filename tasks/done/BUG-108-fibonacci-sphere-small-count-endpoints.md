---
id: BUG-108
theme: G
depends_on: []
maturity_target: CPUContracted
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this repair makes one geometry sphere-sampling free function safe and deterministic for zero, one, and two samples and removes an unused, uncallable helper. It publishes nothing to an ECS geometry source, changes no element-domain property, adjacency, support radius, parameterization, or method-integration surface, and adds no reusable task-workflow contract."
---
# BUG-108 — Fibonacci sphere sampling small-count and endpoint safety

## Status

- Completed and retired on 2026-08-07 at `CPUContracted`, the declared target;
  no `Operational` follow-up is owed.
- `SampleSurfaceFibonacciLattice` now resolves the shared `0`/`1`/`2` contract
  before any index or divisor is computed: `0` returns empty, `1` returns the
  north pole, and `2` returns north then south, for every `FibonacciLattice`
  value. That ordering is what removes the underflow — the old code derived
  `end_index = num_samples - 1` for `FLTHIRD`/`FLOFFSET`, which wrapped to
  `SIZE_MAX` at zero samples and indexed past the end at one.
- For three or more samples the explicit-pole variants now write the north pole
  to index `0` and the south pole to index `n - 1`. Previously the north pole
  was written to index `1` and immediately overwritten by the fill loop, so
  index `0` stayed default-initialized and a non-origin sphere emitted an
  off-sphere point at the origin. Interior lattice points are passed the same
  `i` values as before, so distributions for counts above two are unchanged
  apart from that repaired endpoint.
- The unused, unexported `SampleSurfaceUniform` helper is removed after
  confirming its only occurrence in the tree was its own definition. It divided
  by `num_samples - 1`.
- Three regressions cover every enum value: small counts, larger counts with
  the endpoint contract, and a degenerate zero-radius sphere. Each case runs
  twice and asserts equality. Against the unfixed source these fail — two with
  SEGFAULT and one on the origin-point assertion — which is the reported
  symptom reproduced.
- Verified: `IntrinsicGeometryTests` builds clean, the five `SphereSampling`
  cases pass, and the default CPU gate passes 4126/4126 with its expected
  GLFW/LeakSanitizer skip. Layering and test-layout gates report zero findings.
- Completion commit: this retirement commit.

## Goal
- Make every exported Fibonacci-lattice variant safe and deterministic for zero, one, and two requested samples while preserving the established formulas for larger sample sets.

## Non-goals
- No new sampling lattice, blue-noise optimizer, quality benchmark, or GPU path.
- No change to seeded random surface/volume sampling.
- Do not export the unused `SampleSurfaceUniform` helper.

## Context
- Symptom: `Geometry.Sphere.Sampling.cpp::SampleSurfaceFibonacciLattice` sets `start_index = 1` for `FLTHIRD`/`FLOFFSET`, writes the north pole at that index, overwrites it in the fill loop, leaves element zero default-initialized, and underflows or writes out of bounds for small counts.
- Expected behavior: every lattice returns exactly the requested count; `0` returns empty, `1` returns the north pole, and `2` returns north then south. For `FLTHIRD`/`FLOFFSET` with at least three samples, the poles occupy indices `0` and `n - 1` and only the interior is filled by the lattice formula.
- Impact: non-origin spheres can emit an off-sphere origin point, and small valid requests can trigger undefined behavior.
- The unexported `SampleSurfaceUniform` helper has no caller and divides by `num_samples - 1`; remove it instead of maintaining a second near-duplicate Fibonacci implementation.

## Required changes
- [x] Define and document the shared `0`/`1`/`2` result contract for all `FibonacciLattice` values.
- [x] Repair endpoint indexing so `FLTHIRD` and `FLOFFSET` preserve both explicit poles and fill only valid interior slots for larger requests.
- [x] Ensure all arithmetic and index calculations occur only after the small-count cases have returned.
- [x] Remove the unused, unexported `SampleSurfaceUniform` implementation after confirming it still has no callers.

## Tests
- [x] Extend `tests/unit/geometry/Test.Sampling.cpp` over every enum value and counts `0`, `1`, and `2`.
- [x] Use a valid non-origin, non-unit sphere (finite center and finite nonnegative radius) and assert exact result count, finite coordinates, and radius membership for every returned point.
- [x] Assert the exact north/south endpoint contract and that `FLTHIRD`/`FLOFFSET` preserve endpoints for a larger count.
- [x] Run each case twice and assert deterministic equality.

## Docs
- [x] Update the `Geometry.Sphere.Sampling` interface comments with the small-count and endpoint contract.
- [x] Add this issue to `tasks/backlog/bugs/index.md`.

## Acceptance criteria
- [x] No count can underflow an index, divide by zero, or write outside the returned vector.
- [x] For every valid sphere (finite center and finite nonnegative radius), each emitted point is finite and lies on the requested sphere within the documented tolerance.
- [x] Lattice behavior for counts greater than two changes only where required to repair the two explicit endpoint variants.
- [x] The focused geometry tests and layering checks pass.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicGeometryTests IntrinsicTests
ctest --test-dir build/ci --output-on-failure -R 'SphereSampling' --timeout 120
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
python3 tools/repo/check_layering.py --root src --strict
python3 tools/repo/check_test_layout.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Shipping the fix without exhaustive small-count regression coverage.
- Adding a second sphere-sampling abstraction or changing unrelated random-sampling semantics.

## Maturity
- Target: `CPUContracted`; no `Operational` follow-up is owed.
