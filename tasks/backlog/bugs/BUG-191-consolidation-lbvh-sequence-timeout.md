---
id: BUG-191
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnosis of an existing Vulkan verification timeout; no performance claim.
contract_schema: 1
contracts: []
contract_review: Existing GPU test and timeout policies apply; no engine algorithm, backend contract or publication behavior changed in this diagnosis so far.
---
# BUG-191 — Consolidation LBVH sequence reaches its watchdog

## Goal
Determine whether the LOP and EAR multi-domain failure sequences stall or simply
exceeds its time budget, preserving all numerical, publication and backend checks.

## Evidence and ranked explanations
During RUNTIME-236 on 2026-09-13,
`PointCloudConsolidationGpuParity.VulkanLbvhMovingStepsAcrossDomainsAndFailures`
failed after 153.82 seconds: its internal 150-second watchdog set `TimedOut`,
and `Done` remained false. Two rejected bool completion envelopes from
`Consolidation Vulkan neighborhoods` preceded the failure; these could belong
to the deliberately failing capacity/stale phases. The assertion printed no
phase or pending-result count.

All eight consolidation files, the test, `JobService.cpp` and
`SpatialIndexCache.cpp` are byte-identical to the start-of-turn snapshot.
The basic GPU LOP/WLOP reference and child-mesh publication cases passed.
EAR also reached its 300-second internal watchdog (303.90 seconds total),
after the same two rejected completion envelopes. The WLOP version passed in
263.97 seconds, anisotropic WLOP in 208.56 seconds and CLOP in 138.81 seconds;
each has an existing 300-second internal watchdog. LOP uses 150 seconds. No concurrent
build or other test suite ran during this selection.

Initial ranking: a stalled cancellation/publication phase; finite work exceeding
the budget at current display-off frame pacing; backend readiness/driver stalls.
The WLOP pass raises the finite-work explanation. A phase/result-count/elapsed
probe in the existing LOP harness will discriminate: a late phase still advancing
supports budget exhaustion; a cancelled job with no terminal event and no phase
progress supports an actual reconciliation defect. No deadline has changed yet.

Raw evidence: `/tmp/intrinsic-runtime236-20260913/vulkan-focused.log` and
`consolidation-source-comparison.json`; final archive under
`build/analysis/runtime236-remaining-processing-2026-09-13/`.
Claude's fixed-source review found that cancellation is checked before GPU
readiness and delivers a terminal event from the first unpublished finalizer.
An individual pending spatial batch has no deadline. Each request has eleven
dependent jobs, and the eight concurrent requests in each initial phase share
the eight-completions-per-frame drain budget. The test-local probe records
completed phase durations and prints the current phase/result count on failure;
all original budgets and assertions remain in effect.

## Acceptance criteria
- [x] Preserve the original failure and compare exact source state.
- [x] Identify the stalled or incomplete phase with discriminating evidence.
- [x] Apply only an evidence-backed fix and retain all correctness checks.
- [x] Re-run the original registered backend cases and remove temporary probes.

## Verification
```bash
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointCloudConsolidationGpuParity.VulkanLbvhMovingStepsAcrossDomainsAndFailures$' --no-tests=error --timeout 120
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^PointCloudConsolidationGpuParity.VulkanLbvhEarAcrossDomainsInsertionAndFailures$' --no-tests=error --timeout 120
```

## Diagnosis and reviewed correction — 2026-09-13

Both isolated cases reproduced with their original deadlines. LOP stopped in
phase 6 (its final convergence phase) after 15.0 seconds there; completed phases
0–5 took 40.7, 41.0, 7.0, 3.0, 4.0 and 40.0 seconds. EAR stopped in phase 7
(its final normal-estimation phase) after 13.0 seconds there; completed phases
0–6 took 67.7, 68.0, 6.0, 4.0, 4.0, 69.0 and 69.0 seconds. In both cases,
cancellation phase 4 completed and its terminal-status assertion passed.
The accumulated completed work plus the current phase accounts for each
expired total budget. All preceding numerical, history and failure assertions
passed. This supports a budget shortfall and rules out a lost cancellation
event in these runs; it does not establish completion of the final phases.

The test-only correction increases LOP's internal/CTest budgets from 150/180
to 240/300 seconds and EAR's from 300/360 to 480/540 seconds. Other strategies
retain 300/360 seconds. There are no service, algorithm, fixture, assertion,
tolerance, backend, label or sanitizer-environment changes. The existing
registered tests remain the reproducers. Final registered execution is required
before calling the correction verified.

Claude reviewed the source trace and both phase probes, supported the correction,
and recommended retaining phase durations as permanent failure diagnostics.
That small test-local record stays alongside current phase/result counts;
the temporary debug tag is removed. Root narrowed Claude's claim that a pending
batch stall was excluded: the final phases still need to finish in the rerun.
The observed cadence agrees with the existing display-pacing diagnosis in
BUG-143/179; no new controlled display experiment or performance claim is made.
WLOP's passing 264 seconds against its unchanged 300-second internal budget has
less margin; it remains a bounded passing case, not an unobserved failure.

Raw records: `consolidation-probe-ledger.md`, `lop-phase-probe.log`,
`ear-phase-probe.log`, `claude-timeout-fix-review.txt`, and `timeout-final.diff`
in the evidence archive named above.


## Verified resolution — 2026-09-13

Both corrected original registered cases passed on the actual Vulkan backend
with combined ASan/UBSan instrumentation on NVIDIA GeForce RTX 3050, driver
590.48.01 (`gpu-host.log`). The evidence archive contains
`lop-final.log`, `ear-final.log` and their command/elapsed JSON records. LOP
completed all seven phases in 163.79 seconds, beyond its original internal
150-second limit. EAR completed all eight phases in 361.83 seconds, beyond its original internal
300-second limit (`ear-final.log`). No lost terminal event, numerical mismatch, history failure
or new Vulkan validation error was reported.

`timeout-registry-comparison.json` checks all 32 selected test registrations:
only these two TIMEOUT values differ; commands, working directories, labels,
sanitizer environments and all other properties are identical. The final
source contains no temporary debug tag. Phase-duration capture is retained
as permanent test failure context at Claude's recommendation. The unchanged
CPU locality checks also passed (19/19), and both ci and ci-vulkan test targets
rebuilt successfully. Implemented and verified, pending integration.
