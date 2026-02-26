# IntrinsicEngine — Architecture TODOs (Living Document)

This document tracks **what's left to do** in IntrinsicEngine's architecture.

**Policy:** If something is fixed/refactored, it should **not** remain as an "issue" here. We rely on Git history for the past.

---

## 0. Scope & Success Criteria

**Goal:** a data-oriented, testable, modular engine architecture with:

- Deterministic per-frame orchestration (CPU + GPU), explicit dependencies.
- Robust multithreading contracts in Core/RHI.
- Minimal "god objects"; subsystems testable in isolation.

---

## 1. Open TODOs (What's left)

### 1.1 2026-02-25 Architecture Review Follow-ups

- [ ] **Complete `Core::Tasks` fiber parking for dependency waits (High).**
  - Hybrid work-stealing foundations are now in place (worker-local LIFO deques + cross-worker stealing + external inject queue).
  - Add true fiber parking/unparking for wait-heavy dependency chains so worker OS threads never block on fine-grain sync.
  - Remaining: complete dependency-level continuation parking integration across scheduler wait-heavy boundaries and finalize park/unpark percentile telemetry.

- [ ] **Resolve runtime API/ownership drift in orchestration paths (Medium).**
  - Audit central runtime ownership boundaries and reduce `std::shared_ptr` usage where borrowed references or explicit owners are sufficient.

- [ ] **Establish architecture SLOs + telemetry milestones (Cross-cutting).**
  - Define measurable targets for DAG compile budget, scheduler contention/tail latency, and frame critical-path timing.
  - Add instrumentation for queue contention, steal ratio, barrier/idle wait time, and per-frame compile/execute split.

## 1.2 2026-02-26 Code Quality Audit (Style/Syntax, Architecture, Duplication)

This section captures **newly observed inconsistencies** and concrete remediation actions.

### B. Architecture findings

- [ ] **Close the gap between “fiber parking” goal and current scheduler behavior (High).**
  - Scheduler currently uses worker-local deques + stealing + global inject queue, but waits are thread-level (`WaitForAll`) rather than dependency-level fiber parking.
  - Result: potential head-of-line blocking for wait-heavy chains and weaker latency isolation under mixed workloads.
  - Action: add parked continuation queues keyed by dependency counters/events, with wake-on-ready semantics and telemetry (`park_count`, `park_ns`, `unpark_ns`).

- [ ] **Clarify ownership boundaries and remove avoidable shared ownership in runtime hot paths (Medium).**
  - Device/runtime orchestration APIs still expose widespread `std::shared_ptr` ownership even when ownership appears single-rooted.
  - Action: convert to explicit owner + borrowed refs/spans/handles where lifetime is frame- or system-scoped.

### C. Code duplication hotspots

- [ ] **Consolidate importer text parsing boilerplate (Medium).**
  - Multiple importers repeat near-identical patterns: byte-span → `string_view` → `istringstream` line loop → per-line `stringstream` tokenization.
  - Action: introduce shared tokenizer/line-reader utilities (non-allocating span parser) to cut duplication and improve parser throughput.

- [ ] **Consolidate importer post-process defaults (Medium).**
  - Fallback normal/color/aux population and “invalid/empty data” checks are duplicated across loaders.
  - Action: centralize into `GeometryImportPostProcess` helpers with deterministic policy flags per format.

- [ ] **Unify shader registration and pass wiring declarations (Low).**
  - `RenderOrchestrator::InitPipeline()` manually registers many shaders via repetitive statements.
  - Action: switch to table-driven registration (`constexpr` array of `{id,path}`), enabling consistency checks and easier hot-reload integration.

### D. Problems in current TODO governance (meta)

- [ ] **Missing measurable acceptance criteria for several high-impact TODOs (High).**
  - Items mention goals (fairness/tail behavior/critical path) without explicit thresholds.
  - Action: for each High item define concrete SLO gates (example: `FrameGraph CPU execute p95 < 0.35 ms @ 2k nodes`, `steal ratio target band`, `compile budget p99`).

---

## 1.3 Next TODO Deep-Dive (Execution Plan)

### Target: Complete `Core::Tasks` fiber parking for dependency waits

This is the **next implementation item** and should be executed before other scheduler/perf backlog entries because it unlocks lower tail latency for all wait-heavy systems (FrameGraph, async streaming, and asset processing).

#### Problem statement

Current behavior relies on worker-level blocking semantics for some dependency waits (e.g., `WaitForAll()` boundaries), which wastes worker throughput when continuations are logically runnable but physically tied to blocked threads.

We want **continuation-level parking**: a task waiting on dependency count `d > 0` should suspend its fiber, release the worker to execute other ready work, and resume only when `d == 0`.

#### Formal model (for correctness)

Represent each task node `i` with:

- unresolved dependency count: $\delta_i \in \mathbb{N}_0$
- continuation handle (optional): $c_i$
- state: $s_i \in \{\text{Ready}, \text{Running}, \text{Parked}, \text{Done}\}$

Execution rules:

1. **Ready condition:** $\delta_i = 0 \Rightarrow s_i = \text{Ready}$ and node can enter a runnable deque.
2. **Park transition:** running node with unresolved wait `w > 0` transitions to $s_i = \text{Parked}$ and stores continuation `c_i` in wait-bucket keyed by dependency/event id.
3. **Wake transition:** each dependency completion performs atomic decrement on blocked nodes; if a blocked node reaches $\delta_i = 0$, atomically move $s_i: \text{Parked} \to \text{Ready}$ and enqueue continuation exactly once.
4. **Safety invariant:** each node enters `Ready` from `Parked` at most once per wait epoch (no duplicate enqueue).

Expected complexity targets:

- Parking: amortized $O(1)$ enqueue into wait bucket.
- Wake: amortized $O(1)$ decrement + conditional ready enqueue.
- Extra memory: $O(N + W)$ for task records + wait buckets (`N` tasks, `W` active waits).

#### Data-oriented design

Use SoA-style scheduler-side state to minimize cache misses and false sharing:

- `TaskState[]` (byte state + generation)
- `DependencyCount[]` (atomic `uint32_t`)
- `ContinuationSlot[]` (fiber id / handle index)
- `WaitBucketHead[]` (intrusive index list per event/counter)

Implementation notes:

- Keep per-worker ready deques as-is (LIFO local, stealing remote).
- Add parked continuation queues per wait primitive (counter/event).
- Guard wake-up with CAS state transition to prevent duplicate enqueue races.
- Pad high-contention atomics to cache line boundaries to avoid false sharing.

#### Incremental implementation plan

1. **Core primitive:** introduce `ParkCurrentFiber(wait_token)` and `UnparkReady(wait_token)` internal APIs in `Core::Tasks`.
2. **Counter integration:** wire dependency counters to invoke `UnparkReady` on transition-to-zero.
3. **Scheduler loop:** ensure workers always prefer local ready tasks, then steal, then poll unpark queues before idle sleep.
4. **Telemetry:** add `park_count`, `park_ns_p50/p95/p99`, `unpark_ns_p50/p95/p99`, per-worker deque depth histogram, steal ratio.
5. **FrameGraph touchpoint:** replace layer-level thread waits in hot path with dependency-ready continuations where feasible.

#### Acceptance criteria (must all pass)

- No worker OS thread blocks on fine-grain dependency waits during normal task execution.
- Zero lost wakeups under stress test (random dependency DAG fuzzing, 10k+ nodes/frame equivalent).
- No duplicate continuation execution (exactly-once continuation resume guarantee).
- Tail-latency improvement over baseline: p95 frame task wait time reduced by >= 25% on synthetic wait-heavy benchmark.
- Telemetry emitted each frame for park/unpark + steal/deque metrics.

#### Verification matrix

- **Unit tests:** dependency counter wake semantics, ABA/generation safety, duplicate enqueue prevention.
- **Concurrency stress tests:** randomized DAG with adversarial completion order + high worker counts.
- **Regression tests:** existing task scheduler tests and FrameGraph execution tests unchanged/green.
- **Perf tests:** A/B benchmark (`baseline` vs `fiber-park`) with fixed workload seeds and p50/p95/p99 reporting.

#### Risks & mitigations

- **Risk:** wake-queue contention under fan-in joins.
  **Mitigation:** sharded wait buckets + per-worker drain batches.
- **Risk:** starvation from local LIFO bias.
  **Mitigation:** periodic fairness tick (force steal/poll interval).
- **Risk:** lifecycle bugs from stale continuation handles.
  **Mitigation:** generational ids and debug-mode poison checks.


#### Execution work packages (implementation-ready)

**WP1 — Scheduler wait-token substrate (Core::Tasks internals)**
- Add a compact wait-token abstraction (`wait_kind`, `wait_slot`, `generation`) so parking/unparking paths never pass raw pointers.
- Store token-indexed intrusive parked lists in SoA-friendly arrays (`head`, `tail`, `next`, `task_state`, `continuation`).
- Require generational validation on every enqueue/dequeue to prevent ABA resume on recycled slots.
- Exit criteria: deterministic single-resume behavior for token reuse under adversarial slot recycling tests.
- **Status (2026-02-26): Partially implemented.** `Core::Tasks` now uses pooled intrusive parked-node queues (`parkedHead`/`parkedTail` + free-list recycling) and generation checks across acquire/release/park/unpark paths, with regression tests for exactly-once multi-waiter resume and stale-token wake isolation (`CounterEventMultipleWaitersResumeExactlyOnce`, `StaleWaitTokenUnparkDoesNotResumeNewWaiters`).

**WP2 — Fiber park/unpark API and worker loop integration**
- Introduce internal APIs in `Core::Tasks`:
  - `ParkCurrentFiber(WaitToken token)`
  - `DrainReadyFromWaitQueues(uint32_t budget)`
  - `TryTransitionParkedToReady(TaskId id)`
- Worker main loop policy per tick: `local pop -> steal -> unpark drain -> idle backoff` (never block OS worker on dependency wait).
- Add fairness tick every `N` local pops to force unpark/steal polling and avoid LIFO starvation.
- Exit criteria: no blocking waits in worker hot path; runnable parked continuations are observed within bounded polling latency.

**WP3 — Dependency counter wake wiring**
- On dependency completion, atomically decrement unresolved count; when transition reaches zero, wake exactly once via CAS state change (`Parked -> Ready`).
- Encode wake path as branch-light fast path:
  - non-zero result => return
  - zero result => CAS + ready enqueue
- Keep wake logic sharded (per-worker/per-token shard) to reduce fan-in cache-line contention.
- Exit criteria: zero lost wakeups, zero duplicate enqueue on randomized high-fan-in DAG stress.

**WP4 — Telemetry + SLO instrumentation**
- Emit per-frame counters/histograms:
  - `tasks.park.count`, `tasks.unpark.count`
  - `tasks.park.ns.{p50,p95,p99}`, `tasks.unpark.ns.{p50,p95,p99}`
  - `tasks.deque.depth.{worker}` histogram
  - `tasks.steal.ratio`, `tasks.idle.wait_ns`
- Add compile-time telemetry toggle compatible with existing lock-free ring-buffer pipeline.
- Exit criteria: metrics visible in telemetry export and stable under stress (no counter drift or negative deltas).

**WP5 — FrameGraph orchestration touchpoint (incremental adoption)**
- First pass: retain current layer metadata but replace dependency waits in hot path with continuation parking where dependency counters already exist.
- Second pass: prepare migration path to ready-queue execution (TODO 1.1 item 2) without changing debug layer visualization semantics.
- Exit criteria: existing FrameGraph regression tests pass unchanged; no additional frame stalls introduced.

#### File-level change map (planned)

- `src/Core/Tasks/`
  - Scheduler internals and worker loop (`Scheduler`, wait queues, state transitions).
  - Counter/event primitives that currently drive `WaitForAll()` behavior.
- `src/Core/FrameGraph/`
  - Narrow integration points where dependency waits currently force thread-level blocking.
- `src/Core/Telemetry/`
  - Counter/histogram declarations and per-frame emission plumbing.
- `tests/` (Core + ECS targets)
  - Unit tests for wake semantics and duplicate-prevention invariants.
  - Stress tests for randomized DAG completion order and slot-generation safety.

#### Delivery sequence (PR slicing)

1. **PR-A (mechanics):** wait-token + park/unpark substrate + unit tests.
2. **PR-B (integration):** dependency-counter wake wiring + worker-loop fairness policy.
3. **PR-C (observability):** telemetry and SLO dashboards/exports.
4. **PR-D (orchestration):** FrameGraph hot-path adoption + regression/perf comparison report.

Each PR must remain bisect-safe and keep baseline tests green before proceeding.

## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
