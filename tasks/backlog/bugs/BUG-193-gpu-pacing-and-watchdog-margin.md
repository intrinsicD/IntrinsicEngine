---
id: BUG-193
theme: G
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: Interactive cleanup and follow-up tracking; no new performance or capability claim.
contract_schema: 1
contracts: [repo.task-contract-discovery]
---
# BUG-193 — Investigate GPU pacing variability and watchdog margin

## Goal
Explain variable Vulkan acceptance pacing under controlled host/display conditions
and evaluate the remaining WLOP watchdog margin. BUG-189/191/192 repaired fixture
budget mismatches; they did not establish an engine pacing fix or a speedup.

## Evidence
RUNTIME-244 surface-appearance evidence records display-off low clocks and variable
runs, without a new controlled display-state comparison. RUNTIME-236's combined
LBVH sequence passed after its finite-work fixture deadline correction, but its
WLOP case took about 264 seconds against the unchanged 300-second CTest limit.
Keep these historical observations distinct from a causal diagnosis.

BUG-194 follow-up on 2026-09-14 used the actual dragon and the real keypoint
panel with AsyncWorkModule, ci-vulkan and existing frame-phase diagnostics.
Before and after Show, total frames were roughly one second; presentation
accounted for about 0.65–0.77 seconds, UI for 0.12–0.13 seconds, and renderer
preparation for 0.09 seconds. The Show frame added roughly 0.05 seconds of UI
work, and displaying the scalar added roughly 0.049 seconds of extraction per
frame. These are local diagnostic samples, not matched benchmark evidence or a
confirmed presentation cause. The user reports responsive UI/camera during the
missing saliency display, so this slow-frame probe does not reproduce that
failure. Source/host display-state controls remain necessary here. Full local
logs: `build/analysis/bug194-saliency-followup-2026-09-14/dragon-ui-pacing.log`.

## Acceptance criteria
- [ ] Reproduce serially with no concurrent compilation, fixed source and recorded
  GPU/display/power state; compare display states when supported.
- [ ] Separate presentation pacing, asynchronous job progress and fixture scheduling
  using bounded diagnostics and finite-work/timeout reasons.
- [ ] Repair the confirmed owner and verify finite progress with adequate measured
  margin while preserving pixel, history, backend and sanitizer assertions.

## Verification
Use the supported ci-vulkan preset and exact registered surface-appearance,
distance-ratio and consolidation LBVH cases recorded in the retired bug notes.
Run capability tests serially after compilation, retaining raw diagnostics and
actual backend identity. Do not weaken assertions or infer leak freedom from
cohorts with leak detection disabled; BUG-180 owns that separate investigation.
