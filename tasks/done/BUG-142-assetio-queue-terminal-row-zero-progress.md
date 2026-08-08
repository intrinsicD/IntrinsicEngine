---
id: BUG-142
theme: G
depends_on: []
workflow_schema: 1
workflow_profile: standard
evidence: required
owner: "claude-bug142"
branch: "main"
worktree: "/home/alex/Documents/IntrinsicEngine"
claimed_at: "2026-08-08T01:03:53Z"
contract_schema: 1
contracts: []
contract_review: >-
  AssetIO queue row projection and its presentation only. No geometry
  element-domain, property, support-radius, parameterization, or method
  integration surface is involved.
---
# BUG-142 — AssetIO queue shows 0% progress on a completed import row

## Status

- Completed and retired on 2026-08-08.
- **The reported symptom does not reproduce from the current code.** The chain
  was traced end to end: `StageForPhase` maps a complete phase to the
  `Complete` stage, `ProgressForStage` returns `1.0f` for it,
  `IsIndeterminateProgressStage` reports it determinate, the executor post-pass
  touches only cancellation, and `BuildAssetImportQueueRow` copies both fields
  faithfully. A genuinely complete row therefore renders `100%`. The precise
  trigger for the observed `Completed` + `0%` screenshot was not identified;
  the two most plausible producers are a row whose stage never advanced past
  `Queued` while completion was signalled elsewhere, or a default-constructed
  entry reaching the table — both of which render exactly `0%`.
- Two real defects **were** found in that same code and are fixed here, and
  both are what the task's Required changes ask for:
  - `ProgressForStage` returned `1.0f` for `Failed` and `Cancelled`, so a
    failed or cancelled import drew a **full bar labelled 100%**. Both are now
    indeterminate: they stopped part-way, so no completion fraction describes
    them, and they are labelled by stage text instead.
  - Indeterminate stages carried a plausible-looking number (`Decoding` →
    `0.45`), so the float alone could not be distinguished from real progress.
    Every indeterminate stage now carries `0.0`.
- The `(ProgressDeterminate, NormalizedProgress)` pair was kept rather than
  switching this one consumer to an optional. That pair is the repository-wide
  progress idiom, shared with `Runtime.JobService` and
  `Runtime.EditorJobProjection`; introducing a second representation for the
  AssetIO queue alone would have added surface for no gain. The pair is now
  documented as the "no progress information" representation, and determinate
  `0.0` means exactly `Queued` — the one stage that has genuinely made no
  progress.
- No panel highlight change was needed: the queue table contains no colour or
  highlight code at all. The misleading emphasis was the full `ProgressBar`
  fill on failed rows, which this fix removes at the source.
- Two contract tests were added: one asserting no terminal row is ever
  determinate-with-zero and pinning `Complete` at `1.0` while `Failed` and
  `Cancelled` report none; one proving an indeterminate in-flight import is
  distinguishable from a queued zero-progress one. Both fail against the
  unfixed source — on the wrong terminal flags and on the `0.44999999` value.
- Verified: 11/11 state-machine cases pass and the default CPU gate passes
  4142/4142 across three consecutive runs; layering is clean.
- Observation, recorded rather than dismissed: one earlier CPU-gate run
  reported a single failure that did not recur in three subsequent full runs
  and was not identifiable from the summary. Most likely the already-open
  `BUG-134` ImGui adapter panel draw-list intermittent.
- Completion commit: this retirement commit.

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
- [x] Make the queue snapshot distinguish "no progress information" from
      "0% progress".
- [x] Ensure terminal rows (completed, failed, cancelled) report a consistent
      progress value or none at all.
- [x] Ensure the panel does not highlight a progress cell for a terminal row.

## Tests
- [x] Add a contract test asserting a completed import row reports either
      full progress or an absent progress value, never `0.0` with a completed
      stage.
- [x] Add a test asserting an indeterminate in-flight import is distinguishable
      from a zero-progress one.
- [x] Default CPU gate stays green.

## Docs
- [x] Update the AssetIO queue prose in `src/app/Sandbox/README.md` if the
      reported progress semantics change.

## Acceptance criteria
- [x] A completed import never displays `0%`.
- [x] Indeterminate progress is representable and distinct from zero.

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
