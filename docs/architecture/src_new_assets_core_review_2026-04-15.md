# src_new Assets + Core Critical Review (2026-04-15, Rev B)

## Scope
- Reviewed `src_new/Assets/*` and `src_new/Core/*` for: code quality, modularity, concurrency correctness, performance orientation, API design, maintainability, observability, and testability.
- This revision includes implementation updates for P0 and P1 action items.

## Execution status tracker

### P0
- [x] **P0-1** `GetAssetMetaData` synchronized with `m_Mutex` (shared lock).
- [x] **P0-2** `TryGetFast` no longer performs unsynchronized registry reads.
- [x] **P0-3** Asset payload ownership normalized to a single slot representation (`shared_ptr<T>` in `AssetSlot`).

### P1
- [ ] **P1-1** `Core.Tasks.cpp` monolith split into implementation partitions/files (**not completed** in this pass).
- [x] **P1-2** FileWatcher gained debounce/coalescing and per-tick dispatch cap to reduce burst enqueue pressure.
- [x] **P1-3** Logging console output moved to asynchronous sink thread (non-blocking caller path wrt `std::cout`).

## Updated scorecard (1-10)

| Metric | Initial | Updated | Delta |
|---|---:|---:|---:|
| Correctness (single-thread) | 7.5 | 7.8 | +0.3 |
| Concurrency safety | 5.5 | 7.1 | +1.6 |
| Data-oriented / cache-friendly design | 6.0 | 6.2 | +0.2 |
| Modularity / boundaries | 6.5 | 6.6 | +0.1 |
| API ergonomics and consistency | 6.0 | 6.4 | +0.4 |
| Testability | 5.0 | 5.4 | +0.4 |
| Production readiness for <2ms CPU budget | 5.5 | 6.4 | +0.9 |

## What improved materially

1. **Registry read contract is now coherent on critical paths**
   - Previously mixed synchronized/unsynchronized reads around `AssetMetaData` and payload access.
   - Now the targeted access paths are synchronized.

2. **Ownership model complexity reduced in Assets**
   - `AssetSlot<T>` no longer branches between `unique_ptr` and `shared_ptr` storage variants.
   - This simplifies reasoning and reduces branch-dependent semantics in payload access.

3. **Log emission no longer blocks on console I/O in caller threads**
   - The caller path updates the ring buffer and enqueues for async console flush.
   - This addresses frame-time variance risk from synchronous `std::cout` in worker/game threads.

4. **FileWatcher now coalesces bursts**
   - Debounce + per-tick cap reduces pathological enqueue storms from editor save bursts / rapid write sequences.

## Remaining high-priority gap

### P1-1 (remaining): `Core.Tasks.cpp` monolith
- Still very large and mixes worker policy, telemetry, queueing, and wait-slot parking in one implementation unit.
- This remains the primary maintainability/testability debt in Core.

## Critical analysis vs engine target (<2ms CPU frame)

### Latency model (qualitative)
- Let frame-time overhead contribution from utility systems be approximated as:
$$
T_{utility} = T_{asset-read} + T_{log} + T_{watcher} + T_{scheduler-overhead}
$$

- Changes here improve two terms:
  - `T_{log}`: shifted from synchronous console I/O on caller thread to async flush thread.
  - `T_{watcher}`: bounded burst scheduling pressure through coalescing and dispatch cap.

- Tradeoff introduced:
  - `T_{asset-read}` for `TryGetFast` now includes shared-lock acquisition, increasing predictable per-call overhead but removing race risk.

### Complexity notes
- FileWatcher scan remains linear in watch count per tick:
  - Time: $O(n)$ for scan + $O(\min(n, k))$ dispatch selection where $k$ is per-tick cap.
  - Space: $O(k)$ temporary dispatch buffer.
- Async logger enqueue path:
  - Time: amortized $O(1)$ per log push.
  - Space: unbounded queue in current form (can be improved with bounded MPMC ring + drop policy).

## Detailed issue matrix

| ID | Status | Outcome |
|---|---|---|
| P0-1 | Done | Synchronized metadata read path now aligned with mutex discipline. |
| P0-2 | Done | `TryGetFast` safety issue closed by synchronization; no release-only race window in this API. |
| P0-3 | Done | Single ownership representation in `AssetSlot` reduced complexity. |
| P1-1 | Pending | Structural decomposition of `Core.Tasks.cpp` still needed. |
| P1-2 | Done | Debounce/coalescing + capped dispatch implemented for FileWatcher. |
| P1-3 | Done | Async logging sink implemented; caller no longer writes directly to `std::cout`. |

## Next remediation sequence

### Stage A (next)
1. Split `Core.Tasks.cpp` into cohesive implementation partitions:
   - worker loop/policy,
   - parked-continuation subsystem,
   - telemetry aggregation.
2. Add targeted tests per partition.

### Stage B
1. Add bounded backpressure policy for async logging queue (drop oldest/drop debug-first).
2. Add telemetry counters for dropped logs and watcher coalesced events.

### Stage C
1. Revisit `TryGetFast` for genuinely lock-free safe path (RCU/epoch snapshot model) if profiling shows lock cost in hot frame loops.

## Final verdict
- **Substantial risk was removed** in Assets concurrency paths and runtime diagnostics behavior.
- The system is now materially safer and more frame-time-stable than Rev A.
- Remaining architectural bottleneck is mainly concentrated in `Core.Tasks` implementation decomposition and associated stress-test depth.
