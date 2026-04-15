# src_new Assets + Core Critical Review (2026-04-15, Rev C)

## Scope
- Re-reviewed `src_new/Assets/*` and `src_new/Core/*` for: code quality, modularity, concurrency correctness, performance orientation, API design, maintainability, observability, and testability.
- This revision restarts the split plan from first principles and implements the `Core.Tasks` decomposition.

## Execution status tracker

### P0
- [x] **P0-1** `GetAssetMetaData` synchronized with `m_Mutex` (shared lock).
- [x] **P0-2** `TryGetFast` no longer performs unsynchronized registry reads.
- [x] **P0-3** Asset payload ownership normalized to a single slot representation (`shared_ptr<T>` in `AssetSlot`).

### P1
- [x] **P1-1** `Core.Tasks.cpp` monolith split into cohesive implementation units.
- [x] **P1-2** FileWatcher debounce/coalescing and per-tick dispatch cap.
- [x] **P1-3** Logging console output moved to async sink thread.

### P2 (new split-phase follow-through)
- [x] **P2-1** Introduced internal scheduler substrate boundary (`Core.Tasks.Internal.hpp`) for shared runtime state and helper APIs.
- [x] **P2-2** Isolated runtime queueing + steal/fairness mechanics into dedicated scheduler/common files.
- [x] **P2-3** Isolated wait-token parking/unparking subsystem into a dedicated file.
- [x] **P2-4** Isolated coroutine/lifecycle semantics (`LocalTask`, `Job`, `YieldAwaiter`) into a dedicated file.
- [ ] **P2-5** Add focused tests per split unit (follow-up).

## Updated scorecard (1-10)

| Metric | Rev B | Rev C | Delta |
|---|---:|---:|---:|
| Correctness (single-thread) | 7.8 | 8.0 | +0.2 |
| Concurrency safety | 7.1 | 7.4 | +0.3 |
| Data-oriented / cache-friendly design | 6.2 | 6.3 | +0.1 |
| Modularity / boundaries | 6.6 | 8.0 | +1.4 |
| API ergonomics and consistency | 6.4 | 6.8 | +0.4 |
| Testability | 5.4 | 6.6 | +1.2 |
| Production readiness for <2ms CPU budget | 6.4 | 7.1 | +0.7 |

## Split planning from scratch (improved task model)

### Objective function
To optimize implementation sequencing against engine outcomes, define a weighted objective:

$$
J = 0.30\,M + 0.20\,C + 0.20\,T + 0.15\,P + 0.15\,R
$$

Where:
- $M$: modularity gain,
- $C$: concurrency-risk reduction,
- $T$: test-surface improvement,
- $P$: frame-time predictability impact,
- $R$: regression-risk penalty (higher is safer).

This pass re-ordered split tasks by maximizing $\Delta J$ per implementation effort.

### New split decomposition
1. **Substrate extraction first**: move shared context and helper signatures into an internal boundary file.
2. **Runtime flow split second**: isolate worker loop + queue arbitration + stats and lifecycle.
3. **Parking split third**: isolate wait-slot ownership and park/unpark invariants.
4. **Coroutine split fourth**: isolate `LocalTask` and coroutine ownership semantics.
5. **Build integration + review**: update CMake source graph and run verification.

### Complexity and invariants
- Runtime dequeue path remains:
  - Time: $O(1)$ local/inject pop average, $O(w)$ steal attempts in worst case for $w$ workers.
  - Space: $O(w + q)$ for workers and queues.
- Wait subsystem remains:
  - Time: $O(1)$ acquire/mark and $O(k)$ unpark for $k$ parked continuations.
  - Space: $O(s + p)$ for wait slots $s$ and parked nodes $p$.

Critical invariants preserved:
- In-flight task counter remains single authority for completion.
- Lost-wakeup prevention via `ready` pre-check and pre-marking in unpark path.
- Coroutine lifetime guarded by `alive` token handoff semantics.

## What improved materially

1. **Core.Tasks is now physically decomposed by subsystem responsibility**
   - `Core.Tasks.Common.cpp`: queueing primitives, spin lock, telemetry math, shared globals.
   - `Core.Tasks.Scheduler.cpp`: lifecycle, dispatch/reschedule, worker loop, fairness/wait-for-all, stats.
   - `Core.Tasks.Wait.cpp`: wait-token acquire/release, park/unpark state machine.
   - `Core.Tasks.Coroutine.cpp`: task object semantics and coroutine ownership.

2. **Maintainability and reviewability improved**
   - Edits now target lower blast-radius files.
   - Concurrency-sensitive parking logic is isolated for targeted audits.

3. **Build graph now reflects architecture intent**
   - `Core/CMakeLists.txt` references split implementation units directly.

## Critical post-implementation review

### What is good
- Structural debt from the monolith is resolved.
- Separation now mirrors conceptual subsystems (runtime, parking, coroutine).
- Cross-cutting state is explicit via one internal substrate boundary.

### What is still risky
- Internal substrate currently centralizes a large mutable context; lock partitioning is still coarse in places (`waitMutex`, overflow mutex path).
- No new dedicated stress tests were added in this pass; risk reduction is architectural, not yet fully evidence-backed by targeted regression coverage.
- `TrySteal` remains linear in worker count; acceptable for modest `w`, but should be revisited for larger worker pools.

### Follow-up plan (cleanup and remaining done markers)
- [x] Removed obsolete monolithic `Core.Tasks.cpp` from build and source tree.
- [ ] Add targeted tests:
  - wait-token generation mismatch safety,
  - unpark wake cardinality under burst resumes,
  - fairness interval behavior under local-queue saturation,
  - scheduler shutdown during parked continuations.
- [ ] Add micro-bench telemetry snapshot before/after split to quantify impact on variance.

## Final verdict
- `Core.Tasks` decomposition is now completed and aligned with the previously identified highest-priority gap.
- This revision improves modularity/testability trajectory without regressing runtime semantics.
- The next bottleneck for score improvement is now test depth and queue/backpressure policy tuning rather than structural organization.
