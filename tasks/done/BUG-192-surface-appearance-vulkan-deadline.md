---
id: BUG-192
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive diagnosis of a Vulkan acceptance timeout; no performance claim.
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-192 — Diagnose the surface-appearance Vulkan acceptance deadline

## Completion — 2026-09-14
Completed locally and retired after acceptance/evidence review. Accumulated
implementation commit: `8a35af54aa70c8e7a7f4bebe48ceabc1eda1e186`.
Historical dirty-source measurements retain their original eligibility limits;
this retirement is not a publication or whole-engine completion verdict.
Closure covers the registered fixture budget correction for observed finite
work. BUG-193 owns unexplained pacing variation and the WLOP timeout margin;
no engine speedup or pacing repair is established here.


## Goal
Resolve the SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes timeout
without weakening backend, pixel, history, normal-encoding or sanitizer checks.

## Evidence and diagnosis
During RUNTIME-244 the registered ci-vulkan case hit its inherited 30-second
CTest deadline, before its own 45-second/240-frame watchdog. The unchanged case
had passed in 12.27 seconds earlier. Two exact-environment direct probes took
51.565 and 51.615 seconds, reaching phase 14 after the earlier color, history and
vertex-normal assertions passed. The second retained the fixture's existing
ExitSummary: phase=14, settle=2, frames=47, ready=never, wall-clock-budget-exhausted.
The frame cap was not reached; incomplete face-normal readbacks caused the pixel
failures. The test requires at least 52 transition/settling frames without bake
latency. This demonstrates insufficient wall-clock allowance, not a lost command.

Read-only observations showed Monitor is Off and RTX 3050 driver 590.48.01 at
P8/210 MHz. The frame count/time is consistent with prior BUG-143/179 presentation
pacing evidence; no new controlled on/off comparison was performed. The final
run was faster while the post-run display still reported off, so timing is
variable and no constant frame rate or new host-causality claim is inferred.

## Acceptance criteria
- [x] Preserve initial failure, exact source/registry and ranked hypotheses.
- [x] Distinguish watchdog exhaustion from a frame cap or stationary workflow.
- [x] Apply the evidence-backed test-only correction and review it with Claude.
- [x] Pass the original registered Vulkan case with all assertions and labels.

## Verification
```bash
ctest --test-dir build/ci-vulkan --output-on-failure -L gpu -L vulkan -R '^RuntimeSandboxAcceptanceGpuSmoke\.SurfaceAppearanceBakesSelectedPropertyAndRestoresAttributes$' --no-tests=error --timeout 120
```

## Reviewed correction and result
The internal budget is now 90 seconds and this test's CTest property is 120,
allowing fixture failure diagnostics and shutdown to finish. The 240-frame cap,
all nine readbacks, settling stages, pixel tolerances, backend, labels and
sanitizer environment are unchanged. Existing ExitSummary appears in failure
traces and ReadyObserved is asserted explicitly. No production file changed.
Claude independently reviewed the exact diff and found no blocker.

The corrected registered test passed on actual Vulkan in 30.4 seconds, without
skipping. Registry comparison shows only TIMEOUT changed for this case. All CPU
registrations remain identical; reconciled native/ASan/UBSan editor checks passed
102/102 each, preserving the already-passing full CPU gates on unchanged
production inputs. Implemented, verified and locally integrated.

Evidence: `build/analysis/runtime244-editor-command-frame-locality-2026-09-14/`,
including `vulkan-diagnosis.md`, initial failure, both diagnostic runs, fixed
Claude review, source snapshots, registry comparisons and final passing log.
