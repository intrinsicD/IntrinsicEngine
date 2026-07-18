# CORE-007 scheduler-hardening result

This report records a matched-host local comparison for
`core.scheduler_hardening.smoke`. It supports the CORE-007 priority and
wait-registry decisions; it is not a cross-machine, release-wide, or realtime
scheduling claim.

## Provenance and procedure

- Baseline production revision:
  `59fbb84a905df42710940e7169442eabb3a83894`.
- Baseline harness revision: `0001f37c`; the scheduler and TaskGraph sources
  measured there were byte-identical to the baseline production revision.
- Final candidate revision:
  `54a397e819b6ab71165ed6bdbf0b8422d586ae7e`.
- Harness: `Bench_SchedulerHardeningSmoke.cpp`, its declaration, manifest, and
  monolithic runner path were byte-identical between baseline and candidate
  collection.
- Build: local optimized `ci-release`, unsanitized Release, Clang 23.0.0.
- Host: Linux 6.14.0-37-generic x86_64; 11th Gen Intel Core i9-11900KF,
  8 cores / 16 threads.
- Dataset: `builtin.synthetic_scheduler_contention.v1`.
- Sampling: each result uses three warmups followed by the median of nine
  measurements. The baseline is one result capture. The final candidate was
  captured three times sequentially; the table uses the middle per-capture
  value for every reported throughput/runtime metric.

The durable pre-change payload is
[`core_scheduler_hardening_smoke_59fbb84a.json`](../baselines/core_scheduler_hardening_smoke_59fbb84a.json).
Candidate payloads remain temporary verification artifacts because checking a
second payload with the same stable `benchmark_id` into the repository would
make result discovery ambiguous.

## Result

| Measurement | `59fbb84a` baseline | `54a397e8` candidate | Candidate / baseline |
| --- | ---: | ---: | ---: |
| Dispatch median runtime, 8,192 tasks (ms) | 2.159489 | 1.606879 | 0.744101 |
| Dispatch throughput (tasks/s) | 3,793,490 | 5,098,081 | 1.343903 |
| Priority quality error | 16 | 0 | contract restored |
| Low callbacks before first High callback | 64 | 0 | contract restored |
| Low callbacks in first 16 slots | 16 | 0 | contract restored |
| Single-thread wait-token pairs/s | 32,005,501 | 31,267,176 | 0.976931 |
| Eight-thread aggregate wait-token pairs/s | 3,402,629 | 24,688,809 | 7.255805 |
| Eight-thread scaling efficiency | 1.3289% | 9.8701% | 7.427271 |

Under these local conditions, the final candidate's middle dispatch capture
was 25.590% shorter and its throughput was 34.390% higher. More importantly
for the measure-first sharding decision, aggregate eight-thread wait-registry
throughput was 7.256 times the baseline while the single-thread result remained
within 2.307% of baseline. The priority probe passed in all final captures
after failing deterministically in the baseline.

The three sequential candidate captures bounded dispatch throughput between
4,703,024 and 5,126,876 tasks/s, single-thread wait-token throughput between
30,499,564 and 31,651,096 pairs/s, aggregate contended throughput between
24,207,287 and 25,575,024 pairs/s, and contended scaling efficiency between
9.5602% and 10.4817%.

## Engineering decisions

- The first priority/sharding candidate at `491aac49` restored the priority
  contract and materially improved contended wait-token throughput, but its
  3,436,134-task/s dispatch result was 9.42% below baseline. CORE-007 therefore
  avoided `Normal`-lane counter updates and replaced the shared per-allocation
  shard selector with a scheduler-instance-qualified thread-local rotation
  before accepting the final implementation.
- Worker notification suppression cannot be compared through the baseline
  runner because the baseline public stats had no notification counter.
  Candidate contract tests instead prove both sides: active-worker dispatch
  does not increment `WorkerWakeNotifications`, while repeated
  scan-to-park/dispatch handshakes make progress. Shutdown remains an
  unconditional broadcast.
- The existing spin-locked local deque remains. The final optimized Release
  `ArchitectureSLO.TaskSchedulerLocalStealAndWakeCompletionBudgets` capture
  completed all 69,000 expected steals, with local-fanout p95 of 3.347 ms and
  signal-to-resume p99 of 0.372 ms against 16.667 ms budgets. Queue contention
  telemetry was nonzero, but there is no matched evidence that a Chase-Lev
  rewrite is required or beneficial, so CORE-007 does not add that complexity.
- TaskGraph callback ownership and ready-successor scratch are deliberately
  deferred to CORE-008, which owns retained compiled-plan storage.

## Correctness and validation

The candidate adds deterministic priority, conditional-wake, fairness, and
wait-shard scheduler-instance contracts. The exact focused filter
`CoreTasks.*:CoreTaskGraphCompletionLifetime.*` passed all 47 tests, including
CORE-005 late-enqueue, worker-owned-wait, stale-token, and
scheduler-replacement regressions. The new race, per-shard recycling, and
worker-local priority contracts also passed 50 consecutive repetitions. Every
final benchmark directory passed strict validation for all 23 emitted result
files, and the existing optimized scheduler Architecture SLO passed.

Reproduction:

```bash
cmake --preset ci-release
cmake --build --preset ci-release --target IntrinsicBenchmarkSmoke IntrinsicBenchmarkTests
GIT_COMMIT=54a397e819b6ab71165ed6bdbf0b8422d586ae7e \
  build/ci-release/bin/IntrinsicBenchmarkSmoke /tmp/core007-candidate
python3 tools/benchmark/validate_benchmark_results.py \
  --root /tmp/core007-candidate --strict
ctest --test-dir build/ci-release --output-on-failure \
  -R '^ArchitectureSLO\.TaskSchedulerLocalStealAndWakeCompletionBudgets$' \
  --timeout 120
```
