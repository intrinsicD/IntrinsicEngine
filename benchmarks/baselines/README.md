# Benchmark Baselines

Store machine-readable benchmark baselines used for regression comparisons.

- [`core_scheduler_hardening_smoke_59fbb84a.json`](core_scheduler_hardening_smoke_59fbb84a.json)
  is the matched-host CORE-007 pre-implementation capture. The benchmark
  harness lives in commit `0001f37c`, while all measured scheduler and
  TaskGraph production sources are byte-identical to merge commit `59fbb84a`.
  Its failed status is intentional: worker dispatch ignored TaskGraph priority
  and placed 16 low-priority callbacks in the first 16 execution slots.
