---
id: METHOD-063
theme: I
depends_on: [METHOD-056]
maturity_target: ParityProven
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Finished interactively; the performance and parity result is ARA claim C117 with sealed, source-bound evidence in ara/evidence/diagnostics/method063_cpd_vulkan_per_row_20260929/.
contract_schema: 1
contracts: [method.engine-integration]
---
# METHOD-063 — Per-row precision routing for the Vulkan CPD E-step

## Completion — 2026-09-29
Commit: `83e5b533f` (float-float coordinates, per-row estimates, skipped device rows merged
from `ExactRow` + `SourceRowTerms`, counts in results, tests); the evidence commit on
`claude/cpd-nystrom` follows. ParityProven on the recorded host (C117): 9.3x over the CPU twin
at 10^5 points with 21 of 48 device iterations, as many as before the precision guard. On the
scaling fixture no row needed the CPU (its clutter rows are negligible against the uniform
term), so the gain there comes from the float-float coordinates; the per-row CPU path is
exercised by the review's two-cluster fixture (unit test exact to 1e-12, smoke within 1e-7).
Tile-centring was not needed.

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
- [x] Default gate: mock evaluator parity with masked rows (the review's two-cluster + far-row
      counterexample), statistics equal to CPU dense within 1e-12 for the CPU rows.
- [x] gpu;vulkan smoke adds that fixture: every admitted row within the 2e-5 estimate, overall
      parity within the frozen 1e-5.
- [x] Resealed scaling run; C117 revised with the recovered speedup (or the measured reason not).

## Engine integration

| Field | Disposition |
| --- | --- |
| Least-structured input | Two point spans (unchanged). |
| Compatible entity sources | Every canonical point domain via RUNTIME-273 (unchanged). |
| RuntimeModule | Existing `Runtime.CoherentPointDriftGpuEStep` broker (unchanged interface). |
| Config/agent | Unchanged `e_step` value `vulkan`; results keep `e_step_device_iterations`. |
| UI | Unchanged CPD panel Performance node. |
| Publication | Unchanged. |
| End-to-end tests | The existing gpu;vulkan editor-command smoke plus the new masked-row fixture. |

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R 'CoherentPointDrift' -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 120
DISPLAY=:7 ctest --test-dir build/ci-vulkan --output-on-failure -R 'CoherentPointDrift' -L 'gpu|vulkan' --timeout 300
```
