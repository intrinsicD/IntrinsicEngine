---
id: BUG-114
theme: G
depends_on: []
---
# BUG-114 — Release architecture SLOs use mismatched metrics and uncalibrated budgets

## Goal
- Restore a scientifically defensible, fail-closed `ci-release` architecture
  SLO gate whose workloads exercise the metrics they assert, whose units and
  thresholds have explicit rationale, and whose measurements remain in hosted
  evidence on both success and failure.

## Non-goals
- No `continue-on-error`, quarantine, label exclusion, selector weakening,
  path bypass, or stable-result-wrapper relaxation.
- No production scheduler priority, stealing, parking, wake, wait-mutex, or
  bookkeeping changes; `CORE-007` owns those changes.
- No frame-graph or scheduler optimization unless matched evidence first
  proves a production regression.
- No larger-runner experiment or lifecycle-topology redesign; `CI-009` retains
  that scope.
- No broad CPU rebuild or second hosted sample while the focused Release pilot
  remains red.

## Context
- Status: active on 2026-07-18; owner: Codex; branch: `main`; blocks
  `CI-009`. Next verification: redesign the two SLO cases, rebuild only
  `IntrinsicBenchmarkTests`, and repeat the exact cases in `ci-release`.
- Owner: `tests/benchmark/slo/Test.ArchitectureSLO.cpp`, its retained JUnit
  evidence, and the documented `ci-release` SLO contract.
- Symptom: manual `ci-release` run
  [`29631970411`](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29631970411)
  at exact SHA `191bec28dd96687a856c137693c34a77d50a2b9f` configured and
  built the unsanitized `Release` preset successfully, then failed both
  selected architecture SLO cases.
- Frame-graph failure: `executeP95 = 2,464,368 ns`, expected
  `< 1,500,000 ns`. The threshold dates to February 2026, has no checked-in
  promoted-Core hosted Release calibration, and failed 2/20 repetitions in the
  local `ci-release` tree at `1,531,060` and `1,599,170 ns`.
- Scheduler failures: `StealSuccessRatio = 0`, expected `>= 0.20`;
  `IdleWaitTotalNs = 31,869,833`, expected `< 700,000`; and
  `UnparkLatencyP99Ns = 131,072`, expected `< 80,000`.
- The route job succeeded, the optimized job failed, and stable `ci-release`
  failed as designed. The exact vcpkg cache hit; configure took 10.674 seconds,
  build 414.146 seconds, and the SLO phase 0.767 seconds. The benchmark phase
  correctly did not run after the known SLO failure.
- Always-run aggregation emitted a validated error result that recorded the
  SLO return code and missing downstream benchmark phase. That secondary
  failure is intentional fail-closed evidence, not a workflow defect.
- Retained evidence: result artifact `8425852275`,
  `sha256:df6e45ae647c2657607f4ad4dfd765147ecb0cd0df56a2d904decf495097884f`;
  route artifact `8425775613`,
  `sha256:d1cebd1a916f969e64a324fc167fa4cbdbf0f36327a91675ff4e483027520130`.
- No second Release sample was dispatched. Run `29631970411` is diagnostic and
  does not count toward `CI-009`'s five successful unchanged-SHA samples.
- The scheduler workload submits its measured tasks from external threads into
  the global inject queue. Stealing counts only work removed from worker-local
  deques, so this workload cannot justify a positive steal-ratio lower bound;
  the unchanged case reported zero steals in 20/20 local Release repetitions.
- `IdleWaitTotalNs` sums intentional blocked sleep across every worker for the
  process lifetime. `UnparkLatencyP99Ns` measures park-to-signal dwell time,
  not signal-to-resume latency, and reports power-of-two histogram buckets.
- `FrameGraph::LastCriticalPathTimeNs()` currently exposes the graph scheduler's
  abstract `criticalPathCost`, not elapsed nanoseconds. Existing Core tests
  already cover the cost and tie-breaker behavior, so it is not a valid
  performance assertion.
- Ranked, falsifiable hypotheses:
  1. The scheduler SLO combines workloads and metrics with incompatible
     semantics. A worker-local backlog plus direct signal-to-resume samples
     will make the asserted work and timing windows explicit without changing
     production code.
  2. The frame-graph execution ceiling is stale and flake-prone rather than
     evidence of a new regression. A predeclared conservative smoke ceiling
     based on repeated local Release measurements, the first hosted result,
     and a fresh unchanged-SHA hosted population will remain stable while still
     failing material regressions.
  3. The current evidence surface hides passing measurements. Emitting every
     derived percentile and budget to stdout will retain them in CTest JUnit
     for passing and failing runs.
  4. A real production regression exists. If the redesigned focused case still
     fails outside the declared measurement margin, stop and re-gate the
     owning production task rather than calibrating around it.

## Required changes
- [ ] Retain the frame-graph compile and execute warmup/measured populations,
      print their percentiles and budgets on every run, and remove the false
      critical-path nanosecond assertion.
- [ ] Replace the scheduler's global-inject steal-ratio assertion with a
      controlled worker-local backlog that must complete through actual steals.
- [ ] Park the complete waiter population before signaling, measure
      signal-to-resume directly in each coroutine, and gate the derived tail
      rather than park-to-signal dwell time.
- [ ] Record cumulative idle, contention, ratio, and histogram telemetry as
      diagnostics only; do not present them as latency SLOs.
- [ ] Give retained smoke ceilings an explicit, conservative calibration rule
      and document that they are regression guardrails rather than performance
      improvement claims.
- [ ] Keep the workflow fail closed and preserve its existing stop-before-
      benchmark behavior after an SLO failure.
- [ ] Run one hosted pilot at the fixed SHA. Only after it passes may that
      unchanged SHA begin the five sequential `CI-009` retirement samples.

## Tests
- [ ] Pass each redesigned SLO case independently and together in the
      unsanitized `ci-release` tree.
- [ ] Repeat the focused local Release cases enough to expose deterministic
      harness defects without treating local timing as hosted claim evidence.
- [ ] Pass the existing workflow routing, concurrency, and timing regressions;
      no workflow behavior changes are expected.
- [ ] Pass one hosted `ci-release` pilot with the stable result context
      remaining fail closed.
- [ ] After the pilot, pass five sequential hosted samples at one unchanged
      SHA, preset, selector, runner image, and cache identity; stop immediately
      if any sample fails.

## Docs
- [ ] Correct `docs/benchmarking/ci-policy.md` with the failed-run diagnosis,
      metric definitions, workload shape, guardrail rationale, and final hosted
      evidence.
- [ ] Re-gate `CI-009` on `BUG-114` and state that run `29631970411` is
      diagnostic, not sample 1.
- [ ] Update the active-task, bug, and process indexes while the blocker is
      active.
- [ ] Regenerate `tasks/SESSION-BRIEF.md` after opening, re-gating, and
      retirement.

## Acceptance criteria
- [ ] Every asserted scheduler performance metric is exercised by the workload
      and has explicit units and an explicit observation window.
- [ ] Frame-graph and scheduler derived percentiles plus budgets are retained
      in hosted JUnit evidence on success and failure.
- [ ] No threshold is edited after reading a candidate population to
      manufacture a pass.
- [ ] A focused hosted pilot passes before the five-run population starts.
- [ ] Five sequential `ci-release` runs pass at one unchanged SHA with the
      exact Release preset, selector, and standard `ubuntu-24.04` runner.
- [ ] A failing SLO still fails `optimized-release` and stable `ci-release`;
      no skip, retry, quarantine, or nonblocking path is introduced.
- [ ] No production scheduler change, public module-surface change, dependency
      edge, or `CORE-007` scope is absorbed.
- [ ] `CI-009` resumes only after `BUG-114` is retired with hosted evidence.

## Verification
```bash
cmake --preset ci-release --fresh
cmake --build --preset ci-release --target IntrinsicBenchmarkTests
ctest --test-dir build/ci-release --output-on-failure \
  -L '^slo$' -LE 'gpu|vulkan|flaky-quarantine' \
  --no-tests=error --timeout 120 --parallel 1 \
  --output-junit reports/architecture-slo.junit.xml
python3 tests/regression/tooling/Test.WorkflowRouting.py
python3 tests/regression/tooling/Test.WorkflowConcurrency.py
python3 tests/regression/tooling/Test.CiTiming.py
python3 tools/agents/validate_tasks.py --root tasks --strict
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/agents/check_task_state_links.py --root . --strict
python3 tools/docs/check_doc_links.py --root .
python3 tools/agents/generate_session_brief.py --root . --check
```

## Forbidden changes
- Raising a budget until run `29631970411` would pass without an independently
  declared guardrail rationale.
- Treating global-inject work as proof of worker-local stealing.
- Comparing cumulative multi-worker idle time with a per-event latency budget.
- Comparing park-to-signal histogram dwell with a signal-to-resume threshold.
- Disabling, excluding, quarantining, retrying, or making either SLO
  nonblocking.
- Changing production scheduler behavior under this bug; `CORE-007` owns it.
- Counting the failed pilot or any changed-SHA run toward the final five.
- Dispatching sample N+1 after sample N fails.
- Running broad unrelated gates before the focused Release pilot is green.
