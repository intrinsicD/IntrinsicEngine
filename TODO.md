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
  - Extend telemetry with per-worker deque depth, steal ratio, and park/unpark latency to validate fairness and tail behavior.

- [ ] **Remove coarse FrameGraph layer barriers (High).**
  - Replace per-layer `WaitForAll()` execution with dependency-count-driven ready queues.
  - Preserve layer grouping for diagnostics/visualization only.
  - Add optional critical-path-aware pass prioritization to reduce frame makespan and tail latency.

- [ ] **Optimize `Core::DAGScheduler` compile-path lookup + dedupe structures (Medium).**
  - Replace linear `resourceKey` scans with a flat hash/robin-hood table.
  - Add faster producer-edge dedupe for high-degree nodes (sorted-vector fast path and/or compact bloom/bitset guard).
  - Keep current pool/high-water-mark reuse strategy while changing lookup structures.

- [ ] **Resolve runtime API/ownership drift in orchestration paths (Medium).**
  - Remove or fully wire `defaultTextureIndex` in `Runtime.RenderOrchestrator` constructor.
  - Audit central runtime ownership boundaries and reduce `std::shared_ptr` usage where borrowed references or explicit owners are sufficient.

- [ ] **Add offline dependency mode to CMake configure (Medium).**
  - Provide an `INTRINSIC_OFFLINE_DEPS=ON` path that avoids network fetch during configure.
  - Document vendored/mirrored dependency workflows for restricted CI and reproducible local setup.

- [ ] **Establish architecture SLOs + telemetry milestones (Cross-cutting).**
  - Define measurable targets for DAG compile budget, scheduler contention/tail latency, and frame critical-path timing.
  - Add instrumentation for queue contention, steal ratio, barrier/idle wait time, and per-frame compile/execute split.

## 1.2 2026-02-26 Code Quality Audit (Style/Syntax, Architecture, Duplication)

This section captures **newly observed inconsistencies** and concrete remediation actions.

### A. Coding style & syntax consistency

- [ ] **Eliminate contradictory CMake release-flag assignments (High).**
  - `INTRINSIC_RELEASE_FLAGS` is assigned twice in top-level `CMakeLists.txt`; the second assignment silently overrides the first.
  - This introduces configuration drift and makes optimization policy ambiguous (`-march=native` path is partially duplicated then reset).
  - Action: keep a single source of truth for release flags and gate native-arch only through `INTRINSIC_ENABLE_NATIVE_ARCH`.

- [ ] **Normalize TODO comment policy in production code (Medium).**
  - There is an inline unresolved TODO in `Runtime.RenderOrchestrator` constructor about `defaultTextureIndex` ownership/use.
  - TODOs should be tracked in this architecture backlog with acceptance criteria; code comments should reference an issue/backlog item ID, not open-ended questions.
  - Action: either remove the parameter and call-site plumbing, or wire it to a concrete behavior (fallback material/texture path).

- [ ] **Align modern C++ usage with stated C++23 standards (Medium).**
  - Current codebase uses `std::expected` but does not use monadic `.and_then/.transform` patterns; explicit object parameters are also absent.
  - Action: define a focused adoption policy: require monadic chaining for multi-stage expected pipelines (importers/asset loading), and document where explicit object parameters are beneficial vs noisy.

### B. Architecture findings

- [ ] **Close the gap between “fiber parking” goal and current scheduler behavior (High).**
  - Scheduler currently uses worker-local deques + stealing + global inject queue, but waits are thread-level (`WaitForAll`) rather than dependency-level fiber parking.
  - Result: potential head-of-line blocking for wait-heavy chains and weaker latency isolation under mixed workloads.
  - Action: add parked continuation queues keyed by dependency counters/events, with wake-on-ready semantics and telemetry (`park_count`, `park_ns`, `unpark_ns`).

- [ ] **Remove CPU FrameGraph layer barriers that serialize independent work (High).**
  - `FrameGraph::Execute()` executes per-layer and performs `Tasks::Scheduler::WaitForAll()` barrier after each multi-pass layer.
  - This preserves correctness but over-serializes the schedule and can inflate frame critical path.
  - Action: execute ready tasks continuously from indegree counters (Kahn-ready queue) and keep layers only as debug metadata.

- [ ] **Reduce compile-time overhead in `Core::DAGScheduler` hot paths (High).**
  - `GetResourceState()` and edge dedupe both rely on linear scans (`O(R)` and `O(out_degree)` respectively).
  - Existing TODO marks this medium priority, but this is on the per-frame compile path and should be treated as a high-priority perf risk as node/resource counts grow.
  - Action: switch resource-state lookup to flat hash table and use per-node small-set/bit-guard for edge dedupe.

- [ ] **Clarify ownership boundaries and remove avoidable shared ownership in runtime hot paths (Medium).**
  - Device/runtime orchestration APIs still expose widespread `std::shared_ptr` ownership even when ownership appears single-rooted.
  - Action: convert to explicit owner + borrowed refs/spans/handles where lifetime is frame- or system-scoped.

- [ ] **Replace manual `new/delete` for core RHI members (Medium).**
  - `RHI::VulkanDevice` still uses raw `new`/`delete` for `TransientAllocator`.
  - Action: migrate to `std::unique_ptr` (or arena-owned handle) to tighten exception-safety assumptions and ownership clarity.

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

- [ ] **Priority calibration mismatch (Medium).**
  - DAGScheduler compile-path optimization is labeled Medium despite being in per-frame orchestration path.
  - Action: re-rank based on frame-time impact and CI perf telemetry, not implementation convenience.

- [ ] **Ownership drift TODO duplicates unresolved code-level TODO (Medium).**
  - Backlog item and inline code TODO point to the same unresolved `defaultTextureIndex` issue.
  - Action: keep a single source (backlog item with ID), remove ambiguous inline TODO text once linked.

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


## 2. Related Documents

- `ROADMAP.md` — feature roadmap, prioritization phases, and long-horizon planning details.
- `DONE.md` — historical notes and previously completed architecture narratives.
