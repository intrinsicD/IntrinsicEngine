---
id: BUG-081
theme: G
depends_on: []
---
# BUG-081 — Warm-configure CI budget still flakes on hosted-runner variance

## Status
- Completed on 2026-07-16; owner: Codex; implementation commit: `d3017621`;
  branch: `agent/sandbox-model-workflow-completion`; PR:
  [`#1024`](https://github.com/intrinsicD/IntrinsicEngine/pull/1024).
- Immediate evidence: the exact vcpkg cache hit in
  [`ci-sanitizers` run 29519782498](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29519782498)
  completed CMake configure/generate successfully, but the ASan matrix context
  took `30.368 s` against the `20.000 s` budget and stopped before compilation.
- Hosted verification on repaired head `d3017621` confirmed every executing PR
  context passed Configure and reached compilation under the calibrated guard.
  The retirement head still requires the complete all-green PR merge gate.
- Seven guarded call sites now use the finite
  `40 s` fleet budget derived from `ceil-to-5-seconds(1.25 × 30.368 s)`;
  workflow-policy and fail-closed regressions pass.

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
- [x] Collect a contemporary, same-context population for every executing
      GitHub-hosted `time_command.py --max-warm-seconds` context, retaining raw
      configure times, runner image, exact-cache identity, and run URLs; audit
      inactive/self-hosted call sites and document the transfer policy when no
      direct population exists.
- [x] Define and document a population-based budget rule with explicit
      headroom (or evidence-backed workflow-specific budgets) instead of
      calibrating at one observed maximum.
- [x] Apply the smallest consistent workflow change while keeping
      `time_command.py` fail-closed semantics and timing JSON unchanged.

## Tests
- [x] Extend workflow regression coverage to pin the evidence-backed budget
      values across all call sites.
- [x] Keep a direct `time_command.py` regression proving an exact cache hit
      above the configured budget still exits non-zero.

## Docs
- [x] Record the sample population, statistic, headroom rule, and affected
      workflows in the canonical CI timing policy.
- [x] Update this bug index and retirement log when the fix is verified.

## Acceptance criteria
- [x] The calibration uses at least five comparable exact-hit samples per
      executing GitHub-hosted workflow context and reports median plus p95;
      inactive self-hosted contexts inherit the conservative fleet maximum and
      require five direct samples before any lower context-specific limit.
- [x] The chosen budget exceeds the observed hosted-context p95 by declared
      headroom without becoming an unbounded or warning-only guard.
- [x] Hosted verification reaches compilation across the sampled workflows,
      and the synthetic over-budget regression still fails closed.

## Evidence

- Exact-hit populations, run URLs, runner images, raw configure seconds,
  medians and nearest-rank p95s, and the calibration rule are recorded in
  [`docs/benchmarking/ci-policy.md`](../../docs/benchmarking/ci-policy.md#warm-configure-failure-guard).
- Context p95s are `22.002 s` (`pr-fast`), `18.261 s`
  (`ci-linux-clang`), `30.368 s` (ASan), `11.399 s` (UBSan), `15.074 s`
  (`ci-vulkan`), `14.019 s` (`ci-bench-smoke`), and `24.265 s`
  (`nightly-deep` CPU). The optional self-hosted nightly GPU context has not
  run a comparable population and inherits the conservative fleet maximum.
- The image-version split overlaps heavily: the newer hosted image weakly
  correlates with higher Linux timing, but the data do not distinguish
  shared-runner, image/context, or source/config interaction as the cause. The
  same image completed benchmark-smoke configure in `11.251 s` while the ASan
  leg took `30.368 s`; the calibration retains that tail without attributing
  it.
- The declared rule is `ceil-to-5-seconds(1.25 × max context p95)`, yielding
  `40 s`; `time_command.py` and its JSON schema are unchanged.
- Repaired-head hosted Configure steps passed and reached compilation in
  [`pr-fast` run 29521730095](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29521730095),
  [`ci-linux-clang` run 29521730145](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29521730145),
  both sanitizer contexts in
  [run 29521730062](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29521730062),
  [`ci-vulkan` run 29521730079](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29521730079),
  and [`ci-bench-smoke` run 29521730126](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29521730126).

## Verification
```bash
python3 tests/regression/tooling/Test.CiTiming.py -v
rg -n -- '--max-warm-seconds' .github/workflows
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/check_task_policy.py --root . --strict
```

Local results on 2026-07-16:

- `Test.CiTiming.py`: 13/13 passed, including the synthetic exact-hit
  over-budget hard failure and the seven-call-site budget inventory.
- `Test.WorkflowConcurrency.py`: 5/5 passed.
- Strict workflow naming, task policy/state, documentation-link, root-hygiene,
  PR-contract, skill-sync, and diff checks passed.
- `rg` reports exactly seven `--max-warm-seconds 40` call sites across the six
  declared workflows. Every executing repaired-head PR context passed its
  Configure step and entered the build on the run IDs above.

## Forbidden changes
- Deleting the guard, its telemetry, or its hard-fail behavior.
- Treating a rerun that happens to pass as evidence that the defect is fixed.
- Mixing ccache retention or other CI-latency optimizations into this task.
