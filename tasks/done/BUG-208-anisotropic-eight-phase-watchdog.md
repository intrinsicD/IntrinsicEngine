---
id: BUG-208
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive test-fixture diagnosis; retained failure and corrected execution provide evidence without a performance claim.
contract_schema: 1
contracts: []
contract_review: Catalog reviewed; this corrects a GPU fixture watchdog for its existing finite phase sequence, with no algorithm, binding or backend contract change.
---
# BUG-208 — Match anisotropic consolidation watchdog to eight phases

## Goal
Let the existing anisotropic WLOP GPU regression finish its normal-estimation
phase under the same finite-work budget as the other eight-phase variant.

## Evidence and diagnosis
RUNTIME-270's full serial Vulkan selector reproduced an overall watchdog exit
in `VulkanLbvhAnisotropicWlopAcrossDomainsAndFailures`: phases zero through six
took about 266 seconds, leaving only 35 seconds for the unfinished eighth phase.
The registered CTest limit was 360 seconds, but the fixture itself exited at 300.
See [failure output](../evidence/BUG-208/anisotropic-eight-phase-timeout.log).
The existing fixture gives EAR's eight phases 480 seconds internally and 540 in
CTest, while anisotropic WLOP incorrectly shares the seven-phase 300/360 budget.
`UsesNormals()` governs the eighth phase for both variants and is the correct
existing predicate. Earlier stale/cancel publisher rejections are expected phases,
not the reported failure. The fixture already disables VSync and uses a 64x64 window.

The fixture was unchanged by RUNTIME-270 before this correction. That task's
consolidation edit removes UI name-based normal guessing; this test supplies
explicit normals and deliberately resets them only for the estimation phase.
[BUG-193](../backlog/bugs/BUG-193-gpu-pacing-and-watchdog-margin.md) retains the broader controlled
GPU/display pacing investigation; this task makes no engine pacing or speed claim.

## Acceptance criteria
- [x] Identify the actual unfinished phase and finite-work budget mismatch.
- [x] Use the existing eight-phase allowance for both normal-consuming variants,
  preserving phase completion, numerical tolerance, history, backend and sanitizer assertions.
- [x] Independently review the correction and pass the complete anisotropic sequence
  serially after compilation, recording GPU/display state and completion time.

## Verification
```bash
cmake --preset ci-vulkan
cmake --build --preset ci-vulkan --target IntrinsicRuntimePointCloudConsolidationGpuParityTests
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointCloudConsolidationGpuParity.VulkanLbvhAnisotropicWlopAcrossDomainsAndFailures$' --timeout 120
```

Independent fixed-diff review found no blockers: `UsesNormals()` selects both
the eight-phase termination and the new allowance. LOP and seven-phase
WLOP/CLOP budgets, labels and all assertions are unchanged. The final serial execution passes.

## Completion — 2026-09-22
Retired as a test-fixture contract correction; no backend maturity promotion.
PR/commit: `7dbe421e8`. The complete anisotropic sequence passes in 335.93 seconds
with all eight phases and the original completion, numerical, history and backend
assertions. This exceeds the old 300-second fixture limit and fits the existing
480-second eight-phase allowance. CTest retains its 60-second shutdown margin.
The run was serial after compilation, alongside no CPU test suite, using the
recorded RTX 3050/display-off configuration. See [final execution](../evidence/RUNTIME-270/gpu-final-run.log)
and [GPU state](../evidence/RUNTIME-270/gpu-host.csv). Independent review found no
blockers. BUG-193 retains the separate pacing investigation; no further work is
required for this finite-work budget correction.
