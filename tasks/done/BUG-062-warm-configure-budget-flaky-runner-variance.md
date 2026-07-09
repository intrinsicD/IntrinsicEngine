---
id: BUG-062
theme: G
depends_on: []
---
# BUG-062 — Warm-configure CI budget (10 s) flakes on shared-runner variance

## Status

- **Closed 2026-07-08.** Commit/PR: pull request #1010 (merge commit:
  `977244d`).

## Goal
- Stop the `tools/ci/time_command.py --max-warm-seconds 10` guard from
  killing CI jobs before their build step on ordinary shared-runner
  variance, while keeping the warm-configure regression gate that
  `INFRA-001` introduced.

## Non-goals
- No removal of the guard or its telemetry JSON — the warm-cache timing
  evidence stays.
- No change to `time_command.py` semantics (hard-fail over budget with an
  exact vcpkg cache hit stays; only the budget value moves).
- No runner/self-hosting changes.

## Context
- Symptom: on 2026-07-08, five workflows (`pr-fast` via `ci-linux-clang`
  configure step, `ci-sanitizers` asan+ubsan, `ci-bench-smoke`,
  `ci-vulkan`) failed with "Configure … took N s with an exact vcpkg
  cache hit; limit is 10.000 s" across three different heads of
  PR #1010 **including a markdown-only diff (PR #1009 run 510)**.
  Measured: 10.060 s, 11.455 s, 12.625 s, 14.797 s. Every kill happens
  before the build step, so the jobs never compile the change under
  test.
- Expected behavior: the budget catches genuine configure-time
  regressions but tolerates GitHub shared-runner variance (same-cache
  runs measured 9.4–14.8 s within two hours).
- Impact: any PR can red every merge-gating workflow with no relation to
  its diff; PR #1010's actual compile verification was blocked twice.
- Root cause: the 10 s budget was calibrated near the runner *median*
  (successful runs at ~9.4 s configure), leaving no headroom for the
  observed ~±50 % shared-runner spread.

## Required changes
- [x] Raise `--max-warm-seconds` from 10 to 20 in all seven invocations
      across the six workflows: `.github/workflows/pr-fast.yml`,
      `ci-linux-clang.yml`, `ci-sanitizers.yml`, `ci-bench-smoke.yml`,
      `ci-vulkan.yml`, `nightly-deep.yml` (2 call sites)
      (~35 % headroom over the worst observed 14.797 s while still
      bounding regressions; the telemetry JSON keeps recording exact
      values for future recalibration).

## Tests
- [x] Evidence run: the PR #1010 CI round after this change must pass the
      configure step in all five workflows (verified in CI — this
      environment cannot execute GitHub runners locally).

## Docs
- [x] Calibration evidence recorded in this task's Context (2026-07-08
      measurement series); `INFRA-001` tracks only deprecation cleanup and
      can reference it from here.

## Acceptance criteria
- [x] Three consecutive PR #1010 CI rounds (heads `e732e69`, `5d0c773`,
      `635915a`) completed all configure steps with zero "Warm configure
      budget exceeded" kills at the 20 s budget.
- [x] The guard still hard-fails when the budget is exceeded (semantics
      unchanged in `tools/ci/time_command.py`; only the workflow value
      moved).

## Verification
```bash
grep -rn "max-warm-seconds" .github/workflows/
# CI evidence: all five workflows pass their configure step on the next
# PR #1010 round.
```

## Forbidden changes
- Deleting the guard, its hard-fail semantics, or the timing telemetry.
- Batching unrelated workflow changes into this fix.
