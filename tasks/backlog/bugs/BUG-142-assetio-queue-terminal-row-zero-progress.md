---
id: BUG-142
theme: G
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
contract_review: >-
  AssetIO queue row projection and its presentation only. No geometry
  element-domain, property, support-radius, parameterization, or method
  integration surface is involved.
---
# BUG-142 — AssetIO queue shows 0% progress on a completed import row

## Goal
- Make the AssetIO queue progress cell agree with the row's terminal state, so
  a completed import never displays `0%`.

## Non-goals
- No redesign of the AssetIO queue window or its column set (`UI-049` owns
  readability/column sizing).
- No change to import execution or staging.

## Context
- Symptom: after a successful `sculpt.obj` import, the AssetIO queue row shows
  stage `Completed` and elapsed `0.29 s`, while the progress cell renders a
  highlighted yellow **`0%`**. Reproduced on both the drag-and-drop and
  `Import asset` button routes.
- Expected behavior: a terminal row shows 100%, or shows no progress value at
  all — an indeterminate-progress import should not fall back to a literal `0%`
  once it has completed.
- Impact: low severity but actively misleading — the row simultaneously claims
  "completed" and "no progress made", and the highlight draws the eye to the
  wrong signal.
- Owner: `runtime` asset-workflow import queue snapshot projection; the app
  panel renders whatever the snapshot reports.
- Likely adjacent: the same snapshot drives the determinate/indeterminate stage
  labelling described in `src/app/Sandbox/README.md`; check whether
  indeterminate imports report `0.0` rather than "unknown".

## Required changes
- [ ] Make the queue snapshot distinguish "no progress information" from
      "0% progress".
- [ ] Ensure terminal rows (completed, failed, cancelled) report a consistent
      progress value or none at all.
- [ ] Ensure the panel does not highlight a progress cell for a terminal row.

## Tests
- [ ] Add a contract test asserting a completed import row reports either
      full progress or an absent progress value, never `0.0` with a completed
      stage.
- [ ] Add a test asserting an indeterminate in-flight import is distinguishable
      from a zero-progress one.
- [ ] Default CPU gate stays green.

## Docs
- [ ] Update the AssetIO queue prose in `src/app/Sandbox/README.md` if the
      reported progress semantics change.

## Acceptance criteria
- [ ] A completed import never displays `0%`.
- [ ] Indeterminate progress is representable and distinct from zero.

## Verification
```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure \
  -R 'AssetImportQueue|AssetWorkflow|SandboxEditor' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
```

## Forbidden changes
- Hiding the progress column instead of fixing the reported value.
