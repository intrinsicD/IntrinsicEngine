---
id: GEOM-117
theme: I
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Operator-requested backlog note from the BUG-240 diagnosis; the implementation owes its own regression, timing and any numerics review.
contract_schema: 1
contracts: []
contract_review: Solver-cost change inside an existing CPU reference; no engine surface, domain or publication contract changes.
---
# GEOM-117 — Cheaper noise-level weight search for the variational fit

## Goal
- `FitFidelity::NoiseLevel` in `src/geometry/Geometry.HarmonicField.Fit.cpp` finds its data weight
  with far fewer fit solves, so the noise-level cases no longer dominate the CPU and sanitizer lanes.

## Context
- Source: the BUG-240 diagnosis (2026-10-24, operator decision to file it as its own product task).
- The weight search bisects log(weight) over 16 decades around the balanced weight, up to 100 steps,
  and each step runs a full fit (the reweighted L1 path re-solves to convergence every time). One
  temporary probe counted about 2327 operator applications for one noise-level fit, against 11 for
  one fixed-weight fit.
- Cost locally (ASan, serial): the "noise-level fidelity" step takes 14.90 s of the 22.96 s of
  `PropertySmoothingWorkflows.VariationalFitVariantsAndSolverSwitches`, and the same search dominates
  `VariationalFit.NoiseLevelChoosesTheWeightWhoseResidualMatches` (22.1 s inside
  `IntrinsicGeometryTests.Grouped`). BUG-240 doubled the sanitizer-lane budgets; that budget does not
  remove this cost.
- Candidate directions (none chosen): warm-starting each step from the previous fit, a secant or
  safeguarded-Newton step on log(weight) instead of pure bisection, a narrower initial bracket, or a
  looser inner tolerance during the bracket phase and full tolerance at the final weight.
- Contract today (`docs/methods/property-smoothing.md`): the mass-weighted RMS residual equals the
  noise level to `1e-4` relative; ADMM factors once per run, including every bisection step.

## Acceptance criteria
- [ ] The noise-level search needs measurably fewer fit solves or operator applications (counted from
      `Stats.Solves`/iterations, not only wall time) on the two cases above.
- [ ] Either the numerics are unchanged (same chosen weight and values within the existing test
      tolerances, the `1e-4` residual contract and `NoiseTargetClamped` behaviour), or the numerics
      change is stated, reviewed independently, and the method doc and tests are updated with it.
- [ ] `VariationalFit.*` and `PropertySmoothingWorkflows.*` pass; their local ASan timings are recorded
      before and after.

## Verification
```bash
cmake --build --preset ci --target IntrinsicCpuTests
ctest --test-dir build/ci --output-on-failure -R '^VariationalFit\.|^PropertySmoothingWorkflows\.VariationalFit'
cmake --preset ci-asan && cmake --build --preset ci-asan --target IntrinsicCpuTests
ctest --test-dir build/ci-asan --output-on-failure --parallel 1 -R 'NoiseLevelChoosesTheWeight|VariationalFitVariantsAndSolverSwitches'
```
