# Commit Review (Last 2 Days)

Review window: **2026-04-18 → 2026-04-20 (UTC)**.

## Scope
Non-merge commits reviewed:
- `f18fa0c` Consolidate Assets/Core review and refactor docs into unified plan
- `ed16d34` Audit and harden sec_new assets/core unified plan gaps
- `251fb0b` fixed issues in src_new/Core and src_new/Assets
- `3355038` core: add explicit CPU/GPU/streaming graph interfaces and remove graph modes
- `37fd0dd` update
- `e51b5d3` trying to debug a mutex deadlock in Playstore
- `286c243` fixed function local static inline variable and AssetService

## Executive Summary
- **Overall direction is positive** (better graph/domain separation, improved AssetService unwind paths, better sharded stores).
- **However, there are still correctness risks** in scheduler and asset typing semantics that can surface as hard-to-reproduce frame/task bugs.
- **Primary concern:** scheduler currently tolerates malformed dependency input and can silently produce an invalid execution plan instead of failing fast.

## Critical Findings

### 1) Missing dependency validation in DAG scheduler (High)
In plan building, dependencies not found in the task set are currently ignored:

```cpp
if (depIt == idToIndex.end())
    continue;
```

This means a producer can emit a task with a missing prerequisite and still get a “valid” schedule; the task may run too early and violate data hazards.

**Where:** `src_new/Core/Core.Dag.Scheduler.cpp`.

**Recommendation:** return `InvalidArgument` (or a dedicated dependency error) when any dependency ID is missing, and include producer/task diagnostics in `ScheduleStats`.

---

### 2) Duplicate task IDs are not rejected (High)
Task IDs are inserted with `idToIndex.emplace(...)` but insertion failure is not checked. Duplicate IDs therefore pass through with ambiguous semantics.

**Where:** `src_new/Core/Core.Dag.Scheduler.cpp`.

**Recommendation:** check `emplace` result and fail schedule construction on duplicate IDs. This should be covered by tests in `tests/Core/Test.Core.DagScheduler.cpp`.

---

### 3) Type contract hole in AssetService path de-dup path (Medium-High)
`LoadErased(path, typeId, loader)` returns an existing asset ID when path already exists, but does not validate requested `typeId` against existing asset metadata. A caller can request the same path with a different payload type and receive an ID of incompatible type.

**Where:** `src_new/Assets/Asset.Service.cpp`.

**Recommendation:** when `pathIndex.Find(abs)` hits, query `registry.GetMeta(id)` and reject mismatched `typeId` with `TypeMismatch`.

## Per-Commit Assessment

### `f18fa0c` (2026-04-18)
- Mostly planning/docs plus initial `Core.DagScheduler` module interface.
- Good architectural consolidation; low runtime risk.
- **Grade:** B+

### `ed16d34` (2026-04-18)
- Meaningful hardening in `Asset.LoadPipeline`, `Asset.Registry`, and filesystem plus additional tests.
- Good rollback/unwind intent and improved state transitions.
- **Grade:** A-

### `251fb0b` (2026-04-18)
- Small fix batch (renames, CMake, load pipeline and registry touchups).
- Appears corrective; low regression footprint.
- **Grade:** B

### `3355038` (2026-04-18)
- Major scheduler/graph refactor and broad additions.
- Strong momentum, but introduced validation gaps (missing dependency/duplicate ID handling).
- **Grade:** B- (would be A- with strict validation)

### `37fd0dd` (2026-04-18)
- ECS module cleanup and API reshaping.
- Commit message is non-descriptive (`update`), making archaeology and bisect quality worse.
- **Grade:** C+ (message quality penalty)

### `e51b5d3` (2026-04-19)
- Refactors PathIndex/PayloadStore to PImpl and sharded locking internals.
- Good direction for encapsulation and lock hygiene.
- Commit message indicates debug intent, not outcome; should be squashed/reworded before long-term retention.
- **Grade:** B

### `286c243` (2026-04-20)
- Cleans up AssetService significantly and fixes hash-map key hashing issue in payload store.
- Improves lifecycle unwind and loader token management.
- Still carries the type mismatch hole on path de-dup load.
- **Grade:** B+

## Risk Ranking (Current HEAD inherited from reviewed commits)
1. Scheduler dependency validation gaps (highest impact, cross-domain hazards).
2. Duplicate task ID acceptance (nondeterministic schedule semantics).
3. AssetService type mismatch on duplicate-path load (runtime type safety hole).

## Suggested Immediate Actions
1. Add strict input validation in `BuildPlanFromTasks`:
   - fail on missing dependency IDs,
   - fail on duplicate task IDs,
   - expose counts/details in stats.
2. Add test vectors:
   - missing dep should fail,
   - duplicate ID should fail,
   - cycle should produce deterministic diagnostic.
3. Enforce path/type consistency in `AssetService::LoadErased` for cache hits.
