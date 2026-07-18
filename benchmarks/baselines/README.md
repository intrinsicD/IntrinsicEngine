# Benchmark Baselines

Store machine-readable benchmark baselines used for regression comparisons.

- [`core_scheduler_hardening_smoke_59fbb84a.json`](core_scheduler_hardening_smoke_59fbb84a.json)
  is the matched-host CORE-007 pre-implementation capture. The benchmark
  harness lives in commit `0001f37c`, while all measured scheduler and
  TaskGraph production sources are byte-identical to merge commit `59fbb84a`.
  Its failed status is intentional: worker dispatch ignored TaskGraph priority
  and placed 16 low-priority callbacks in the first 16 execution slots.
- [`core_taskgraph_plan_reuse_ecs3_smoke_e8df606f.json`](core_taskgraph_plan_reuse_ecs3_smoke_e8df606f.json)
  and
  [`core_taskgraph_plan_reuse_renderprep9_smoke_e8df606f.json`](core_taskgraph_plan_reuse_renderprep9_smoke_e8df606f.json)
  are the CORE-008 full-rebuild baselines from the frozen replay harness at
  `e8df606f`. They are the median representatives from five sequential
  same-host `ci-release` runs. Both report zero reuse and zero quality error;
  the matched candidate comparison is recorded in the CORE-008 report.
