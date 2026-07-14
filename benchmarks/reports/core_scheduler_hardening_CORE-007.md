# CORE-007 scheduler hardening benchmark report

## Comparison

The before and after payloads use
`core.scheduler_hardening.smoke`, the same built-in workloads, three warmups,
nine measured dispatch iterations, and the same host/build configuration. The
benchmark runner was backported unchanged onto reachable aggregate parent
`87dace4d4ac5392358fde2d6465c01f3de69c98b` for the before capture. Its relevant
CORE-005 sources are byte-identical to the isolated development parent from
which CORE-007 began. The before payload is checked in as
`benchmarks/baselines/core_scheduler_hardening_smoke_87dace4d.json`; the after
payload was captured from the completed CORE-007 working tree before commit.

- Host: 11th Gen Intel Core i9-11900KF, 8 cores / 16 threads, Linux 6.14 x86-64.
- Toolchain: Clang 23.0.0, Debug, ASan + UBSan, `ci` preset.
- Dispatch workload: 8,192 empty callbacks, three scheduler workers, median of
  nine measured dispatch-and-drain samples after three warmups.
- Steal workload: 256 callbacks resident in a blocked single worker's local
  deque, drained one at a time by an external helper.
- Priority workload: 64 Background passes registered before 16 Critical passes
  behind a blocked single worker. `quality_error_l2` counts low callbacks in
  the first 16 execution slots.

| Metric | Parent | CORE-007 | Change |
|---|---:|---:|---:|
| Dispatch median runtime | 6.867 ms | 6.180 ms | -10.0% |
| Dispatch throughput | 1,193,013.647 tasks/s | 1,325,574.923 tasks/s | +11.1% |
| Priority quality error | 16 | 0 | contract restored |
| Low callbacks before first high | 64 | 0 | contract restored |
| External steal success | 256 / 256 | 256 / 256 | unchanged |
| External steal median | 1,277 ns | 1,266 ns | -0.9% |
| External steal p95 | 1,514 ns | 1,468 ns | -3.0% |
| Steal queue-contention events | 0 | 0 | unchanged |

The aggregate dispatch workload improved on this host and the deterministic
priority probe moved from failing to passing. These numbers do not isolate the
relative contribution of lane counters, conditional wakeups, cheap worker
counting, wait-token shards, or TaskGraph scratch reuse, so no per-change
performance claim is made. The sharded wait registry and wake protocol are
instead covered by deterministic concurrency contracts and sanitizer runs.

## Chase-Lev decision

The optional Chase-Lev deque rewrite was not adopted. Both payloads recorded
zero deque-lock contention during all 256 external steals. The canonical
capture's 1–3% latency reductions are too small for a claim: repeated
after captures on the same working tree ranged from 1,201 to 1,309 ns median
and 1,416 to 2,155 ns p95. There is no robust evidence that replacing the
bounded spin lock would repay the added algorithmic and memory-ordering
complexity. The current lock retains `notify_one()` because its slow path parks
with `atomic::wait()`; that notification is load-bearing for progress.

## Reproduction

```bash
cmake --preset ci
cmake --build --preset ci --target IntrinsicSchedulerBenchmarkSmoke
GIT_COMMIT=<commit> build/ci/bin/IntrinsicSchedulerBenchmarkSmoke /tmp/core007/result.json
python3 tools/benchmark/validate_benchmark_results.py --root /tmp/core007 --strict
```

The smoke is PR-fast correctness/performance evidence, not a stable machine
SLO. Cross-host timing comparisons are invalid; use a same-host parent capture
for future regression decisions.
