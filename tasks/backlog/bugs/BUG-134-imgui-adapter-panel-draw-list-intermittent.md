---
id: BUG-134
theme: F
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner:
branch:
worktree:
claimed_at:
contract_schema: 1
contracts: []
contract_review: "Reviewed the full catalog; this task diagnoses an intermittent existing runtime contract-test failure and changes no reusable subsystem, data-domain, publication, or control-surface contract unless a later reproducible cause proves otherwise."
---
# BUG-134 — ImGui adapter frame timer intermittently undercounts a nested phase

## Goal

- Diagnose and repair the timer-containment failure in
  `ImGuiAdapter.EditorPanelDrawProducesNonEmptyDrawList`, preserving its
  non-empty draw-list coverage and all valid timing contracts.

## Non-goals

- No quarantine, retry wrapper, weakened assertion, global timeout increase,
  or exclusion from the default CPU selector.
- No production ImGui behavior change without a failing-state diagnosis that
  demonstrates the adapter, rather than the test setup, is wrong.
- No coupling to the unrelated `RUNTIME-216` render-extraction forwarding
  deletion during which the failure was observed.

## Context

- Symptom: on 2026-08-06, the default CPU selector at implementation commit
  `7e61e215` reported only
  `ImGuiAdapter.EditorPanelDrawProducesNonEmptyDrawList` failed. The other
  4,101 executed tests passed and the environment-gated GLFW/LSan case
  skipped.
- Immediate evidence: the exact case then passed ten isolated
  `--repeat until-fail:10` executions, and an immediate complete selector
  rerun passed all 4,103 selected cases with the same expected skip. The
  failure is therefore intermittent and currently lacks a stable repro.
- Expected behavior: the panel produces a non-empty ImGui draw list, and the
  whole-frame duration consistently contains its measured nested phases.
- Impact: a recurrence can make the required full CPU gate nondeterministic.
  The fresh `REVIEW-003` audit completed without recurrence, so that readiness
  gate retired cleanly and this bug remains an independent follow-up.
- **Recurrence captured 2026-08-09** during an unrelated `BUG-145` gate run, with
  the exact assertion this task asked to preserve. It is **not** the draw-list
  assertion the test is named for. Every draw-list, byte-count, and command
  assertion passed; the failure was
  `Test.ImGuiAdapter.cpp:298`,
  `EXPECT_GE(diag.LastEndFrameMicros, diag.LastEditorCallbackMicros)`, actual
  `12 vs 13`. That is a whole-frame duration compared against one of its own
  nested phases at microsecond resolution, where the two are within one tick of
  each other; an immediate rerun of the complete selector passed 4156/4156.
  This reframes the diagnosis: the candidate is the timing instrumentation
  (independent clock reads, rounding at 1 µs granularity, or a phase timer that
  is not strictly nested inside the frame timer), not the ImGui frame lifecycle
  or draw-data translation. The three `LastEndFrameMicros >= <phase>` assertions
  at `:298-300` share the shape.
- The captured recurrence establishes the failed timer assertion, not its
  root cause. Another passing run does not resolve this defect.

## Required changes

- [ ] Retain the captured `12 vs 13` timer failure and reduce it to a
      deterministic clock, rounding, timer-boundary, or state condition.
- [ ] Establish whether phase boundaries are actually nested and whether
      independent reads/conversion explain the one-microsecond inversion.
      Investigate lifecycle/draw translation only if new evidence points there.
- [ ] Implement the smallest deterministic correction at the owning surface;
      retain the existing non-empty draw-list assertion.

## Tests

- [ ] Add a deterministic regression for the captured cause.
- [ ] The exact case passes a bounded repeated isolated run and the complete
      default CPU selector passes without retry semantics.

## Docs

- [ ] Update this task with the captured cause and evidence; update runtime
      docs only if production behavior changes.

## Acceptance criteria

- [ ] The original intermittent condition is reproducible before the fix and
      clean under the same bounded stress after the fix.
- [ ] The test remains in the ordinary `contract;runtime` selector with its
      non-empty draw-list assertion intact.
- [ ] Strict layering and task-policy checks pass.

## Verification

```bash
ctest --test-dir build/ci --output-on-failure -R '^ImGuiAdapter\.EditorPanelDrawProducesNonEmptyDrawList$' --repeat until-fail:1000 --timeout 60
ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/repo/check_layering.py --root src --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes

- Marking the test `flaky-quarantine`, retrying it in CI, or accepting a
  passing rerun as the root-cause fix.
- Deleting the assertion or replacing it with callback-only coverage.
- Mixing unrelated ImGui, editor, renderer, or runtime cleanup into the fix.
