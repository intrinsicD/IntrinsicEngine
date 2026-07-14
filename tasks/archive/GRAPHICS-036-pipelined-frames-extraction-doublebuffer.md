# GRAPHICS-036 — Pipelined frames and double-buffered render world (planning)

- Status: completed (2026-06-03; planning-only; `Scaffolded`).
- Owner / agent: rendering modernization roadmap planning (multi-task loop).
- Commit reference: this task-retirement commit.
- Next verification step: none; task is retired. Implementation children stay unopened until Theme A is complete.

## Goal
Lock down the contract for running simulation of frame N concurrently with rendering of frame N-1 against an immutable, double-buffered `ImmutableRenderWorld`, so that subsequent feature work (RT, neural shaders, async streaming) overlaps execution rather than serializes it. Planning only — no producer/consumer rewiring lands here.

## Non-goals
- No code changes to `Runtime.RenderExtraction`, `IRenderer::SubmitRuntimeSnapshots`, or `Graphics.RenderWorld` in this slice.
- No threading model changes outside the documented snapshot handoff.
- No relaxation of `AGENTS.md` §4 ("graphics has no live ECS knowledge").
- No introduction of mutable cross-frame state into graphics.
- No changes to `RuntimeRenderSnapshotBatch` shape; only its lifecycle.

## Context
- Owner layers: `runtime` (extraction producer + double-buffer ownership), `graphics/renderer` (consumer of immutable snapshot).
- Today extraction runs synchronously to completion before graphics consumes the snapshot. As phase-2/3 features (RT, hybrid GI, neural denoise) raise the per-frame budget, this serial model compounds latency.
- Bevy's render-world separation and Unity DOTS Hybrid Renderer V2 both pipeline simulation and rendering against an immutable copy of the scene. The same shape fits IntrinsicEngine because the snapshot boundary is already a hard architectural invariant.
- Cross-links: `GRAPHICS-002` (snapshot contract), `GRAPHICS-016` (extraction handoff), `GRAPHICS-022` (rendergraph diagnostics), `GRAPHICS-052` (deltaful scene depends on a stable double-buffer).

## Recorded decisions
1. **Buffer count.** Default **3** (triple-buffer with reclamation), runtime-configurable down to 2. Rationale: with only 2 buffers the producer must block whenever the consumer still holds the single front buffer at publish time, which reintroduces exactly the serial stall this task removes; a third slot lets the producer write a new snapshot while the consumer reads the current front and the previous front drains its in-flight window. The cost is one extra `ImmutableRenderWorld` of retained memory — bounded, because the snapshot is a compact extraction (instances/lights/visualization spans), not the live scene. Memory-constrained configurations may set 2 and accept the occasional producer stall; the synchronous mode (decision 6) collapses to one logical buffer.
2. **Ownership.** The pool is `Runtime::RenderWorldPool`, a new runtime value type owned by the composition root (`Engine`), in module `Extrinsic.Runtime.RenderWorldPool` (`src/runtime/Runtime.RenderWorldPool.cppm` + `.cpp` implementation unit). Graphics never allocates, rotates, or evicts pool storage; it observes only `const ImmutableRenderWorld&`. Rationale: AGENTS.md §4 keeps lifetime/composition in `runtime`, and the existing `RuntimeRenderSnapshotBatch → renderer stable-storage` handoff already proves runtime-owned snapshot lifetime, so the pool is the natural extension of an established edge rather than a new one.
3. **Handoff API.** Replace the one-shot submit with a producer/consumer rotation. Extraction calls `RenderWorldPool::AcquireBack()` for a free slot, writes the immutable snapshot in full, then `PublishFront(slot)` rotates it to front. The renderer's `BeginFrame()` calls `AcquireFront()`, which takes the current front and increments that slot's in-flight refcount; `EndFrame()`/frame-retire calls `ReleaseFront()`. **Atomicity guarantee:** the front index is a single `std::atomic<uint32_t>` published with `memory_order_release` and consumed with `memory_order_acquire`; per-slot refcounts are `std::atomic<uint32_t>`. Slot contents are fully written before the index publish, so no torn snapshot is ever visible — the only cross-thread shared mutation is the index and the refcounts.
4. **Reclamation rule.** A slot returns to the free list only once (a) its refcount has reached zero and (b) it is no longer the published front. Reclamation reuses the existing `framesInFlight` retire-deadline pattern from `GRAPHICS-015Q`: the renderer holds a front reference across the in-flight GPU window and releases it at frame-retire, so a snapshot still referenced by an in-flight frame is never recycled. The pool drains pending reclamations at the start of each `AcquireBack()`.
5. **Stall policy.** **Producer faster than consumer** (no free back slot at `AcquireBack()`): default back-pressure is **replace** — the newest extraction overwrites the still-unpublished back slot (newest state wins) and increments `RenderWorldExtractionSkipCount`. *Block* is rejected (reintroduces serialization) and *drop-newest* is rejected (loses the most recent state). **Consumer faster than producer** (no new front published since the last consume): the renderer **reuses the previous front** buffer and increments `RenderWorldPipelineStallCount`. Both rules are deterministic and counted.
6. **Determinism mode.** Opt-in `RenderConfig::SynchronousExtraction`, **default `true`** until `GRAPHICS-036-Impl-D` proves the pipelined path, collapses the pool to N == N-1 with one logical buffer: extraction publishes and the renderer consumes the same frame before the next tick. The flag lives on the runtime `RenderConfig`, never in graphics. Rationale: keeps tests/benchmarks deterministic and preserves today's serial behavior as the unconditional default until the pipelined path is operational.
7. **Diagnostics.** Exactly three counters, atomic increments only, on the runtime extraction diagnostics surface (and mirrored read-only where the renderer reports frame stats): `RenderWorldPipelineStallCount` (consumer reused the previous front), `RenderWorldExtractionSkipCount` (producer replaced an unpublished back), and `RenderWorldFrameAgeFrames` (age in frames of the consumed snapshot; `0` in synchronous mode, surfaced as a small bucketed histogram). No per-frame string logging on the hot path.
8. **Snapshot shape stability.** `ImmutableRenderWorld` and `RuntimeRenderSnapshotBatch` field shapes are **unchanged**; only their lifetime (pooled, multi-buffered, refcounted) changes. The pool stores N instances of the existing types. Rationale: this is a lifecycle change, not a contract change, so all downstream pass/extraction consumers are untouched and GRAPHICS-052 (deltaful scene) can build on the stable multi-buffer later.
9. **Test seam.** `RenderWorldPool` exposes a deterministic test seam (mirroring the `FindRenderableSidecarForTest`-style seams) so `contract;runtime` tests can inject a fixed schedule — `AcquireBack(A)`, `PublishFront(A)`, `AcquireFront → A`, `AcquireBack(B)`, `PublishFront(B)`, `ReleaseFront(A)`, … — and assert refcount, reclamation, stall, and skip counters without a real renderer or device.
10. **Layering audit.** `runtime` owns the pool, the atomics, and the rotation; `graphics` sees only `const ImmutableRenderWorld&` through the existing submit/consume seam and holds no handle that can mutate pool state. No new dependency edge is introduced (the `runtime → graphics` snapshot edge already exists), no live ECS access is added to graphics, and pool storage is never an ECS component.

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
- [x] Implementation children:
  - [x] `contract;runtime` — pool swap atomicity, reclamation against `framesInFlight`, back-pressure rules.
  - [x] `contract;graphics` — renderer reads only `const ImmutableRenderWorld&`; no mutation paths exist.
  - [x] `integration` — opt-in deterministic test that validates render-of-N-1 against expected snapshot N-1 fields.
- [x] Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- [x] Pipeline-shape + diagnostic-counter doc updates for `docs/architecture/graphics.md` and `src/runtime/README.md` are deferred to `GRAPHICS-036-Impl-A`; no architecture-doc change lands in this planning slice.
- [x] `docs/architecture/rendering-three-pass.md` snapshot-lifetime callout is unchanged by this planning slice; any edit is deferred to the implementation child that changes lifetime.

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

## Completion
Completed 2026-06-03 as a planning-only `Scaffolded` slice. All ten pipelined-frame / double-buffer design decisions are recorded with explicit answers and trade-off rationales (buffer count, `Runtime::RenderWorldPool` ownership, acquire/release handoff atomicity, `framesInFlight` reclamation, replace/reuse stall policy, default-on synchronous determinism mode, the three atomic diagnostic counters, snapshot-shape stability, the deterministic test seam, and the layering audit). Implementation children `GRAPHICS-036-Impl-A/B/C/D` are identified but not opened; no code, shader, or snapshot-contract change lands. The pipelined path stays gated behind the default-on `SynchronousExtraction` mode until `GRAPHICS-036-Impl-D`.

## Forbidden changes
- No mutation of snapshot data from graphics.
- No removal of the snapshot-extraction boundary.
- No introduction of live ECS access in graphics.
- No mixing of mechanical file moves with semantic refactors.
- No premature opening of implementation children.
