---
id: BUG-088
theme: G
depends_on: []
---
# BUG-088 — Benchmark smoke hard timeout flakes under host contention

## Status

- In progress on 2026-07-16; owner: Codex; branch:
  `agent/sandbox-model-workflow-completion`; PR:
  [`#1024`](https://github.com/intrinsicD/IntrinsicEngine/pull/1024).
- Promoted after final-head
  [`ci-linux-clang` run 29532745081](https://github.com/intrinsicD/IntrinsicEngine/actions/runs/29532745081)
  repeated the known 30-second timeout and blocked the merge while all six
  other required checks passed.

## Goal
- Keep the benchmark smoke useful and fail-closed while preventing ordinary
  host contention from turning the required default CPU gate red before its
  result validator can run.

## Non-goals
- No retry-until-green behavior.
- No warning-only benchmark validation or removal from the complete
  `IntrinsicTests` build aggregate or dedicated benchmark workflow.
- No performance claim or benchmark-kernel tuning without baseline evidence.
- No timeout increase based on this single loaded-host observation alone.

## Context
- Symptom: the 2026-07-15 default CPU-supported gate timed out
  `IntrinsicBenchmarkSmoke.Run` at its CTest `TIMEOUT 30`; fixture-dependent
  `IntrinsicBenchmarkSmoke.Validate` was then Not Run. All other 3,777 tests
  passed. The same pair immediately passed in isolation in 14.71 s plus 0.02 s
  after a competing CPU-heavy process ended.
- Expected behavior: the required PR-fast/default gate tolerates ordinary host
  variance, while benchmark execution and strict result validation remain
  bounded and fail closed.
- Impact: unrelated feature work can lose its full CPU gate solely because the
  monolithic 22-result smoke crosses a hard 30-second wall under contention.
  `benchmarks/CMakeLists.txt` labels the pair
  `benchmark;geometry;graphics;physics` without `slow`, so the standard
  `-LE 'gpu|vulkan|slow|flaky-quarantine'` gate includes it.
- Current evidence is stronger than a single contended observation. Seven
  successful same-branch hosted `ci-bench-smoke` gate-timing artifacts record
  `IntrinsicBenchmarks` workflow execution phases of 38.167, 27.157, 37.551,
  35.097, 34.947, 37.893, and 37.203 seconds. The conventional median is 37.203
  seconds and nearest-rank p95 is 38.167 seconds. These phases include
  build-tool orchestration and are not direct CTest-process timings; they show
  that duplicate aggregate execution does not belong in the fast default lane.
  The final-head full CPU gate directly timed out at 30.04 seconds while the
  dedicated benchmark lane on the same head completed and strictly validated
  all 22 outputs.

## Required changes
- [x] Collect representative isolated-lane and full-suite timings for the 22-result
      smoke, retaining per-result timing/diagnostics and host context.
- [x] Choose the smallest evidence-backed correction: make the aggregate
      genuinely PR-fast, split it into bounded shards, or route it through the
      documented slow/heavy lane with an appropriate timeout.
- [x] Preserve the Run→Validate fixture dependency so a failed producer cannot
      produce a green validation step; retain strict validation of every JSON
      emitted by a successful producer.

## Tests
- [x] Add registration-policy coverage for the selected labels, timeout, and
      Run→Validate fixture relationship.
- [x] Demonstrate repeated dedicated-lane and local opt-in runs stay inside the
      selected lane's declared budget and still fail nonzero for invalid result
      JSON.

## Docs
- [x] Record the PR-fast versus slow/nightly classification and timing evidence
      in the benchmark/testing policy docs.
- [ ] Update this bug index and retirement log when the correction is verified.

## Acceptance criteria
- [x] A representative timing population, not one retry, justifies the final
      route and timeout.
- [x] The default CPU gate no longer flakes from ordinary contention, or the
      smoke is explicitly and correctly classified outside that PR-fast lane.
- [x] Strict validation still runs after every successful smoke and rejects a
      missing result root plus malformed or schema-invalid emitted results.
- [ ] Exact implementation-head `ci-linux-clang`, `ci-bench-smoke`, and
      `ci-docs`/`docs-validation` checks pass before retirement.

## Verification
```bash
python3 tests/regression/tooling/Test.CiTiming.py
cmake --preset ci
cmake --build --preset ci --target IntrinsicTests
ctest --test-dir build/ci --output-on-failure -L benchmark -L slow \
  -R '^IntrinsicBenchmarkSmoke\.(Run|Validate)$' --timeout 120
ctest --test-dir build/ci --output-on-failure \
  -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

Local verification on 2026-07-16:

- The new registration-policy regression failed before the CMake correction
  because `slow` was absent, then passed with the complete tooling suite
  (14/14).
- Fresh `ci` configuration generated `slow` on the target and both fixture
  tests, a 120-second runner timeout, the retained 30-second validator timeout,
  and the original setup/required fixture relationship. `IntrinsicTests` still
  contains the executable while `IntrinsicCpuTests` and the default CPU test
  selector exclude it.
- A clean `IntrinsicTests` build completed, and the default CPU-supported gate
  passed all 3,789 selected tests in 412.89 seconds without selecting the slow
  benchmark fixture pair.
- The opt-in fixture pair passed (runner 19.40 seconds; strict validator 0.02
  seconds), and independent strict validation accepted all 22 emitted results.
- The benchmark result-validator regression remains fail closed for a missing
  result root and for malformed or schema-invalid emitted results. Producer
  failures remain fail closed through the CTest fixture and workflow command
  exit status; the generic validator does not assert a runner-specific ID set.
- Final hosted exact-head `ci-linux-clang`, `ci-bench-smoke`, and
  `ci-docs`/`docs-validation` results remain required before retirement; the
  latter owns the new result-validator regression cases.

## Forbidden changes
- Silently excluding the smoke from all CI lanes.
- Detaching strict result validation from successful benchmark execution.
- Treating one passing rerun as proof that the variance defect is fixed.
