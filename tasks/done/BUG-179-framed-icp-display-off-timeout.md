---
id: BUG-179
theme: none
depends_on: []
template: micro
workflow_schema: 1
workflow_profile: micro
evidence: not_applicable
evidence_skip_reason: "Interactive test-harness diagnosis; no new engine capability or performance claim."
contract_schema: 1
contracts: []
contract_review: "Existing GPU correctness and sanitizer test policies apply; no new engine API or ownership contract."
owner: codex-interactive
branch: main
worktree: /home/alex/Documents/IntrinsicEngine
claimed_at: "2026-09-08T14:40:00Z"
---
# BUG-179 — Framed ICP comparison exceeds the cohort timeout with display off

## Goal
- Keep all seven CPU/GPU registration comparisons and index-reuse assertions executable under diagnosed display-off present pacing, with a bounded, informative failure if progress stalls.

## Observation ledger
- The original cohort passed in 4.46 s (LBVH queries) and 9.39 s (registration) earlier in the session.
- After final rebuild, queries passed in 16.73 s; registration hit its discovered CTest `TIMEOUT 30`, which takes precedence over command-line `--timeout 120`. `/tmp/icp-gpu-final2.log` preserves the failure.
- Read-only `xset q` reported `Monitor is Off`. This matches the independently diagnosed per-frame throttling in `BUG-143`.
- Ranked explanations: display present pacing (predict completion beyond 30 s with consistent per-run costs); stalled GPU query (predict no completion under a longer bound); cold pipeline compilation (predict most delay before the first GPU run).
- Unchanged test under a diagnostic 120 s bound completed all seven comparisons in 46.715 s. Each GPU registration cost approximately 13 s, versus approximately 1.5 s earlier. All transform, RMSE, backend, live-row and cache assertions passed. This contradicts a stalled query and a first-use-only delay. `/tmp/icp-gpu-display-off-diagnostic.log` records the run; its separate process-exit LSan finding is owned by `BUG-180`.

- The first timeout-property fix used CMake `if(TEST ...)` in a CTest-loaded include; this condition did not see the discovered test, so the second cohort still timed out at 30 s. The fix now follows the existing property writer: guard on the published GoogleTest list. The CTest JSON registry must confirm 120 s before another run.

## Change
- Keep completion-driven exit after seven rounds; add a 70 s wall-clock bound and report completed rounds/frames on exhaustion.
- Apply `TIMEOUT 120` only to this multi-run test after GoogleTest discovery. No correctness tolerance, benchmark threshold, label, or sanitizer environment changes.
- Add elapsed wall time and frame count to optional benchmark diagnostics. Display-off timings remain diagnostic; the original 5 s benchmark threshold is preserved.

## Acceptance criteria
- [x] Diagnose the 30 s timeout with unchanged algorithm/assertions.
- [x] Bound stalled work by elapsed time and retain useful failure diagnostics.
- [x] Pass both LBVH tests with the display still off, preserving all assertions.

## Verification
```bash
cmake --build --preset ci-vulkan --target IntrinsicPointLBVHGpuTests
ctest --test-dir build/ci-vulkan --output-on-failure -R '^PointLBVHGpuSmoke\.' -L gpu -L vulkan --timeout 120
python3 tools/agents/check_task_policy.py --root . --strict
```

Implementation is committed; see the completion record below. See the [ICP review](../../docs/reviews/2026-09-08-icp-spatial-integration.md).

Final CTest registry confirmed 30 s for the query test and 120 s only for the framed comparison. The display-off cohort passed 2/2 in 63.60 s (16.74 s queries, 46.72 s registration). The final registration run used 45 frames and built each target index once. Its sealed runtime benchmark reports a failed timing disposition at 12,999.1 ms versus the unchanged 5,000 ms threshold; transform error is zero.

## Completion — 2026-09-09

- PR/commit: `3276c70597761b089bea0364e3fad7eb091fa869`

Completed in implementation commit `3276c70597761b089bea0364e3fad7eb091fa869`. Operational test-harness correction. The recorded display-off Vulkan run completed all seven ICP comparisons within the diagnosed bound. The original benchmark timing threshold is unchanged; BUG-180 retains the separate leak-enabled process-retention investigation.

Publication verification on the combined source: Clang 23 `ci` configured; `IntrinsicTests` and `ExtrinsicSandbox` built. The full CPU selector passed 4,363 tests with one expected unsanitized GLFW/LSan skip and zero failures (115.50 s). Strict layering, task policy, doc links, test layout, manifests and skill mirrors pass. GPU/sanitizer evidence above is from 2026-09-08; this publication check did not rerun those lanes.
