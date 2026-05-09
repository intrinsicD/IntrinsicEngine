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

## Required changes
- Capture the design decisions above as explicit recorded answers with trade-off rationales.
- Cross-link upstream and downstream tasks enumerated in Context.
- Identify follow-up implementation children below; do **not** open them in this slice.

## Implementation child slices (named, not opened)
- **GRAPHICS-036-Impl-A** — `Runtime.RenderWorldPool` value type + atomic swap primitives + reclamation queue + `contract;runtime` tests.
- **GRAPHICS-036-Impl-B** — Diagnostic counters wired through `RenderDiagnostics` + emission tests.
- **GRAPHICS-036-Impl-C** — Renderer-side acquire/release at `BeginFrame()` / `EndFrame()` + `contract;graphics` tests under null RHI.
- **GRAPHICS-036-Impl-D** — Synchronous-mode test seam + opt-in deterministic CPU integration test.

## Tests
- Planning slice: validators only.
- Implementation children:
  - `contract;runtime` — pool swap atomicity, reclamation against `framesInFlight`, back-pressure rules.
  - `contract;graphics` — renderer reads only `const ImmutableRenderWorld&`; no mutation paths exist.
  - `integration` — opt-in deterministic test that validates render-of-N-1 against expected snapshot N-1 fields.
- Verification gate (CPU-only):
  ```bash
  ctest --test-dir build/ci --output-on-failure -LE 'gpu|vulkan|slow|flaky-quarantine' --timeout 60
  ```

## Docs
- Update `docs/architecture/graphics.md` and `src/runtime/README.md` with the pipeline shape and diagnostic counters once Impl-A lands.
- Update `docs/architecture/rendering-three-pass.md` only if the snapshot lifetime callout changes.

## Acceptance criteria
- Ten decisions are recorded with explicit answers and trade-off rationales.
- Implementation child slices are identified with scope and dependency gates but not opened.
- No code or shader changes land in this slice.
- Layering invariants (`AGENTS.md` §2 and §4) hold.

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
