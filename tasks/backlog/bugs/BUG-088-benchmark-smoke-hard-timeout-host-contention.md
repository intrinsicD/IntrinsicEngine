---
id: BUG-088
theme: G
depends_on: []
---
# BUG-088 — Benchmark smoke hard timeout flakes under host contention

## Goal
- Keep the benchmark smoke useful and fail-closed while preventing ordinary
  host contention from turning the required default CPU gate red before its
  result validator can run.

## Non-goals
- No retry-until-green behavior.
- No warning-only benchmark validation or removal from aggregate builds.
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
  monolithic 21-result smoke crosses a hard 30-second wall under contention.
  `benchmarks/CMakeLists.txt` labels the pair
  `benchmark;geometry;graphics;physics` without `slow`, so the standard
  `-LE 'gpu|vulkan|slow|flaky-quarantine'` gate includes it.

## Required changes
- [ ] Collect representative clean and contended-host timings for the 21-result
      smoke, retaining per-result timing/diagnostics and host context.
- [ ] Choose the smallest evidence-backed correction: make the aggregate
      genuinely PR-fast, split it into bounded shards, or route it through the
      documented slow/heavy lane with an appropriate timeout.
- [ ] Preserve the Run→Validate fixture dependency so missing or partial result
      sets cannot produce a green validation step.

## Tests
- [ ] Add registration-policy coverage for the selected labels, timeout, and
      Run→Validate fixture relationship.
- [ ] Demonstrate repeated clean and loaded-host runs stay inside the selected
      lane's declared budget and still fail nonzero for invalid result JSON.

## Docs
- [ ] Record the PR-fast versus slow/nightly classification and timing evidence
      in the benchmark/testing policy docs.
- [ ] Update this bug index and retirement log when the correction is verified.

## Acceptance criteria
- [ ] A representative timing population, not one retry, justifies the final
      route and timeout.
- [ ] The default CPU gate no longer flakes from ordinary contention, or the
      smoke is explicitly and correctly classified outside that PR-fast lane.
- [ ] Strict validation still runs after every successful smoke and rejects
      missing, malformed, or schema-invalid results.

## Verification
```bash
cmake --build --preset ci --target IntrinsicBenchmarkSmoke
ctest --test-dir build/ci --output-on-failure -V -R '^IntrinsicBenchmarkSmoke\.(Run|Validate)$'
python3 tools/benchmark/validate_benchmark_results.py --root build/ci/benchmark-ctest/IntrinsicBenchmarkSmokeTest --strict
python3 tools/agents/check_task_policy.py --root . --strict
```

## Forbidden changes
- Silently excluding the smoke from all CI lanes.
- Detaching strict result validation from successful benchmark execution.
- Treating one passing rerun as proof that the variance defect is fixed.
