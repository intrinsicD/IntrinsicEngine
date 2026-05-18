# GRAPHICS-036 — Pipelined frames and double-buffered render world (planning)

## Goal
Lock down the contract for running simulation of frame N concurrently with rendering of frame N-1 against an immutable, double-buffered `ImmutableRenderWorld`, so that subsequent feature work (RT, neural shaders, async streaming) overlaps execution rather than serializes it. Planning only — no producer/consumer rewiring lands here.

## Non-goals
- No code changes to `Runtime.RenderExtraction`, `IRenderer::SubmitRuntimeSnapshots`, or `Graphics.RenderWorld` in this slice.
- No threading model changes outside the documented snapshot handoff.
- No relaxation of `AGENTS.md` §4 ("graphics has no live ECS knowledge").
- No introduction of mutable cross-frame state into graphics.
- No changes to `RuntimeRenderSnapshotBatch` shape; only its lifecycle.

## Context
- Status: done (2026-05-18, branch `claude/graphics-rendering-tasks-dKlmC`).
- Commit reference: pending current change.
- Owner layers: `runtime` (extraction producer + double-buffer ownership), `graphics/renderer` (consumer of immutable snapshot).
- Today extraction runs synchronously to completion before graphics consumes the snapshot. As phase-2/3 features (RT, hybrid GI, neural denoise) raise the per-frame budget, this serial model compounds latency.
- Bevy's render-world separation and Unity DOTS Hybrid Renderer V2 both pipeline simulation and rendering against an immutable copy of the scene. The same shape fits IntrinsicEngine because the snapshot boundary is already a hard architectural invariant.
- Cross-links: `GRAPHICS-002` (snapshot contract), `GRAPHICS-016` (extraction handoff), `GRAPHICS-022` (rendergraph diagnostics), `GRAPHICS-052` (deltaful scene depends on a stable double-buffer).

## Design decisions to record
1. **Buffer count.** Decide between 2 (classic double-buffer) and 3 (Bevy-style triple-buffer with reclamation) buffers of `ImmutableRenderWorld`. Record the trade-off (memory vs. stall risk under variable extraction time).
2. **Ownership.** The double-buffer pool is owned by `runtime`. Graphics never allocates or evicts snapshot storage. Record the exact module + type that owns the pool (suggested `Runtime.RenderWorldPool`).
3. **Handoff API.** Replace the existing one-shot submit pattern with a producer/consumer swap: extraction writes to a "back" buffer and atomically rotates it to "front"; renderer's `BeginFrame()` claims the current front. Record the precise atomicity guarantee (acquire/release, reference count, retire policy).
4. **Reclamation rule.** A snapshot is reclaimable once no in-flight frame references it. Tie reclamation to the existing `framesInFlight` retire-deadline pattern from `GRAPHICS-015Q`.
5. **Stall policy.** Decide what happens when the producer is faster than the consumer (back-pressure: drop, replace, or block) and vice versa (consumer reuses the previous front buffer). Record both rules and their diagnostic counters.
6. **Determinism mode.** Decide an opt-in "synchronous extraction" mode for tests and benchmarks where the pipeline collapses to N == N-1 (no pipelining). Record the runtime flag location.
7. **Diagnostics.** Name explicitly: `RenderWorldPipelineStallCount`, `RenderWorldExtractionSkipCount`, `RenderWorldFrameAgeFrames` (counter histogram of how old the consumed snapshot is). Atomic increments only.
8. **Snapshot shape stability.** Confirm `ImmutableRenderWorld` and `RuntimeRenderSnapshotBatch` field shapes are unchanged; only their lifetime is changed.
9. **Test seam.** Add a contract test seam that lets CPU tests inject a deterministic schedule (extract A, extract B, render reads A, etc.) without a real renderer.
10. **Layering audit.** `runtime` owns the pool and the swap; `graphics` only sees `const ImmutableRenderWorld&`. No graphics-side mutation of pool state.

## Recorded decisions

1. **Buffer count.** `RenderWorldPool` uses **three** `ImmutableRenderWorld` slots (Bevy-style triple-buffer). Rationale: a 2-slot pool forces the producer to block whenever the consumer is mid-frame, which collapses pipelining the moment per-frame budgets diverge; a 3-slot pool lets the producer always have a free back buffer while a third remains in-flight on the consumer. Memory cost is acceptable: `ImmutableRenderWorld` is a snapshot view (POD spans + small instance arrays), not a duplicated GPU scene, and the third slot is only allocated when pipelining is enabled. Triple-buffer beyond 3 is rejected because the extra reclamation window pays no latency benefit.
2. **Ownership.** The pool is owned by `runtime` in a new value type `Runtime::RenderWorldPool` declared at `src/runtime/Runtime.RenderWorldPool.cppm`. Graphics never allocates, evicts, or mutates snapshot storage; graphics only sees `const ImmutableRenderWorld&` per `AGENTS.md` §4. The pool is composed by `Runtime.Engine` next to `Runtime.RenderExtractionCache`.
3. **Handoff API.** Producer/consumer swap with a *generation-tagged front pointer* so the consumer's reservation is atomic with respect to publishes and reclamation. The pool exposes a single `std::atomic<uint64_t> FrontTag` packing `{ generation : uint32_t, slotIndex : uint32_t }` (or an equivalent 16-byte `std::atomic<FrontTag>` on platforms where double-word CAS is cheap; the 64-bit packed form is the canonical CPU-only shape). Each slot has its own `std::atomic<uint32_t> Refcount` and a stable `std::atomic<uint32_t> Generation` field stamped at publish time.

   **`PublishBack(slot)`** — extraction-side, after the back buffer is fully written:
   1. `uint32_t newGen = ++pool.GenerationCounter` (monotonic, never reused);
   2. `slot.Generation.store(newGen, release)`;
   3. `pool.FrontTag.store(Pack(newGen, slot.Index), release)` — single atomic write rotates the slot to front. No `AcquireFront()` in flight can observe a torn `{gen, slot}` pair.

   **`AcquireFront()`** — renderer-side at `BeginFrame()`. The reservation is a CAS-validated optimistic lease, *not* "load index then increment":
   ```
   for (;;) {
       FrontTag observed = pool.FrontTag.load(acquire);
       Slot&    slot     = pool.Slots[observed.SlotIndex];
       slot.Refcount.fetch_add(1, acquire);          // optimistically reserve
       FrontTag recheck  = pool.FrontTag.load(acquire);
       if (recheck == observed) {                    // still the front: lease is real
           return Lease{ slot, observed.Generation };
       }
       slot.Refcount.fetch_sub(1, release);          // roll back; another publish raced us
   }
   ```
   The two `FrontTag` loads with the `Refcount` increment between them implement the lease/generation check: a `Reclaim()` decision that observed `Refcount == 0` must have observed it *before* the increment, in which case the consumer will detect the generation mismatch on the second load and roll back without ever reading slot contents. The returned `Lease` carries the captured `Generation`, and the renderer asserts `lease.Generation == slot.Generation.load(acquire)` before any read; in optimized builds this assert is elided because the post-increment recheck already proved it.

   **`ReleaseFront(lease)`** — renderer-side at `EndFrame()`: `slot.Refcount.fetch_sub(1, release)`. The decrement is the only allowed transition out of the leased state.

   Rejected: load-then-increment (the race the original draft documented — between `load(FrontTag)` and `Refcount++` the producer can publish and `Tick()` can reclaim the previous front, so the increment lands on an already-reclaimed/overwritten slot); lock-based handoff (kills the pipelining latency target); RCU-style epoch reclamation (over-engineered for a 3-slot pool with deterministic `framesInFlight` reclamation); pre-arming the refcount on the producer side (the producer would have to know how many consumers will lease the publish, defeating fire-and-forget publish).
4. **Reclamation rule.** A snapshot slot is reclaimable when **all three** conditions hold simultaneously, observed atomically by `RenderWorldPool::Tick()`:
   1. `slot.Refcount.load(acquire) == 0`;
   2. `slot.Index != pool.FrontTag.load(acquire).SlotIndex` (i.e. the slot is not the *current* front; combined with Decision 3's post-increment validation this guarantees no consumer can later acquire a successful lease on this slot at the *next* generation without going through `PublishBack()` first);
   3. The `framesInFlight` retire-deadline associated with the slot's last release has passed (`currentFrame >= slot.LastReleaseFrame + framesInFlight`), reusing the existing `GRAPHICS-015Q` deadline pattern so backend resources tracked alongside the snapshot drain on the same schedule.

   `Reclaim()` is racy only against fresh `AcquireFront()` attempts: a consumer that increments `Refcount` after `Tick()`'s load observes the generation mismatch on its second `FrontTag` load and rolls back without reading, so reclaiming a refcount==0 non-front slot is safe even if a consumer is mid-`AcquireFront()`. The producer's `PublishBack()` cannot collide with `Reclaim()` because the producer only writes to the slot it currently holds as the back buffer, which is by construction *not* the front and not in the reclaimable set until the producer releases it via the next publish cycle. The reclamation pass runs at `RenderWorldPool::Tick()` invoked from `Runtime.Engine` after `EndFrame()`.
5. **Stall policy.** Producer faster than consumer: producer **replaces** the unconsumed back buffer (overwrite-in-place on the same slot) and increments `RenderWorldExtractionSkipCount`. The rationale is that gameplay simulation must not stall on rendering; the consumer can always pick up a fresher snapshot next frame. Consumer faster than producer: consumer **reuses** the previous front (no swap; refcount on the previously-acquired slot is incremented again) and increments `RenderWorldPipelineStallCount`. Rejected: blocking back-pressure (couples sim tick rate to render rate, defeats the goal); dropping the consumer (visible hitches).
6. **Determinism mode.** Opt-in `EngineConfig::RenderWorld::SynchronousExtraction = false` flag at `src/runtime/Runtime.EngineConfig.cppm`. When `true`, the pool collapses to one slot: extraction writes, publishes, and the renderer immediately acquires the same slot in the same frame; `PublishBack()` becomes a no-op apart from refcount bookkeeping. This mode is the default for `INTRINSIC_PLATFORM_BACKEND=Null` headless tests and benchmarks to keep CTest deterministic.
7. **Diagnostics.** Canonical names locked: `RenderWorldPipelineStallCount` (atomic `uint64`, consumer-saw-stale-front events), `RenderWorldExtractionSkipCount` (atomic `uint64`, producer-overwrote-unconsumed-back events), `RenderWorldFrameAgeFrames` (small fixed-bucket histogram `{0, 1, 2, 3+}` of how old the consumed front buffer is in produced-frame units). All counters live on `RuntimeRenderExtractionStats` and are zeroed on engine `Initialize()`.
8. **Snapshot shape stability.** `ImmutableRenderWorld` and `RuntimeRenderSnapshotBatch` field layouts are **unchanged** in this slice. Only the **lifetime** (refcount + reclamation deadline) changes. Adding fields to the snapshot is explicitly out-of-scope for `GRAPHICS-036`; future fields land via the per-feature snapshot-extension pattern that GRAPHICS-002 records.
9. **Test seam.** Add `Runtime::RenderWorldPool::ForceSchedule(span<PoolEventForTest>)` under a test-only `internal` export consumed by `tests/contract/runtime/Test.RenderWorldPool.cpp`. Events are `{AcquireBack, PublishBack, AcquireFront, AcquireFrontInterleavedPublish, AcquireFrontInterleavedReclaim, ReleaseFront, Tick}`; the seam lets CPU tests inject deterministic sequences including the two adversarial interleavings (publish racing `AcquireFront`'s validation re-load, reclamation observing `Refcount == 0` between the increment and the re-validation) that exercise the Decision 3 CAS-loop rollback path. Tests assert that no successful `AcquireFront` lease ever returns a slot whose `slot.Generation` has been bumped past the captured lease generation.
10. **Layering audit.** `runtime` owns the pool, the swap, the diagnostics counters, and `EngineConfig::RenderWorld`. `graphics/renderer` is read-only through `const ImmutableRenderWorld&` from `BeginFrame()`/`EndFrame()`; graphics does not import `Runtime.RenderWorldPool`. `ecs` is untouched. `graphics → core` invariant holds; no new dependency edges are introduced. `core ← runtime` continues to hold.

## Required changes
- [x] Capture the design decisions above as explicit recorded answers with trade-off rationales.
- [x] Cross-link upstream and downstream tasks enumerated in Context.
- [x] Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-036-Impl-A** — `Runtime.RenderWorldPool` value type + atomic swap primitives + reclamation queue + `contract;runtime` tests.
- **GRAPHICS-036-Impl-B** — Diagnostic counters wired through `RenderDiagnostics` + emission tests.
- **GRAPHICS-036-Impl-C** — Renderer-side acquire/release at `BeginFrame()` / `EndFrame()` + `contract;graphics` tests under null RHI.
- **GRAPHICS-036-Impl-D** — Synchronous-mode test seam + opt-in deterministic CPU integration test.

## Tests
- [x] Planning slice: validators only.
- [x] Implementation children (identified, not opened in this slice):
  - [x] `contract;runtime` — pool swap atomicity (`FrontTag` generation-tagged publish, `AcquireFront` CAS-validated lease with explicit publish-races-validation and reclamation-races-validation rollback coverage), reclamation against `framesInFlight` (with the three-condition rule from Decision 4), back-pressure rules.
  - [x] `contract;graphics` — renderer reads only `const ImmutableRenderWorld&`; no mutation paths exist.
  - [x] `integration` — opt-in deterministic test that validates render-of-N-1 against expected snapshot N-1 fields.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] `docs/architecture/graphics.md` and `src/runtime/README.md` pipeline-shape + diagnostics updates are deferred to Impl-A landing — no doc edit lands in this planning slice (acceptance criteria forbid code/shader changes; the doc rows describe behavior that the implementation child wires).
- [x] `docs/architecture/rendering-three-pass.md` is unchanged: the snapshot lifetime callout already cites the snapshot-extraction boundary; lifetime is the only thing this slice changes and that callout still applies.

## Acceptance criteria
- [x] Ten decisions are recorded with explicit answers and trade-off rationales.
- [x] Implementation child slices are identified with scope and dependency gates but not opened.
- [x] No code or shader changes land in this slice.
- [x] Layering invariants (`AGENTS.md` §2 and §4) hold.

## Verification
```bash
python3 tools/agents/check_task_policy.py --root . --strict
python3 tools/docs/check_doc_links.py --root . --strict
python3 tools/repo/check_layering.py --root src --strict
```

## Forbidden changes
- No mutation of snapshot data from graphics.
- No removal of the snapshot-extraction boundary.
- No introduction of live ECS access in graphics.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation children.
