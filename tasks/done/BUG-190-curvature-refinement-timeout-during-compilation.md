---
id: BUG-190
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnosis of a local CPU verification timeout; no performance claim.
contract_schema: 1
contracts: []
contract_review: Existing CPU verification and timeout policies apply; no engine algorithm, API, benchmark or reusable contract change.
---
# BUG-190 — Curvature refinement test timeout during concurrent compilation

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.


## Goal
Distinguish local load from a regression without changing the registered test,
its assertions or its timeout.

## Evidence
During RUNTIME-236 on 2026-09-13, the full unsanitized CPU selector terminated
`CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve` at 30.03 seconds.
Its explicit CTest `TIMEOUT 30` overrides the command's `--timeout 60`.
This was the only failure among 4570 selected registrations (six skipped).
Two sanitizer builds, each with `--parallel 8`, overlapped that run.
The saved RUNTIME-235 CPU run passed this unchanged test in 16.60 seconds.

Raw failure: `/tmp/intrinsic-runtime236-20260913/cpu-full.log`.
Prior result: `/tmp/intrinsic-runtime235-remaining-20260913/cpu-full.log`.
Retained evidence will live in
`build/analysis/runtime236-remaining-processing-2026-09-13/`.

Ranked explanations: competing compilation load; unrelated host timing
variation; an indirect regression despite unchanged geometry/test source.
The discriminating check is the same registered test on the same binary after
all builds finish, followed by the complete CPU selector without other builds
or test suites. No timeout, label, assertion or sanitizer setting has changed.

## Acceptance criteria
- [x] Compare current and baseline geometry/test sources: all 224 geometry and test files compared are byte-identical.
- [x] Run the unchanged registered case without competing builds.
- [x] Reconcile the full CPU selector and record the supported conclusion.

## Verification
```bash
ctest --test-dir build/ci --output-on-failure -R '^CurvatureExtrema.RetriangulationAndRefinementRetainCenterCurve$' --no-tests=error --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --no-tests=error --timeout 60
```

## Resolution — 2026-09-13
The unchanged binary passed the isolated registered case in 17.76 seconds and
then passed the full CPU selector: 4569 passed, one ASan-only leak-control check
skipped, 126.06 seconds total. Both ran after all session builds and sanitizer
suites finished. These reruns used host execution, also allowing five windowing
cases that the initial sandboxed run skipped to execute. The pure geometry case
uses no windowing or sandbox capability.

The evidence is consistent with competing compilation load causing the earlier
deadline miss; it does not establish a general timing guarantee or uniquely
attribute all host variation. The session-local verification procedure now finishes all compilation
before serial test suites; this is sequencing discipline, not a shipped runner change. No engine code, registered deadline, assertion,
label, or sanitizer setting changed for this diagnosis. The failure and passing
reruns remain alongside the source comparison in the RUNTIME-236 archive.
Diagnosed and reconciled; this record is locally integrated.
