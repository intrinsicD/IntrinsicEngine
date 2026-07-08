---
id: BUG-062
theme: G
depends_on: []
---
# BUG-062 — Warm-configure CI budget (10 s) flakes on shared-runner variance

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
- [ ] If `INFRA-001` (warm-cache CI timing evidence) is still open at
      closure time, record the 2026-07-08 measurement series there as
      calibration evidence.

## Acceptance criteria
- [ ] A full PR CI round completes with no "Warm configure budget
      exceeded" kill at the new budget.
- [ ] The guard still hard-fails when the budget is exceeded (semantics
      unchanged; only the value moved).

## Verification
```bash
grep -rn "max-warm-seconds" .github/workflows/
# CI evidence: all five workflows pass their configure step on the next
# PR #1010 round.
```

## Forbidden changes
- Deleting the guard, its hard-fail semantics, or the timing telemetry.
- Batching unrelated workflow changes into this fix.
