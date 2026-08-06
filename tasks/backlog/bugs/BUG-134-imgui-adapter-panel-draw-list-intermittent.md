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
# BUG-134 — ImGui adapter panel draw-list test fails intermittently

## Goal

- Make `ImGuiAdapter.EditorPanelDrawProducesNonEmptyDrawList` deterministic or
  repair the underlying adapter defect once a reproducible failing state is
  captured.

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
- Expected behavior: the callback's explicitly positioned and sized panel
  produces a non-empty ImGui draw list on every initialized Null/headless
  adapter frame.
- Impact: a recurrence can make the required full CPU gate nondeterministic.
  This task is nonblocking for `REVIEW-003` while its fresh audit gate remains
  green; any recurrence during that audit promotes this task to a static
  dependency before the audit can retire.

## Required changes

- [ ] Preserve the exact failed assertion and adapter/ImGui diagnostics on the
      next recurrence, then reduce the failure to the smallest deterministic
      ordering, state, or environment condition.
- [ ] Classify whether the defect is test setup, ImGui frame lifecycle, font
      atlas state, callback execution, or draw-data translation using evidence
      rather than a retry-based guess.
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
