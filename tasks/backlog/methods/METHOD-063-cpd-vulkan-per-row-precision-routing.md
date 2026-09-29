---
id: METHOD-063
theme: I
depends_on: [METHOD-056]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: interactive backlog note from the METHOD-056 review round (2026-09-29); implementation owes its own tests, gpu;vulkan smoke and a resealed scaling run.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-063 — Per-row precision routing for the Vulkan CPD E-step

## Goal
- The METHOD-056 precision guard judges a whole iteration by its worst row (largest relevant
  fp32 exponent and coordinate term), so a few far rows (the fixture's uniform clutter) keep
  most mid-sigma iterations on the CPU: 1.6x over the CPU twin at 10^5 points instead of 9.8x
  without the guard (C117). Route per row instead: rows whose own first-order estimate exceeds
  `kExternalErrorLimit` are masked out of the device target pass (no contribution to P1/PX),
  evaluated exactly on the CPU (their denominators, Pt1 and P1/PX contributions, O(rows x M)),
  and merged in fixed order.
- Also evaluate lowering the coordinate term by centring coordinates per target tile before the
  fp32 conversion.

## Acceptance criteria
- [ ] Default gate: mock evaluator parity with masked rows (the review's two-cluster + far-row
      counterexample), statistics equal to CPU dense within 1e-12 for the CPU rows.
- [ ] gpu;vulkan smoke adds that fixture: every admitted row within the 2e-5 estimate, overall
      parity within the frozen 1e-5.
- [ ] Resealed scaling run; C117 revised with the recovered speedup (or the measured reason not).

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'CoherentPointDrift' -L 'gpu|vulkan' --timeout 300
```
