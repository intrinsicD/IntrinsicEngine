# IntrinsicEngine Architecture SLOs and Telemetry Milestones

This document defines concrete service-level objectives (SLOs) for engine orchestration and the telemetry fields that verify them.

## SLO Gates

## 1) FrameGraph (CPU DAG)

- **Compile budget:** p99 `FrameGraphCompileTimeNs < 350,000 ns` (0.35 ms) at 2,000 nodes.
- **Execute budget:** p95 `FrameGraphExecuteTimeNs < 1,500,000 ns` (1.5 ms) at 2,000 nodes.
- **Critical path budget:** p95 `FrameGraphCriticalPathTimeNs < 900,000 ns` (0.9 ms) at 2,000 nodes.

## 2) Task scheduler contention and tail behavior

- **Steal quality band:** `0.20 <= TaskStealSuccessRatio <= 0.65` under saturated synthetic load.
- **Queue contention ceiling:** p95 `TaskQueueContentionCount < 4,096` lock misses / frame on 16-worker stress profile.
- **Idle wait ceiling:** p95 `TaskIdleWaitTotalNs < 700,000 ns` (0.7 ms) during active gameplay frames.
- **Wake latency tail:** p99 `TaskUnparkP99Ns < 80,000 ns` (80 us).

## Telemetry Milestones

- **Milestone A (implemented):** Per-frame telemetry export for:
  - `TaskQueueContentionCount`
  - `TaskStealSuccessRatio`
  - `TaskIdleWaitCount` / `TaskIdleWaitTotalNs`
  - `FrameGraphCompileTimeNs`
  - `FrameGraphExecuteTimeNs`
  - `FrameGraphCriticalPathTimeNs`
- **Milestone B (implemented):** Performance panel now computes rolling p95/p99 values from frame telemetry and surfaces PASS/ALERT status against the SLO gates.
- **Milestone C (implemented):** CI now runs `ArchitectureSLO.FrameGraphP95P99BudgetsAt2000Nodes`, a 2,000-node FrameGraph performance scenario that asserts p99/p95 compile/execute/critical-path budgets from this document.
