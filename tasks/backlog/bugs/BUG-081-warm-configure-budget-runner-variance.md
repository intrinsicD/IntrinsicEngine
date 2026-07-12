---
id: BUG-081
theme: G
depends_on: []
---
# BUG-081 — Warm-configure CI budget still flakes on hosted-runner variance

## Goal
- Recalibrate the exact-vcpkg-hit configure budget from a comparable hosted
  population so ordinary runner variance cannot stop CI before compilation,
  while preserving a hard failure for genuine warm-configure regressions.

## Non-goals
- No removal or warning-only downgrade of the warm-configure guard.
- No retry-until-green behavior or success-shaped fallback.
- No ccache policy, build-cache, test-selection, or runner-size changes.
- No budget increase based on this single observation alone.

## Context
- Symptom: GitHub Actions run `29211167225` on 2026-07-12 restored the exact
  vcpkg binary-package cache, completed CMake configure/generate successfully,
  then failed before ccache restore or compilation because `Configure (ci
  preset)` took `22.002 s` against `--max-warm-seconds 20`.
- Expected behavior: exact-cache configure telemetry remains fail-closed for a
  sustained regression but tolerates ordinary shared-runner variance.
- Impact: unrelated changes can lose their entire compile/test gate before the
  code under test executes. The failure interrupted `CI-007`'s hosted
  interface-invalidation evidence even though local clean no-ccache builds and
  the preceding five hosted warm-cache gates passed.
- `BUG-062` raised the budget from 10 s to 20 s after observing a 9.4–14.8 s
  range. The new 22.002 s sample exceeds that headroom, so another one-value
  bump would repeat the original calibration error.

## Required changes
- [ ] Collect a contemporary, same-context hosted population for every
      `time_command.py --max-warm-seconds` workflow call site, retaining raw
      configure times, runner image, exact-cache identity, and run URLs.
- [ ] Define and document a population-based budget rule with explicit
      headroom (or evidence-backed workflow-specific budgets) instead of
      calibrating at one observed maximum.
- [ ] Apply the smallest consistent workflow change while keeping
      `time_command.py` fail-closed semantics and timing JSON unchanged.

## Tests
- [ ] Extend workflow regression coverage to pin the evidence-backed budget
      values across all call sites.
- [ ] Keep a direct `time_command.py` regression proving an exact cache hit
      above the configured budget still exits non-zero.

## Docs
- [ ] Record the sample population, statistic, headroom rule, and affected
      workflows in the canonical CI timing policy.
- [ ] Update this bug index and retirement log when the fix is verified.

## Acceptance criteria
- [ ] The calibration uses at least five comparable exact-hit samples per
      affected workflow context and reports median plus p95.
- [ ] The chosen budget exceeds the observed ordinary-variance p95 by declared
      headroom without becoming an unbounded or warning-only guard.
- [ ] Hosted verification reaches compilation across the sampled workflows,
      and the synthetic over-budget regression still fails closed.

## Verification
```bash
python3 tests/regression/tooling/Test.CiTiming.py -v
rg -n -- '--max-warm-seconds' .github/workflows
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Deleting the guard, its telemetry, or its hard-fail behavior.
- Treating a rerun that happens to pass as evidence that the defect is fixed.
- Mixing ccache retention or other CI-latency optimizations into this task.
