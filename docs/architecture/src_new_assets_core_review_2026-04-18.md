# `src_new` Assets + Core Critical Architecture Review (2026-04-18)

## Scope and framing

This review covers only:
- `src_new/Assets/*`
- `src_new/Core/*`
- `tests/Asset/*` and `tests/Core/*` (coverage posture only)

Interpreting your request "sec_new" as `src_new` based on repository layout.

Primary evaluation axes:
1. Correctness and concurrency safety.
2. Modularity and API boundary quality.
3. Data-oriented performance suitability for low-latency runtime.
4. C++23/module hygiene and maintainability.
5. Testability and observability.

---

## Executive summary

Overall assessment: **architecturally improving, still not production-robust for a strict <2ms CPU frame-time envelope**.

- The Assets stack has clear subsystem separation (`Registry`, `PathIndex`, `PayloadStore`, `LoadPipeline`, `EventBus`, `Service`) and good error-surface consistency with `Core::Expected`/`Core::Result`.
- Core task runtime is materially better than monolithic historical shapes, but still has **coarse synchronization and lifecycle hazards** that can surface under high contention.
- Test coverage is strong in Assets; Core has meaningful blind spots in exactly the highest-risk runtime components (`Tasks`, `Logging`, `LockFreeQueue`).

### Scorecard (1–10)

| Metric | Score | Notes |
|---|---:|---|
| Correctness (single-thread) | 8.0 | Generally explicit state guards and handle validity checks. |
| Concurrency safety | 6.7 | Multiple race-prone interleavings and coarse mutex domains remain. |
| Modularity / boundary clarity | 8.1 | Good decomposition, especially in Assets and split Tasks units. |
| Data-oriented / cache efficiency | 5.9 | Hot paths still pay mutex + hash-map + heap-allocation tax. |
| C++23 / modules discipline | 6.8 | Modules are used; advanced C++23 idioms are only partially adopted. |
| Observability / diagnosability | 7.0 | Useful counters/histograms exist, but no complete lifecycle traces. |
| Test depth (risk-focused) | 6.2 | Assets good; Core high-risk files partially untested or disabled. |
| Runtime readiness for aggressive frame budget | 6.0 | Functional but not yet tuned for deterministic low-latency operation. |

---

## Quantitative shape

### Size footprint (rough structural signal)

- `src_new/Assets`: **~1748 LOC**
- `src_new/Core`: **~2982 LOC**

Largest files in reviewed scope:
- `Asset.Service.cppm` (~337 LOC)
- `Asset.LoadPipeline.cpp` (~312 LOC)
- `Core.Tasks.WaitToken.cpp` (~215 LOC)
- `Core.Filesystem.PathResolver.cpp` (~214 LOC)
- `Core.CallbackRegistry.cppm` (~205 LOC)
- `Core.Logging.cpp` (~196 LOC)

Interpretation: no catastrophic file bloat, but several files are large enough to hide subtle invariants that deserve more dedicated tests.

---

## Assets subsystem review

## A. What is strong

1. **Boundary decomposition is clean and coherent**
   - The decomposition into registry/index/payload/pipeline/event/service is conceptually sound and maintainable.

2. **State-machine intent is explicit**
   - `AssetState` transitions are mostly guarded via `SetState(expected,next)` compare-and-transition semantics.

3. **Failure unwind paths are thoughtfully implemented**
   - `AssetService::Load` performs multi-step rollback on partial failure (path index, payload, registry, loader token).

4. **Generational handles used correctly at system boundaries**
   - Strong-handle style IDs reduce stale-reference class of bugs.

## B. Critical issues / risks

### B1 — Divergent reload semantics create race-sensitive behavior

`Reload<T>(id, loader)` and parameterless `Reload(id)` do not mutate in exactly the same order.

- Templated reload: decode first, then transition `Ready -> Unloaded`, then publish slot.
- Token-based reload: callback thunk can publish/update payload slot before the explicit state transition in `Reload(id)`.

This is not always incorrect, but it enlarges the interleaving surface where payload/state can temporarily diverge under concurrent operations.

**Impact:** medium-high correctness risk under concurrent destroy/reload contention.

### B2 — EventBus flush complexity can explode under high event fanout

Current `Flush()` pattern repeatedly:
- locks,
- copies broadcast listeners,
- copies per-asset listeners,
- unlocks,
- invokes callbacks,
for each queued event.

Complexity is approximately:
$$
T_{flush} = O\left(\sum_{e=1}^{E}(B + L_e)\right)
$$
with additional allocation churn from callback copying.

**Impact:** bursty stalls and allocator pressure in event-heavy scenes.

### B3 — PayloadStore is type-erased but not data-oriented for hot reads

`Publish` stores `std::vector<T>` per asset even when many payloads are scalar-or-single-object in practice.

- Extra heap indirection/allocation overhead.
- Hash-map lookup + mutex on every read path.

**Impact:** unnecessary memory traffic in frequent reads.

### B4 — Stage trail observability is transient-only

`GetStageTrail` only works while asset is in-flight; completion erases entry.

**Impact:** post-mortem debugging of slow/failing loads is limited.

### B5 — PathIndex/Registry locking is coarse for scale

Single mutexes protect entire structures. Correct but not scalable under many threads or high-frequency metadata polling.

---

## Core subsystem review

## A. What is strong

1. **Tasks split into subsystem-focused units**
   - Lifecycle, dispatch, worker, wait-token, stats are separated.

2. **Useful scheduler telemetry exists**
   - Steal attempts/success, park/unpark counters, latency histograms are valuable operational signals.

3. **CallbackRegistry is robustly designed**
   - Generational token invalidation + invoke-outside-lock is clean and practical.

4. **LockFreeQueue implementation follows known bounded MPMC ring shape**
   - Turn counters and power-of-two mask architecture are sensible.

## B. Critical issues / risks

### C1 — `ParkCountAtomic()` can dereference null scheduler context

`ParkCountAtomic()` returns `s_Ctx->parkCount` with no null guard.

**Impact:** undefined behavior if called before `Initialize()` or after `Shutdown()`.

### C2 — Coroutine lifetime handoff still has sharp edges

`Job::DestroyIfOwned()` may destroy a not-yet-completed coroutine frame while reschedule tasks may still exist. The `alive` flag mitigates this logically, but this is still a subtle ownership/liveness contract with high bug potential.

**Impact:** high severity if misused by callers; difficult-to-reproduce crashes.

### C3 — Work stealing remains $O(w)$ scan per attempt

For worker count $w$, steal probing is linear:
$$
T_{steal} = O(w)
$$
per failure path.

For modest worker counts this is acceptable, but it can degrade under oversubscription/high contention.

### C4 — Wait-token subsystem is mutex-centric and allocation-backed

Wait-slot and parked-node operations are serialized by `waitMutex` and vector/free-list management.

**Impact:** correctness can be maintained, but this is not aligned with lock-free/fiber-first performance intent.

### C5 — FileWatcher architecture is polling-based and globally static

- 500ms scan cadence is high latency for hot reload.
- Duplicate watch registration is not deduplicated.
- Global static state can complicate test isolation.

### C6 — Logging async sink queue is unbounded

Under sustained log bursts, queue growth is effectively unbounded.

**Impact:** memory growth risk + timing jitter.

---

## Modularity and architecture fitness

## Positive

- Subsystem decomposition is directionally correct.
- Module naming and partitioning are coherent enough to scale.

## Remaining anti-patterns

1. **Global singleton-like runtime state** (`s_Ctx`, static FileWatcher/log state) increases coupling and lifecycle fragility.
2. **Coarse lock domains** in registry/path/payload/event/wait paths trade correctness for throughput.
3. **Mixed semantic levels** in `AssetService` (orchestration + state semantics + token lifecycle) make it a likely future God-object.

---

## C++23 and modern style conformance

Adoption status is mixed:

- ✅ Good: modules, `std::expected`, `std::span`, atomics, strong-handle generational IDs.
- ⚠️ Partial: monadic operations on expected (`.and_then/.transform`) are rarely used; many manual branching chains remain.
- ⚠️ Missing in hot-path design: allocator strategy is not explicit (heavy default heap usage in event/payload paths).
- ⚠️ Not evident: explicit object parameters and ranges-based idioms in performance-sensitive sections.

---

## Test and verification posture

## Positive

- Assets tests are comprehensive and behavior-focused.
- CallbackRegistry has concurrency-oriented tests.

## High-risk gaps

In `tests/Core/CMakeLists.txt`, several important suites are currently commented out:
- `Test_CoreLockFreeQueue.cpp`
- `Test_CoreLogging.cpp`
- `Test_CoreTasks.cpp`

These are exactly the areas where regressions are both subtle and high-impact.

---

## Prioritized remediation plan

## P0 (must-do before claiming production robustness)

1. **Fix `ParkCountAtomic()` lifetime safety**
   - Return pointer/optional reference or guard with initialized-state contract.
2. **Unify reload sequencing semantics**
   - Make both reload entry points pass through one state-transition and payload-swap order.
3. **Re-enable and enforce Core high-risk tests**
   - Tasks, Logging, LockFreeQueue must be CI-gating.

## P1 (short-term performance + reliability gains)

1. **Bound logging queue or apply backpressure/drop policy**.
2. **Introduce per-asset or sharded listener/storage locks in Asset event path**.
3. **Persist bounded stage history ring for recent completed/failed assets**.

## P2 (architecture alignment with low-latency target)

1. **Replace polling file watcher with platform-native event backend** (inotify/FSEvents/ReadDirectoryChangesW abstraction).
2. **Investigate lock partitioning/sharding for `AssetRegistry` + `AssetPathIndex` + `AssetPayloadStore`**.
3. **Evaluate steal policy improvements** (randomized victim selection, hierarchical queues, or per-core affinity heuristics).

---

## Final verdict

The current `src_new` Assets/Core state is **good engineering groundwork** with real modular progress, but still **below the rigor/perf bar expected for a next-gen low-latency runtime**.

The fastest path to materially better readiness is:
1. remove a few lifecycle hazards,
2. close test blind spots,
3. harden event/task hot paths against burst contention and allocation churn.

Do those three, and this architecture transitions from “promising prototype quality” to “credible production core.”
